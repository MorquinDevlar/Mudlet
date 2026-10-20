/***************************************************************************
 *   Copyright (C) 2026 by Mudlet Developers                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

/*
 * A font set on the editor window, or on the application, has to reach the
 * item forms in the right-hand column the same way it reaches the sidebar
 * and its trees - and it has to keep doing so once a stylesheet is in the
 * way, be it the profile's on the window or one of the form's own, since
 * Qt's default stylesheet mode stops a parent's font at any widget a
 * stylesheet style has polished. What a widget pins for itself has to hold
 * through that: a size of its own, a weight of its own, a size a sheet sets.
 *
 * Run with: ctest -R EditorColumnFontTest -V
 */

#include <QtTest/QtTest>
#include <chrono>

#include "HostDialogs.h"
#include "ProfileSessionTestHelper.h"
#include "Host.h"
#include "dlgAliasMainArea.h"
#include "dlgScriptsMainArea.h"
#include "dlgTimersMainArea.h"
#include "dlgTriggerEditor.h"
#include "dlgTriggerPatternEdit.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorColumnFontTest : public QObject
{
    Q_OBJECT

private:
    TestProfile::Session mSession;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    QFont mOriginalApplicationFont;
    const QString mProfileName = qsl("ColumnFont-Test-Profile");
    // What a sheet on the alias form does on the redesign branch, plus a size
    // of the sheet's own on one field
    const QString mFormSheet = qsl("QLineEdit { border: 1px solid red; } QLineEdit#lineEdit_alias_command { font-size: 7pt; }");
    static constexpr qreal scmSheetPointSize = 7.0;
    // pinned by this test on label_timer_time: the redesign left no widget
    // carrying a font size of its own in the forms' .ui files
    static constexpr qreal scmPinnedPointSize = 8.0;

    QFont fourPointsLarger(const QFont& base) const
    {
        QFont larger = base;
        larger.setPointSizeF(base.pointSizeF() + 4);
        return larger;
    }

    // An application font reaches a window by a posted event, so give the
    // loop a turn before reading anything back
    void setApplicationFont(const QFont& font)
    {
        QApplication::setFont(font);
        QCoreApplication::processEvents();
    }

    // The profile's stylesheet lands on the editor window, which is where
    // Qt's stylesheet mode then stops the window's font
    void applyProfileStyleSheet()
    {
        QVERIFY(mpHost->setProfileStyleSheet(qsl("QLabel#nothingInParticular { color: red; }")));
        QVERIFY(mpEditor->testAttribute(Qt::WA_StyleSheet));
    }

    // Every link from the window down to a field in two of the forms, plus
    // the sidebar's tree for comparison, so a failure names the link that
    // dropped the size rather than only the field at the end
    void verifyColumnFollows(const qreal expectedPointSize)
    {
        struct Probe
        {
            const char* name;
            const QWidget* widget;
        };
        const QList<Probe> probes = {
                {"window", mpEditor},
                {"frame_left", mpEditor->frame_left},
                {"treeWidget_aliases", mpEditor->treeWidget_aliases},
                {"treeWidget_aliases viewport", mpEditor->treeWidget_aliases->viewport()},
                {"frame_right", mpEditor->frame_right},
                {"splitter_right", mpEditor->splitter_right},
                {"mpNonCodeWidgets", mpEditor->mpNonCodeWidgets},
                {"mpAliasMainArea", mpEditor->mpAliasMainArea},
                {"lineEdit_alias_name", mpEditor->mpAliasMainArea->lineEdit_alias_name},
                {"label_alias_command (bold only)", mpEditor->mpAliasMainArea->label_alias_command},
                {"mpScriptsMainArea", mpEditor->mpScriptsMainArea},
                {"lineEdit_script_name", mpEditor->mpScriptsMainArea->lineEdit_script_name},
        };
        QStringList wrong;
        for (const auto& probe : probes) {
            const qreal actual = probe.widget->font().pointSizeF();
            if (!qFuzzyCompare(actual, expectedPointSize)) {
                wrong << qsl("%1 is %2pt").arg(QLatin1String(probe.name)).arg(actual);
            }
        }
        QVERIFY2(wrong.isEmpty(), qPrintable(qsl("expected %1pt under the window, but: %2").arg(expectedPointSize).arg(wrong.join(qsl(", ")))));
    }

private slots:
    void initTestCase()
    {
        if (TestProfile::Session::configCannotBeRedirected()) {
            QSKIP(TestProfile::Session::configCannotBeRedirectedReason());
        }

        QVERIFY2(mSession.start(mProfileName), qPrintable(mSession.why()));
        mpHost = mSession.host();

        mudlet::self()->slot_showScriptDialog();
        QTest::qWait(100ms);

        mpEditor = HostDialogs::of(mpHost).mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mOriginalApplicationFont = QApplication::font();

        // A label that is bold and nothing else: it has to follow the size
        // while staying bold, which is what a widget's resolve mask is for
        QFont boldOnly;
        boldOnly.setBold(true);
        mpEditor->mpAliasMainArea->label_alias_command->setFont(boldOnly);

        // A label given a size of its own: it has to keep that size while the
        // column around it follows the window
        QFont pinned = mpEditor->mpTimersMainArea->label_timer_time->font();
        pinned.setPointSizeF(scmPinnedPointSize);
        mpEditor->mpTimersMainArea->label_timer_time->setFont(pinned);
        QCOMPARE(mpEditor->mpTimersMainArea->label_timer_time->font().pointSizeF(), scmPinnedPointSize);
    }

    void cleanupTestCase()
    {
        mpEditor = nullptr;
        mpHost = nullptr;
        mSession.stop();
    }

    // Each test starts from the fonts the editor opened with, and with no
    // stylesheet anywhere. The sheets go first, so that the resets after them
    // reach every widget whatever the code under test does about sheets
    void cleanup()
    {
        if (mpHost) {
            mpHost->setProfileStyleSheet(QString());
        }
        if (mpEditor) {
            mpEditor->mpAliasMainArea->setStyleSheet(QString());
            // an unresolved QFont hands the window back to the application font
            mpEditor->setFont(QFont());
        }
        setApplicationFont(mOriginalApplicationFont);
    }

    void testFontReachesRightColumn_data()
    {
        QTest::addColumn<QString>("sheet");
        QTest::addColumn<bool>("viaApplication");
        QTest::newRow("window font") << qsl("none") << false;
        QTest::newRow("application font") << qsl("none") << true;
        QTest::newRow("window font, profile stylesheet on the window") << qsl("profile") << false;
        QTest::newRow("application font, profile stylesheet on the window") << qsl("profile") << true;
        QTest::newRow("window font, stylesheet on the alias form") << qsl("form") << false;
        QTest::newRow("application font, stylesheet on the alias form") << qsl("form") << true;
    }

    void testFontReachesRightColumn()
    {
        QFETCH(QString, sheet);
        QFETCH(bool, viaApplication);
        if (sheet == qsl("profile")) {
            applyProfileStyleSheet();
        } else if (sheet == qsl("form")) {
            mpEditor->mpAliasMainArea->setStyleSheet(mFormSheet);
            QVERIFY(mpEditor->mpAliasMainArea->lineEdit_alias_name->testAttribute(Qt::WA_StyleSheet));
            QCOMPARE(mpEditor->mpAliasMainArea->lineEdit_alias_command->font().pointSizeF(), scmSheetPointSize);
        }

        const QFont larger = fourPointsLarger(viaApplication ? QApplication::font() : mpEditor->font());
        if (viaApplication) {
            setApplicationFont(larger);
        } else {
            mpEditor->setFont(larger);
        }

        verifyColumnFollows(larger.pointSizeF());
        QVERIFY2(mpEditor->mpAliasMainArea->label_alias_command->font().bold(), "a label that was only bold has to stay bold");
        QCOMPARE(mpEditor->mpTimersMainArea->label_timer_time->font().pointSizeF(), scmPinnedPointSize);
        // the field the sheet sizes keeps the sheet's size; with no sheet on it, it follows like the rest
        QCOMPARE(mpEditor->mpAliasMainArea->lineEdit_alias_command->font().pointSizeF(), sheet == qsl("form") ? scmSheetPointSize : larger.pointSizeF());
    }

    // A pattern row made after the font changed: under a sheet, Qt gives a
    // new widget the application font rather than its parent's
    void testNewPatternRowTakesWindowFontUnderProfileStyleSheet()
    {
        // A class font, as the macOS theme registers for labels and buttons:
        // a widget with one takes from its parent only what the parent's
        // resolve mask names, so a row seeded without one is caught here too.
        // Built rather than copied from the application font, or Qt takes
        // it for that font and resolves against the parent after all
        const QFont labelFont(QApplication::font().family(), qRound(QApplication::font().pointSizeF()));
        QVERIFY(!labelFont.isCopyOf(QApplication::font()));
        QApplication::setFont(labelFont, "QLabel");
        applyProfileStyleSheet();
        const QFont larger = fourPointsLarger(mpEditor->font());
        mpEditor->setFont(larger);

        const qsizetype rowsBefore = mpEditor->mTriggerPatternEdit.size();
        mpEditor->showPatternItems(rowsBefore + 1);
        QCOMPARE(mpEditor->mTriggerPatternEdit.size(), rowsBefore + 1);
        auto* pRow = mpEditor->mTriggerPatternEdit.last();
        QCOMPARE(pRow->comboBox_patternType->font().pointSizeF(), larger.pointSizeF());
        // label_prompt rather than label_patternNumber: the redesign reads a
        // pattern's number in the profile's display font, like the pattern
        // beside it, so that one is pinned by applyPatternWidgetStyle() and
        // follows nothing. label_prompt is still interface text, and still a
        // QLabel, which is the class-font case this is here to catch.
        QCOMPARE(pRow->label_prompt->font().pointSizeF(), larger.pointSizeF());
    }

    // Setting the profile's stylesheet again - the same one, as scripts do -
    // repolishes every widget, which hands each its own pre-sheet font back
    // resolved against the application font; so does taking the sheet off
    void testWindowFontSurvivesTheProfileStyleSheetBeingSetAgain()
    {
        applyProfileStyleSheet();
        const QFont larger = fourPointsLarger(mpEditor->font());
        mpEditor->setFont(larger);
        verifyColumnFollows(larger.pointSizeF());

        applyProfileStyleSheet();
        verifyColumnFollows(larger.pointSizeF());

        QVERIFY(mpHost->setProfileStyleSheet(QString()));
        QVERIFY(!mpEditor->testAttribute(Qt::WA_StyleSheet));
        verifyColumnFollows(larger.pointSizeF());
    }
};

#include "EditorColumnFontTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorColumnFontTest)
