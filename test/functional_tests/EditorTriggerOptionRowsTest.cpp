/***************************************************************************
 *   Copyright (C) 2026 by Mudlet Developers - mudlet@mudlet.org           *
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
 * A trigger's options are two rows of its form - Matching and Firing - between
 * the row its name is typed on and the list of its patterns. They were a 280px
 * column of four cards beside those patterns, opened by a button, put away by a
 * button, and folded away again by the window being either short or narrow.
 *
 * What the rows are held to here:
 *
 * - They sit between the head row and the pattern list, and the word leading
 *   each of them starts where every other form's lead word does.
 * - A narrow editor costs the rows a line rather than costing the pattern rows
 *   their width: at 1000px they wrap onto more lines than at 1400px, the list
 *   grows no horizontal bar, and the form pane follows the height.
 * - The code pane still keeps its third of the two panes underneath them.
 * - The two matching modes are radio buttons drawn as joined segments, so a
 *   screen reader still says which of the two is chosen; they write into
 *   spinBox_lineMargin, which is where the trigger is saved from.
 * - With one pattern there is nothing to combine, so the segments and the line
 *   count are greyed out and the caption saying why is on show.
 * - The switches are not gates: choosing a sound file turns the sound on, and
 *   choosing a colour turns the highlight on.
 * - Every control on the rows carries an accessible name and a plain-text
 *   description, since Qt would otherwise read a screen reader the rich text a
 *   tooltip is written in.
 * - The tab chain runs the head row, the Matching row, the Firing row and then
 *   the patterns.
 *
 * Run with: ctest -R EditorTriggerOptionRowsTest -V
 */

#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest/QtTest>
#include <chrono>
#include <cstdlib>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TTrigger.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgTriggerEditor.h"
#include "dlgTriggerPatternEdit.h"
#include "dlgTriggersMainArea.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorTriggerOptionRowsTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    QTreeWidgetItem* mpThreePatternRow = nullptr;
    QTreeWidgetItem* mpOnePatternRow = nullptr;
    const QString mProfileName = qsl("EditorTriggerOptionRows-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    // The two widths every wrapping case is measured between, and the height
    // the seam is measured at
    static constexpr int scmWideEditor = 1400;
    static constexpr int scmNarrowEditor = 1000;
    static constexpr int scmTallEditor = 900;
    // Short enough that the form asks for more of the two panes than the code
    // pane's share leaves it, which is the whole of what the last case measures
    static constexpr int scmShortEditor = 700;
    // The seam is placed by hand often enough to be a pixel or two off what was
    // asked for
    static constexpr int scmSeamTolerance = 2;
    // Mirrors scmEditorSegmentPaddingHorizontal in src/dlgTriggerEditor.cpp,
    // which is file-local to it, and what the hairline either side of it comes
    // to. An indicator that came back would add its own width and the gap after
    // it - some twenty pixels - on top of these.
    static constexpr int scmSegmentPadding = 12;
    static constexpr int scmSegmentSlack = 6;

    void deleteProfileDirectory(const QString& profileName)
    {
        QDir dir(mudlet::getMudletPath(enums::profileHomePath, profileName));
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }

    void settle()
    {
        QCoreApplication::processEvents();
        QTest::qWait(80ms);
        QCoreApplication::processEvents();
    }

    dlgTriggersMainArea* form() const { return mpEditor->mpTriggersMainArea; }

    QGroupBox* matchingRow() const { return form()->findChild<QGroupBox*>(qsl("editorMatchingRow")); }

    QGroupBox* firingRow() const { return form()->findChild<QGroupBox*>(qsl("editorFiringRow")); }

    int topEdgeOf(const QWidget* pWidget) const { return pWidget->mapTo(mpEditor, QPoint(0, 0)).y(); }

    int leftEdgeOf(const QWidget* pWidget) const { return pWidget->mapTo(mpEditor, QPoint(0, 0)).x(); }

    void chooseTrigger(QTreeWidgetItem* pRow)
    {
        mpEditor->slot_showTriggers();
        settle();
        mpEditor->treeWidget_triggers->setCurrentItem(pRow);
        mpEditor->slot_triggerSelected(pRow);
        settle();
        mpEditor->hideSystemMessageArea();
        settle();
    }

    // Settled twice: the width the form is given is answered on the next pass
    // through the event loop, and the refit that follows moves the seam on the
    // pass after that
    void resizeTheEditor(const int width, const int height)
    {
        mpEditor->resize(width, height);
        settle();
        settle();
    }

    // Every control on the two rows that the keyboard can reach
    QList<QWidget*> focusableControlsOnTheRows() const
    {
        QList<QWidget*> controls;
        for (const QGroupBox* pRow : {matchingRow(), firingRow()}) {
            for (QWidget* pWidget : pRow->findChildren<QWidget*>()) {
                // Qt's own parts of a control - a spin box's inner line edit -
                // are the control as far as a screen reader is concerned, and
                // are named by the control around them
                if (pWidget->objectName().startsWith(qsl("qt_"))) {
                    continue;
                }
                if (pWidget->focusPolicy() != Qt::NoFocus && pWidget->isVisibleTo(mpEditor)) {
                    controls << pWidget;
                }
            }
        }
        return controls;
    }

    // The chain the keyboard actually walks, from the command field onwards,
    // over what is visible and can hold the focus
    QList<QWidget*> tabChainFromTheCommandField() const
    {
        QList<QWidget*> chain;
        QWidget* pAt = form()->lineEdit_trigger_command;
        for (int step = 0; step < 400; ++step) {
            pAt = pAt->nextInFocusChain();
            if (!pAt || pAt == form()->lineEdit_trigger_command) {
                break;
            }
            if (pAt->focusPolicy() != Qt::NoFocus && pAt->isVisibleTo(mpEditor)) {
                chain << pAt;
            }
        }
        return chain;
    }

    static QString describe(const QWidget* pWidget) { return pWidget->objectName().isEmpty() ? QString::fromLatin1(pWidget->metaObject()->className()) : pWidget->objectName(); }

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

        mpHost = TestProfile::create(mProfileName, mLocalhost, mPort);
        QVERIFY2(mpHost != nullptr, "No active host available for the test.");
        QSignalSpy connected(&(mpHost->mTelnet), &cTelnet::signal_connected);
        QVERIFY2(connected.wait(2000), "Could not connect with the host.");

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(scmWideEditor, scmTallEditor);
        settle();

        mpEditor->slot_showTriggers();
        mpEditor->addTrigger(false);
        settle();
        mpThreePatternRow = mpEditor->mpCurrentTriggerItem;
        QVERIFY2(mpThreePatternRow != nullptr, "the three-pattern trigger these cases edit could not be made");
        mpEditor->showPatternItems(3);
        mpEditor->mTriggerPatternEdit.at(0)->singleLineTextEdit_pattern->setPlainText(qsl("There's water ahead of you. You'll have to swim in that direction."));
        mpEditor->mTriggerPatternEdit.at(1)->singleLineTextEdit_pattern->setPlainText(qsl("You'll have to swim to make it through the water in that direction."));
        mpEditor->mTriggerPatternEdit.at(2)->singleLineTextEdit_pattern->setPlainText(qsl("The water is too deep for you to walk that way, you must swim."));
        mpEditor->slot_saveEdits();
        settle();

        mpEditor->addTrigger(false);
        settle();
        mpOnePatternRow = mpEditor->mpCurrentTriggerItem;
        QVERIFY2(mpOnePatternRow != nullptr, "the one-pattern trigger these cases edit could not be made");
        mpEditor->showPatternItems(1);
        mpEditor->mTriggerPatternEdit.at(0)->singleLineTextEdit_pattern->setPlainText(qsl("You'll have to SWIM"));
        mpEditor->slot_saveEdits();
        settle();
    }

    void cleanupTestCase()
    {
        mpEditor = nullptr;
        mpHost = nullptr;
        delete mpServer;
        mpServer = nullptr;
        if (mudlet::self()) {
            deleteProfileDirectory(mProfileName);
            delete mudlet::self();
        }
        mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME") : qputenv("XDG_CONFIG_HOME", mSavedXdg);
    }

    void init()
    {
        if (!mpEditor) {
            return;
        }
        resizeTheEditor(scmWideEditor, scmTallEditor);
    }

    // (a) Where the rows are, and what they line up with
    void test_theTwoRowsSitBetweenTheNameAndThePatterns()
    {
        chooseTrigger(mpThreePatternRow);

        QVERIFY2(matchingRow() != nullptr && firingRow() != nullptr, "the trigger form has no option rows");
        QVERIFY2(matchingRow()->isVisible() && firingRow()->isVisible(), "the option rows are not on show, and nothing puts them away any more");

        const int headTop = topEdgeOf(form()->widget_top);
        const int matchingTop = topEdgeOf(matchingRow());
        const int firingTop = topEdgeOf(firingRow());
        const int patternsTop = topEdgeOf(form()->widget_left);
        qInfo().noquote() << qsl("  head row at %1, Matching at %2, Firing at %3, patterns at %4").arg(headTop).arg(matchingTop).arg(firingTop).arg(patternsTop);

        QVERIFY2(headTop < matchingTop, "the Matching row is not under the row the name is typed on");
        QVERIFY2(matchingTop < firingTop, "the Firing row is not under the Matching row");
        QVERIFY2(firingTop < patternsTop, "the pattern list is not under the Firing row");

        // The word leading each row starts where the Name label does, and the
        // control after it starts where the Name field does - which is the
        // whole of what one lead width buys
        QLabel* pName = form()->label_trigger_name;
        for (QLabel* pLead : {mpEditor->mpLabel_matchingRow, mpEditor->mpLabel_firingRow}) {
            QVERIFY2(pLead->property("editorRowLabel").toBool(), qPrintable(qsl("the word \"%1\" is not written in the quiet ink a form's scaffolding takes").arg(pLead->text())));
            QCOMPARE(leftEdgeOf(pLead), leftEdgeOf(pName));
        }
        qInfo().noquote() << qsl("  the Name label is %1px wide and the lead words %2px, with the Name field at x=%3 and the Matching row at x=%4")
                                     .arg(pName->width())
                                     .arg(mpEditor->mpLabel_matchingRow->width())
                                     .arg(leftEdgeOf(form()->lineEdit_trigger_name))
                                     .arg(leftEdgeOf(matchingRow()));
        QCOMPARE(leftEdgeOf(matchingRow()), leftEdgeOf(form()->lineEdit_trigger_name));
        QCOMPARE(leftEdgeOf(firingRow()), leftEdgeOf(form()->lineEdit_trigger_name));
    }

    // (b) A narrow editor costs the rows a line, not the pattern rows their
    // width. This is what replaces the fold that used to take the options away.
    void test_aNarrowEditorWrapsTheOptionRowsRatherThanThePatternRows()
    {
        chooseTrigger(mpThreePatternRow);

        const int wideMatching = matchingRow()->height();
        const int wideFiring = firingRow()->height();
        const int widePane = mpEditor->splitter_right->sizes().at(0);

        resizeTheEditor(scmNarrowEditor, scmTallEditor);
        const int narrowMatching = matchingRow()->height();
        const int narrowFiring = firingRow()->height();
        const int narrowPane = mpEditor->splitter_right->sizes().at(0);
        qInfo().noquote() << qsl("  at %1px the rows are %2 and %3 tall in a form pane of %4; at %5px they are %6 and %7 in a pane of %8")
                                     .arg(scmWideEditor)
                                     .arg(wideMatching)
                                     .arg(wideFiring)
                                     .arg(widePane)
                                     .arg(scmNarrowEditor)
                                     .arg(narrowMatching)
                                     .arg(narrowFiring)
                                     .arg(narrowPane);

        QVERIFY2(narrowMatching + narrowFiring > wideMatching + wideFiring,
                 qPrintable(qsl("the two rows are %1px tall at %2px of window and %3px at %4px - they did not wrap onto another line")
                                    .arg(narrowMatching + narrowFiring)
                                    .arg(scmNarrowEditor)
                                    .arg(wideMatching + wideFiring)
                                    .arg(scmWideEditor)));
        QVERIFY2(!mpEditor->mpScrollArea->horizontalScrollBar()->isVisible(),
                 qPrintable(qsl("the pattern list is scrolling sideways in a %1px window, with %2px for a row that wants %3px")
                                    .arg(scmNarrowEditor)
                                    .arg(mpEditor->mpScrollArea->viewport()->width())
                                    .arg(mpEditor->mpWidget_triggerItems->minimumSizeHint().width())));
        QVERIFY2(narrowPane > widePane,
                 qPrintable(qsl("the rows grew by %1px and the form pane stayed at %2px, so the extra lines are being drawn over the patterns")
                                    .arg(narrowMatching + narrowFiring - wideMatching - wideFiring)
                                    .arg(narrowPane)));
    }

    // (c) ...and the code pane still keeps its third of the two panes, in a
    // window short enough for the form to want more than that
    void test_theCodePaneKeepsItsFloorInAShortWindow()
    {
        chooseTrigger(mpThreePatternRow);
        resizeTheEditor(scmNarrowEditor, scmShortEditor);

        const QList<int> sizes = mpEditor->splitter_right->sizes();
        QVERIFY2(sizes.size() >= 2, "the right hand splitter has lost a pane");
        const int paneTotal = sizes.at(0) + sizes.at(1);
        const int floor = mpEditor->codePaneFloor(paneTotal);
        qInfo().noquote() << qsl("  in a %1x%2 window the panes are %3 / %4 of %5, where the code pane keeps %6")
                                     .arg(scmNarrowEditor)
                                     .arg(scmShortEditor)
                                     .arg(sizes.at(0))
                                     .arg(sizes.at(1))
                                     .arg(paneTotal)
                                     .arg(floor);

        // Held at the ceiling rather than sitting under it: a window the form
        // fits in with room to spare says nothing about what the code pane keeps
        QVERIFY2(std::abs(sizes.at(0) - (paneTotal - floor)) <= scmSeamTolerance,
                 qPrintable(qsl("the form is at %1 of the %2 the two panes have, rather than at the %3 the code pane's share leaves it - this window is not short enough for the case to say anything")
                                    .arg(sizes.at(0))
                                    .arg(paneTotal)
                                    .arg(paneTotal - floor)));
        QVERIFY2(sizes.at(1) >= floor - scmSeamTolerance,
                 qPrintable(qsl("the form was given %1 of the %2 the two panes have, leaving the code pane %3 against the %4 it keeps").arg(sizes.at(0)).arg(paneTotal).arg(sizes.at(1)).arg(floor)));
        // ...and that floor is a third of the two panes, written out here rather
        // than read from codePaneFloor(), which is the thing being held
        QVERIFY2(sizes.at(1) >= paneTotal / 3 - scmSeamTolerance,
                 qPrintable(qsl("the code pane was left %1 of the %2 the two panes have, where a third of them is %3").arg(sizes.at(1)).arg(paneTotal).arg(paneTotal / 3)));
    }

    // (d) The two modes are one control of two segments, and they are still the
    // radio pair spinBox_lineMargin is written from
    void test_theSegmentsWriteTheMatchingModeIntoTheHiddenSpinBox()
    {
        chooseTrigger(mpThreePatternRow);

        QRadioButton* pAny = mpEditor->mpRadioButton_matchAny;
        QRadioButton* pAll = mpEditor->mpRadioButton_matchAll;
        QSpinBox* pWithin = mpEditor->mpSpinBox_matchWithinLines;

        pAll->setChecked(true);
        settle();
        QVERIFY2(!pAny->isChecked(), "both segments are on at once, so they are no longer one exclusive choice");
        QVERIFY2(mpEditor->mpWidget_matchWithinRow->isEnabled(), "the line count is greyed out in the All mode, which is the mode it belongs to");
        pWithin->setValue(4);
        settle();
        QCOMPARE(form()->spinBox_lineMargin->value(), 4);

        pAny->setChecked(true);
        settle();
        QVERIFY2(!pAll->isChecked(), "choosing Any left All chosen as well");
        QVERIFY2(!mpEditor->mpWidget_matchWithinRow->isEnabled(), "the line count is still available in the Any mode, which carries none");
        QCOMPARE(form()->spinBox_lineMargin->value(), -1);

        // Drawn as a box round the words rather than as a dot beside them: the
        // indicator is given no size and no gap, so the segment is its own text
        // and the padding round it and nothing else
        const int textWidth = pAll->fontMetrics().horizontalAdvance(pAll->text());
        const int roundTheText = pAll->width() - textWidth;
        qInfo().noquote() << qsl("  \"%1\" is %2px wide against %3px of text, so %4px is round it").arg(pAll->text()).arg(pAll->width()).arg(textWidth).arg(roundTheText);
        QVERIFY2(pAll->property("editorSegment").toBool(), "the segment does not carry the property its rules select on");
        QVERIFY2(roundTheText <= 2 * (scmSegmentPadding + uiDesign::scmInputBorderWidth) + scmSegmentSlack,
                 qPrintable(qsl("\"%1\" is %2px wide for %3px of text - %4px round it, which is a dot and the gap after it rather than the padding alone")
                                    .arg(pAll->text())
                                    .arg(pAll->width())
                                    .arg(textWidth)
                                    .arg(roundTheText)));
    }

    // (e) With one pattern there is nothing to combine, so the choice is greyed
    // out and the caption says why
    void test_onceMorePatternsAreThereTheModesBecomeAvailable()
    {
        chooseTrigger(mpOnePatternRow);
        QCOMPARE(mpEditor->mVisiblePatternCount, 1);
        QVERIFY2(!mpEditor->mpWidget_matchModeRows->isEnabled(), "the two segments are available on a trigger with one pattern, which has nothing to combine");
        QVERIFY2(!mpEditor->mpWidget_matchWithinRow->isEnabled(), "the line count is available on a trigger with one pattern");
        QVERIFY2(!mpEditor->mpLabel_matchModeHint->isHidden(), "the caption saying why the modes are greyed out is not on show");

        chooseTrigger(mpThreePatternRow);
        QCOMPARE(mpEditor->mVisiblePatternCount, 3);
        QVERIFY2(mpEditor->mpWidget_matchModeRows->isEnabled(), "the two segments are still greyed out on a trigger with three patterns");
        QVERIFY2(mpEditor->mpLabel_matchModeHint->isHidden(), "the caption is still on show on a trigger that has patterns to combine");
    }

    // (f) The switches are not gates over what stands beside them: a choice
    // made in either turns its own switch on
    void test_aChoiceTurnsItsOwnSwitchOn()
    {
        chooseTrigger(mpThreePatternRow);
        form()->checkBox_soundTrigger->setChecked(false);
        form()->checkBox_triggerColorizer->setChecked(false);
        mpEditor->showTriggerSoundFile(QString());
        settle();
        QVERIFY2(!form()->toolButton_clearSoundFile->isVisible(), "the cross that forgets the sound file is there with no file to forget");

        const QString chosen = qsl("/tmp/EditorTriggerOptionRowsTest/water-splash.wav");
        mpEditor->mpUndoStack->clear();
        mpEditor->acceptTriggerSoundFile(chosen);
        settle();
        QVERIFY2(form()->checkBox_soundTrigger->isChecked(), "choosing a sound file left the sound switched off, so the file is set and silent");
        QCOMPARE(mpEditor->triggerSoundFilePath(), chosen);
        QVERIFY2(form()->lineEdit_soundFile->text().contains(qsl("water-splash")), qPrintable(qsl("the field reads \"%1\" rather than the file's name").arg(form()->lineEdit_soundFile->text())));
        QVERIFY2(form()->toolButton_clearSoundFile->isVisible(), "the cross that forgets the sound file is away with a file set");
        // The file and the switch, one entry each and in that order
        qInfo().noquote() << qsl("  choosing a sound file pushed %1 undo entries").arg(mpEditor->mpUndoStack->count());
        QCOMPARE(mpEditor->mpUndoStack->count(), 2);

        mpEditor->slot_clearSoundFile();
        settle();
        QVERIFY2(!form()->toolButton_clearSoundFile->isVisible(), "the cross is still there after the file it forgets was forgotten");
        QVERIFY2(mpEditor->triggerSoundFilePath().isEmpty(), "the field still stands for a file after being cleared");

        mpEditor->mpUndoStack->clear();
        mpEditor->turnTriggerHighlightOn();
        settle();
        QVERIFY2(form()->checkBox_triggerColorizer->isChecked(), "choosing a colour left the highlight switched off, so the colour is set and unused");
        QCOMPARE(mpEditor->mpUndoStack->count(), 1);
    }

    // (g) A screen reader is told what each control is and what it does, in
    // words rather than in the rich text a tooltip is written in
    void test_everyControlOnTheRowsNamesItselfToAScreenReader()
    {
        chooseTrigger(mpThreePatternRow);

        const QList<QWidget*> controls = focusableControlsOnTheRows();
        QVERIFY2(controls.size() >= 9, qPrintable(qsl("only %1 focusable controls were found on the two rows, so this walk is not covering them").arg(controls.size())));
        QStringList failures;
        for (QWidget* pControl : controls) {
            if (pControl->accessibleName().isEmpty()) {
                failures << qsl("%1 has no accessible name").arg(describe(pControl));
            }
            const QString description = pControl->accessibleDescription();
            if (description.isEmpty()) {
                failures << qsl("%1 has no accessible description, so a screen reader falls back to its rich text tooltip").arg(describe(pControl));
            } else if (description.contains(QLatin1Char('<'))) {
                failures << qsl("%1 is described as \"%2\", which is markup being read out").arg(describe(pControl), description);
            }
        }
        qInfo().noquote() << qsl("  %1 focusable controls on the two rows were read").arg(controls.size());
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(qsl("\n"))));

        // ...and the rows themselves are groupings rather than loose text: the
        // word leading a row is the form's own label and reaches nothing
        QCOMPARE(matchingRow()->accessibleName(), mpEditor->mpLabel_matchingRow->text());
        QCOMPARE(firingRow()->accessibleName(), mpEditor->mpLabel_firingRow->text());
    }

    // (h) The keyboard reads the form the way the eye does
    void test_theTabChainRunsTheRowsBeforeThePatterns()
    {
        chooseTrigger(mpThreePatternRow);

        const QList<QWidget*> chain = tabChainFromTheCommandField();
        const auto at = [&chain](QWidget* pWidget) {
            return chain.indexOf(pWidget);
        };

        const int anySegment = at(mpEditor->mpRadioButton_matchAny);
        const int allSegment = at(mpEditor->mpRadioButton_matchAll);
        const int within = at(mpEditor->mpSpinBox_matchWithinLines);
        const int everyOccurrence = at(form()->checkBox_perlSlashGOption);
        const int keepFiring = at(form()->spinBox_stayOpen);
        const int onlyMatches = at(form()->checkBox_filterTrigger);
        const int playSound = at(form()->checkBox_soundTrigger);
        const int soundFile = at(form()->lineEdit_soundFile);
        const int highlight = at(form()->checkBox_triggerColorizer);
        const int foreground = at(form()->pushButtonFgColor);
        const int background = at(form()->pushButtonBgColor);
        const int firstPattern = at(mpEditor->mTriggerPatternEdit.at(0)->singleLineTextEdit_pattern);

        QStringList reading;
        for (QWidget* pWidget : chain) {
            reading << describe(pWidget);
            if (reading.size() >= 16) {
                break;
            }
        }
        qInfo().noquote() << qsl("  the chain from the command field reads: %1").arg(reading.join(qsl(" -> ")));

        const QList<QPair<QString, int>> ordered{{qsl("Any pattern"), anySegment},
                                                 {qsl("All patterns"), allSegment},
                                                 {qsl("within lines"), within},
                                                 {qsl("Every occurrence"), everyOccurrence},
                                                 {qsl("Keep firing"), keepFiring},
                                                 {qsl("Only pass matches"), onlyMatches},
                                                 {qsl("Play a sound"), playSound},
                                                 {qsl("sound file"), soundFile},
                                                 {qsl("Highlight matches"), highlight},
                                                 {qsl("Foreground"), foreground},
                                                 {qsl("Background"), background},
                                                 {qsl("the first pattern"), firstPattern}};
        for (const auto& [name, index] : ordered) {
            QVERIFY2(index >= 0, qPrintable(qsl("%1 is not in the tab chain at all").arg(name)));
        }
        for (int step = 1; step < ordered.size(); ++step) {
            QVERIFY2(ordered.at(step - 1).second < ordered.at(step).second,
                     qPrintable(qsl("%1 is reached at %2 and %3 at %4, so the chain does not read left to right down the rows")
                                        .arg(ordered.at(step - 1).first)
                                        .arg(ordered.at(step - 1).second)
                                        .arg(ordered.at(step).first)
                                        .arg(ordered.at(step).second)));
        }
    }

    // (i) A well with no colour of its own still says what it does, in the one
    // word that fits on it
    void test_aColourlessWellStillReadsKeep()
    {
        // Chosen first, so that the colours written below are not saved over by
        // the trigger being left; loaded again without leaving it
        chooseTrigger(mpThreePatternRow);
        TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(mpThreePatternRow->data(0, Qt::UserRole).toInt());
        QVERIFY2(pT != nullptr, "the trigger these cases edit is gone from the profile");
        pT->setColorizerFgColor(QColorConstants::Transparent);
        pT->setColorizerBgColor(QColorConstants::Transparent);
        mpEditor->slot_triggerSelected(mpThreePatternRow);
        settle();
        qInfo().noquote() << qsl("  the wells read \"%1\" and \"%2\", and are described as \"%3\" and \"%4\"")
                                     .arg(form()->pushButtonFgColor->text(),
                                          form()->pushButtonBgColor->text(),
                                          form()->pushButtonFgColor->accessibleDescription(),
                                          form()->pushButtonBgColor->accessibleDescription());
        QVERIFY2(!form()->pushButtonFgColor->text().isEmpty(), "the foreground well with no colour of its own says nothing at all");
        QVERIFY2(!form()->pushButtonBgColor->text().isEmpty(), "the background well with no colour of its own says nothing at all");
        QVERIFY2(!form()->pushButtonFgColor->accessibleDescription().isEmpty(), "the foreground well tells a screen reader nothing about what it holds");
    }
};

#include "EditorTriggerOptionRowsTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorTriggerOptionRowsTest)
