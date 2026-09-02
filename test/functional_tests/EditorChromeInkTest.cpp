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
 * One ink for every word of the editor's chrome.
 *
 * The rule the editor is held to here: everything the window says that is not
 * inside a field is written in `mutedText` - the toolbar's buttons, the
 * sidebar's names, the item trees' rows, a card's title and the words on it, a
 * check box, a radio button, the code pane's heading, the status bar. The full
 * tone is what is typed into a field, and nothing else has it. A window with
 * two greys in its chrome reads as two windows.
 *
 * Read off the palettes rather than off the rules that write them: a
 * stylesheet's "color:" reaches a widget through QStyleSheetStyle::polish(), so
 * the palette is the answer to what a widget is actually drawn in - and a view,
 * whose rows a stylesheet can only reach through ::item, is read by its palette
 * for the same reason.
 *
 * Every view is walked, not only the one the editor opens on: each of the seven
 * is entered with an item of its own in the form, so that the words on all
 * seven forms are read rather than only the trigger's.
 *
 * Five things keep an ink of their own, and each is left out for a reason
 * rather than for convenience:
 *
 * - Anything inside a field. That is the one place the full tone belongs.
 * - Anything unavailable, chosen or under the pointer: those are states, drawn
 *   in `disabledText` and `accentText`, and saying so is the whole point of
 *   them.
 * - The three chips whose colour says a reading, by object name: the compile
 *   chip in a state hue walked against its own fill, the OR/AND chip in the
 *   accent while it is the mode in force, and the note the events row answers a
 *   duplicate with, in the error hue - none of them chrome in the sense this
 *   measures.
 * - A chip holding one of a script's own event names, which is the name the
 *   reader typed and so content in a box rather than a word the window says.
 *   Mudlet's own sys* events are written in the chrome tone by the same row,
 *   which is what tells the two apart on sight.
 * - The error console, which shows what the profile's own script printed in the
 *   colours that profile chose.
 *
 * ...and one thing is read off the window rather than out of a palette: the
 * name on the Triggers heading row, whose pixels have to be that same ink. A
 * palette a view never draws from would pass every case above and paint the
 * tree in the platform's white.
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
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTreeWidget>
#include <QtTest/QtTest>
#include <chrono>

#include "ChipRow.h"
#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
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
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    QTemporaryDir mCacheDir;
    QByteArray mSavedXdgCache;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    dlgProfilePreferences* mpPreferences = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorChromeInk-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    // Measured over the seven views together and pinned under what the walk
    // actually reaches - 127 things on the dark appearance and 133 on the
    // light - so that a walk which stopped finding widgets cannot pass as a
    // walk that found them all in the one ink. The two counts differ because
    // what is unavailable, chosen or empty of words is left out, and that is
    // not the same set on both appearances.
    static constexpr int scmLeastAudited = 100;
    // What antialiasing is allowed to have left of a letter's core. Never more:
    // the edges of a glyph are steps between the ink and what is behind it, so
    // the pixel furthest from the surface is the ink itself.
    static constexpr int scmInkTolerance = 2;

    // The chips whose colour says a reading rather than a tone: the compile
    // state, drawn in a state hue walked against its own fill; the OR/AND chip,
    // which takes the accent while it is the mode in force; and the note the
    // events row answers a duplicate with, which is the error hue walked
    // against the page. Named here because each is drawn as a widget with words
    // inside it, and the words are what would otherwise be measured.
    //
    // A script's own event names go with them, for the opposite reason: what a
    // chip holds is the name the reader typed, so it is content in a box rather
    // than a word the window says - the full tone, as in a field. The row
    // writes Mudlet's own sys* events in the chrome tone, which is what tells
    // the two apart on sight, but the ink of a chip is the row's to choose.
    static bool aStateChipOrAChipsOwnName(const QWidget* pWidget)
    {
        for (const QWidget* pAt = pWidget; pAt; pAt = pAt->parentWidget()) {
            const QString name = pAt->objectName();
            if (name == qsl("editorCompileChip") || name == qsl("editorModeChip") || name == qsl("editorChipNote") || name == qsl("editorChipLabel")) {
                return true;
            }
        }
        return false;
    }

    // The full tone is what is typed into or picked in a field, so the field
    // and everything under it is left alone: a combo box's own line edit, a
    // spin box's, the code pane edbee draws from a syntax theme, and the
    // console the profile's script writes its errors to.
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

    // What a widget writes its words in, or nothing where it shows none. A view
    // is read once, by the colour its rows are drawn in - which is its palette,
    // since a stylesheet can only reach a row through ::item and a ::item rule
    // reaches the palette for some selectors and not others.
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
            // A button that is currently doing something, or being pressed, is
            // lit in the accent - which is a state rather than an ink
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

    // Every word the editor shows on one appearance. Answers how many things it
    // read, and adds a line to misses for each one written in something else.
    int walkTheChrome(const QString& appearance, const QColor& chrome, QStringList& misses) const
    {
        int audited = 0;
        for (const QWidget* pWidget : mpEditor->findChildren<QWidget*>()) {
            // An unavailable word is held to the design's quieter tone, and one
            // under the pointer to the accent - both states rather than inks
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

    // Three rows rather than one, and one of each shape the row can take: a
    // pattern typed into a field, the words that stand in for the prompt line,
    // and the pair of wells a colour trigger is set from
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

    // One item in each of the other six views, so that entering a view shows
    // the form rather than the placeholder an empty view stands in with - and,
    // on the scripts form, one event of the reader's own beside one of Mudlet's
    // so both inks the events row uses are on show while it is walked.
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

    // Each of the seven, with the form filled in rather than the placeholder up
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

    // Through the settings dialog's own control, which is the one path an
    // appearance change takes while these windows are open - and the path that
    // restyles them. A window on screen paints itself between one change and
    // the next, and painting is what settles a widget's palette against the
    // application's; under the offscreen platform nothing paints unless asked.
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

    void deleteProfileDirectory(const QString& profileName)
    {
        QDir dir(mudlet::getMudletPath(enums::profileHomePath, profileName));
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }

    void writeFreshEditorThemesFile()
    {
        const QString file = mudlet::getMudletPath(enums::editorWidgetThemeJsonFile);
        QVERIFY(QDir().mkpath(QFileInfo(file).absolutePath()));
        QFile themes(file);
        QVERIFY(themes.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(themes.write("[]") == 2);
    }

private slots:
    void initTestCase()
    {
        if (portableMarkerPresent()) {
            QSKIP("portable.txt present - it takes precedence over XDG_CONFIG_HOME, so the config dir cannot be redirected");
        }

        QVERIFY(mConfigDir.isValid());
        QVERIFY(QDir().mkpath(qsl("%1/mudlet/profiles").arg(mConfigDir.path())));
        mSavedXdg = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", mConfigDir.path().toUtf8());
        QVERIFY(mCacheDir.isValid());
        mSavedXdgCache = qgetenv("XDG_CACHE_HOME");
        qputenv("XDG_CACHE_HOME", mCacheDir.path().toUtf8());

        mpServer = new TelnetServerStub(qApp);
        mpServer->start(mLocalhost, 0);
        QVERIFY2(mpServer->isListening(), qPrintable(qsl("TelnetServerStub failed to start: %1").arg(mpServer->errorString())));
        mPort = QString::number(mpServer->serverPort());
        mudlet::start();
        mudlet::self()->setupConfig();
        QCOMPARE(mudlet::getMudletPath(enums::mainPath), qsl("%1/mudlet").arg(mConfigDir.path()));
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>(qsl("MudletInstanceCoordinator")));
        mudlet::self()->init();
        mudlet::self()->setStorePasswordsSecurely(false);
        deleteProfileDirectory(mProfileName);
        writeFreshEditorThemesFile();

        mpHost = TestProfile::create(mProfileName, mLocalhost, mPort);
        QVERIFY2(mpHost, "No active host after profile creation");
        QSignalSpy connected(&(mpHost->mTelnet), &cTelnet::signal_connected);
        QVERIFY2(connected.wait(2000), "Could not connect with the host.");

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
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
        // The options are what the sound card, the highlight card and the rest
        // of the form live on, and they open closed
        mpEditor->setTriggerOptionsShown(true);
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
        delete mpServer;
        mpServer = nullptr;
        if (mudlet::self()) {
            mudlet::self()->setAppearance(enums::Appearance::systemSetting);
            deleteProfileDirectory(mProfileName);
            delete mudlet::self();
        }
        mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME") : qputenv("XDG_CONFIG_HOME", mSavedXdg);
        mSavedXdgCache.isNull() ? qunsetenv("XDG_CACHE_HOME") : qputenv("XDG_CACHE_HOME", mSavedXdgCache);
    }

    // One case for both appearances and all seven views, with one list at the
    // end: a run that stopped at the first word in the wrong tone would take as
    // many runs to clear as there are of them
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

    // A palette a view never draws from would pass the walk above and leave the
    // tree painted in the platform's white, so one row's pixels are read off a
    // shot of the window: the Triggers heading, which is neither chosen nor
    // under the pointer. The ink is the pixel furthest from the pane it is
    // written on - antialiasing gives lighter steps towards the surface, never
    // a core beyond the ink.
    void test_theTreesHeadingRowIsPaintedInThatSameTone()
    {
        setAppearance(enums::Appearance::dark);
        // The panel shows one tree at a time, and the walk above leaves
        // whichever view it ended on up
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
