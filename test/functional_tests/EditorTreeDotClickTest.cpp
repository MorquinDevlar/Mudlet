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
 * The state dot at the head of an editor tree row is a switch, and
 * uiDesign::EditorTreeDelegate answers the clicks that land on it. What the
 * view does with the rest of such a click is the subject here: it records what
 * was pressed before it consults a delegate at all, and it only consults one
 * about an event that lands on a row - so a press the dot answered has to be
 * seen through by a filter on the viewport rather than by editorEvent().
 *
 * The cases are the three ways that used to go wrong: a drifted double click
 * toggling twice, a press on the dot turning into a drag of its row, and a
 * press whose release landed elsewhere leaving the delegate swallowing every
 * later drag-select.
 *
 * Run with: ctest -R EditorTreeDotClickTest -V
 */

#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <chrono>

#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "EditorTreeDelegate.h"
#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "TTreeWidget.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgConnectionProfiles.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

namespace {
// Whether the view has entered DraggingState is the whole of one assertion
// below, and QAbstractItemView keeps both state() and the State enum protected.
// Using-declarations in a class derived from it make those names public there,
// and the address taken of the member function is an ordinary pointer to a
// QAbstractItemView member - so the view is asked through its own type, with no
// cast of the object involved.
struct ViewStatePeek : QTreeView
{
    using QAbstractItemView::NoState;
    using QAbstractItemView::State;

    static State of(const QAbstractItemView* pView)
    {
        State (QAbstractItemView::*pStateGetter)() const = &ViewStatePeek::state;
        return (pView->*pStateGetter)();
    }
};
} // namespace

class EditorTreeDotClickTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("TreeDotClick-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    void deleteProfileDirectory(const QString& profileName)
    {
        const QString path = mudlet::getMudletPath(enums::profileHomePath, profileName);
        QDir dir(path);
        if (dir.exists() && !dir.removeRecursively()) {
            qWarning() << "deleteProfileDirectory: could not remove" << path << "- later failures may stem from this stale state";
        }
    }

    void startProfile(const QString& profileName, const QString& address, const QString& port)
    {
        mpHost = TestProfile::create(profileName, address, port);
        if (!mpHost) {
            QFAIL("No active host available for the test.");
        }

        QSignalSpy connectedSpy(&(mpHost->mTelnet), &cTelnet::signal_connected);
        if (!connectedSpy.wait(2000)) {
            QFAIL("Could not connect with the host.");
        }
    }

    TTreeWidget* tree() const { return mpEditor->treeWidget_triggers; }
    QWidget* viewport() const { return tree()->viewport(); }
    QTreeWidgetItem* baseItem() const { return mpEditor->mpTriggerBaseItem; }

    // QTreeWidget::indexFromItem() is protected, and the row's rectangle is
    // wanted here anyway
    QModelIndex indexOf(QTreeWidgetItem* pItem) const { return tree()->indexAt(tree()->visualItemRect(pItem).center()); }

    uiDesign::EditorTreeDelegate* dotDelegate() const { return qobject_cast<uiDesign::EditorTreeDelegate*>(tree()->itemDelegateForIndex(QModelIndex())); }

    QRect dotRect(QTreeWidgetItem* pItem) const { return dotDelegate()->dotHitRect(indexOf(pItem)); }

    QRect chevronRect(QTreeWidgetItem* pItem) const { return dotDelegate()->chevronHitRect(indexOf(pItem)); }

    // A group with a trigger inside it, made the way the editor makes one: a new
    // item goes inside whichever folder is chosen, and what has just been added
    // is what is chosen next
    QTreeWidgetItem* aGroupHoldingATrigger()
    {
        tree()->setCurrentItem(baseItem());
        mpEditor->addTrigger(true);
        QTreeWidgetItem* pGroup = tree()->currentItem();
        mpEditor->addTrigger(false);
        // Opening the group is animated, and a view part-way through one of
        // those answers a press itself rather than passing it on to whatever
        // was pressed - the same guard the view's own arrow used to sit behind
        QTest::qWait(400ms);
        return pGroup;
    }

    // Both ways the editor can be asked to switch a row: the delegate's own
    // request, and the view's activated() that a double click on a row reaches.
    // They arrive at one slot, so their sum is how many times the switch was
    // thrown - which is what these cases are about, and is not the same
    // question as what the item ended up as. A trigger with no patterns is one
    // activeToggle_trigger() forces straight back off however it was asked, and
    // every trigger the editor has just made is one of those.
    struct ToggleCounter
    {
        ToggleCounter(uiDesign::EditorTreeDelegate* pDelegate, TTreeWidget* pTree)
        : delegateSpy(pDelegate, &uiDesign::EditorTreeDelegate::toggleRequested)
        , activatedSpy(pTree, &QTreeWidget::itemActivated)
        {
        }

        [[nodiscard]] int count() const { return delegateSpy.count() + activatedSpy.count(); }

        QSignalSpy delegateSpy;
        QSignalSpy activatedSpy;
    };

private slots:
    void initTestCase()
    {
        if (portableMarkerPresent()) {
            QSKIP("portable.txt present - it takes precedence over XDG_CONFIG_HOME, so the config dir cannot be redirected");
        }

        // A config root of this process's own. Sharing the developer's
        // ~/.config/mudlet means sharing a profile list, so a second copy of
        // this test running at the same time is told the name it types is
        // already in use and never gets an enabled Connect button. Since #9712
        // the opt-in that makes setupConfig() adopt a directory is
        // $XDG_CONFIG_HOME/mudlet/profiles, not the mudlet directory alone.
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
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>("MudletInstanceCoordinator"));
        mudlet::self()->init();
        mudlet::self()->setStorePasswordsSecurely(false);
        deleteProfileDirectory(mProfileName);
        startProfile(mProfileName, mLocalhost, mPort);
        // QFAIL inside startProfile() only returns from that helper - bail out
        // here too or the mpHost dereference below crashes and buries the
        // recorded diagnostic under a segfault
        if (QTest::currentTestFailed()) {
            return;
        }

        mudlet::self()->slot_showScriptDialog();
        QTest::qWait(100ms);

        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->slot_showTriggers();
        QTest::qWait(50ms);
        QVERIFY2(dotDelegate() != nullptr, "The triggers tree should be drawn by an EditorTreeDelegate");
    }

    void cleanupTestCase()
    {
        mpEditor = nullptr;
        mpHost = nullptr;
        delete mpServer;
        mpServer = nullptr;
        // Null when initTestCase skipped or failed ahead of mudlet::start(), and
        // getMudletPath() dereferences the instance rather than checking it
        if (mudlet::self()) {
            deleteProfileDirectory(mProfileName);
            delete mudlet::self();
        }
        mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME") : qputenv("XDG_CONFIG_HOME", mSavedXdg);
    }

    // Two sibling triggers under the base item, both freshly made, so that every
    // case starts from the same rows in the same order
    void init()
    {
        mpEditor->slot_showTriggers();
        tree()->clearSelection();
        tree()->setCurrentItem(nullptr);
        QCoreApplication::processEvents();

        while (baseItem()->childCount() > 0) {
            tree()->setCurrentItem(baseItem()->child(0));
            mpEditor->slot_deleteItemOrGroup();
        }
        tree()->setCurrentItem(nullptr);
        mpEditor->addTrigger(false);
        mpEditor->addTrigger(false);
        baseItem()->setExpanded(true);
        QCoreApplication::processEvents();

        QCOMPARE(baseItem()->childCount(), 2);
        QVERIFY2(!tree()->visualItemRect(baseItem()->child(1)).isEmpty(), "Both rows should be laid out and visible");
    }

    // A click on the dot switches the row it leads, once, and picks that row
    void testSingleClickOnDotTogglesOnce()
    {
        QTreeWidgetItem* pItem = baseItem()->child(1);
        tree()->setCurrentItem(baseItem()->child(0));
        QCoreApplication::processEvents();

        const QRect dot = dotRect(pItem);
        QVERIFY2(!dot.isEmpty(), "The row should report a dot to click");

        ToggleCounter toggles(dotDelegate(), tree());
        QTest::mouseClick(viewport(), Qt::LeftButton, Qt::NoModifier, dot.center());
        QCoreApplication::processEvents();

        QCOMPARE(toggles.count(), 1);
        // The slot the request reaches reads the tree rather than being handed a
        // row, so choosing the row is half of what a dot click has to do
        QCOMPARE(tree()->currentItem(), pItem);
    }

    // The pair's first press has already switched the row, so the second press
    // and the release that closes it are both eaten
    void testDoubleClickOnDotTogglesOnce()
    {
        QTreeWidgetItem* pItem = baseItem()->child(1);
        const QRect dot = dotRect(pItem);
        QVERIFY(!dot.isEmpty());

        ToggleCounter toggles(dotDelegate(), tree());
        QTest::mousePress(viewport(), Qt::LeftButton, Qt::NoModifier, dot.center());
        QTest::mouseRelease(viewport(), Qt::LeftButton, Qt::NoModifier, dot.center());
        QTest::mouseDClick(viewport(), Qt::LeftButton, Qt::NoModifier, dot.center());
        QTest::mouseRelease(viewport(), Qt::LeftButton, Qt::NoModifier, dot.center());
        QCoreApplication::processEvents();

        QCOMPARE(toggles.count(), 1);
    }

    // The platform allows a double click a few pixels of drift, and the dot is
    // small enough that the second press can land off it while still being the
    // second press of the pair. Handed on, it reaches the view's activated() and
    // switches the row straight back - a visible flip and flip back, with the
    // trigger stopped and restarted on the way.
    void testDriftedDoubleClickTogglesOnce()
    {
        QTreeWidgetItem* pItem = baseItem()->child(1);
        const QRect dot = dotRect(pItem);
        QVERIFY(!dot.isEmpty());
        const QPoint onDot(dot.right() - 1, dot.center().y());
        const QPoint drifted = onDot + QPoint(4, 0);
        QVERIFY2(dot.contains(onDot), "The first press has to land on the dot");
        QVERIFY2(!dot.contains(drifted), "The second press has to land off the dot");
        QVERIFY2(tree()->visualItemRect(pItem).contains(drifted), "...but still on the same row");
        QCOMPARE(tree()->indexAt(drifted), indexOf(pItem));

        ToggleCounter toggles(dotDelegate(), tree());
        QTest::mousePress(viewport(), Qt::LeftButton, Qt::NoModifier, onDot);
        QTest::mouseRelease(viewport(), Qt::LeftButton, Qt::NoModifier, onDot);
        QTest::mouseDClick(viewport(), Qt::LeftButton, Qt::NoModifier, drifted);
        QTest::mouseRelease(viewport(), Qt::LeftButton, Qt::NoModifier, drifted);
        QCoreApplication::processEvents();

        QCOMPARE(toggles.count(), 1);
    }

    // A press on the dot must not become a drag of the row it is on, however far
    // the pointer travels before the button comes back up - including out of the
    // rows altogether, which is where the view stops consulting the delegate.
    //
    // The drag distance is raised for the duration so that the view can enter
    // DraggingState without going on to start a real drag: startDrag() ends in
    // QDrag::exec(), which runs a modal event loop that only a mouse release can
    // end, and the release is in the events this test has yet to send. Entering
    // DraggingState is the assertion either way - a drag cannot start without
    // it, and the state is reached before the distance is even measured.
    void testPressOnDotDoesNotStartDrag()
    {
        QTreeWidgetItem* pItem = baseItem()->child(1);
        const QRect dot = dotRect(pItem);
        QVERIFY(!dot.isEmpty());
        const QRect lastRow = tree()->visualItemRect(pItem);
        const QPoint blank(lastRow.center().x(), lastRow.bottom() + 12);
        QVERIFY2(viewport()->rect().contains(blank), "The tree needs blank space below its last row for this case");
        QVERIFY2(!tree()->indexAt(blank).isValid(), "...which is what makes it blank");

        const int rowInParent = baseItem()->indexOfChild(pItem);

        const int savedDragDistance = QApplication::startDragDistance();
        QApplication::setStartDragDistance(10000);

        ToggleCounter toggles(dotDelegate(), tree());
        QTest::mousePress(viewport(), Qt::LeftButton, Qt::NoModifier, dot.center());
        QTest::mouseMove(viewport(), dot.center() + QPoint(2, 6));
        QTest::mouseMove(viewport(), lastRow.center());
        QTest::mouseMove(viewport(), blank);
        QTest::mouseMove(viewport(), blank + QPoint(20, 8));
        const ViewStatePeek::State stateWhileHeld = ViewStatePeek::of(tree());
        QTest::mouseRelease(viewport(), Qt::LeftButton, Qt::NoModifier, blank + QPoint(20, 8));
        QCoreApplication::processEvents();

        QApplication::setStartDragDistance(savedDragDistance);

        QCOMPARE(stateWhileHeld, ViewStatePeek::NoState);
        QCOMPARE(toggles.count(), 1);
        QCOMPARE(pItem->parent(), baseItem());
        QCOMPARE(baseItem()->indexOfChild(pItem), rowInParent);
        QCOMPARE(baseItem()->childCount(), 2);
    }

    // A press on the dot whose release lands on another row is one the view
    // never hands back, so the delegate has to hear the release for itself -
    // otherwise it goes on believing a dot press is live and swallows the moves
    // of every drag-select made afterwards.
    void testReleaseOffTheRowLeavesDragSelectWorking()
    {
        QTreeWidgetItem* pFirst = baseItem()->child(0);
        QTreeWidgetItem* pSecond = baseItem()->child(1);
        const QRect dot = dotRect(pFirst);
        QVERIFY(!dot.isEmpty());
        const QRect secondRow = tree()->visualItemRect(pSecond);
        const QPoint blank(secondRow.center().x(), secondRow.bottom() + 12);
        QVERIFY2(viewport()->rect().contains(blank), "The tree needs blank space below its last row for this case");
        QVERIFY2(!tree()->indexAt(blank).isValid(), "...which is what makes it blank");

        ToggleCounter toggles(dotDelegate(), tree());
        QTest::mousePress(viewport(), Qt::LeftButton, Qt::NoModifier, dot.center());
        QTest::mouseMove(viewport(), secondRow.center());
        QTest::mouseRelease(viewport(), Qt::LeftButton, Qt::NoModifier, secondRow.center());
        QCoreApplication::processEvents();

        QCOMPARE(toggles.count(), 1);

        // ...and now a rubber band drawn from the blank space up across both rows
        tree()->clearSelection();
        QCoreApplication::processEvents();
        QTest::mousePress(viewport(), Qt::LeftButton, Qt::NoModifier, blank);
        QTest::mouseMove(viewport(), secondRow.center());
        QTest::mouseMove(viewport(), tree()->visualItemRect(pFirst).center());
        QTest::mouseRelease(viewport(), Qt::LeftButton, Qt::NoModifier, tree()->visualItemRect(pFirst).center());
        QCoreApplication::processEvents();

        const QList<QTreeWidgetItem*> selected = tree()->selectedItems();
        QVERIFY2(selected.contains(pFirst) && selected.contains(pSecond), qPrintable(qsl("A drag-select across both rows should have selected both, but selected %1 item(s)").arg(selected.size())));
    }

    // The chevron is drawn by the same delegate, in the cell the view used to
    // draw its branch arrow in, and answers its presses the same way - so what
    // the view did with a click there has to go on happening: the row folds, and
    // nothing else about it moves.
    void testClickOnChevronFoldsTheRowAndLeavesTheSelectionAlone()
    {
        QTreeWidgetItem* pGroup = aGroupHoldingATrigger();
        QTreeWidgetItem* pChosen = tree()->currentItem();
        QVERIFY2(pGroup && pGroup->childCount() == 1, "The group this case folds was not made");
        QVERIFY2(pGroup->isExpanded(), "The editor leaves a group it has just added to open");

        const QRect chevron = chevronRect(pGroup);
        QVERIFY2(!chevron.isEmpty(), "A group with something inside it should report a chevron to click");

        ToggleCounter toggles(dotDelegate(), tree());
        QTest::mouseClick(viewport(), Qt::LeftButton, Qt::NoModifier, chevron.center());
        QCoreApplication::processEvents();
        QVERIFY2(!pGroup->isExpanded(), "A click on the chevron did not fold the group away");

        // ...and the folding is animated in its turn
        QTest::qWait(400ms);
        QTest::mouseClick(viewport(), Qt::LeftButton, Qt::NoModifier, chevronRect(pGroup).center());
        QCoreApplication::processEvents();
        QVERIFY2(pGroup->isExpanded(), "A second click on the chevron did not open the group again");

        QCOMPARE(toggles.count(), 0);
        QCOMPARE(tree()->currentItem(), pChosen);
    }

    // The keyboard reaches the same folding, and never went through the arrow
    // the view drew - so it has to be unaffected by the arrow having moved into
    // the row
    void testArrowKeysStillFoldTheRow()
    {
        QTreeWidgetItem* pGroup = aGroupHoldingATrigger();
        tree()->setCurrentItem(pGroup);
        tree()->setFocus();
        QCoreApplication::processEvents();
        QVERIFY(pGroup->isExpanded());

        QTest::keyClick(tree(), Qt::Key_Left);
        QCoreApplication::processEvents();
        QVERIFY2(!pGroup->isExpanded(), "The left arrow key did not fold the group away");

        QTest::keyClick(tree(), Qt::Key_Right);
        QCoreApplication::processEvents();
        QVERIFY2(pGroup->isExpanded(), "The right arrow key did not open the group again");
    }

    // A double click on the chevron folds the row once and switches nothing: the
    // pair's first press has already folded it, and the second handed on would
    // reach the view's activated() and switch the group on or off
    void testDoubleClickOnChevronFoldsOnceAndSwitchesNothing()
    {
        QTreeWidgetItem* pGroup = aGroupHoldingATrigger();
        const QRect chevron = chevronRect(pGroup);
        QVERIFY(!chevron.isEmpty());
        QVERIFY(pGroup->isExpanded());

        ToggleCounter toggles(dotDelegate(), tree());
        QTest::mousePress(viewport(), Qt::LeftButton, Qt::NoModifier, chevron.center());
        QTest::mouseRelease(viewport(), Qt::LeftButton, Qt::NoModifier, chevron.center());
        QTest::mouseDClick(viewport(), Qt::LeftButton, Qt::NoModifier, chevron.center());
        QTest::mouseRelease(viewport(), Qt::LeftButton, Qt::NoModifier, chevron.center());
        QCoreApplication::processEvents();

        QCOMPARE(toggles.count(), 0);
        QVERIFY2(!pGroup->isExpanded(), "A double click on the chevron should leave the group folded, having folded it once");
    }

    // A double click on the row itself is the editor's way of switching an item
    // on or off, and the view folds the row under it at the same time. Neither
    // half went through the chevron, and neither may be swallowed by it.
    void testDoubleClickOnTheRowStillSwitchesIt()
    {
        QTreeWidgetItem* pGroup = aGroupHoldingATrigger();
        const QRect row = tree()->visualItemRect(pGroup);
        const QRect chevron = chevronRect(pGroup);
        const QPoint onTheName(row.right() - 6, row.center().y());
        QVERIFY2(!chevron.contains(onTheName) && !dotRect(pGroup).contains(onTheName), "The point this case double clicks has to be clear of both marks the delegate answers for");

        ToggleCounter toggles(dotDelegate(), tree());
        QTest::mousePress(viewport(), Qt::LeftButton, Qt::NoModifier, onTheName);
        QTest::mouseRelease(viewport(), Qt::LeftButton, Qt::NoModifier, onTheName);
        QTest::mouseDClick(viewport(), Qt::LeftButton, Qt::NoModifier, onTheName);
        QTest::mouseRelease(viewport(), Qt::LeftButton, Qt::NoModifier, onTheName);
        QCoreApplication::processEvents();

        QCOMPARE(toggles.count(), 1);
    }
};

#include "EditorTreeDotClickTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorTreeDotClickTest)
