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
 * The notice at the top of the column an item is edited in, and what the
 * pattern list under it does to the seam over the code pane.
 *
 * Thirteen things this holds them to:
 *
 * - A notice takes its own height and no more. The column is the notice, the
 *   gap under it and the form, so the fields sit exactly where they sit with
 *   no notice, moved down by the notice. They used to spread apart: the
 *   notice's wrapping label reported the height its text would have in a
 *   column a hundred pixels wide as its minimum, and the column was held to
 *   that.
 *
 * - With nothing chosen there is no form and no code pane, so the notice is
 *   the whole of the column - at the top of the pane, at its own height,
 *   rather than centred in it or stretched down it.
 *
 * - A form with no code pane under it - a Lua table has no value to edit -
 *   leads the pane the same way, at the height its own fields ask for. It was
 *   held to that height and then centred in the pane by QSplitter, which left
 *   the Name field floating half way down the window.
 *
 * - A pattern row added or taken away resizes the pane it is in. One click on
 *   Add pattern used to leave the pane where it was, so the new row pushed the
 *   button under the code heading and a scroll bar appeared beside the list.
 *
 * - A height the reader drags the seam to is kept net of the notice: a notice
 *   put up and taken down over it hands its room back to the pattern list.
 *   The drag used to be written down with whatever notice was up inside it, so
 *   each one that came and went afterwards took its own height off the list.
 *
 * - The count beside the "show hidden variables" switch belongs to the
 *   Variables view and leaves with it.
 *
 * Run with: ctest -R EditorNoticeSeamTest -V
 */

#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitterHandle>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest/QtTest>
#include <algorithm>
#include <chrono>

#include "EditorPlaceholderButton.h"
#include "GripSplitter.h"
#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "SingleLineTextEdit.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgAliasMainArea.h"
#include "dlgSourceEditorArea.h"
#include "dlgSystemMessageArea.h"
#include "dlgTriggerEditor.h"
#include "dlgTriggerPatternEdit.h"
#include "dlgTriggersMainArea.h"
#include "dlgVarsMainArea.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorNoticeSeamTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    // The rows the two items these cases edit are on. A fresh profile ships
    // with items of its own, so neither is the first row of its tree.
    QTreeWidgetItem* mpTriggerRow = nullptr;
    QTreeWidgetItem* mpAliasRow = nullptr;
    const QString mProfileName = qsl("EditorNoticeSeam-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    // How far the Add pattern button may sit off the code heading before the
    // room between them reads as a void rather than as the gap the column is
    // laid out with: the margins between them, and a pixel or two of
    // rounding in where the seam is placed
    static constexpr int scmRoomUnderTheLastRow = 22;

    // A drag of the seam, in the small steps a pointer really arrives in
    static constexpr int scmDragTravel = 120;
    static constexpr int scmDragStep = 20;

    // The seam is placed by hand often enough to be a pixel or two off what was
    // asked for, and this holds a height against where it was put
    static constexpr int scmSeamTolerance = 1;

    // The window every case here runs in
    static constexpr int scmEditorWidth = 1200;
    static constexpr int scmEditorHeight = 800;

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

    // The one frame every measurement is taken in: each widget is nested a
    // different number of layouts deep
    int topEdgeOf(const QWidget* pWidget) const { return pWidget->mapTo(mpEditor, QPoint(0, 0)).y(); }

    int bottomEdgeOf(const QWidget* pWidget) const { return topEdgeOf(pWidget) + pWidget->height() - 1; }

    int columnSpacing() const
    {
        QLayout* pLayout = mpEditor->mpNonCodeWidgets->layout();
        return pLayout ? pLayout->spacing() : 0;
    }

    // The package warning, word for word, so that a case measures the notice
    // against the length of text a reader actually meets
    void raiseTheNotice()
    {
        mpEditor->showWarning(qsl("This item is part of a package. To best preserve your changes, copy this item before editing as package upgrades may overwrite modifications."), false);
        settle();
    }

    void takeTheNoticeDown()
    {
        mpEditor->hideSystemMessageArea();
        settle();
    }

    // The alias made in initTestCase(), loaded into the form the way choosing
    // its row loads it, with nothing over that form
    void openTheAlias()
    {
        QVERIFY2(mpAliasRow != nullptr, "the alias these cases edit was never made");
        mpEditor->slot_showAliases();
        settle();
        mpEditor->treeWidget_aliases->setCurrentItem(mpAliasRow);
        mpEditor->slot_aliasSelected(mpAliasRow);
        settle();
        takeTheNoticeDown();
    }

    // ...and the trigger, chosen the way clicking its row chooses it - which
    // is also what puts whatever the item has to say over the form
    void chooseTheTrigger()
    {
        QVERIFY2(mpTriggerRow != nullptr, "the trigger these cases edit was never made");
        mpEditor->slot_showTriggers();
        settle();
        mpEditor->treeWidget_triggers->setCurrentItem(mpTriggerRow);
        mpEditor->slot_triggerSelected(mpTriggerRow);
        settle();
    }

    // ...which comes back with the three patterns it was saved with however
    // many rows the case before this one left on show
    void openTheTrigger()
    {
        chooseTheTrigger();
        takeTheNoticeDown();
        QVERIFY2(mpEditor->mVisiblePatternCount == 3,
                 qPrintable(qsl("the trigger these cases edit came back with %1 pattern rows rather than the three it was saved with").arg(mpEditor->mVisiblePatternCount)));
    }

    bool patternListScrolls() const { return mpEditor->mpScrollArea->verticalScrollBar()->isVisible(); }

    // Where the Add pattern button sits against the list it is the last thing
    // in: negative while it is above the bottom of what can be seen
    int addPatternButtonOverhang() const { return bottomEdgeOf(mpEditor->mpButton_addPattern) - bottomEdgeOf(mpEditor->mpScrollArea->viewport()); }

    // The row a global of this name is on, which the Variables tree keeps under
    // its one root
    QTreeWidgetItem* variableRow(const QString& name) const
    {
        QTreeWidgetItem* pBase = mpEditor->treeWidget_variables->topLevelItem(0);
        for (int i = 0; pBase && i < pBase->childCount(); ++i) {
            if (pBase->child(i)->text(0) == name) {
                return pBase->child(i);
            }
        }
        return nullptr;
    }

    void chooseTheVariable(QTreeWidgetItem* pRow)
    {
        mpEditor->treeWidget_variables->setCurrentItem(pRow);
        mpEditor->slot_variableSelected(pRow);
        settle();
    }

    // A move with the button held, which QTest::mouseMove does not send: it
    // reports no button down, and a splitter only follows a pointer that has
    // one. The global point is what leads, since the handle moves out from
    // under the pointer as the panes resize.
    static void dragTo(QWidget* pHandle, const QPointF& global)
    {
        QMouseEvent move(QEvent::MouseMove, pHandle->mapFromGlobal(global), global, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(pHandle, &move);
    }

    // A trigger that will not compile says so whenever it is chosen, which is
    // what puts a notice over the form at the moment the pane is being sized
    void breakTheTriggersScript()
    {
        mpEditor->mpSourceEditorEdbee->textDocument()->setText(qsl("local a = 1\nlocal b = 1 +* 2\n"));
        mpEditor->slot_saveEdits();
        settle();
    }

    void mendTheTriggersScript()
    {
        mpEditor->mpSourceEditorEdbee->textDocument()->setText(QString());
        mpEditor->slot_saveEdits();
        settle();
    }

    // The seam over the code pane, pulled down by scmDragTravel in the small
    // steps a real pointer arrives in - which is the only thing that reaches
    // slot_rightSplitterMoved(), and so the only thing that records a height.
    //
    // Not past what the seam will hand that height back at, though: a drag is
    // free to leave the code pane as little as the reader likes, but the snap
    // that reads the height afterwards keeps that pane its share of the two
    // (codePaneFloor()), and a case that drags past the share is measuring the
    // clamp rather than what it came to measure. roomForANoticeToCome is what
    // the notice a case will put up afterwards takes out of the same pane.
    void dragTheSeamDown(const int roomForANoticeToCome = 0)
    {
        QSplitterHandle* pHandle = mpEditor->splitter_right->handle(1);
        QVERIFY2(pHandle != nullptr, "the right hand splitter has no handle over the code pane");
        const QList<int> sizes = mpEditor->splitter_right->sizes();
        QVERIFY2(sizes.size() >= 2, "the right hand splitter has lost a pane");
        const int paneTotal = sizes.at(0) + sizes.at(1);
        // What the cap leaves over the pane as it stands, less the notice to
        // come and a step's worth on top of it: the notice measured now and the
        // one that goes up later can differ by a pixel of wrapping
        const int room = paneTotal - mpEditor->codePaneFloor(paneTotal) - sizes.at(0) - roomForANoticeToCome - scmDragStep;
        const int travel = std::min(scmDragTravel, room - room % scmDragStep);
        QVERIFY2(travel >= scmDragStep,
                 qPrintable(qsl("the form pane is at %1 of %2 with %3 to come for a notice, which leaves nothing to drag into before the seam's own cap")
                                    .arg(sizes.at(0))
                                    .arg(paneTotal)
                                    .arg(roomForANoticeToCome)));
        const QPoint on = pHandle->rect().center();
        QPointF at = pHandle->mapToGlobal(QPointF(on));

        QTest::mousePress(pHandle, Qt::LeftButton, Qt::NoModifier, on);
        for (int travelled = 0; travelled < travel; travelled += scmDragStep) {
            at += QPointF(0, scmDragStep);
            dragTo(pHandle, at);
        }
        QTest::mouseRelease(pHandle, Qt::LeftButton, Qt::NoModifier, pHandle->mapFromGlobal(at).toPoint());
        settle();
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
        mpEditor->resize(scmEditorWidth, scmEditorHeight);
        settle();

        // A trigger holding three patterns, which is enough of a list that one
        // row more than it asks for does not fit in the room the pane was given
        mpEditor->slot_showTriggers();
        mpEditor->addTrigger(false);
        settle();
        mpTriggerRow = mpEditor->mpCurrentTriggerItem;
        QVERIFY2(mpTriggerRow != nullptr, "the trigger these cases edit could not be made");
        mpEditor->showPatternItems(3);
        mpEditor->mTriggerPatternEdit.at(0)->singleLineTextEdit_pattern->setPlainText(qsl("^one$"));
        mpEditor->mTriggerPatternEdit.at(1)->singleLineTextEdit_pattern->setPlainText(qsl("^two$"));
        mpEditor->mTriggerPatternEdit.at(2)->singleLineTextEdit_pattern->setPlainText(qsl("^three$"));
        mpEditor->slot_saveEdits();
        settle();

        mpEditor->slot_showAliases();
        mpEditor->addAlias(false);
        settle();
        mpAliasRow = mpEditor->mpCurrentAliasItem;
        QVERIFY2(mpAliasRow != nullptr, "the alias these cases edit could not be made");
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

    // The notice is worth its own height and the gap under it, and nothing
    // else: the fields of the form below it neither spread apart nor move
    // anywhere but down
    void test_aNoticeMovesTheFormDownAndLeavesItAlone()
    {
        openTheAlias();
        QVERIFY2(mpEditor->mpAliasMainArea->isVisible(), "the alias form is not showing, so there is nothing to measure under a notice");

        const int formHeight = mpEditor->mpAliasMainArea->height();
        const int nameTop = topEdgeOf(mpEditor->mpAliasMainArea->lineEdit_alias_name);
        const int patternTop = topEdgeOf(mpEditor->mpAliasMainArea->lineEdit_alias_pattern);

        raiseTheNotice();
        QVERIFY2(mpEditor->mpSystemMessageArea->isVisible(), "the notice did not come up, so this case says nothing");

        const int noticeHeight = mpEditor->mpSystemMessageArea->height();
        const int columnHeight = mpEditor->mpNonCodeWidgets->height();
        qInfo().noquote() << qsl("  notice %1, gap %2, form %3 -> column %4").arg(noticeHeight).arg(columnSpacing()).arg(formHeight).arg(columnHeight);

        QVERIFY2(mpEditor->mpAliasMainArea->height() == formHeight,
                 qPrintable(qsl("the alias form is %1 tall under a notice and %2 tall without one").arg(mpEditor->mpAliasMainArea->height()).arg(formHeight)));
        QVERIFY2(columnHeight == noticeHeight + columnSpacing() + formHeight,
                 qPrintable(qsl("the column is %1 tall while the notice, the gap and the form come to %2").arg(columnHeight).arg(noticeHeight + columnSpacing() + formHeight)));

        const int shift = noticeHeight + columnSpacing();
        QVERIFY2(topEdgeOf(mpEditor->mpAliasMainArea->lineEdit_alias_name) == nameTop + shift,
                 qPrintable(qsl("the name field moved from %1 to %2 while the notice takes %3").arg(nameTop).arg(topEdgeOf(mpEditor->mpAliasMainArea->lineEdit_alias_name)).arg(shift)));
        QVERIFY2(topEdgeOf(mpEditor->mpAliasMainArea->lineEdit_alias_pattern) == patternTop + shift,
                 qPrintable(qsl("the pattern field moved from %1 to %2 while the notice takes %3").arg(patternTop).arg(topEdgeOf(mpEditor->mpAliasMainArea->lineEdit_alias_pattern)).arg(shift)));

        takeTheNoticeDown();
    }

    // With nothing chosen the notice is all the column holds, so it starts at
    // the top of the pane and is as tall as the words in it - not centred half
    // way down, and not stretched to the bottom
    void test_aNoticeWithNothingChosenLeadsThePane()
    {
        for (const bool triggers : {false, true}) {
            const QString view = triggers ? qsl("triggers") : qsl("aliases");
            QTreeWidget* pTree = triggers ? mpEditor->treeWidget_triggers : mpEditor->treeWidget_aliases;

            // Where a notice starts with an item chosen, which is the top of
            // the column either way
            if (triggers) {
                openTheTrigger();
            } else {
                openTheAlias();
            }
            raiseTheNotice();
            QVERIFY2(mpEditor->mpSystemMessageArea->isVisible(), qPrintable(qsl("no notice came up over the chosen %1 item, so there is nothing to measure against").arg(view)));
            const int noticeTopWithAnItem = topEdgeOf(mpEditor->mpSystemMessageArea);

            QTreeWidgetItem* pRoot = pTree->topLevelItem(0);
            QVERIFY2(pRoot != nullptr, qPrintable(qsl("the %1 tree has no root row").arg(view)));
            pTree->setCurrentItem(pRoot);
            if (triggers) {
                mpEditor->slot_showTriggers();
            } else {
                mpEditor->slot_showAliases();
            }
            settle();

            QVERIFY2(mpEditor->mpSystemMessageArea->isVisible(), qPrintable(qsl("the %1 root row put up no notice, so this case says nothing").arg(view)));
            QVERIFY2(!mpEditor->mpSourceEditorArea->isVisible(), qPrintable(qsl("the %1 root row left the code pane showing").arg(view)));

            const int noticeTop = topEdgeOf(mpEditor->mpSystemMessageArea);
            const int noticeHeight = mpEditor->mpSystemMessageArea->height();
            const int frameHeight = mpEditor->mpSystemMessageArea->frame_notificationArea->height();
            qInfo().noquote() << qsl("  %1: a notice over an item starts at %2, one with nothing chosen at %3 and is %4 tall around a %5 tall frame")
                                         .arg(view)
                                         .arg(noticeTopWithAnItem)
                                         .arg(noticeTop)
                                         .arg(noticeHeight)
                                         .arg(frameHeight);

            QVERIFY2(noticeTop == noticeTopWithAnItem,
                     qPrintable(qsl("the %1 notice starts at %2 with nothing chosen and at %3 over a chosen item").arg(view).arg(noticeTop).arg(noticeTopWithAnItem)));
            QVERIFY2(noticeHeight == frameHeight,
                     qPrintable(qsl("the %1 notice is %2 tall around a frame of %3, so it is being stretched past the words in it").arg(view).arg(noticeHeight).arg(frameHeight)));
        }
    }

    // A Lua table has no value to edit, so its form comes up with no code pane
    // under it. The column has the pane to itself and leads it at the height
    // its own fields ask for - held to that height it was a lone child shorter
    // than the pane, and QSplitter centres one of those, which left the Name
    // field floating half way down the window.
    void test_aFormWithNoCodePaneLeadsThePane()
    {
        QVERIFY2(mpHost->mLuaInterpreter.compileAndExecuteScript(qsl("seamString = \"north\"")), "the string variable this case measures against could not be made");
        QVERIFY2(mpHost->mLuaInterpreter.compileAndExecuteScript(qsl("seamTable = { a = 1 }")), "the table variable this case edits could not be made");
        mpEditor->slot_showVariables();
        settle();

        // A string has a value, so its form comes up over a code pane: where
        // that form starts is where one with no code pane has to start too
        QTreeWidgetItem* pString = variableRow(qsl("seamString"));
        QVERIFY2(pString != nullptr, "the string variable is not on the Variables tree");
        chooseTheVariable(pString);
        QVERIFY2(mpEditor->mpSourceEditorArea->isVisible(), "a string variable came up with no code pane, so there is nothing to measure against");
        const int formTopOverACodePane = topEdgeOf(mpEditor->mpVarsMainArea);

        QTreeWidgetItem* pTable = variableRow(qsl("seamTable"));
        QVERIFY2(pTable != nullptr, "the table variable is not on the Variables tree");
        chooseTheVariable(pTable);
        QVERIFY2(mpEditor->mpSourceEditorArea->isHidden(), "the table variable came up with a code pane, so this case says nothing");
        QVERIFY2(mpEditor->mpVarsMainArea->isVisible(), "the variable form is away, so there is nothing to measure");

        const int formTop = topEdgeOf(mpEditor->mpVarsMainArea);
        const int wanted = mpEditor->mpVarsMainArea->sizeHint().height();
        qInfo().noquote() << qsl("  the form starts at %1 over a code pane and at %2 with none, and is %3 tall where its fields ask for %4")
                                     .arg(formTopOverACodePane)
                                     .arg(formTop)
                                     .arg(mpEditor->mpVarsMainArea->height())
                                     .arg(wanted);

        QVERIFY2(formTop == formTopOverACodePane,
                 qPrintable(qsl("the form starts at %1 with no code pane under it and at %2 with one, so it is %3 pixels down the pane")
                                    .arg(formTop)
                                    .arg(formTopOverACodePane)
                                    .arg(formTop - formTopOverACodePane)));
        QVERIFY2(topEdgeOf(mpEditor->mpNonCodeWidgets) == formTop, qPrintable(qsl("the column starts at %1 and the form in it at %2").arg(topEdgeOf(mpEditor->mpNonCodeWidgets)).arg(formTop)));
        QVERIFY2(mpEditor->mpVarsMainArea->height() == wanted,
                 qPrintable(qsl("the form is %1 tall with the pane to itself, where its fields ask for %2").arg(mpEditor->mpVarsMainArea->height()).arg(wanted)));

        // ...and a notice over it costs it the same as it costs a form with a
        // code pane under it: the height of the notice and the gap, and nothing
        // else
        raiseTheNotice();
        QVERIFY2(mpEditor->mpSystemMessageArea->isVisible(), "the notice did not come up over the form with no code pane, so the rest of this case says nothing");
        const int shift = mpEditor->mpSystemMessageArea->height() + columnSpacing();
        QVERIFY2(topEdgeOf(mpEditor->mpVarsMainArea) == formTop + shift,
                 qPrintable(qsl("the form moved from %1 to %2 while the notice over it takes %3").arg(formTop).arg(topEdgeOf(mpEditor->mpVarsMainArea)).arg(shift)));
        QVERIFY2(mpEditor->mpVarsMainArea->height() == wanted,
                 qPrintable(qsl("the form is %1 tall under a notice with no code pane, where its fields ask for %2").arg(mpEditor->mpVarsMainArea->height()).arg(wanted)));
        takeTheNoticeDown();
    }

    // The trigger form's pattern list keeps its room under a notice: the seam
    // moves by what the notice takes rather than the list being squeezed under
    // it, so Add pattern still sits on the code heading
    void test_theAddPatternButtonKeepsItsPlaceUnderANotice()
    {
        openTheTrigger();
        QVERIFY2(mpEditor->mpTriggersMainArea->isVisible(), "the trigger form is not showing, so there is nothing to measure");

        const int roomWithout = topEdgeOf(mpEditor->mpWidget_editorCodeHeader) - bottomEdgeOf(mpEditor->mpButton_addPattern);
        raiseTheNotice();
        const int roomWith = topEdgeOf(mpEditor->mpWidget_editorCodeHeader) - bottomEdgeOf(mpEditor->mpButton_addPattern);
        qInfo().noquote() << qsl("  Add pattern sits %1 above the code heading without a notice and %2 with one").arg(roomWithout).arg(roomWith);

        QVERIFY2(roomWith > 0 && roomWith <= scmRoomUnderTheLastRow && qAbs(roomWith - roomWithout) <= 2,
                 qPrintable(qsl("Add pattern sits %1 above the code heading with a notice up, where without one it sits %2 above it").arg(roomWith).arg(roomWithout)));
        QVERIFY2(!patternListScrolls(), "the pattern list is scrolling with a notice up, so the rows lost the room the notice took");

        takeTheNoticeDown();
    }

    // One click on Add pattern is one row more than the pane was sized for, so
    // the pane is sized again - the new row and the button under it are both on
    // show without the list having to scroll
    void test_addingAPatternRowMakesRoomForIt()
    {
        openTheTrigger();
        QVERIFY2(!patternListScrolls(), "the pattern list is already scrolling before a row is added, so this case says nothing");
        const int rowsBefore = mpEditor->mVisiblePatternCount;

        mpEditor->slot_addPattern();
        settle();

        qInfo().noquote() << qsl("  %1 rows became %2, and Add pattern overhangs the list by %3").arg(rowsBefore).arg(mpEditor->mVisiblePatternCount).arg(addPatternButtonOverhang());
        QCOMPARE(mpEditor->mVisiblePatternCount, rowsBefore + 1);
        QVERIFY2(addPatternButtonOverhang() < 0, qPrintable(qsl("Add pattern hangs %1 pixels past the bottom of the pattern list after one row was added").arg(addPatternButtonOverhang())));
        QVERIFY2(!patternListScrolls(), "a scroll bar came up beside the pattern list after one row was added");
    }

    // ...and a row taken away gives the room back rather than leaving it empty
    // until the trigger is chosen afresh
    void test_deletingAPatternRowGivesTheRoomBack()
    {
        openTheTrigger();
        const int rowsBefore = mpEditor->mVisiblePatternCount;
        const int columnBefore = mpEditor->mpNonCodeWidgets->height();

        mpEditor->deletePatternRow(0);
        settle();

        qInfo().noquote()
                << qsl("  the column was %1 tall with %2 rows and is %3 tall with %4").arg(columnBefore).arg(rowsBefore).arg(mpEditor->mpNonCodeWidgets->height()).arg(mpEditor->mVisiblePatternCount);
        QVERIFY2(mpEditor->mVisiblePatternCount < rowsBefore, "the deleted row was not taken off the list");
        QVERIFY2(mpEditor->mpNonCodeWidgets->height() < columnBefore,
                 qPrintable(qsl("the column is still %1 tall with %2 rows, where %3 rows asked for %4")
                                    .arg(mpEditor->mpNonCodeWidgets->height())
                                    .arg(mpEditor->mVisiblePatternCount)
                                    .arg(rowsBefore)
                                    .arg(columnBefore)));
    }

    // A height the reader drags the seam to is theirs to keep, and a notice
    // that comes and goes over it hands its room back. The dragged height was
    // written down raw - with whatever notice was up inside it - and re-applied
    // whole while a notice was up again, so the pattern list paid the notice's
    // height a second time and kept paying it: every notice that came and went
    // took another one off.
    //
    // Choosing an item is the one moment a notice is up while the pane is being
    // sized: the selection puts up what the item has to say, and sizes the pane
    // under it. A broken item says so every time it is chosen.
    void test_aDraggedSeamKeepsItsHeightAcrossANotice()
    {
        chooseTheTrigger();
        breakTheTriggersScript();
        // Measured while it is up, since the drag below has to stop short of
        // the room the same notice will ask for when the item is chosen again
        const int noticeRoomToCome = mpEditor->noticeRoomInFormColumn();
        takeTheNoticeDown();

        dragTheSeamDown(noticeRoomToCome);
        const int draggedTo = mpEditor->splitter_right->sizes().at(0);
        QVERIFY2(mpEditor->mDraggedFormPaneHeights.contains(EditorViewType::cmTriggerView), "the drag never reached the splitter, so no height was written down and this case says nothing");

        chooseTheTrigger();
        QVERIFY2(mpEditor->mpSystemMessageArea->isVisible(), "choosing the broken trigger said nothing about it, so no notice was up while the pane was sized");
        const int noticeRoom = mpEditor->noticeRoomInFormColumn();
        const int underTheNotice = mpEditor->splitter_right->sizes().at(0);

        takeTheNoticeDown();
        const int afterTheNotice = mpEditor->splitter_right->sizes().at(0);

        qInfo().noquote() << qsl("  the seam was dragged to %1, an item chosen under a notice worth %2 left it at %3, and taking the notice down left %4")
                                     .arg(draggedTo)
                                     .arg(noticeRoom)
                                     .arg(underTheNotice)
                                     .arg(afterTheNotice);

        QVERIFY2(noticeRoom > 0, "the notice cost the column nothing, so this case says nothing");
        QVERIFY2(qAbs(underTheNotice - (draggedTo + noticeRoom)) <= scmSeamTolerance,
                 qPrintable(qsl("the item chosen under the notice left the seam at %1 where the dragged %2 and the notice's %3 come to %4 - the list paid for the notice twice")
                                    .arg(underTheNotice)
                                    .arg(draggedTo)
                                    .arg(noticeRoom)
                                    .arg(draggedTo + noticeRoom)));
        QVERIFY2(qAbs(afterTheNotice - draggedTo) <= scmSeamTolerance, qPrintable(qsl("the seam was dragged to %1 and a notice that came and went left it at %2").arg(draggedTo).arg(afterTheNotice)));

        mendTheTriggersScript();
        // A dragged height is kept for the rest of the session, and every other
        // case here reads a seam that snaps to the item it is showing
        mpEditor->mDraggedFormPaneHeights.remove(EditorViewType::cmTriggerView);
    }

    // The reading of how many variables are hidden is said beside the switch
    // that shows them, and both belong to the Variables view
    void test_theHiddenVariablesCountLeavesWithTheVariablesView()
    {
        mpEditor->slot_showVariables();
        settle();
        QVERIFY2(mpEditor->mpLabel_hiddenVariablesCount->isVisible(), "nothing is hidden in this profile's variables, so the reading beside the switch was never shown and this case says nothing");

        mpEditor->slot_showTriggers();
        settle();
        QVERIFY2(!mpEditor->mpLabel_hiddenVariablesCount->isVisible(), qPrintable(qsl("the Triggers view is still being told \"%1\"").arg(mpEditor->mpLabel_hiddenVariablesCount->text())));

        QVERIFY2(!mpEditor->checkBox_displayAllVariables->isVisible(), "the switch itself is still showing outside the Variables view");
    }

    // A trigger with more rows than the pane has room for used to open scrolled
    // to the empty row after its last pattern, with the first ones out of sight;
    // the reader came to see the patterns from the top. Adding a row still
    // scrolls to the row it adds.
    void test_aTriggerWithMoreRowsThanFitOpensAtItsFirstRow()
    {
        chooseTheTrigger();
        takeTheNoticeDown();
        constexpr int rows = 16;
        mpEditor->showPatternItems(rows);
        for (int row = 0; row < rows; ++row) {
            mpEditor->mTriggerPatternEdit.at(row)->singleLineTextEdit_pattern->setPlainText(qsl("^row %1$").arg(row + 1));
        }
        mpEditor->slot_saveEdits();
        settle();
        chooseTheTrigger();
        takeTheNoticeDown();

        QScrollBar* pBar = mpEditor->mpScrollArea->verticalScrollBar();
        QVERIFY2(pBar->maximum() > 0, "sixteen rows fit the pane, so there is nothing to scroll and this case says nothing");
        QVERIFY2(pBar->value() == 0, qPrintable(qsl("the pattern list opened scrolled to %1 of %2 rather than at its first row").arg(pBar->value()).arg(pBar->maximum())));

        // Back to the three rows the other cases are written for
        mpEditor->showPatternItems(3);
        mpEditor->slot_saveEdits();
        settle();
    }

    // The seam keeps the code pane its floor whenever it places it; a drag of
    // the heading used to be free to take it under, down to whatever the pane's
    // widgets would shrink to
    void test_aDragCannotTakeTheCodePaneUnderItsFloor()
    {
        chooseTheTrigger();
        takeTheNoticeDown();
        QSplitterHandle* pHandle = mpEditor->splitter_right->handle(1);
        QVERIFY2(pHandle != nullptr, "the right hand splitter has no handle over the code pane");
        const QList<int> before = mpEditor->splitter_right->sizes();
        QVERIFY2(before.size() >= 2, "the right hand splitter has lost a pane");
        const int paneTotal = before.at(0) + before.at(1);

        // Further than the pane can go, so wherever the drag stops is the limit
        const QPoint on = pHandle->rect().center();
        QPointF at = pHandle->mapToGlobal(QPointF(on));
        QTest::mousePress(pHandle, Qt::LeftButton, Qt::NoModifier, on);
        for (int travelled = 0; travelled < paneTotal; travelled += scmDragStep) {
            at += QPointF(0, scmDragStep);
            dragTo(pHandle, at);
        }
        QTest::mouseRelease(pHandle, Qt::LeftButton, Qt::NoModifier, pHandle->mapFromGlobal(at).toPoint());
        settle();

        const QList<int> after = mpEditor->splitter_right->sizes();
        qInfo().noquote() << qsl("  dragged the seam from %1 / %2 to %3 / %4").arg(before.at(0)).arg(before.at(1)).arg(after.at(0)).arg(after.at(1));
        QVERIFY2(after.at(0) > before.at(0), "the drag did not move the seam at all, so this case says nothing");
        QVERIFY2(after.at(1) >= dlgTriggerEditor::scmEditorSourcePaneFloor,
                 qPrintable(qsl("a drag took the code pane down to %1, under its floor of %2").arg(after.at(1)).arg(dlgTriggerEditor::scmEditorSourcePaneFloor)));
    }
};

#include "EditorNoticeSeamTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorNoticeSeamTest)
