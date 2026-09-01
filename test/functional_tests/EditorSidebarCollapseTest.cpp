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
 * The editor's sidebar has two ways of losing its names, and they must not be
 * able to overwrite each other.
 *
 * The panel-left button at the left end of the actions toolbar - where every
 * platform puts the control that shows and hides a sidebar - is the user's own
 * choice, kept across sessions under editorSidebarLabelsShown. A labels-shown
 * preference, since the sidebar has no closed state to remember: it minimises
 * to a rail of icons and the rows go on being reachable, with their names as
 * tooltips.
 *
 * A window too narrow to draw the names takes them away regardless, and that
 * one is transient: it must never reach the stored preference, or a stretch of
 * work in a small window would decide what every later session opens with. This
 * is the split slot_rightSplitterMoved() already makes for the trigger options
 * panel, where only the explicit click writes anything down.
 *
 * Where the button sits and how it is drawn are checked here too. It used to be
 * a chevron at the foot of the sidebar, which reads as an arrow somebody left
 * behind rather than as the control it is; it is now the first thing on the
 * toolbar, drawn as a picture alone, and held down while the names are showing.
 */

#include <QAction>
#include <QListWidget>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QtTest/QtTest>
#include <chrono>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorSidebarCollapseTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorSidebarCollapse-Test-Profile");
    QString mPort; // assigned the stub's actual ephemeral port in initTestCase()
    const QString mLocalhost = qsl("localhost");

    // Measured off the editor rather than written down here: the breakpoint is
    // the widest of the translated names plus the narrowest the body can be
    // drawn at, neither of which is a number in the source
    int mWideEnough = 0;
    int mTooNarrow = 0;
    int mHeight = 700;

    void deleteProfileDirectory(const QString& profileName)
    {
        QDir dir(mudlet::getMudletPath(enums::profileHomePath, profileName));
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }

    void startProfile(const QString& profileName, const QString& address, const QString& port)
    {
        mpHost = TestProfile::create(profileName, address, port);
        if (!mpHost) {
            QFAIL("No active host available for the test.");
        }

        QSignalSpy spy(&(mpHost->mTelnet), &cTelnet::signal_connected);
        if (!spy.wait(1000)) {
            QFAIL("Could not connect with the host.");
        }
    }

    // The mode is the property the shared delegate and the sidebar's rules both
    // read, so it is what the window is actually drawing rather than a copy of
    // the intent
    bool railShowing() const { return mpEditor->mpListWidget_editorSidebar->property(uiDesign::scmProp_rail).toBool(); }

    bool storedLabelsShown() const { return mudlet::getQSettings()->value(qsl("editorSidebarLabelsShown"), true).toBool(); }

    // The toolbar's own button for the toggle action, which is the widget a
    // pointer and a screen reader both reach
    QToolButton* toggle() const { return mpEditor->mpButton_editorSidebarToggle; }

    void resizeEditor(const int width)
    {
        mpEditor->resize(width, mHeight);
        QCoreApplication::sendPostedEvents();
        QTest::qWait(50ms);
    }

    void pressTheToggle()
    {
        QVERIFY2(toggle() != nullptr, "The toolbar has no sidebar toggle");
        QVERIFY2(toggle()->isEnabled(), "The sidebar toggle is not pressable at this width");
        QTest::mouseClick(toggle(), Qt::LeftButton);
        QCoreApplication::sendPostedEvents();
        QTest::qWait(50ms);
    }

    QString state() const
    {
        return qsl("window %1, sidebar %2, rail %3, stored labelsShown %4")
                .arg(QString::number(mpEditor->width()),
                     QString::number(mpEditor->mpWidget_editorSidebarPane->width()),
                     railShowing() ? qsl("yes") : qsl("no"),
                     storedLabelsShown() ? qsl("true") : qsl("false"));
    }

private slots:
    void initTestCase()
    {
        if (portableMarkerPresent()) {
            QSKIP("portable.txt present - it takes precedence over XDG_CONFIG_HOME, "
                  "so the config dir cannot be redirected");
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

        startProfile(mProfileName, mLocalhost, mPort);

        mudlet::self()->slot_showEditorDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");

        // Only measurable with the window on screen: the breakpoint is taken off
        // splitter_main's minimum size hint, and a splitter that has never been
        // shown reports the hint of a layout that has never been run
        const dlgTriggerEditor::EditorSidebarWidths widths = mpEditor->editorSidebarWidths();
        mWideEnough = widths.collapseBelow + 120;
        mTooNarrow = widths.collapseBelow - 40;
        qInfo().noquote() << qsl("  sidebar expanded at %1px, names given up below %2px; editor minimum width %3px")
                                     .arg(QString::number(widths.expanded), QString::number(widths.collapseBelow), QString::number(mpEditor->minimumWidth()));
        // A layout's total minimum is the sidebar's fixed width plus the body's,
        // so a breakpoint at or below that is one no drag can ever reach. It is
        // what this one used to be - see editorSidebarWidths().
        QVERIFY2(mTooNarrow >= mpEditor->minimumWidth(),
                 qPrintable(qsl("The editor cannot be dragged below its own breakpoint (minimum %1px, breakpoint %2px), so the forced rail is unreachable")
                                    .arg(QString::number(mpEditor->minimumWidth()), QString::number(widths.collapseBelow))));

        resizeEditor(mWideEnough);
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

    // Nothing stored yet, and a window with room: the names are on show, and
    // the toggle is pressable
    void test_aFreshProfileStartsWithTheNamesShowing()
    {
        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(!railShowing(), qPrintable(qsl("A fresh profile opened as a rail: %1").arg(state())));
        QVERIFY2(toggle() != nullptr, "The toolbar has no sidebar toggle");
        QVERIFY2(toggle()->isEnabled(), "The sidebar toggle cannot be pressed on a window with room for the names");
    }

    // Where it is and what it looks like: the leading item of the actions
    // toolbar, ahead of everything that acts on what the sidebar points at,
    // drawn as a picture alone on a bar whose other buttons carry their names
    void test_theToggleLeadsTheToolbarAsAPictureAlone()
    {
        QToolBar* pToolBar = mpEditor->toolBar;
        QVERIFY2(pToolBar != nullptr, "The editor has no actions toolbar");
        const QList<QAction*> actions = pToolBar->actions();
        QVERIFY2(!actions.isEmpty(), "The actions toolbar is empty");
        qInfo().noquote() << qsl("  the toolbar leads with \"%1\", drawn %2, and holds %3 items")
                                     .arg(actions.constFirst()->text(),
                                          toggle()->toolButtonStyle() == Qt::ToolButtonIconOnly ? qsl("as a picture alone") : qsl("with its name beside the picture"),
                                          QString::number(actions.size()));

        QCOMPARE(actions.constFirst(), mpEditor->mpAction_editorSidebarToggle);
        QVERIFY2(!mpEditor->mpAction_editorSidebarToggle->icon().isNull(), "The sidebar toggle has no picture");
        QCOMPARE(toggle()->toolButtonStyle(), Qt::ToolButtonIconOnly);
        // ...and the buttons it leads still carry theirs, or this says nothing
        if (auto* pAddItem = qobject_cast<QToolButton*>(pToolBar->widgetForAction(mpEditor->mAddItem))) {
            QVERIFY2(pAddItem->toolButtonStyle() != Qt::ToolButtonIconOnly, "Every button on the toolbar is a picture alone, so the toggle being one says nothing");
        }
    }

    // It says which of the sidebar's two states is in force rather than what
    // the next press will do, which is what a checkable button is for
    void test_theToggleIsHeldDownWhileTheNamesAreShowing()
    {
        QVERIFY2(mpEditor->mpAction_editorSidebarToggle->isCheckable(), "The sidebar toggle does not hold a state");
        QVERIFY2(!railShowing(), "This case starts from the names showing");
        QVERIFY2(mpEditor->mpAction_editorSidebarToggle->isChecked(), "The names are showing and the toggle is not held down");

        pressTheToggle();
        qInfo().noquote() << qsl("  %1, toggle held down: %2").arg(state(), mpEditor->mpAction_editorSidebarToggle->isChecked() ? qsl("yes") : qsl("no"));
        QVERIFY2(railShowing(), "The toggle did not minimise the sidebar");
        QVERIFY2(!mpEditor->mpAction_editorSidebarToggle->isChecked(), "The sidebar is a rail and the toggle is still held down");

        pressTheToggle();
        QVERIFY2(!railShowing() && mpEditor->mpAction_editorSidebarToggle->isChecked(), "The toggle did not come back up with the names");
    }

    // The toggle says what happens rather than open or close, and a screen
    // reader hears the same words - which matters most in the rail, where the
    // rows have given their names up to their tooltips
    void test_theToggleNamesWhatItDoesToTheLabels()
    {
        const QString expanded = toggle()->accessibleName();
        QVERIFY2(!expanded.isEmpty(), "The sidebar toggle has no accessible name");
        qInfo().noquote() << qsl("  with the names showing: \"%1\"").arg(expanded);

        pressTheToggle();
        const QString collapsed = toggle()->accessibleName();
        qInfo().noquote() << qsl("  as a rail: \"%1\"").arg(collapsed);
        QVERIFY2(collapsed != expanded, "The sidebar toggle says the same thing in both modes");

        // Whatever the translation, neither wording may be about opening or
        // closing the sidebar - it is always there
        for (const QString& wording : {expanded, collapsed}) {
            QVERIFY2(!wording.contains(qsl("close"), Qt::CaseInsensitive) && !wording.contains(qsl("open"), Qt::CaseInsensitive),
                     qPrintable(qsl("The sidebar toggle reads as opening or closing the sidebar: \"%1\"").arg(wording)));
        }
    }

    // Pressed once, on a window with all the room it needs: the rail is the
    // user's choice and the width has nothing to say about it
    void test_aManualRailStaysOnAWideWindow()
    {
        QVERIFY2(railShowing(), qPrintable(qsl("The toggle did not minimise the sidebar: %1").arg(state())));

        resizeEditor(mWideEnough + 200);
        QVERIFY2(railShowing(), qPrintable(qsl("A wider window put the names back over the user's choice: %1").arg(state())));
        resizeEditor(mWideEnough);
    }

    // ...and pressed again, the names come back
    void test_theTogglePutsTheNamesBack()
    {
        pressTheToggle();
        QVERIFY2(!railShowing(), qPrintable(qsl("The toggle did not bring the names back: %1").arg(state())));
    }

    // The width's own collapse, over a preference that says otherwise. It is
    // the sidebar that changes and nothing else: the preference is untouched,
    // and so is what a close would write out
    void test_aNarrowWindowForcesTheRailWithoutStoringIt()
    {
        QVERIFY2(!railShowing(), "This case starts from the names showing");

        resizeEditor(mTooNarrow);
        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(railShowing(), qPrintable(qsl("A window too narrow for the names kept them: %1").arg(state())));
        QVERIFY2(mpEditor->mEditorSidebarLabelsShown, qPrintable(qsl("Running out of room rewrote the user's preference: %1").arg(state())));
        // The toggle cannot be pressed here, so it says why rather than doing
        // nothing
        QVERIFY2(!toggle()->isEnabled(), "The toggle offers labels a window this narrow cannot draw");
    }

    // ...so widening it again is settled by the preference alone
    void test_wideningTheWindowBringsTheNamesBack()
    {
        resizeEditor(mWideEnough);
        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(!railShowing(), qPrintable(qsl("The names did not come back when the window had room again: %1").arg(state())));
    }

    // The round trip across instances: the toggle's choice is written on
    // close, and a brand new editor opens with it
    void test_aManualRailSurvivesAFreshEditor()
    {
        pressTheToggle();
        QVERIFY2(railShowing(), "The toggle did not minimise the sidebar");

        mpEditor->close();
        QCoreApplication::processEvents();
        QVERIFY2(!storedLabelsShown(), "Closing the editor did not write the toggle's choice down");

        delete mpEditor;
        mpEditor = nullptr;
        QVERIFY2(mpHost->mpEditorDialog.isNull(), "The editor should have been destroyed");

        mudlet::self()->slot_showEditorDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "A fresh editor should have been created");
        resizeEditor(mWideEnough);

        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(railShowing(), qPrintable(qsl("A fresh editor forgot that the names had been given up: %1").arg(state())));
    }
};

#include "EditorSidebarCollapseTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorSidebarCollapseTest)
