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
 * A package can be switched off from the tree a row of it is being looked at
 * in, and the row says which package that is.
 *
 * Three things can go wrong without anything else noticing: the context menu
 * entry offering itself over a row that came from no package, the switch not
 * surviving the clean reset Host::setPackageEnabled() asks the editor for - the
 * trees are emptied and refilled, so a command holding a tree item or an item id
 * would be undone against something that no longer exists - and the package's
 * top folder being drawn as a plain folder, which is the one row in the tree
 * where the dot beside it means the whole package rather than one item.
 *
 * Run with: ctest -R EditorPackageToggleTest -V
 */

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <chrono>

#include "EditorTreeDelegate.h"
#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TTreeWidget.h"
#include "TTrigger.h"
#include "TelnetServerStub.h"
#include "TriggerUnit.h"
#include "ctelnet.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorPackageToggleTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    std::unique_ptr<QTemporaryDir> mpWorkDir;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorPackageToggle-Test-Profile");
    // An install names the package after the file it came out of, so this is both
    const QString mPackageName = qsl("editor-package-toggle-fixture");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    // One trigger inside one folder, and one script. The install wraps each
    // kind in a master folder of its own named after the package, and that
    // folder is the row these cases are about. The script is what the view case
    // below picks a row in a tree that is not the Triggers one.
    QString packageXml() const
    {
        return qsl(R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE MudletPackage>
<MudletPackage version="1.001">
    <TriggerPackage>
        <TriggerGroup isActive="yes" isFolder="yes" isTempTrigger="no" isMultiline="no" isPerlSlashGOption="no" isColorizerTrigger="no" isFilterTrigger="no" isSoundTrigger="no" isColorTrigger="no" isColorTriggerFg="no" isColorTriggerBg="no">
            <name>%1 folder</name>
            <script></script>
            <triggerType>0</triggerType>
            <conditonLineDelta>0</conditonLineDelta>
            <mStayOpen>0</mStayOpen>
            <mCommand></mCommand>
            <packageName></packageName>
            <mFgColor>#000000</mFgColor>
            <mBgColor>#000000</mBgColor>
            <mSoundFile></mSoundFile>
            <colorTriggerFgColor>#000000</colorTriggerFgColor>
            <colorTriggerBgColor>#000000</colorTriggerBgColor>
            <regexCodeList />
            <regexCodePropertyList />
            <Trigger isActive="yes" isFolder="no" isTempTrigger="no" isMultiline="no" isPerlSlashGOption="no" isColorizerTrigger="no" isFilterTrigger="no" isSoundTrigger="no" isColorTrigger="no" isColorTriggerFg="no" isColorTriggerBg="no">
                <name>%1 trigger</name>
                <script>editorPackageToggleFired = true</script>
                <triggerType>0</triggerType>
                <conditonLineDelta>0</conditonLineDelta>
                <mStayOpen>0</mStayOpen>
                <mCommand></mCommand>
                <packageName></packageName>
                <mFgColor>#000000</mFgColor>
                <mBgColor>#000000</mBgColor>
                <mSoundFile></mSoundFile>
                <colorTriggerFgColor>#000000</colorTriggerFgColor>
                <colorTriggerBgColor>#000000</colorTriggerBgColor>
                <regexCodeList>
                    <string>editor package toggle</string>
                </regexCodeList>
                <regexCodePropertyList>
                    <integer>0</integer>
                </regexCodePropertyList>
            </Trigger>
        </TriggerGroup>
    </TriggerPackage>
    <ScriptPackage>
        <Script isActive="yes" isFolder="no">
            <name>%1 script</name>
            <packageName></packageName>
            <script>editorPackageToggleScriptRan = true</script>
            <eventHandlerList />
        </Script>
    </ScriptPackage>
</MudletPackage>
)")
                .arg(mPackageName);
    }

    void deleteProfileDirectory(const QString& profileName)
    {
        QDir dir(mudlet::getMudletPath(enums::profileHomePath, profileName));
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }

    TTreeWidget* tree() const { return mpEditor->treeWidget_triggers; }

    uiDesign::EditorTreeDelegate* delegate() const { return qobject_cast<uiDesign::EditorTreeDelegate*>(tree()->itemDelegateForIndex(QModelIndex())); }

    QModelIndex indexOf(QTreeWidgetItem* pItem) const { return tree()->indexAt(tree()->visualItemRect(pItem).center()); }

    void settle() const
    {
        QCoreApplication::processEvents();
        QTest::qWait(120ms);
        QCoreApplication::processEvents();
    }

    // The package's top folder, found again after every clean reset: the reset
    // empties the trees and refills them, so an item pointer held across one
    // names freed memory
    QTreeWidgetItem* packageRoot() const
    {
        QTreeWidgetItem* pBase = mpEditor->mpTriggerBaseItem;
        if (!pBase) {
            return nullptr;
        }
        for (int row = 0; row < pBase->childCount(); ++row) {
            if (pBase->child(row)->text(0) == mPackageName) {
                return pBase->child(row);
            }
        }
        return nullptr;
    }

    void chooseRow(QTreeWidgetItem* pItem) const
    {
        tree()->setCurrentItem(pItem);
        settle();
    }

    TTreeWidget* scriptsTree() const { return mpEditor->treeWidget_scripts; }

    // The package's top folder in the scripts tree - the one the view case works
    // in, so that the view it is left in is not the view a reset would land on
    QTreeWidgetItem* scriptsPackageRoot() const
    {
        QTreeWidgetItem* pBase = mpEditor->mpScriptsBaseItem;
        if (!pBase) {
            return nullptr;
        }
        for (int row = 0; row < pBase->childCount(); ++row) {
            if (pBase->child(row)->text(0) == mPackageName) {
                return pBase->child(row);
            }
        }
        return nullptr;
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

        mpWorkDir = std::make_unique<QTemporaryDir>();
        QVERIFY(mpWorkDir->isValid());

        mpServer = new TelnetServerStub(qApp);
        mpServer->start(mLocalhost, 0);
        QVERIFY2(mpServer->isListening(), qPrintable(qsl("TelnetServerStub failed to start: %1").arg(mpServer->errorString())));
        mPort = QString::number(mpServer->serverPort());
        mudlet::start();
        mudlet::self()->setupConfig();
        QCOMPARE(mudlet::getMudletPath(enums::mainPath), qsl("%1/mudlet").arg(mConfigDir.path()));
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>(qsl("MudletInstanceCoordinator")));
        mudlet::self()->init();
        mudlet::self()->setInterfaceLanguage(qsl("en_US"));
        mudlet::self()->setStorePasswordsSecurely(false);

        deleteProfileDirectory(mProfileName);
        mpHost = TestProfile::create(mProfileName, mLocalhost, mPort);
        QVERIFY2(mpHost, "No active host after profile creation");
        QSignalSpy connected(&(mpHost->mTelnet), &cTelnet::signal_connected);
        QVERIFY2(connected.wait(2000), "Could not connect with the host.");

        mudlet::self()->slot_showScriptDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor, "the script editor was not opened at all");

        const QString path = qsl("%1/%2.xml").arg(mpWorkDir->path(), mPackageName);
        QFile file(path);
        QVERIFY2(file.open(QFile::WriteOnly | QFile::Text), "could not write the fixture package");
        file.write(packageXml().toUtf8());
        file.close();
        QVERIFY2(mpHost->installPackage(path, enums::PackageModuleType::Package).first, "the fixture package would not install");
        mpHost->waitForProfileSave();
        settle();

        mpEditor->slot_showTriggers();
        settle();
        QVERIFY2(delegate(), "the triggers tree is not drawn by an EditorTreeDelegate");
        QVERIFY2(packageRoot(), qPrintable(qsl("SETUP: the triggers tree has no top folder named \"%1\" after the install").arg(mPackageName)));
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

    // The one row in the tree that stands for the package rather than for an
    // item of it carries the package manager's own glyph, so a reader can tell
    // which folder is the package and which is a folder somebody made
    void test_aPackagesTopFolderIsMarkedAsOne()
    {
        QTreeWidgetItem* pRoot = packageRoot();
        QVERIFY(pRoot);
        const uiDesign::EditorTreeDelegate::RowMark mark = delegate()->markOf(indexOf(pRoot));
        qInfo().noquote() << qsl("  the package's top folder is marked %1").arg(QString::number(static_cast<int>(mark)));
        QVERIFY2(mark == uiDesign::EditorTreeDelegate::RowMark::Package,
                 qPrintable(qsl("the package's top folder is marked %1 rather than %2 - it is drawn as an ordinary folder")
                                    .arg(QString::number(static_cast<int>(mark)), QString::number(static_cast<int>(uiDesign::EditorTreeDelegate::RowMark::Package)))));

        // The folder the package ships inside that one is somebody's folder, not
        // a package, and is left reading as one. Opened first: a row that is
        // folded away has no rectangle on the viewport, and so no index to read.
        tree()->expandItem(pRoot);
        QTest::qWait(400ms);
        QTreeWidgetItem* pInner = pRoot->childCount() > 0 ? pRoot->child(0) : nullptr;
        QVERIFY2(pInner, "the package's top folder holds nothing, so there is no ordinary folder to tell it apart from");
        QVERIFY2(indexOf(pInner).isValid(), "the folder inside the package has no row on the viewport to read");
        QVERIFY2(delegate()->markOf(indexOf(pInner)) == uiDesign::EditorTreeDelegate::RowMark::Folder,
                 qPrintable(qsl("the folder inside the package is marked %1 rather than as a folder - every folder of the package reads as the package")
                                    .arg(QString::number(static_cast<int>(delegate()->markOf(indexOf(pInner)))))));
    }

    // The context menu over a package's row offers to switch that package, and
    // names it. A row that came from no package is offered nothing.
    void test_theMenuOffersTheSwitchOnlyWhereThereIsAPackage()
    {
        QVERIFY2(mpEditor->mpAction_togglePackage, "the trees carry no package switch action at all");
        QVERIFY2(tree()->actions().contains(mpEditor->mpAction_togglePackage), "the triggers tree's context menu does not hold the package switch");

        chooseRow(packageRoot());
        qInfo().noquote() << qsl("  over the package's row the entry reads \"%1\"").arg(mpEditor->mpAction_togglePackage->text());
        QVERIFY2(mpEditor->mpAction_togglePackage->isVisible(), "the package switch is hidden over a row that came from a package");
        QCOMPARE(mpEditor->mpAction_togglePackage->text(), qsl("Turn off the %1 package").arg(mPackageName));
        QVERIFY2(!mpEditor->mpAction_togglePackage->icon().isNull(), "the package switch carries no glyph, where every other entry in the menu does");

        // A trigger of the profile's own, made where the package is not
        tree()->setCurrentItem(mpEditor->mpTriggerBaseItem);
        mpEditor->addTrigger(false);
        settle();
        QTreeWidgetItem* pOwn = tree()->currentItem();
        QVERIFY2(pOwn && pOwn != packageRoot(), "no trigger of the profile's own was added to read against");
        QVERIFY2(!mpEditor->mpAction_togglePackage->isVisible(),
                 qPrintable(qsl("the package switch is offered over \"%1\", which came from no package, and reads \"%2\"").arg(pOwn->text(0), mpEditor->mpAction_togglePackage->text())));
    }

    // Triggering it switches the package, the row says so, and an undo puts it
    // back - across the clean reset that empties and refills every tree
    void test_theSwitchGoesThroughTheUndoStackAndSurvivesTheReset()
    {
        mpEditor->slot_showTriggers();
        settle();
        chooseRow(packageRoot());
        QVERIFY2(mpHost->packageEnabled(mPackageName), "SETUP: the fixture package is already switched off");
        QVERIFY2(mpEditor->mpAction_togglePackage->isVisible(), "SETUP: the package switch is not offered over the package's row");

        mpEditor->mpAction_togglePackage->trigger();
        settle();
        QVERIFY2(!mpHost->packageEnabled(mPackageName), "the menu entry was triggered and the package is still running");

        QTreeWidgetItem* pRoot = packageRoot();
        QVERIFY2(pRoot, "the package's top folder went missing when the package was switched off");
        const uiDesign::EditorTreeDelegate::DotState off = delegate()->dotStateOf(indexOf(pRoot));
        qInfo().noquote() << qsl("  with the package off the row's dot reads %1").arg(QString::number(static_cast<int>(off)));
        QVERIFY2(off == uiDesign::EditorTreeDelegate::DotState::Off,
                 qPrintable(qsl("the switched-off package's row reads %1 rather than %2 - the row does not say the package is off")
                                    .arg(QString::number(static_cast<int>(off)), QString::number(static_cast<int>(uiDesign::EditorTreeDelegate::DotState::Off)))));

        QVERIFY2(mpEditor->mpUndoStack && mpEditor->mpUndoStack->canUndo(), "switching a package left nothing on the undo stack");
        qInfo().noquote() << qsl("  the undo stack offers \"%1\"").arg(mpEditor->mpUndoStack->undoText());
        QCOMPARE(mpEditor->mpUndoStack->undoText(), qsl("Turn off package %1").arg(mPackageName));

        mpEditor->mpUndoStack->undo();
        settle();
        QVERIFY2(mpHost->packageEnabled(mPackageName), "undoing the switch left the package off");

        pRoot = packageRoot();
        QVERIFY2(pRoot, "the package's top folder went missing when the switch was undone");
        const uiDesign::EditorTreeDelegate::DotState on = delegate()->dotStateOf(indexOf(pRoot));
        QVERIFY2(on == uiDesign::EditorTreeDelegate::DotState::Running,
                 qPrintable(qsl("after the undo the package's row reads %1 rather than %2")
                                    .arg(QString::number(static_cast<int>(on)), QString::number(static_cast<int>(uiDesign::EditorTreeDelegate::DotState::Running)))));
    }

    // Switching a package changes states, not structure, so the editor is left
    // exactly where the reader was: the view they pressed the switch in, the row
    // they were on, and the same item behind that row. The clean reset this used
    // to ask for empties and refills all six trees and ends in the Triggers
    // view, which threw them out of whichever view they were reading.
    void test_theSwitchLeavesTheViewAndTheChosenRowAlone()
    {
        mpEditor->slot_showScripts();
        settle();
        QVERIFY2(mpEditor->resolveCurrentView() == EditorViewType::cmScriptView, "SETUP: the editor would not open the Scripts view");

        QTreeWidgetItem* pScriptRoot = scriptsPackageRoot();
        QVERIFY2(pScriptRoot, qPrintable(qsl("SETUP: the scripts tree has no top folder named \"%1\" - the fixture package shipped no script").arg(mPackageName)));
        scriptsTree()->expandItem(pScriptRoot);
        settle();
        QTreeWidgetItem* pChosen = pScriptRoot->childCount() > 0 ? pScriptRoot->child(0) : pScriptRoot;
        const QString chosenName = pChosen->text(0);
        scriptsTree()->setCurrentItem(pChosen);
        settle();

        QVERIFY2(mpHost->packageEnabled(mPackageName), "SETUP: the fixture package is already switched off, so the switch below would do nothing");
        QVERIFY2(mpEditor->mpAction_togglePackage->isVisible(), qPrintable(qsl("SETUP: the package switch is not offered over the script row \"%1\"").arg(chosenName)));

        mpEditor->mpAction_togglePackage->trigger();
        QCoreApplication::processEvents();
        // Past the single shot the clean reset was queued on: it did its damage
        // a turn of the event loop after the switch, not inside it
        QTest::qWait(50ms);
        QCoreApplication::processEvents();

        QVERIFY2(!mpHost->packageEnabled(mPackageName), "the menu entry was triggered and the package is still running");
        qInfo().noquote() << qsl("  after the switch the editor shows view %1, row \"%2\", and the entry reads \"%3\"")
                                     .arg(QString::number(static_cast<int>(mpEditor->resolveCurrentView())),
                                          scriptsTree()->currentItem() ? scriptsTree()->currentItem()->text(0) : qsl("(none)"),
                                          mpEditor->mpAction_togglePackage->text());
        QVERIFY2(mpEditor->resolveCurrentView() == EditorViewType::cmScriptView,
                 qPrintable(qsl("switching the package threw the editor out of the Scripts view (%1) into view %2")
                                    .arg(QString::number(static_cast<int>(EditorViewType::cmScriptView)), QString::number(static_cast<int>(mpEditor->resolveCurrentView())))));
        QVERIFY2(scriptsTree()->currentItem() == pChosen, "the row the switch was pressed over is not the item chosen afterwards - the scripts tree was emptied and refilled under it");
        QVERIFY2(scriptsTree()->currentItem()->text(0) == chosenName,
                 qPrintable(qsl("the chosen row reads \"%1\" where it read \"%2\" before the switch").arg(scriptsTree()->currentItem()->text(0), chosenName)));
        QVERIFY2(mpEditor->mpAction_togglePackage->text().startsWith(qsl("Turn on")),
                 qPrintable(qsl("with the package switched off the entry over the same row still reads \"%1\"").arg(mpEditor->mpAction_togglePackage->text())));

        // Nothing was rebuilt, so what says the package is off in the five trees
        // the switch was not pressed in is the repaint alone. Opening a view
        // refills nothing, so the dot read here is the one the refresh left.
        mpEditor->slot_showTriggers();
        settle();
        QTreeWidgetItem* pRoot = packageRoot();
        QVERIFY2(pRoot, "the package's top folder went missing from the triggers tree when the package was switched off");
        const uiDesign::EditorTreeDelegate::DotState off = delegate()->dotStateOf(indexOf(pRoot));
        qInfo().noquote() << qsl("  the triggers tree was never refilled and its package row reads %1").arg(QString::number(static_cast<int>(off)));
        QVERIFY2(off == uiDesign::EditorTreeDelegate::DotState::Off,
                 qPrintable(qsl("the triggers tree's package row reads %1 rather than %2 - a repaint is not enough to say the package is off")
                                    .arg(QString::number(static_cast<int>(off)), QString::number(static_cast<int>(uiDesign::EditorTreeDelegate::DotState::Off)))));

        mpHost->setPackageEnabled(mPackageName, true);
        settle();
    }
};

#include "EditorPackageToggleTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorPackageToggleTest)
