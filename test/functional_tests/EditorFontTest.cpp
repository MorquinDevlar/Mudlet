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
 * The script editor reads code in a font of its own: the display font for as
 * long as it follows that, else a family and size the profile gave it. What
 * reads it is the code pane, the error console and the pattern fields; the
 * form's labels stay in the interface font either way.
 *
 * Run with: ctest -R EditorFontTest -V
 */

#include <QFontDatabase>
#include <QtTest/QtTest>
#include <chrono>
#include <utility>

#include "HostDialogs.h"
#include "ProfileSessionTestHelper.h"
#include "Host.h"
#include "TConsole.h"
#include "dlgTriggerEditor.h"
#include "dlgTriggerPatternEdit.h"
#include "dlgTriggersMainArea.h"
#include "mudlet.h"

#include "edbee/models/texteditorconfig.h"
#include "edbee/texteditorwidget.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorFontTest : public QObject
{
    Q_OBJECT

private:
    TestProfile::Session mSession;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    QFont mOriginalDisplayFont;
    const QString mProfileName = qsl("EditorFont-Test-Profile");
    // Bundled, so on every machine, and not the family a new profile's display font is
    static inline const QString scmOwnFamily = qsl("Ubuntu Mono");
    static constexpr int scmOwnSize = 19;
    QList<int> mApplicationFontIds;

    dlgTriggerPatternEdit* patternRow() const { return mpEditor->mTriggerPatternEdit.isEmpty() ? nullptr : mpEditor->mTriggerPatternEdit.first(); }

    // Everything that reads code, named so a failure says which one stayed behind
    QStringList readersNotIn(const QString& family, const int pointSize) const
    {
        const std::pair<const char*, QFont> readers[] = {{"code pane", mpEditor->mpSourceEditorEdbee->config()->font()},
                                                         {"error console", mpEditor->mpErrorConsole->font()},
                                                         {"pattern field", patternRow()->singleLineTextEdit_pattern->font()}};
        QStringList wrong;
        for (const auto& [name, font] : readers) {
            if (font.family() != family || font.pointSize() != pointSize) {
                wrong << qsl("%1 is %2 %3pt").arg(QLatin1String(name), font.family()).arg(font.pointSize());
            }
        }
        return wrong;
    }

    void verifyReadersIn(const QString& family, const int pointSize)
    {
        const QStringList wrong = readersNotIn(family, pointSize);
        QVERIFY2(wrong.isEmpty(), qPrintable(qsl("expected %1 %2pt, but: %3").arg(family).arg(pointSize).arg(wrong.join(qsl(", ")))));
    }

    // A test binary never copies the bundled fonts out of the resources the way main.cpp does, so
    // without this both families would be missing and stood in for - see MissingDisplayFontTest
    void registerBundledFonts()
    {
        for (const QString& resourcePath : {qsl(":/fonts/ttf-bitstream-vera-1.10/VeraMono.ttf"), qsl(":/fonts/ubuntu-font-family-0.83/UbuntuMono-R.ttf")}) {
            const int id = QFontDatabase::addApplicationFont(resourcePath);
            QVERIFY2(id != -1, qPrintable(qsl("could not register the bundled font \"%1\"").arg(resourcePath)));
            mApplicationFontIds.append(id);
        }
    }

    void setOwnFont()
    {
        mpHost->setEditorFont(QFont(scmOwnFamily, scmOwnSize));
        mpHost->setEditorFontFollowsDisplay(false);
    }

private slots:
    void initTestCase()
    {
        if (TestProfile::Session::configCannotBeRedirected()) {
            QSKIP(TestProfile::Session::configCannotBeRedirectedReason());
        }

        registerBundledFonts();
        QVERIFY2(mSession.start(mProfileName), qPrintable(mSession.why()));
        mpHost = mSession.host();

        mudlet::self()->slot_showScriptDialog();
        QTest::qWait(100ms);
        mpEditor = HostDialogs::of(mpHost).mpEditorDialog;
        QVERIFY2(mpEditor, "Editor dialog should be created");
        QVERIFY2(patternRow(), "the trigger form opened without a pattern row");

        mOriginalDisplayFont = mpHost->getDisplayFont();
        QVERIFY2(mOriginalDisplayFont.family() != scmOwnFamily, "the profile's display font is the family this test gives the editor");
        QVERIFY(mOriginalDisplayFont.pointSize() != scmOwnSize);
    }

    void cleanupTestCase()
    {
        mpEditor = nullptr;
        mpHost = nullptr;
        mSession.stop();
        for (const int id : std::as_const(mApplicationFontIds)) {
            QFontDatabase::removeApplicationFont(id);
        }
    }

    void cleanup()
    {
        if (mpHost) {
            mpHost->setEditorFontFollowsDisplay(true);
            mpHost->setDisplayFont(mOriginalDisplayFont, Host::DisplayFontChange::UserChoice);
        }
    }

    void test_aNewProfileReadsCodeInTheDisplayFont()
    {
        QVERIFY(mpHost->editorFontFollowsDisplay());
        verifyReadersIn(mOriginalDisplayFont.family(), mOriginalDisplayFont.pointSize());
    }

    // The editor starts from what is on show when it first leaves the display font
    void test_leavingTheDisplayFontStartsFromIt()
    {
        QVERIFY(!mpHost->editorFontChoice().has_value());
        mpHost->setEditorFontFollowsDisplay(false);
        QVERIFY(mpHost->editorFontChoice().has_value());
        verifyReadersIn(mOriginalDisplayFont.family(), mOriginalDisplayFont.pointSize());
    }

    void test_anOwnFontReachesWhatReadsCodeAndNothingElse()
    {
        const QString labelFamily = mpEditor->mpTriggersMainArea->label_trigger_name->font().family();
        QVERIFY(labelFamily != scmOwnFamily);

        setOwnFont();
        QCOMPARE(mpHost->getEditorFont().family(), scmOwnFamily);
        verifyReadersIn(scmOwnFamily, scmOwnSize);

        QCOMPARE(mpEditor->mpTriggersMainArea->label_trigger_name->font().family(), labelFamily);
        QVERIFY2(patternRow()->label_prompt->font().family() != scmOwnFamily, "the pattern row's prompt label took the code font");
        // A row is measured off the font it holds
        QVERIFY(patternRow()->minimumHeight() > QFontMetrics(QFont(scmOwnFamily, scmOwnSize)).height());
    }

    void test_followingAgainBringsTheDisplayFontBack()
    {
        setOwnFont();
        verifyReadersIn(scmOwnFamily, scmOwnSize);

        mpHost->setEditorFontFollowsDisplay(true);
        verifyReadersIn(mOriginalDisplayFont.family(), mOriginalDisplayFont.pointSize());
    }

    void test_aFollowingEditorMovesWithTheDisplayFont()
    {
        QFont display = mOriginalDisplayFont;
        display.setPointSize(mOriginalDisplayFont.pointSize() + 3);
        QVERIFY(mpHost->setDisplayFont(display, Host::DisplayFontChange::UserChoice).first);
        verifyReadersIn(mOriginalDisplayFont.family(), mOriginalDisplayFont.pointSize() + 3);
    }

    void test_anEditorWithAFontOfItsOwnStaysPutWhenTheDisplayFontMoves()
    {
        setOwnFont();
        QFont display = mOriginalDisplayFont;
        display.setPointSize(mOriginalDisplayFont.pointSize() + 3);
        QVERIFY(mpHost->setDisplayFont(display, Host::DisplayFontChange::UserChoice).first);
        QCOMPARE(mpHost->getDisplayFont().pointSize(), mOriginalDisplayFont.pointSize() + 3);
        verifyReadersIn(scmOwnFamily, scmOwnSize);
    }
};

#include "EditorFontTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorFontTest)
