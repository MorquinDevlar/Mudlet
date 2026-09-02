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
 * Every word in the editor and the settings dialog, in both appearances, read
 * against what is actually painted behind it.
 *
 * The design mixes its inks and its surfaces from the same palette, so the pair
 * is meant to hold at any theme - but a rule that names one and not the other,
 * or a control the platform styles itself, lands text on a surface nobody
 * measured. That defect is invisible in whichever theme it was written in and
 * unreadable in the other, and nothing catches it until somebody switches.
 *
 * So this opens both windows, flips the appearance, and for every visible thing
 * that shows words compares the ink its palette answers with against the colour
 * most of the pixels behind it are. WCAG's floors: 4.5:1 for text, 3:1 for
 * what is unavailable or not yet typed in.
 *
 * Two things are deliberately not audited:
 *
 * - The code pane. Edbee carries a syntax theme of its own with its own
 *   background, and what it draws is not this design's ink on this design's
 *   surface.
 * - A colour well - a button filled with the colour the user picked. The fill
 *   is a value being shown rather than a surface, and the words on it are
 *   chosen against that fill by generateButtonStyleSheet() rather than by the
 *   theme. They are skipped by object name, listed in wellNames() below.
 *
 * Run with: ctest -R ReadabilityAuditTest -V
 */

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHash>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <chrono>

#include "ChipRow.h"
#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "ScriptUnit.h"
#include "TScript.h"
#include "TTrigger.h"
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

class ReadabilityAuditTest : public QObject
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
    // The two items the forms walked on their own below are shown from
    QTreeWidgetItem* mpKeyItem = nullptr;
    QTreeWidgetItem* mpTimerItem = nullptr;
    const QString mProfileName = qsl("ReadabilityAudit-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    // What text has to clear against what it is written on, and the lower floor
    // WCAG allows for words that are unavailable or not yet typed
    static constexpr qreal scmTextFloor = uiDesign::scmTextMinimumRatio;
    static constexpr qreal scmQuietFloor = uiDesign::scmQuietMinimumRatio;
    // Measured floors, pinned below what the walk actually reaches - 42 things
    // in the editor and 26 on the settings dialog's first page - so that a walk
    // which silently stopped finding widgets cannot pass as a clean one
    static constexpr int scmLeastAuditedInTheEditor = 30;
    static constexpr int scmLeastAuditedInTheSettings = 18;
    // ...and on the scripts form, which is walked on its own below: the name,
    // the ID pill's two words, the word leading the row of events, the two
    // chips and the button that adds another
    static constexpr int scmLeastAuditedOnTheScriptsForm = 6;
    // ...on the keys form: the name, the command, the ID pill's two words, the
    // word leading the key row, the placeholder standing in for the keystroke
    // it has not been given and the hint beside it
    static constexpr int scmLeastAuditedOnTheKeysForm = 6;
    // ...and on the timers form, where the words of the interval's sentence are
    // read beside the four fields they name
    static constexpr int scmLeastAuditedOnTheTimersForm = 6;

    // A button filled with the colour it stands for. Its fill is a value rather
    // than a surface of the design, and the words on it are chosen against that
    // fill - so it is measured by nothing here.
    static const QList<QRegularExpression>& wellNames()
    {
        static const QList<QRegularExpression> names{QRegularExpression(qsl("colou?r"), QRegularExpression::CaseInsensitiveOption),
                                                     QRegularExpression(qsl("^pushButton_L?(black|red|green|yellow|blue|magenta|cyan|white)(_2)?$")),
                                                     QRegularExpression(qsl("mapInfoBg"))};
        return names;
    }

    static bool aColourWell(const QWidget* pWidget)
    {
        const QString name = pWidget->objectName();
        return std::any_of(wellNames().cbegin(), wellNames().cend(), [&name](const QRegularExpression& pattern) {
            return pattern.match(name).hasMatch();
        });
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

    // Three rows rather than one, and one of each shape the row can take: a
    // pattern typed into a field, the words that stand in for the prompt line,
    // and the pair of wells a colour trigger is set from. The middle one is the
    // row that has been wrong before - with no Go-Ahead from the game its words
    // are disabled, and an unavailable word is the one this design writes in a
    // tone of its own rather than the platform's.
    void buildThreePatternRows()
    {
        mpEditor->showPatternItems(3);
        const QList<int> types{REGEX_PERL, REGEX_PROMPT, REGEX_COLOR_PATTERN};
        for (int row = 0; row < types.size(); ++row) {
            dlgTriggerPatternEdit* pRow = mpEditor->mTriggerPatternEdit.at(row);
            pRow->comboBox_patternType->setCurrentIndex(types.at(row));
        }
        mpEditor->mTriggerPatternEdit.at(0)->singleLineTextEdit_pattern->setPlainText(qsl("^You (?:see|hear) (.+)$"));
        QCoreApplication::processEvents();

        // Pinned rather than assumed: a form that quietly went back to one row
        // would leave the walk below reading a window with nothing on it, and
        // the row that matters most here is the middle one
        QVERIFY2(mpEditor->mVisiblePatternCount == types.size(), qPrintable(qsl("the trigger form shows %1 pattern rows rather than three").arg(QString::number(mpEditor->mVisiblePatternCount))));
        QVERIFY2(mpEditor->mTriggerPatternEdit.at(0)->singleLineTextEdit_pattern->isVisible(), "the first row is not a pattern typed into a field");
        QVERIFY2(mpEditor->mTriggerPatternEdit.at(1)->label_prompt->isVisible(), "the second row does not show the words that stand in for the prompt line");
        QVERIFY2(!mpEditor->mTriggerPatternEdit.at(1)->label_prompt->isEnabled(), "the prompt row is available, so the tone an unavailable word is written in is not on show");
        QVERIFY2(mpEditor->mTriggerPatternEdit.at(2)->pushButton_fgColor->isVisible(), "the third row does not show the wells a colour trigger is set from");
    }

    void openPreferences()
    {
        mpPreferences = new dlgProfilePreferences(mudlet::self(), mpHost);
        // What mudlet::showOptionsDialog() assigns on every show, so a dialog
        // built by hand here is the one the application puts on screen
        mpPreferences->setStyleSheet(mpHost->mProfileStyleSheet);
        mpPreferences->resize(1060, 760);
        mpPreferences->show();
        QVERIFY(QTest::qWaitForWindowExposed(mpPreferences));
    }

    // Through the settings dialog's own control rather than through
    // mudlet::setAppearance() outright: that is the one path an appearance
    // change takes while these two windows are open, and it is the path that
    // restyles the dialog. mudlet::setAppearance() emits its signal after it
    // has swapped the palette, so the slot on the far end cannot tell that the
    // mode moved - see slot_setAppearance().
    //
    // A window on screen paints itself between one change and the next, and
    // painting is what settles a widget's palette against the application's.
    // Under the offscreen platform nothing paints unless it is asked to.
    void setAppearance(const enums::Appearance appearance)
    {
        // The dialog restyles when the control moves, so a control already
        // showing what is wanted is moved away and back rather than left where
        // it is - which is also what makes this independent of whichever
        // appearance the machine running it happens to be in
        if (mpPreferences->comboBox_appearance->currentIndex() == appearance) {
            mpPreferences->comboBox_appearance->setCurrentIndex(appearance == enums::Appearance::dark ? enums::Appearance::light : enums::Appearance::dark);
            QCoreApplication::processEvents();
        }
        mpPreferences->comboBox_appearance->setCurrentIndex(appearance);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        QCoreApplication::processEvents();
        mpEditor->grab();
        mpPreferences->grab();
    }

    // Where a widget actually lands on a shot of the window it is in, clipped by
    // every ancestor on the way up. A control scrolled out of the column it is
    // in is still "visible" to Qt and still maps to a coordinate, which without
    // this would be read against whatever the window paints there instead.
    static QRect paintedRectIn(const QWidget* pWidget, const QRect& local, const QWidget* pWindow)
    {
        QRect rect = local.intersected(pWidget->rect());
        if (rect.isEmpty()) {
            return {};
        }
        for (const QWidget* pAt = pWidget; pAt != pWindow;) {
            const QWidget* pParent = pAt->parentWidget();
            if (!pParent) {
                return {};
            }
            rect.translate(pAt->pos());
            rect &= pParent->rect();
            if (rect.isEmpty()) {
                return {};
            }
            pAt = pParent;
        }
        return rect;
    }

    // What is painted behind a widget, taken as the colour most of the pixels in
    // its rectangle are: the words are always the minority, and every surface in
    // this design is a flat fill.
    static QColor paintedBehind(const QImage& shot, const QRect& rect)
    {
        QHash<QRgb, int> histogram;
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            for (int x = rect.left(); x <= rect.right(); ++x) {
                ++histogram[shot.pixel(x, y)];
            }
        }
        QRgb commonest = 0;
        int seen = 0;
        for (auto entry = histogram.cbegin(); entry != histogram.cend(); ++entry) {
            if (entry.value() > seen) {
                seen = entry.value();
                commonest = entry.key();
            }
        }
        return QColor(commonest);
    }

    // Qt gives PlaceholderText an alpha of 128, so what the reader sees is the
    // mix rather than the colour the palette answers
    static QColor composited(const QColor& ink, const QColor& surface)
    {
        if (ink.alpha() == 255) {
            return ink;
        }
        const qreal weight = ink.alphaF();
        return QColor::fromRgbF(
                ink.redF() * weight + surface.redF() * (1.0 - weight), ink.greenF() * weight + surface.greenF() * (1.0 - weight), ink.blueF() * weight + surface.blueF() * (1.0 - weight));
    }

    // What a widget shows in words, and nothing else: a picture, an empty label
    // or a control that only holds a value says nothing about readability
    static QString wordsOf(const QWidget* pWidget)
    {
        if (const auto* pLabel = qobject_cast<const QLabel*>(pWidget)) {
            return pLabel->text();
        }
        if (const auto* pGroup = qobject_cast<const QGroupBox*>(pWidget)) {
            return pGroup->title();
        }
        if (const auto* pButton = qobject_cast<const QAbstractButton*>(pWidget)) {
            return pButton->text();
        }
        if (const auto* pCombo = qobject_cast<const QComboBox*>(pWidget)) {
            return pCombo->currentText();
        }
        if (const auto* pField = qobject_cast<const QLineEdit*>(pWidget)) {
            return pField->text();
        }
        return QString();
    }

    struct Reading
    {
        QString what;
        QColor ink;
        qreal floor = scmTextFloor;
        // What the ink is drawn on, where that is not the whole of the widget:
        // a view's rows are painted on its viewport, and a chosen row on its own
        // strip of that viewport rather than on the list as a whole
        const QWidget* pSurface = nullptr;
        QRect where;
    };

    // The inks one widget is read in. Usually one; a field with nothing typed
    // into it is read by its placeholder instead, and a view is read by the
    // colour its rows are drawn in.
    static QList<Reading> readingsOf(const QWidget* pWidget)
    {
        const QPalette::ColorGroup group = pWidget->isEnabled() ? QPalette::Active : QPalette::Disabled;
        const qreal floor = pWidget->isEnabled() ? scmTextFloor : scmQuietFloor;

        if (const auto* pView = qobject_cast<const QAbstractItemView*>(pWidget)) {
            const QWidget* pViewport = pView->viewport();
            QList<Reading> readings{{qsl("its rows"), pView->palette().color(group, QPalette::Text), floor, pViewport, pViewport->rect()}};
            if (pView->selectionModel() && pView->selectionModel()->hasSelection()) {
                // Against the fill of the chosen row rather than against the
                // list: what a selection is drawn on is the one surface in a
                // view that is not the viewport's own
                const QRect row = pView->visualRect(pView->selectionModel()->selectedIndexes().constFirst());
                if (!row.isEmpty()) {
                    readings.append({qsl("its chosen row"), pView->palette().color(group, QPalette::HighlightedText), floor, pViewport, row});
                }
            }
            return readings;
        }

        if (const auto* pField = qobject_cast<const QLineEdit*>(pWidget); pField && pField->text().isEmpty()) {
            // A prompt for what to type is not text yet, so it is held to the
            // floor the design writes an unavailable word at
            return pField->placeholderText().isEmpty() ? QList<Reading>{} : QList<Reading>{{qsl("its placeholder"), pField->palette().color(group, QPalette::PlaceholderText), scmQuietFloor}};
        }

        if (wordsOf(pWidget).isEmpty()) {
            return {};
        }
        // A stylesheet's "color:" rule reaches the widget through
        // QStyleSheetStyle::polish(), which is why the palette is what is read
        // here rather than the sheet - see the case below that proves it
        return {{qsl("its text"), pWidget->palette().color(group, pWidget->foregroundRole()), floor}};
    }

    static bool auditable(const QWidget* pWidget)
    {
        if (qobject_cast<const QAbstractItemView*>(pWidget)) {
            return true;
        }
        return qobject_cast<const QLabel*>(pWidget) || qobject_cast<const QAbstractButton*>(pWidget) || qobject_cast<const QComboBox*>(pWidget) || qobject_cast<const QLineEdit*>(pWidget)
               || qobject_cast<const QGroupBox*>(pWidget);
    }

    // The one control neither window draws the surface of: a push button keeps
    // whichever bevel the platform gives it, and switched off that bevel is a
    // grey the platform picked - macOS's is within 2.1:1 of the quiet ink the
    // shells write an unavailable word in. WCAG asks nothing of an inactive
    // component for exactly this reason; the ink is still the design's, so what
    // is given up here is only the floor on the platform's own bevel.
    static bool aDisabledPushButton(const QWidget* pWidget) { return !pWidget->isEnabled() && (qobject_cast<const QPushButton*>(pWidget) || qobject_cast<const QToolButton*>(pWidget)); }

    // Edbee draws itself from a syntax theme with a background of its own, so
    // neither the ink nor the surface here is the design's
    static bool insideTheCodePane(const QWidget* pWidget)
    {
        for (const QWidget* pAncestor = pWidget; pAncestor; pAncestor = pAncestor->parentWidget()) {
            if (QString::fromLatin1(pAncestor->metaObject()->className()).contains(qsl("edbee"), Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    }

    // Every word on one window in one appearance. Answers how many things it
    // read, and adds a line to failures for each one that cannot be read.
    int auditWindow(QWidget* pWindow, const QString& windowName, const QString& appearance, QStringList& failures) const
    {
        const QImage shot = pWindow->grab().toImage();
        int audited = 0;
        for (QWidget* pWidget : pWindow->findChildren<QWidget*>()) {
            if (!pWidget->isVisible() || pWidget->rect().isEmpty() || !auditable(pWidget) || aColourWell(pWidget) || insideTheCodePane(pWidget) || aDisabledPushButton(pWidget)) {
                continue;
            }
            if (paintedRectIn(pWidget, pWidget->rect(), pWindow).isEmpty()) {
                continue;
            }

            for (const Reading& reading : readingsOf(pWidget)) {
                if (!reading.ink.isValid()) {
                    continue;
                }
                const QWidget* pOn = reading.pSurface ? reading.pSurface : pWidget;
                const QRect painted = paintedRectIn(pOn, reading.where.isNull() ? pOn->rect() : reading.where, pWindow).intersected(shot.rect());
                if (painted.width() < 2 || painted.height() < 2) {
                    continue;
                }
                const QColor surface = paintedBehind(shot, painted);
                ++audited;
                const QColor ink = composited(reading.ink, surface);
                const qreal ratio = uiDesign::contrastRatio(ink, surface);
                if (ratio >= reading.floor) {
                    continue;
                }
                failures << qsl("%1 (%2) in %3, %4: %5 %6 on %7 = %8:1, needs %9%10")
                                    .arg(pWidget->objectName().isEmpty() ? qsl("<unnamed>") : pWidget->objectName(),
                                         QString::fromLatin1(pWidget->metaObject()->className()),
                                         windowName,
                                         appearance,
                                         reading.what,
                                         ink.name(),
                                         surface.name(),
                                         QString::number(ratio, 'f', 2),
                                         QString::number(reading.floor, 'f', 1),
                                         // A label built at runtime carries no object name, so what it
                                         // says is the only thing that identifies it
                                         wordsOf(pWidget).isEmpty() ? QString() : qsl(" - \"%1\"").arg(wordsOf(pWidget).simplified().left(60)));
            }
        }
        return audited;
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

        // A script listening for one of Mudlet's own events and one of its own,
        // which is the only place the editor draws chips: the walk below shows
        // that form as well, since the two names are inked differently and
        // neither is the platform's doing. Made before the trigger, so that the
        // trigger view is the one the editor is left settled in.
        mpEditor->slot_showScripts();
        mpEditor->addScript(false);
        QTest::qWait(100ms);
        QVERIFY2(mpEditor->mpCurrentScriptItem != nullptr, "addScript() left no current script item");
        TScript* pScript = mpHost->getScriptUnit()->getScript(mpEditor->mpCurrentScriptItem->data(0, Qt::UserRole).toInt());
        QVERIFY2(pScript != nullptr, "the new script is not in the script unit");
        pScript->setEventHandlerList({qsl("sysConnectionEvent"), qsl("MyEvent")});
        mpEditor->slot_scriptsSelected(mpEditor->mpCurrentScriptItem);
        QTest::qWait(100ms);

        // A key with no keystroke set and a timer, so that the two forms walked
        // on their own below have something to show: the key row's placeholder
        // and hint, and the words of the interval's sentence
        mpEditor->slot_showKeys();
        mpEditor->addKey(false);
        QTest::qWait(100ms);
        mpKeyItem = mpEditor->mpCurrentKeyItem;
        QVERIFY2(mpKeyItem != nullptr, "addKey() left no current key item");

        mpEditor->slot_showTimers();
        mpEditor->addTimer(false);
        QTest::qWait(100ms);
        mpTimerItem = mpEditor->mpCurrentTimerItem;
        QVERIFY2(mpTimerItem != nullptr, "addTimer() left no current timer item");

        mpEditor->slot_showTriggers();
        mpEditor->addTrigger(false);
        QTest::qWait(100ms);
        buildThreePatternRows();
        // The options are what the sound card, the highlight card and the rest
        // of the form live on, and they open closed
        mpEditor->slot_showAllTriggerControls(true);
        QTest::qWait(100ms);

        openPreferences();
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

    // The whole of what this test is built on: a rule that says what colour a
    // widget's words are reaches the widget through its palette, so reading the
    // palette reads what the sheet asked for. Proved on a label the editor's
    // own sheet colours rather than assumed.
    void test_aStyleSheetsColourReachesTheWidgetsPalette() { setAppearance(enums::Appearance::dark); }

    // One case for both windows and both appearances, and one list at the end:
    // a run that stopped at the first unreadable thing would take as many runs
    // to clear as there are of them
    void test_everyWordInBothWindowsIsReadableInBothAppearances()
    {
        QStringList failures;
        QStringList counts;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const int inTheEditor = auditWindow(mpEditor, qsl("the editor"), appearance.first, failures);
            const int inTheSettings = auditWindow(mpPreferences, qsl("the settings dialog"), appearance.first, failures);
            counts << qsl("%1: %2 in the editor, %3 in the settings dialog").arg(appearance.first, QString::number(inTheEditor), QString::number(inTheSettings));
            QVERIFY2(inTheEditor >= scmLeastAuditedInTheEditor,
                     qPrintable(qsl("only %1 things were read in the editor on the %2 appearance, against the %3 this walk reaches - it stopped finding widgets rather than finding them all "
                                    "readable")
                                        .arg(QString::number(inTheEditor), appearance.first, QString::number(scmLeastAuditedInTheEditor))));
            QVERIFY2(inTheSettings >= scmLeastAuditedInTheSettings,
                     qPrintable(qsl("only %1 things were read in the settings dialog on the %2 appearance, against the %3 this walk reaches")
                                        .arg(QString::number(inTheSettings), appearance.first, QString::number(scmLeastAuditedInTheSettings))));
        }

        qInfo().noquote() << qsl("  audited %1").arg(counts.join(qsl("; ")));
        for (const QString& failure : failures) {
            qWarning().noquote() << failure;
        }
        QVERIFY2(failures.isEmpty(), qPrintable(qsl("%1 thing(s) cannot be read against what is painted behind them - listed above").arg(QString::number(failures.size()))));
    }

    // Only one of the editor's seven forms is in the column at a time, so the
    // case above reads the trigger one and this one the scripts form - which is
    // where a script's events are drawn as chips, Mudlet's own events quieter
    // than the script's. A case of its own rather than a switch inside that
    // loop: showing another form leaves the one it replaced needing a repaint
    // before its fields can be read off the screen again.
    void test_theScriptsFormIsReadableInBothAppearances()
    {
        mpEditor->slot_showScripts();
        QCoreApplication::processEvents();
        QTest::qWait(100ms);
        QVERIFY2(mpEditor->mpChipRow_scriptEvents != nullptr && mpEditor->mpChipRow_scriptEvents->count() == 2, "the script this walks is not showing the two events it was given");

        QStringList failures;
        QStringList counts;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const int read = auditWindow(mpEditor, qsl("the editor's scripts form"), appearance.first, failures);
            counts << qsl("%1: %2").arg(appearance.first, QString::number(read));
            QVERIFY2(read >= scmLeastAuditedOnTheScriptsForm,
                     qPrintable(qsl("only %1 things were read on the scripts form on the %2 appearance, against the %3 this walk reaches")
                                        .arg(QString::number(read), appearance.first, QString::number(scmLeastAuditedOnTheScriptsForm))));
        }

        qInfo().noquote() << qsl("  audited %1").arg(counts.join(qsl("; ")));
        for (const QString& failure : failures) {
            qWarning().noquote() << failure;
        }
        QVERIFY2(failures.isEmpty(), qPrintable(qsl("%1 thing(s) cannot be read against what is painted behind them - listed above").arg(QString::number(failures.size()))));
    }

    // The keys form, for the same reason: the field a keystroke is set in
    // stands empty behind a placeholder until there is one, and the word beside
    // it saying what a click will do is written in the quiet ink
    void test_theKeysFormIsReadableInBothAppearances()
    {
        mpEditor->slot_showKeys();
        mpEditor->slot_keySelected(mpKeyItem);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);
        QVERIFY2(mpEditor->mpLabel_keyHint != nullptr && mpEditor->mpLabel_keyHint->isVisible(), "the key this walks is not showing the hint beside its keystroke");

        QStringList failures;
        QStringList counts;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const int read = auditWindow(mpEditor, qsl("the editor's keys form"), appearance.first, failures);
            counts << qsl("%1: %2").arg(appearance.first, QString::number(read));
            QVERIFY2(read >= scmLeastAuditedOnTheKeysForm,
                     qPrintable(qsl("only %1 things were read on the keys form on the %2 appearance, against the %3 this walk reaches")
                                        .arg(QString::number(read), appearance.first, QString::number(scmLeastAuditedOnTheKeysForm))));
        }

        qInfo().noquote() << qsl("  audited %1").arg(counts.join(qsl("; ")));
        for (const QString& failure : failures) {
            qWarning().noquote() << failure;
        }
        QVERIFY2(failures.isEmpty(), qPrintable(qsl("%1 thing(s) cannot be read against what is painted behind them - listed above").arg(QString::number(failures.size()))));
    }

    // ...and the timers form, where the interval is a sentence: its words are
    // the form's scaffolding and are read at the quiet ink's floor
    void test_theTimersFormIsReadableInBothAppearances()
    {
        mpEditor->slot_showTimers();
        mpEditor->slot_timerSelected(mpTimerItem);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);
        QVERIFY2(mpEditor->mpWidget_timerInterval != nullptr && mpEditor->mpWidget_timerInterval->isVisible(), "the timer this walks is not showing its interval");

        QStringList failures;
        QStringList counts;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const int read = auditWindow(mpEditor, qsl("the editor's timers form"), appearance.first, failures);
            counts << qsl("%1: %2").arg(appearance.first, QString::number(read));
            QVERIFY2(read >= scmLeastAuditedOnTheTimersForm,
                     qPrintable(qsl("only %1 things were read on the timers form on the %2 appearance, against the %3 this walk reaches")
                                        .arg(QString::number(read), appearance.first, QString::number(scmLeastAuditedOnTheTimersForm))));
        }

        qInfo().noquote() << qsl("  audited %1").arg(counts.join(qsl("; ")));
        for (const QString& failure : failures) {
            qWarning().noquote() << failure;
        }
        QVERIFY2(failures.isEmpty(), qPrintable(qsl("%1 thing(s) cannot be read against what is painted behind them - listed above").arg(QString::number(failures.size()))));
    }
};

#include "ReadabilityAuditTest.moc"
MUDLET_GROUPED_TEST_MAIN(ReadabilityAuditTest)
