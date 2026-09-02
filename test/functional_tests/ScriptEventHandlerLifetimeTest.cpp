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

#include <QFileInfo>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <chrono>

#include "ChipRow.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "EditorUndoStack.h"
#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "ScriptUnit.h"
#include "TScript.h"
#include "TTreeWidget.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgConnectionProfiles.h"
#include "dlgScriptsMainArea.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

// Run with: ctest -R ScriptEventHandlerLifetimeTest -V
//
// One script's events must never land on another, whatever the editor was in
// the middle of when the user moved between them. That was #9835 in its first
// form: the "Add User Event" field noted a row of the list beside it, the note
// outlived the list being torn down, and "+" then renamed a freed row - or a row
// belonging to the next script. The controls are a row of chips now and there is
// no note to outlive anything, but the rule those cases were written for is the
// same one, so they are kept: what a script is registered for follows the
// script, and a name half typed follows nothing at all.
class ScriptEventHandlerLifetimeTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("ScriptEventHandlerLifetime-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    void deleteProfileDirectory(const QString& profileName)
    {
        const QString path = mudlet::getMudletPath(enums::profileHomePath, profileName);
        QDir dir(path);
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

        QSignalSpy spy2(&(mpHost->mTelnet), &cTelnet::signal_connected);
        if (!spy2.wait(2000)) {
            QFAIL("Could not connect with the host.");
        }
    }

    uiDesign::ChipRow* eventRow() const { return mpEditor->mpChipRow_scriptEvents; }

    // The field the row opens to take a name; null while it is closed
    QLineEdit* eventField() const
    {
        QLineEdit* pField = eventRow()->findChild<QLineEdit*>(qsl("editorChipEditor"));
        return pField && !pField->isHidden() ? pField : nullptr;
    }

    // The whole of what a user does to add one: open the field, type, press
    // Return
    void addEvent(const QString& name)
    {
        eventRow()->beginAdd();
        QCoreApplication::processEvents();
        QLineEdit* pField = eventField();
        if (!pField) {
            QTest::qFail("the row did not open its field", __FILE__, __LINE__);
            return;
        }
        pField->setText(name);
        QTest::keyClick(pField, Qt::Key_Return);
        QCoreApplication::processEvents();
    }

    QStringList savedHandlersOf(QTreeWidgetItem* pTreeItem) const
    {
        if (!pTreeItem) {
            return {};
        }
        TScript* pScript = mpHost->getScriptUnit()->getScript(pTreeItem->data(0, Qt::UserRole).toInt());
        return pScript ? pScript->getEventHandlerList() : QStringList{};
    }

    QTreeWidgetItem* addSavedScript(const QString& name, const QStringList& handlers)
    {
        mpEditor->treeWidget_scripts->setCurrentItem(mpEditor->mpScriptsBaseItem);
        mpEditor->addScript(false);
        QTest::qWait(50ms);

        QTreeWidgetItem* pTreeItem = mpEditor->mpCurrentScriptItem;
        if (!pTreeItem) {
            QTest::qFail("addScript() left no current script item", __FILE__, __LINE__);
            return nullptr;
        }
        mpEditor->mpScriptsMainArea->lineEdit_script_name->setText(name);
        for (const QString& handler : handlers) {
            addEvent(handler);
        }
        if (eventRow()->count() != handlers.count()) {
            QTest::qFail("the events did not all reach the row of chips", __FILE__, __LINE__);
            return nullptr;
        }
        mpEditor->slot_saveSelectedItem();
        QTest::qWait(50ms);
        return pTreeItem;
    }

    QTreeWidgetItem* findEventHandlerSearchResult() const
    {
        QList<QTreeWidgetItem*> pending;
        for (int i = 0; i < mpEditor->treeWidget_searchResults->topLevelItemCount(); ++i) {
            pending.append(mpEditor->treeWidget_searchResults->topLevelItem(i));
        }
        while (!pending.isEmpty()) {
            QTreeWidgetItem* pResult = pending.takeFirst();
            if (pResult->data(0, dlgTriggerEditor::TypeRole).toInt() == dlgTriggerEditor::SearchResultIsEventHandler) {
                return pResult;
            }
            for (int i = 0; i < pResult->childCount(); ++i) {
                pending.append(pResult->child(i));
            }
        }
        return nullptr;
    }

    void removeScripts(const QList<QTreeWidgetItem*>& treeItems)
    {
        for (QTreeWidgetItem* pTreeItem : treeItems) {
            mpEditor->treeWidget_scripts->setCurrentItem(pTreeItem);
            mpEditor->slot_deleteItemOrGroup();
            QTest::qWait(20ms);
        }
        mpEditor->mpUndoStack->clear();
    }

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
        mpServer->start(mLocalhost, 0); // ephemeral OS-assigned port avoids collisions across concurrent test runs
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

        mudlet::self()->slot_showScriptDialog();
        QTest::qWait(200ms);

        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->slot_showScripts();
        QTest::qWait(100ms);
    }

    void init()
    {
        if (!mpEditor) {
            QFAIL("the editor was never created, the profile setup must have failed");
        }
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

    // Each script's row is filled from that script, so moving between two of
    // them cannot leave one showing the other's events - nor save them onto it
    void testSwitchingScriptsKeepsEachScriptsOwnEvents()
    {
        QTreeWidgetItem* pScriptA = addSavedScript(qsl("ScriptA"), {qsl("myTestEvent")});
        QTreeWidgetItem* pScriptB = addSavedScript(qsl("ScriptB"), {});

        mpEditor->treeWidget_scripts->setCurrentItem(pScriptA);
        QTest::qWait(50ms);
        QCOMPARE(eventRow()->items(), QStringList{qsl("myTestEvent")});

        mpEditor->treeWidget_scripts->setCurrentItem(pScriptB);
        QTest::qWait(50ms);
        QCOMPARE(eventRow()->count(), 0);

        addEvent(qsl("otherEvent"));
        mpEditor->slot_saveSelectedItem();
        QTest::qWait(50ms);

        QCOMPARE(savedHandlersOf(pScriptB), QStringList{qsl("otherEvent")});
        QCOMPARE(savedHandlersOf(pScriptA), QStringList{qsl("myTestEvent")});

        removeScripts({pScriptA, pScriptB});
    }

    // One click on a tree entry emits itemSelectionChanged and then itemClicked, so
    // slot_scriptsSelected() used to run twice - which tore the events down and
    // built them again under the user. The itemClicked wiring drops same-row
    // emissions, and the row is left as it was.
    void testReselectingTheSameScriptLeavesItsEventsAlone()
    {
        QTreeWidgetItem* pScript = addSavedScript(qsl("SoloScript"), {qsl("myTestEvent")});

        mpEditor->treeWidget_scripts->setCurrentItem(pScript);
        QTest::qWait(50ms);
        QCOMPARE(eventRow()->items(), QStringList{qsl("myTestEvent")});

        emit mpEditor->treeWidget_scripts->itemClicked(pScript, 0);
        QTest::qWait(50ms);

        QCOMPARE(eventRow()->items(), QStringList{qsl("myTestEvent")});

        mpEditor->slot_saveSelectedItem();
        QTest::qWait(50ms);
        QCOMPARE(savedHandlersOf(pScript), QStringList{qsl("myTestEvent")});

        removeScripts({pScript});
    }

    // Showing another script shuts the field along with everything else in the
    // row, so a half-typed name cannot follow the user and land on that one
    void testTypedButUnaddedTextDoesNotFollowToTheNextScript()
    {
        QTreeWidgetItem* pScriptA = addSavedScript(qsl("TypedTextA"), {});
        QTreeWidgetItem* pScriptB = addSavedScript(qsl("TypedTextB"), {});

        mpEditor->treeWidget_scripts->setCurrentItem(pScriptA);
        QTest::qWait(50ms);
        eventRow()->beginAdd();
        QCoreApplication::processEvents();
        QVERIFY2(eventField() != nullptr, "the row did not open its field");
        eventField()->setText(qsl("neverAddedEvent"));

        mpEditor->treeWidget_scripts->setCurrentItem(pScriptB);
        QTest::qWait(50ms);
        QVERIFY2(eventField() == nullptr, "the field is still open on the next script, holding a name typed for the last one");
        QCOMPARE(eventRow()->count(), 0);

        mpEditor->slot_saveSelectedItem();
        QTest::qWait(50ms);

        QCOMPARE(savedHandlersOf(pScriptB), QStringList{});
        QCOMPARE(savedHandlersOf(pScriptA), QStringList{});

        removeScripts({pScriptA, pScriptB});
    }

    // addScript() points mpCurrentScriptItem at the new script before selecting it, so
    // that selection skips the save as well while still emptying the row
    void testAddingAScriptStartsWithNoEvents()
    {
        QTreeWidgetItem* pScript = addSavedScript(qsl("BeforeNewScript"), {qsl("myTestEvent")});

        mpEditor->treeWidget_scripts->setCurrentItem(pScript);
        QTest::qWait(50ms);
        QCOMPARE(eventRow()->count(), 1);

        mpEditor->addScript(false);
        QTest::qWait(50ms);
        QTreeWidgetItem* pNewScript = mpEditor->mpCurrentScriptItem;
        QVERIFY(pNewScript != pScript);
        QCOMPARE(eventRow()->count(), 0);

        mpEditor->mpScriptsMainArea->lineEdit_script_name->setText(qsl("AfterNewScript"));
        addEvent(qsl("brandNewEvent"));
        mpEditor->slot_saveSelectedItem();
        QTest::qWait(50ms);

        QCOMPARE(savedHandlersOf(pNewScript), QStringList{qsl("brandNewEvent")});
        QCOMPARE(savedHandlersOf(pScript), QStringList{qsl("myTestEvent")});

        removeScripts({pScript, pNewScript});
    }

    // F2 on a chip opens the field in its place, and what is typed there
    // replaces that name rather than adding a second one
    void testRenamingAnEventStillWorks()
    {
        QTreeWidgetItem* pScript = addSavedScript(qsl("RenameScript"), {qsl("firstEvent")});

        mpEditor->treeWidget_scripts->setCurrentItem(pScript);
        QTest::qWait(50ms);
        QWidget* pChip = eventRow()->chipAt(0);
        QVERIFY2(pChip != nullptr, "the script's one event is not showing as a chip");

        QTest::keyClick(pChip, Qt::Key_F2);
        QCoreApplication::processEvents();
        QVERIFY2(eventField() != nullptr, "F2 on a chip did not open the field in its place");
        eventField()->setText(qsl("renamedEvent"));
        QTest::keyClick(eventField(), Qt::Key_Return);
        QCoreApplication::processEvents();

        QCOMPARE(eventRow()->items(), QStringList{qsl("renamedEvent")});
        mpEditor->slot_saveSelectedItem();
        QTest::qWait(50ms);

        QCOMPARE(savedHandlersOf(pScript), QStringList{qsl("renamedEvent")});

        removeScripts({pScript});
    }

    // The cross on a chip takes it out of the row, which frees the widget - so
    // this also holds the leak checker over that path
    void testDeletingAnEventReleasesIt()
    {
        QTreeWidgetItem* pScript = addSavedScript(qsl("DeleteScript"), {qsl("firstEvent"), qsl("secondEvent")});

        mpEditor->treeWidget_scripts->setCurrentItem(pScript);
        QTest::qWait(50ms);
        QCOMPARE(eventRow()->count(), 2);

        QWidget* pChip = eventRow()->chipAt(0);
        QVERIFY2(pChip != nullptr, "the first event is not showing as a chip");
        QToolButton* pCross = pChip->findChild<QToolButton*>(qsl("editorChipRemove"));
        QVERIFY2(pCross != nullptr, "a chip has no cross to take it away with");
        pCross->click();
        QTest::qWait(50ms);

        QCOMPARE(eventRow()->items(), QStringList{qsl("secondEvent")});

        mpEditor->slot_saveSelectedItem();
        QTest::qWait(50ms);
        QCOMPARE(savedHandlersOf(pScript), QStringList{qsl("secondEvent")});

        removeScripts({pScript});
    }

    // A search result for an event shows the script it belongs to and points at
    // the chip carrying it, whichever script was open before
    void testSearchResultPointsAtTheEvent()
    {
        QTreeWidgetItem* pScriptA = addSavedScript(qsl("SearchScript"), {qsl("searchableEvent")});
        QTreeWidgetItem* pScriptB = addSavedScript(qsl("OtherScript"), {});

        mpEditor->treeWidget_scripts->setCurrentItem(pScriptB);
        QTest::qWait(50ms);

        mpEditor->comboBox_searchTerms->insertItem(0, qsl("searchableEvent"));
        mpEditor->slot_searchMudletItems(0);
        QTest::qWait(50ms);

        QTreeWidgetItem* pResult = findEventHandlerSearchResult();
        QVERIFY2(pResult != nullptr, "the search found no event handler result to jump to");

        mpEditor->slot_itemSelectedInSearchResults(pResult);
        QTest::qWait(50ms);

        QCOMPARE(mpEditor->mpCurrentScriptItem, pScriptA);
        QCOMPARE(eventRow()->items(), QStringList{qsl("searchableEvent")});
        // focusWidget() rather than hasFocus(): the latter also asks whether the
        // window is the active one, which a test run headlessly cannot promise
        QCOMPARE(eventRow()->focusWidget(), eventRow()->chipAt(0));

        mpEditor->treeWidget_scripts->setCurrentItem(pScriptB);
        QTest::qWait(50ms);
        QCOMPARE(eventRow()->count(), 0);

        addEvent(qsl("afterSearchEvent"));
        mpEditor->slot_saveSelectedItem();
        QTest::qWait(50ms);

        QCOMPARE(savedHandlersOf(pScriptB), QStringList{qsl("afterSearchEvent")});
        QCOMPARE(savedHandlersOf(pScriptA), QStringList{qsl("searchableEvent")});

        removeScripts({pScriptA, pScriptB});
    }
};

#include "ScriptEventHandlerLifetimeTest.moc"
MUDLET_GROUPED_TEST_MAIN(ScriptEventHandlerLifetimeTest)
