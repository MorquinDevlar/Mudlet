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
 * The five forms that are a fixed set of fields, and the seam over them.
 *
 * Three things this holds them to:
 *
 * - The code pane's heading resizes nothing in those five views. Their forms
 *   have no list and no editor in them, so a column dragged taller than the
 *   fields is empty room under them: the column is held to its contents'
 *   height, and a push on the handle leaves it there. The trigger view, whose
 *   form does hold a pattern list, still grows.
 *
 * - Choosing the "Timers" root row leaves the code pane hidden the way the six
 *   other roots do. clearTimerForm() hid its own form twice and the code pane
 *   never, so the root row showed the placeholder notice over a Lua editor
 *   holding the last timer's script.
 *
 * - Every view's first field starts at the same x, and inside a form the field
 *   on a row under the head row starts where the name field above it does.
 *
 * Run with: ctest -R EditorFormShellTest -V
 */

#include <QLineEdit>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <chrono>

#include "ChipRow.h"
#include "GripSplitter.h"
#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgAliasMainArea.h"
#include "dlgKeysMainArea.h"
#include "dlgScriptsMainArea.h"
#include "dlgSourceEditorArea.h"
#include "dlgTimersMainArea.h"
#include "dlgTriggerEditor.h"
#include "dlgTriggersMainArea.h"
#include "dlgVarsMainArea.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorFormShellTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorFormShell-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    // How hard the handle is pushed at a form column that should not move
    static constexpr int scmPush = 200;

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
        if (!spy.wait(2000)) {
            QFAIL("Could not connect with the host.");
        }
    }

    // The one frame every form can be compared in: each field is nested a
    // different number of layouts deep in its own form
    int leftEdgeOf(QWidget* pWidget) const { return pWidget->mapTo(mpEditor, QPoint(0, 0)).x(); }

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
        case EditorViewType::cmKeysView:
            mpEditor->slot_showKeys();
            break;
        case EditorViewType::cmScriptView:
            mpEditor->slot_showScripts();
            break;
        case EditorViewType::cmVarsView:
            mpEditor->slot_showVariables();
            break;
        default:
            QFAIL("this test knows nothing about that view");
        }
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
    }

    // The first row of a view's tree, which is the item its form is filled from
    void chooseTheFirstItem(QTreeWidget* pTree)
    {
        QTreeWidgetItem* pBase = pTree->topLevelItem(0);
        if (!pBase || !pBase->childCount()) {
            return;
        }
        QTreeWidgetItem* pRow = pBase->child(0);
        pTree->setCurrentItem(pRow);
        emit pTree->itemClicked(pRow, 0);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
    }

    QLineEdit* nameFieldOf(const EditorViewType view) const
    {
        switch (view) {
        case EditorViewType::cmTriggerView:
            return mpEditor->mpTriggersMainArea->lineEdit_trigger_name;
        case EditorViewType::cmAliasView:
            return mpEditor->mpAliasMainArea->lineEdit_alias_name;
        case EditorViewType::cmTimerView:
            return mpEditor->mpTimersMainArea->lineEdit_timer_name;
        case EditorViewType::cmKeysView:
            return mpEditor->mpKeysMainArea->lineEdit_key_name;
        case EditorViewType::cmScriptView:
            return mpEditor->mpScriptsMainArea->lineEdit_script_name;
        case EditorViewType::cmVarsView:
            return mpEditor->mpVarsMainArea->lineEdit_var_name;
        default:
            return nullptr;
        }
    }

    // The field on the row under the head row, which has to start where the
    // name field above it does. The variables form has no such row yet.
    QWidget* leadFieldOf(const EditorViewType view) const
    {
        switch (view) {
        case EditorViewType::cmAliasView:
            return mpEditor->mpAliasMainArea->lineEdit_alias_pattern;
        case EditorViewType::cmKeysView:
            return mpEditor->mpKeysMainArea->lineEdit_key_binding;
        case EditorViewType::cmScriptView:
            return mpEditor->mpChipRow_scriptEvents;
        case EditorViewType::cmTimerView:
            // The interval is a sentence, and the words lead it - so what has
            // to start where the name does is the row, not the first field in it
            return mpEditor->mpWidget_timerInterval;
        default:
            return nullptr;
        }
    }

    static QString nameOf(const EditorViewType view)
    {
        switch (view) {
        case EditorViewType::cmTriggerView:
            return qsl("triggers");
        case EditorViewType::cmAliasView:
            return qsl("aliases");
        case EditorViewType::cmTimerView:
            return qsl("timers");
        case EditorViewType::cmKeysView:
            return qsl("keys");
        case EditorViewType::cmScriptView:
            return qsl("scripts");
        case EditorViewType::cmVarsView:
            return qsl("variables");
        default:
            return qsl("an unknown view");
        }
    }

    // The five whose form is a fixed set of fields, in the order they are
    // walked below
    static QList<EditorViewType> fixedViews()
    {
        return {EditorViewType::cmAliasView, EditorViewType::cmTimerView, EditorViewType::cmKeysView, EditorViewType::cmScriptView, EditorViewType::cmVarsView};
    }

    void openAndChoose(const EditorViewType view)
    {
        enterView(view);
        switch (view) {
        case EditorViewType::cmTriggerView:
            chooseTheFirstItem(mpEditor->treeWidget_triggers);
            break;
        case EditorViewType::cmAliasView:
            chooseTheFirstItem(mpEditor->treeWidget_aliases);
            break;
        case EditorViewType::cmTimerView:
            chooseTheFirstItem(mpEditor->treeWidget_timers);
            break;
        case EditorViewType::cmKeysView:
            chooseTheFirstItem(mpEditor->treeWidget_keys);
            break;
        case EditorViewType::cmScriptView:
            chooseTheFirstItem(mpEditor->treeWidget_scripts);
            break;
        case EditorViewType::cmVarsView:
            chooseTheFirstItem(mpEditor->treeWidget_variables);
            break;
        default:
            break;
        }
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

        startProfile(mProfileName, mLocalhost, mPort);

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(1100, 900);
        QTest::qWait(50ms);

        // One item of every kind, so that every form is filled from something a
        // user would be looking at rather than showing the empty-view notice
        mpEditor->slot_showTriggers();
        mpEditor->addTrigger(false);
        mpEditor->slot_showAliases();
        mpEditor->addAlias(false);
        mpEditor->slot_showTimers();
        mpEditor->addTimer(false);
        mpEditor->slot_showKeys();
        mpEditor->addKey(false);
        mpEditor->slot_showScripts();
        mpEditor->addScript(false);
        QTest::qWait(100ms);
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

    // The handle over the code pane says what it is by what it does: a grip and
    // a drag in the two views whose forms can use the room, neither in the five
    // that cannot
    void test_theCodeHeadingOnlyDragsWhereTheFormCanUseTheRoom()
    {
        openAndChoose(EditorViewType::cmTriggerView);
        QVERIFY2(mpEditor->splitter_right->handleResizes(1), "the code pane's heading has stopped resizing the trigger form, which holds the pattern list");

        for (const EditorViewType view : fixedViews()) {
            openAndChoose(view);
            QVERIFY2(!mpEditor->splitter_right->handleResizes(1), qPrintable(qsl("the code pane's heading still resizes the %1 form, which is a fixed set of fields").arg(nameOf(view))));
        }
    }

    // ...and the column itself is held to the height its fields ask for, so a
    // push on the handle - or a window resize - cannot leave empty room under
    // the last of them
    void test_aFixedFormColumnStaysAtTheHeightItsFieldsAskFor()
    {
        for (const EditorViewType view : fixedViews()) {
            openAndChoose(view);
            // With no notice over the form the column asks for exactly what its
            // fields need; the notice's own label wraps, so its minimum is
            // taller than the line it shows and the two answers diverge
            mpEditor->hideSystemMessageArea();
            QCoreApplication::processEvents();
            QTest::qWait(50ms);
            const int wanted = mpEditor->mpNonCodeWidgets->sizeHint().height();
            QList<int> sizes = mpEditor->splitter_right->sizes();
            QVERIFY2(sizes.size() >= 2, "the right hand splitter has lost a pane");
            qInfo().noquote() << qsl("  %1: the column asks for %2 and the panes are %3 / %4").arg(nameOf(view), QString::number(wanted), QString::number(sizes.at(0)), QString::number(sizes.at(1)));
            QCOMPARE(mpEditor->mpNonCodeWidgets->height(), wanted);

            sizes[1] -= scmPush;
            sizes[0] += scmPush;
            mpEditor->splitter_right->setSizes(sizes);
            QCoreApplication::processEvents();
            QTest::qWait(50ms);

            // What the reader sees, rather than what the splitter has written
            // down: QSplitter records the size it asked for and the column's own
            // maximum is what actually holds it there
            QVERIFY2(mpEditor->mpNonCodeWidgets->height() == wanted,
                     qPrintable(qsl("the %1 form column was pushed to %2 and stayed there - it asks for %3")
                                        .arg(nameOf(view), QString::number(mpEditor->mpNonCodeWidgets->height()), QString::number(wanted))));
        }
    }

    // The trigger form is the other half of the same rule: its pattern list can
    // use whatever room it is given, so the same push moves it
    void test_theTriggerFormColumnStillTakesAPush()
    {
        openAndChoose(EditorViewType::cmTriggerView);
        QList<int> sizes = mpEditor->splitter_right->sizes();
        QVERIFY2(sizes.at(1) > scmPush + 150, "not enough code pane to take the push off for this case");
        const int before = sizes.at(0);
        sizes[0] += scmPush;
        sizes[1] -= scmPush;
        mpEditor->splitter_right->setSizes(sizes);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);

        QVERIFY2(mpEditor->splitter_right->sizes().at(0) > before,
                 qPrintable(qsl("the trigger form column was pushed from %1 and came back to %2").arg(QString::number(before), QString::number(mpEditor->splitter_right->sizes().at(0)))));
    }

    // Every other root row hides the code pane along with its form; the timers
    // one hid its form twice instead, leaving the last timer's Lua on show
    // under the "nothing chosen" notice
    void test_theTimersRootRowHidesTheCodePane()
    {
        openAndChoose(EditorViewType::cmTimerView);
        QVERIFY2(mpEditor->mpSourceEditorArea->isVisible(), "the code pane is not showing for a chosen timer, so this case says nothing");

        QTreeWidgetItem* pRoot = mpEditor->treeWidget_timers->topLevelItem(0);
        QVERIFY2(pRoot != nullptr, "the timers tree has no root row");
        mpEditor->treeWidget_timers->setCurrentItem(pRoot);
        emit mpEditor->treeWidget_timers->itemClicked(pRoot, 0);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);

        QVERIFY2(!mpEditor->mpTimersMainArea->isVisible(), "the timer form is still showing on the root row");
        QVERIFY2(!mpEditor->mpSourceEditorArea->isVisible(), "the code pane is still showing on the timers root row, holding the last timer's script under the placeholder notice");
    }

    // One line down the left of the window: whichever view is open, the name is
    // typed at the same x
    void test_everyViewsNameFieldStartsAtTheSameX()
    {
        openAndChoose(EditorViewType::cmTriggerView);
        const int triggerEdge = leftEdgeOf(nameFieldOf(EditorViewType::cmTriggerView));
        QVERIFY2(triggerEdge > 0, "the trigger form's name field is not on the window");

        for (const EditorViewType view : fixedViews()) {
            openAndChoose(view);
            const int edge = leftEdgeOf(nameFieldOf(view));
            qInfo().noquote() << qsl("  %1: the name field starts at %2, the trigger form's at %3").arg(nameOf(view), QString::number(edge), QString::number(triggerEdge));
            QVERIFY2(edge == triggerEdge,
                     qPrintable(qsl("the %1 form types its name at %2 while the trigger form types it at %3").arg(nameOf(view), QString::number(edge), QString::number(triggerEdge))));
        }
    }

    // ...and inside a form, the field on the row under the head row starts
    // where the name field above it does
    void test_aRowUnderTheHeadRowStartsWhereTheNameDoes()
    {
        for (const EditorViewType view : fixedViews()) {
            QWidget* pLeadField = leadFieldOf(view);
            if (!pLeadField) {
                continue;
            }
            openAndChoose(view);
            QVERIFY2(pLeadField->isVisible(), qPrintable(qsl("the %1 form's second row is not showing").arg(nameOf(view))));
            const int nameEdge = leftEdgeOf(nameFieldOf(view));
            const int leadEdge = leftEdgeOf(pLeadField);
            qInfo().noquote() << qsl("  %1: the name field starts at %2 and the row under it at %3").arg(nameOf(view), QString::number(nameEdge), QString::number(leadEdge));
            QVERIFY2(leadEdge == nameEdge,
                     qPrintable(qsl("the %1 form starts the row under its name at %2 while the name starts at %3").arg(nameOf(view), QString::number(leadEdge), QString::number(nameEdge))));
        }
    }

    // A key group never matches a keystroke - TKey::match() answers no for a
    // folder - so the row that sets one is not a setting it has
    void test_aKeyGroupIsNotOfferedAKeystroke()
    {
        openAndChoose(EditorViewType::cmKeysView);
        QVERIFY2(mpEditor->mpKeysMainArea->lineEdit_key_binding->isVisible(), "a key is not offered the row that sets its keystroke");

        mpEditor->slot_showKeys();
        mpEditor->addKey(true);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);

        QVERIFY2(!mpEditor->mpKeysMainArea->lineEdit_key_binding->isVisible(), "a key group is still offered a keystroke it can never match");
        QVERIFY2(!mpEditor->mpLabel_keyHint->isVisible(), "a key group is still told how to set a keystroke");
    }
};

#include "EditorFormShellTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorFormShellTest)
