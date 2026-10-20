/***************************************************************************
 *   Copyright (C) 2026 by Vadim Peretokin - vadim.peretokin@mudlet.org    *
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
 * Every word of the editor's chrome outside a field is the one mutedText ink, on
 * all seven views and both appearances. Read off palettes, since a stylesheet's
 * "color:" reaches a widget through QStyleSheetStyle::polish().
 *
 * Run with: ctest -R EditorChromeInkTest -V
 */

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QGroupBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTreeWidget>
#include <QtTest/QtTest>
#include <chrono>

#include "ChipRow.h"
#include "Host.h"
#include "HostDialogs.h"
#include "MudletApp.h"
#include "ProfileSessionTestHelper.h"
#include "dlgProfilePreferences.h"
#include "dlgTriggerEditor.h"
#include "dlgTriggerPatternEdit.h"
#include "dlgTriggersMainArea.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorChromeInkTest : public QObject
{
    Q_OBJECT

private:
    TestProfile::Session mSession;
    dlgTriggerEditor* mpEditor = nullptr;
    dlgProfilePreferences* mpPreferences = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorChromeInk-Test-Profile");

    // Pinned under the measured 127 (dark) and 133 (light), so a walk that stops
    // finding widgets cannot pass as one that found them all in the one ink
    static constexpr int scmLeastAudited = 100;
    // Small: a glyph's antialiased edges step between ink and surface, so the
    // pixel furthest from the surface is the ink itself
    static constexpr int scmInkTolerance = 2;

    // The compile and duplicate notes say a reading in the error hue, and a chip
    // label holds an event name the reader typed - content, not chrome
    static bool aStateChipOrAChipsOwnName(const QWidget* pWidget)
    {
        for (const QWidget* pAt = pWidget; pAt; pAt = pAt->parentWidget()) {
            const QString name = pAt->objectName();
            if (name == qsl("editorCompileNote") || name == qsl("editorChipNote") || name == qsl("editorChipLabel")) {
                return true;
            }
        }
        return false;
    }

    // Fields keep the full tone; edbee draws from its syntax theme and the error
    // console in the colours the profile chose
    static bool insideAField(const QWidget* pWidget)
    {
        for (const QWidget* pAt = pWidget; pAt; pAt = pAt->parentWidget()) {
            if (qobject_cast<const QLineEdit*>(pAt) || qobject_cast<const QAbstractSpinBox*>(pAt) || qobject_cast<const QComboBox*>(pAt) || qobject_cast<const QTextEdit*>(pAt)
                || qobject_cast<const QPlainTextEdit*>(pAt)) {
                return true;
            }
            const QString className = QString::fromLatin1(pAt->metaObject()->className());
            if (className.contains(qsl("edbee"), Qt::CaseInsensitive) || className == qsl("TConsole") || className == qsl("TTextEdit")) {
                return true;
            }
        }
        return false;
    }

    // A view is read by its palette: a stylesheet only reaches a row through
    // ::item, and a ::item rule reaches the palette for some selectors only
    static QString inkRoleOf(const QWidget* pWidget, QColor& ink)
    {
        if (const auto* pView = qobject_cast<const QAbstractItemView*>(pWidget)) {
            ink = pView->palette().color(QPalette::Active, QPalette::Text);
            return qsl("its rows");
        }
        if (const auto* pGroup = qobject_cast<const QGroupBox*>(pWidget)) {
            if (pGroup->title().isEmpty() || (pGroup->isCheckable() && pGroup->isChecked())) {
                return QString();
            }
            ink = pGroup->palette().color(QPalette::Active, pGroup->foregroundRole());
            return qsl("its title");
        }
        if (const auto* pButton = qobject_cast<const QAbstractButton*>(pWidget)) {
            // Checked or pressed, a button is lit in the accent - a state, not an ink
            if (pButton->text().isEmpty() || pButton->isChecked() || pButton->isDown()) {
                return QString();
            }
            ink = pButton->palette().color(QPalette::Active, pButton->foregroundRole());
            return qsl("its text");
        }
        if (const auto* pLabel = qobject_cast<const QLabel*>(pWidget)) {
            if (pLabel->text().isEmpty()) {
                return QString();
            }
            ink = pLabel->palette().color(QPalette::Active, pLabel->foregroundRole());
            return qsl("its text");
        }
        return QString();
    }

    int walkTheChrome(const QString& appearance, const QColor& chrome, QStringList& misses) const
    {
        int audited = 0;
        for (const QWidget* pWidget : mpEditor->findChildren<QWidget*>()) {
            // Disabled words take disabledText and hovered ones the accent -
            // states, not inks
            if (!pWidget->isVisible() || pWidget->rect().isEmpty() || !pWidget->isEnabled() || pWidget->underMouse()) {
                continue;
            }
            if (insideAField(pWidget) || aStateChipOrAChipsOwnName(pWidget)) {
                continue;
            }

            QColor ink;
            const QString what = inkRoleOf(pWidget, ink);
            if (what.isEmpty() || !ink.isValid()) {
                continue;
            }
            ++audited;
            if (ink.rgb() == chrome.rgb()) {
                continue;
            }
            misses << qsl("%1 (%2), %3 appearance: %4 are %5, not the %6 every word of the editor's chrome is written in")
                              .arg(pWidget->objectName().isEmpty() ? qsl("<unnamed>") : pWidget->objectName(),
                                   QString::fromLatin1(pWidget->metaObject()->className()),
                                   appearance,
                                   what,
                                   ink.name(),
                                   chrome.name());
        }
        return audited;
    }

    void buildThreePatternRows()
    {
        mpEditor->showPatternItems(3);
        const QList<int> types{REGEX_PERL, REGEX_PROMPT, REGEX_COLOR_PATTERN};
        for (int row = 0; row < types.size(); ++row) {
            mpEditor->mTriggerPatternEdit.at(row)->comboBox_patternType->setCurrentIndex(types.at(row));
        }
        mpEditor->mTriggerPatternEdit.at(0)->singleLineTextEdit_pattern->setPlainText(qsl("^You (?:see|hear) (.+)$"));
        QCoreApplication::processEvents();
        QVERIFY2(mpEditor->mVisiblePatternCount == types.size(), qPrintable(qsl("the trigger form shows %1 pattern rows rather than three").arg(QString::number(mpEditor->mVisiblePatternCount))));
    }

    // An empty view shows a placeholder rather than its form; the events row gets
    // an own event and a sys* one so both of its inks are walked
    void buildAnItemInEveryOtherView()
    {
        mpEditor->slot_showAliases();
        mpEditor->addAlias(false);
        mpEditor->slot_showTimers();
        mpEditor->addTimer(false);
        mpEditor->slot_showKeys();
        mpEditor->addKey(false);
        mpEditor->slot_showActions();
        mpEditor->addAction(false);
        mpEditor->slot_showScripts();
        mpEditor->addScript(false);
        QTest::qWait(100ms);
        QVERIFY2(mpEditor->mpChipRow_scriptEvents != nullptr, "the scripts form has no events row for this walk to read");
        mpEditor->mpChipRow_scriptEvents->setItems({qsl("myOwnEvent"), qsl("sysLoadEvent")});
        QCoreApplication::processEvents();
    }

    void enterView(const EditorViewType view)
    {
        switch (view) {
        case EditorViewType::cmTriggerView:
            mpEditor->slot_showTriggers();
            break;
        case EditorViewType::cmAliasView:
            mpEditor->slot_showAliases();
            break;
        case EditorViewType::cmTimerView:
            mpEditor->slot_showTimers();
            break;
        case EditorViewType::cmScriptView:
            mpEditor->slot_showScripts();
            break;
        case EditorViewType::cmActionView:
            mpEditor->slot_showActions();
            break;
        case EditorViewType::cmKeysView:
            mpEditor->slot_showKeys();
            break;
        case EditorViewType::cmVarsView:
            mpEditor->slot_showVariables();
            break;
        case EditorViewType::cmUnknownView:
            break;
        }
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        mpEditor->grab();
    }

    // Through the settings control, the path that restyles these windows. The
    // grab() is there because painting settles a widget's palette against the
    // application's, and under the offscreen platform nothing paints unasked.
    void setAppearance(const enums::Appearance appearance)
    {
        if (mpPreferences->comboBox_appearance->currentIndex() == appearance) {
            mpPreferences->comboBox_appearance->setCurrentIndex(appearance == enums::Appearance::dark ? enums::Appearance::light : enums::Appearance::dark);
            QCoreApplication::processEvents();
        }
        mpPreferences->comboBox_appearance->setCurrentIndex(appearance);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        QCoreApplication::processEvents();
        mpEditor->grab();
    }

    void writeFreshEditorThemesFile()
    {
        const QString file = MudletApp::getMudletPath(enums::editorWidgetThemeJsonFile);
        QVERIFY(QDir().mkpath(QFileInfo(file).absolutePath()));
        QFile themes(file);
        QVERIFY(themes.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(themes.write("[]") == 2);
    }

private slots:
    void initTestCase()
    {
        if (TestProfile::Session::configCannotBeRedirected()) {
            QSKIP(TestProfile::Session::configCannotBeRedirectedReason());
        }

        QVERIFY2(mSession.startMudlet(), qPrintable(mSession.why()));
        writeFreshEditorThemesFile();
        QVERIFY2(mSession.openProfile(mProfileName), qPrintable(mSession.why()));
        mpHost = mSession.host();

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = HostDialogs::of(mpHost).mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(1100, 800);
        buildAnItemInEveryOtherView();
        if (QTest::currentTestFailed()) {
            return;
        }
        mpEditor->slot_showTriggers();
        mpEditor->addTrigger(false);
        QTest::qWait(100ms);
        buildThreePatternRows();
        QTest::qWait(100ms);

        mpPreferences = new dlgProfilePreferences(mudlet::self(), mpHost);
        mpPreferences->setStyleSheet(mpHost->mProfileStyleSheet);
        mpPreferences->resize(1060, 760);
        mpPreferences->show();
        QVERIFY(QTest::qWaitForWindowExposed(mpPreferences));
    }

    void cleanupTestCase()
    {
        delete mpPreferences;
        mpPreferences = nullptr;
        mpEditor = nullptr;
        mpHost = nullptr;
        if (mudlet::self()) {
            mudlet::self()->setAppearance(enums::Appearance::systemSetting);
        }
        mSession.stop();
    }

    // One list at the end: stopping at the first miss would take a run per miss
    // to clear
    void test_everyWordOutsideAFieldIsTheOneQuietTone()
    {
        QStringList misses;
        QStringList counts;
        const QList<EditorViewType> views{EditorViewType::cmTriggerView,
                                          EditorViewType::cmAliasView,
                                          EditorViewType::cmTimerView,
                                          EditorViewType::cmScriptView,
                                          EditorViewType::cmActionView,
                                          EditorViewType::cmKeysView,
                                          EditorViewType::cmVarsView};
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
            int audited = 0;
            for (const EditorViewType view : views) {
                enterView(view);
                audited += walkTheChrome(appearance.first, tokens.mutedText, misses);
            }
            counts << qsl("%1: %2 things in %3").arg(appearance.first, QString::number(audited), tokens.mutedText.name());
            QVERIFY2(audited >= scmLeastAudited,
                     qPrintable(qsl("only %1 things were read on the %2 appearance, against the %3 this walk reaches - it stopped finding widgets rather than finding them all in one ink")
                                        .arg(QString::number(audited), appearance.first, QString::number(scmLeastAudited))));
        }

        qInfo().noquote() << qsl("  audited %1").arg(counts.join(qsl("; ")));
        for (const QString& miss : misses) {
            qWarning().noquote() << miss;
        }
        QVERIFY2(misses.isEmpty(), qPrintable(qsl("%1 thing(s) in the editor's chrome are written in something other than the one quiet tone - listed above").arg(QString::number(misses.size()))));
    }

    // A palette a view never draws from would pass the walk above while the tree
    // paints in the platform's white
    void test_theTreesHeadingRowIsPaintedInThatSameTone()
    {
        setAppearance(enums::Appearance::dark);
        // The panel shows one tree at a time, and the walk above leaves the last
        // view up
        enterView(EditorViewType::cmTriggerView);
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();

        QTreeWidget* pTree = mpEditor->treeWidget_triggers;
        QTreeWidgetItem* pHeading = mpEditor->mpTriggerBaseItem;
        QVERIFY2(pTree && pHeading, "the triggers tree has no heading row to read");
        QVERIFY2(!pHeading->isSelected(), "the heading row is chosen, so what it is painted in is the accent rather than the chrome tone");

        const QRect row = pTree->visualItemRect(pHeading);
        QVERIFY2(row.width() > 20 && row.height() > 4, "the heading row is too small to read a pixel out of");

        const QImage shot = mpEditor->grab().toImage();
        // grab() answers a pixmap at the screen's device pixel ratio, so the
        // window's own coordinates are not the image's on a scaled display
        const qreal ratio = shot.width() / static_cast<qreal>(std::max(1, mpEditor->width()));
        const QPoint topLeft = pTree->viewport()->mapTo(mpEditor, row.topLeft());
        const QRect painted = QRect(QPoint(qRound(topLeft.x() * ratio), qRound(topLeft.y() * ratio)), QSize(qRound(row.width() * ratio), qRound(row.height() * ratio))).intersected(shot.rect());
        QVERIFY2(painted.width() > 8 && painted.height() > 4, "the heading row does not land on the shot of the window");

        const auto distanceFromThePane = [&tokens](const QColor& colour) {
            return std::abs(colour.red() - tokens.pane.red()) + std::abs(colour.green() - tokens.pane.green()) + std::abs(colour.blue() - tokens.pane.blue());
        };
        QColor ink = tokens.pane;
        for (int y = painted.top(); y <= painted.bottom(); ++y) {
            for (int x = painted.left(); x <= painted.right(); ++x) {
                const QColor pixel = shot.pixelColor(x, y);
                if (distanceFromThePane(pixel) > distanceFromThePane(ink)) {
                    ink = pixel;
                }
            }
        }

        qInfo().noquote()
                << qsl("  the Triggers heading is painted %1, against the %2 the editor's chrome is written in, on the %3 the panel is").arg(ink.name(), tokens.mutedText.name(), tokens.pane.name());
        const int off = std::max({std::abs(ink.red() - tokens.mutedText.red()), std::abs(ink.green() - tokens.mutedText.green()), std::abs(ink.blue() - tokens.mutedText.blue())});
        QVERIFY2(off <= scmInkTolerance,
                 qPrintable(qsl("the Triggers heading is painted %1, %2 off the %3 every word of the editor's chrome is written in").arg(ink.name(), QString::number(off), tokens.mutedText.name())));
    }
};

#include "EditorChromeInkTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorChromeInkTest)
