/***************************************************************************
 *   Copyright (C) 2026 by Mudlet Developers                               *
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

// Host::setPackageEnabled() has to outlive the session it was called in, and
// the profile XML is the only thing that carries it there. The items' own
// isActive flags round-trip on their own, so what is proved here is what they
// cannot cover: the Host-level flag, a package whose master folder the player
// deleted, and a package installed while the flag already holds it down.
//
// A spec cannot reach any of this: it would have to save the profile, shut the
// session down and load it again inside one test run.

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "ActionUnit.h"
#include "AliasUnit.h"
#include "Host.h"
#include "HostManager.h"
#include "KeyUnit.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "ScriptUnit.h"
#include "TAction.h"
#include "TAlias.h"
#include "TKey.h"
#include "TScript.h"
#include "TTimer.h"
#include "TTrigger.h"
#include "TelnetServerStub.h"
#include "TimerUnit.h"
#include "TriggerUnit.h"
#include "XMLimport.h"
#include "ctelnet.h"
#include "mudlet.h"

#include "GroupedTest.h"

class PackageToggleRoundTripTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    const QString mHostName = qsl("PackageToggle-Test");
    const QString mTargetName = qsl("PackageToggleTarget-Test");
    QString mPort;
    const QString mLocalhost = qsl("localhost");
    // The install names the package after the file, so this is both
    const QString mPackageName = qsl("package-toggle-fixture");
    std::unique_ptr<QTemporaryDir> mpWorkDir;

    // One item in each of the six units, so that every unit's half of the
    // switch has something to switch. The install wraps all of it in a master
    // folder per unit named after the package - those folders are the roots the
    // switch works on.
    QString packageXml() const
    {
        return qsl(R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE MudletPackage>
<MudletPackage version="1.001">
    <TriggerPackage>
        <Trigger isActive="yes" isFolder="no" isTempTrigger="no" isMultiline="no" isPerlSlashGOption="no" isColorizerTrigger="no" isFilterTrigger="no" isSoundTrigger="no" isColorTrigger="no" isColorTriggerFg="no" isColorTriggerBg="no">
            <name>%1 trigger</name>
            <script>packageToggleTriggerFired = true</script>
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
                <string>package toggle trigger</string>
            </regexCodeList>
            <regexCodePropertyList>
                <integer>0</integer>
            </regexCodePropertyList>
        </Trigger>
    </TriggerPackage>
    <TimerPackage>
        <Timer isActive="yes" isFolder="no" isTempTimer="no" isOffsetTimer="no">
            <name>%1 timer</name>
            <script>packageToggleTimerFired = true</script>
            <command></command>
            <packageName></packageName>
            <time>00:10:00.000</time>
        </Timer>
    </TimerPackage>
    <AliasPackage>
        <Alias isActive="yes" isFolder="no">
            <name>%1 alias</name>
            <script>packageToggleAliasFired = true</script>
            <command></command>
            <packageName></packageName>
            <regex>^packageToggleAlias$</regex>
        </Alias>
    </AliasPackage>
    <ActionPackage>
        <ActionGroup isActive="yes" isFolder="yes" isPushButton="no" isFlatButton="no" useCustomLayout="no">
            <name>%1 toolbar</name>
            <packageName></packageName>
            <script></script>
            <css></css>
            <commandButtonUp></commandButtonUp>
            <commandButtonDown></commandButtonDown>
            <icon></icon>
            <orientation>0</orientation>
            <location>0</location>
            <posX>0</posX>
            <posY>0</posY>
            <mButtonState>1</mButtonState>
            <sizeX>80</sizeX>
            <sizeY>30</sizeY>
            <buttonColumn>1</buttonColumn>
            <buttonFillerOffset>0</buttonFillerOffset>
            <buttonRotation>0</buttonRotation>
            <Action isActive="yes" isFolder="no" isPushButton="no" isFlatButton="no" useCustomLayout="no">
                <name>%1 button</name>
                <packageName></packageName>
                <script>packageToggleButtonPressed = true</script>
                <css></css>
                <commandButtonUp></commandButtonUp>
                <commandButtonDown></commandButtonDown>
                <icon></icon>
                <orientation>0</orientation>
                <location>0</location>
                <posX>0</posX>
                <posY>0</posY>
                <mButtonState>1</mButtonState>
                <sizeX>80</sizeX>
                <sizeY>30</sizeY>
                <buttonColumn>1</buttonColumn>
                <buttonFillerOffset>0</buttonFillerOffset>
                <buttonRotation>0</buttonRotation>
            </Action>
        </ActionGroup>
    </ActionPackage>
    <ScriptPackage>
        <Script isActive="yes" isFolder="no">
            <name>%1 script</name>
            <packageName></packageName>
            <script>packageToggleScriptRuns = (packageToggleScriptRuns or 0) + 1</script>
            <eventHandlerList />
        </Script>
    </ScriptPackage>
    <KeyPackage>
        <Key isActive="yes" isFolder="no">
            <name>%1 key</name>
            <packageName></packageName>
            <script>packageToggleKeyPressed = true</script>
            <command></command>
            <keyCode>16777268</keyCode>
            <keyModifier>0</keyModifier>
        </Key>
    </KeyPackage>
    <VariablePackage>
        <HiddenVariables />
    </VariablePackage>
</MudletPackage>
)")
                .arg(mPackageName);
    }

    QString writePackageFile()
    {
        const QString path = qsl("%1/%2.xml").arg(mpWorkDir->path(), mPackageName);
        QFile file(path);
        if (!file.open(QFile::WriteOnly | QFile::Text)) {
            return QString();
        }
        file.write(packageXml().toUtf8());
        file.close();
        return path;
    }

    template <typename T>
    static QList<T*> packageRoots(const std::list<T*>& rootNodes, const QString& packageName)
    {
        QList<T*> roots;
        for (auto* node : rootNodes) {
            if (node->mPackageName == packageName) {
                roots << node;
            }
        }
        return roots;
    }

    // How many of the package's roots, across all six units, are switched on.
    // shouldBeActive() rather than isActive(): that is the recorded state the
    // XML carries, and the one a timer's family walk leaves behind.
    static int rootsOn(Host* pHost, const QString& packageName)
    {
        int on = 0;
        auto count = [&on](const auto& roots) {
            for (auto* root : roots) {
                if (root->shouldBeActive()) {
                    ++on;
                }
            }
        };
        count(packageRoots(pHost->getTriggerUnit()->getTriggerRootNodeList(), packageName));
        count(packageRoots(pHost->getTimerUnit()->getTimerRootNodeList(), packageName));
        count(packageRoots(pHost->getAliasUnit()->getAliasRootNodeList(), packageName));
        count(packageRoots(pHost->getActionUnit()->getActionRootNodeList(), packageName));
        count(packageRoots(pHost->getScriptUnit()->getScriptRootNodeList(), packageName));
        count(packageRoots(pHost->getKeyUnit()->getKeyRootNodeList(), packageName));
        return on;
    }

    static int rootCount(Host* pHost, const QString& packageName)
    {
        return packageRoots(pHost->getTriggerUnit()->getTriggerRootNodeList(), packageName).size() + packageRoots(pHost->getTimerUnit()->getTimerRootNodeList(), packageName).size()
               + packageRoots(pHost->getAliasUnit()->getAliasRootNodeList(), packageName).size() + packageRoots(pHost->getActionUnit()->getActionRootNodeList(), packageName).size()
               + packageRoots(pHost->getScriptUnit()->getScriptRootNodeList(), packageName).size() + packageRoots(pHost->getKeyUnit()->getKeyRootNodeList(), packageName).size();
    }

    Host* startProfile()
    {
        auto host = TestProfile::create(mHostName, mLocalhost, mPort);
        if (!host) {
            return nullptr;
        }
        QSignalSpy connected(&(host->mTelnet), &cTelnet::signal_connected);
        if (!connected.wait(2000)) {
            return nullptr;
        }
        return mudlet::self()->getActiveHost();
    }

    // Saves the profile the production way and reads it back into a bare Host -
    // the state mudlet::loadProfile() imports a profile's XML into at startup.
    Host* saveAndReload(Host* pSource)
    {
        auto [saved, xmlPath, saveError] = pSource->saveProfile(mpWorkDir->path(), qsl("packagetoggle"));
        if (!saved) {
            qWarning() << "saveProfile() failed:" << saveError;
            return nullptr;
        }
        pSource->waitForProfileSave();
        if (!QFileInfo::exists(xmlPath)) {
            qWarning() << "no profile XML at" << xmlPath;
            return nullptr;
        }

        auto& hostManager = mudlet::self()->getHostManager();
        if (!hostManager.addHost(mTargetName, mPort, QString(), QString())) {
            qWarning() << "could not create the target Host";
            return nullptr;
        }
        Host* pTarget = hostManager.getHost(mTargetName);
        if (!pTarget) {
            return nullptr;
        }

        QFile file(xmlPath);
        if (!file.open(QFile::ReadOnly | QFile::Text)) {
            qWarning() << "could not read" << xmlPath << file.errorString();
            return nullptr;
        }
        XMLimport importer(pTarget);
        if (auto [imported, importError] = importer.importPackage(&file); !imported) {
            qWarning() << "import failed:" << importError;
            return nullptr;
        }
        return pTarget;
    }

    void deleteProfileDirectory(const QString& profileName)
    {
        QDir dir(mudlet::getMudletPath(enums::profileHomePath, profileName));
        if (dir.exists()) {
            dir.removeRecursively();
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
    }

    void cleanupTestCase() { mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME") : qputenv("XDG_CONFIG_HOME", mSavedXdg); }

    void init()
    {
        mpWorkDir = std::make_unique<QTemporaryDir>();
        QVERIFY(mpWorkDir->isValid());
        mpServer = new TelnetServerStub(qApp);
        mpServer->start(mLocalhost, 0);
        mPort = QString::number(mpServer->serverPort());
        mudlet::start();
        mudlet::self()->setupConfig();
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>("MudletInstanceCoordinator"));
        mudlet::self()->init();
        mudlet::self()->setStorePasswordsSecurely(false);
        deleteProfileDirectory(mHostName);
        deleteProfileDirectory(mTargetName);
    }

    void cleanup()
    {
        if (auto* self = mudlet::self()) {
            if (auto* host = self->getActiveHost()) {
                QTest::qWait(50);
                host->waitForProfileSave();
            }
        }
        delete mpServer;
        mpServer = nullptr;
        deleteProfileDirectory(mHostName);
        deleteProfileDirectory(mTargetName);
        delete mudlet::self();
        mpWorkDir.reset();
    }

    // The whole round trip: a package switched off is still switched off after
    // the profile has been saved and read back.
    void test_aSwitchedOffPackageComesBackSwitchedOff()
    {
        auto* host = startProfile();
        QVERIFY2(host, "Could not start the profile");

        const QString path = writePackageFile();
        QVERIFY2(!path.isEmpty(), "SETUP: could not write the package file");
        auto [installed, installError] = host->installPackage(path, enums::PackageModuleType::Package);
        QVERIFY2(installed, qPrintable(installError));
        QVERIFY2(host->mInstalledPackages.contains(mPackageName), "SETUP: the package was not registered");
        QCOMPARE(rootCount(host, mPackageName), 6);
        QCOMPARE(rootsOn(host, mPackageName), 6);
        QVERIFY2(host->getTriggerUnit()->findTrigger(qsl("%1 trigger").arg(mPackageName)), "SETUP: the package's trigger is not there under the name the file gives it");
        QVERIFY2(host->getAliasUnit()->findFirstAlias(qsl("%1 alias").arg(mPackageName)), "SETUP: the package's alias is not there under the name the file gives it");
        QVERIFY2(host->packageEnabled(mPackageName), "SETUP: a freshly installed package did not read as enabled");

        auto [switched, why] = host->setPackageEnabled(mPackageName, false);
        QVERIFY2(switched, qPrintable(why));
        QCOMPARE(rootsOn(host, mPackageName), 0);
        QVERIFY2(!host->packageEnabled(mPackageName), "the package still read as enabled after being switched off");

        Host* target = saveAndReload(host);
        QVERIFY2(target, "the profile could not be saved and read back");

        QVERIFY2(target->mInstalledPackages.contains(mPackageName), "the reloaded profile lost the package itself");
        QVERIFY2(target->mDisabledPackages.contains(mPackageName), "the reloaded profile did not remember that the package was switched off");
        QVERIFY2(!target->packageEnabled(mPackageName), "the reloaded profile reported the package as enabled");
        QCOMPARE(rootCount(target, mPackageName), 6);
        QCOMPARE(rootsOn(target, mPackageName), 0);
    }

    // The player is free to delete a package's master folder in one unit, and
    // an install deletes the ones its file had nothing to put in. The items'
    // own flags cannot speak for a folder that is not there, which is why the
    // Host keeps the name as well.
    void test_theFlagSurvivesAMasterFolderThatIsGone()
    {
        auto* host = startProfile();
        QVERIFY2(host, "Could not start the profile");

        const QString path = writePackageFile();
        QVERIFY2(!path.isEmpty(), "SETUP: could not write the package file");
        auto [installed, installError] = host->installPackage(path, enums::PackageModuleType::Package);
        QVERIFY2(installed, qPrintable(installError));

        auto [switched, why] = host->setPackageEnabled(mPackageName, false);
        QVERIFY2(switched, qPrintable(why));

        const auto triggerRoots = packageRoots(host->getTriggerUnit()->getTriggerRootNodeList(), mPackageName);
        QCOMPARE(triggerRoots.size(), 1);
        // ~TTrigger unregisters it, the same way every deferred-delete path
        // takes a node away
        delete triggerRoots.first();
        QCOMPARE(packageRoots(host->getTriggerUnit()->getTriggerRootNodeList(), mPackageName).size(), 0);

        Host* target = saveAndReload(host);
        QVERIFY2(target, "the profile could not be saved and read back");

        QVERIFY2(target->mDisabledPackages.contains(mPackageName), "a package with one master folder gone was not remembered as switched off");
        QVERIFY2(!target->packageEnabled(mPackageName), "the reloaded profile reported the package as enabled");
        QCOMPARE(packageRoots(target->getTriggerUnit()->getTriggerRootNodeList(), mPackageName).size(), 0);
        QCOMPARE(rootCount(target, mPackageName), 5);
        QCOMPARE(rootsOn(target, mPackageName), 0);
    }

    // What applyDisabledPackages() is for: roots that came back switched on -
    // an install puts fresh ones in, always active - are switched off again
    // once the profile has finished loading.
    void test_theReapplySwitchesARootThatCameBackOnOffAgain()
    {
        auto* host = startProfile();
        QVERIFY2(host, "Could not start the profile");

        const QString path = writePackageFile();
        QVERIFY2(!path.isEmpty(), "SETUP: could not write the package file");
        auto [installed, installError] = host->installPackage(path, enums::PackageModuleType::Package);
        QVERIFY2(installed, qPrintable(installError));
        auto [switched, why] = host->setPackageEnabled(mPackageName, false);
        QVERIFY2(switched, qPrintable(why));

        Host* target = saveAndReload(host);
        QVERIFY2(target, "the profile could not be saved and read back");

        const auto aliasRoots = packageRoots(target->getAliasUnit()->getAliasRootNodeList(), mPackageName);
        QCOMPARE(aliasRoots.size(), 1);
        aliasRoots.first()->setIsActive(true);
        QCOMPARE(rootsOn(target, mPackageName), 1);

        target->applyDisabledPackages();
        QCOMPARE(rootsOn(target, mPackageName), 0);
        QVERIFY2(target->mDisabledPackages.contains(mPackageName), "the re-apply dropped the name of a package that is installed");
    }

    // An install that lands on a name the flag is already holding down brings
    // its items in switched on - every one of them is fresh - so the install
    // has to put them straight back down.
    void test_aPackageInstalledWhileTheFlagHoldsItComesUpOff()
    {
        auto* host = startProfile();
        QVERIFY2(host, "Could not start the profile");

        host->mDisabledPackages << mPackageName;

        const QString path = writePackageFile();
        QVERIFY2(!path.isEmpty(), "SETUP: could not write the package file");
        auto [installed, installError] = host->installPackage(path, enums::PackageModuleType::Package);
        QVERIFY2(installed, qPrintable(installError));

        QCOMPARE(rootCount(host, mPackageName), 6);
        QCOMPARE(rootsOn(host, mPackageName), 0);
        QVERIFY2(!host->packageEnabled(mPackageName), "a package installed under a name held down read as enabled");
    }

    // A name the flag holds is dropped when the package goes, so installing it
    // again from scratch gives the user a package that runs.
    void test_uninstallingLetsTheNameGo()
    {
        auto* host = startProfile();
        QVERIFY2(host, "Could not start the profile");

        const QString path = writePackageFile();
        QVERIFY2(!path.isEmpty(), "SETUP: could not write the package file");
        auto [installed, installError] = host->installPackage(path, enums::PackageModuleType::Package);
        QVERIFY2(installed, qPrintable(installError));
        auto [switched, why] = host->setPackageEnabled(mPackageName, false);
        QVERIFY2(switched, qPrintable(why));
        QVERIFY2(host->mDisabledPackages.contains(mPackageName), "SETUP: the package was not recorded as switched off");

        host->waitForProfileSave();
        QVERIFY2(host->uninstallPackage(mPackageName, enums::PackageModuleType::Package), "SETUP: the package could not be uninstalled");
        QVERIFY2(!host->mDisabledPackages.contains(mPackageName), "an uninstalled package left its name behind to hold down whatever takes it next");
    }

    // The two refusals: a name that is not an installed package, and a module.
    void test_onlyAnInstalledPackageCanBeSwitched()
    {
        auto* host = startProfile();
        QVERIFY2(host, "Could not start the profile");

        auto [missing, missingWhy] = host->setPackageEnabled(qsl("no-such-package"), false);
        QVERIFY2(!missing, "a package that is not installed was switched off");
        QVERIFY2(missingWhy.contains(qsl("not installed")), qPrintable(qsl("the refusal did not say why: %1").arg(missingWhy)));

        // Listed as a package too, so that only the module refusal can turn it
        // away - a name that is merely unlisted is refused for being unlisted
        const QString syncedName = qsl("package-toggle-sync");
        host->mInstalledPackages << syncedName;
        host->mInstalledModules.insert(syncedName, QStringList({qsl("/nowhere/package-toggle-sync.xml"), qsl("0")}));
        auto [module, moduleWhy] = host->setPackageEnabled(syncedName, false);
        QVERIFY2(!module, "a module was switched off");
        QVERIFY2(moduleWhy.contains(qsl("is a module")), qPrintable(qsl("the refusal did not say why: %1").arg(moduleWhy)));
        QVERIFY2(!host->mDisabledPackages.contains(syncedName), "a refused module still had its name written down");
        host->mInstalledModules.remove(syncedName);
        host->mInstalledPackages.removeAll(syncedName);
    }
};

#include "PackageToggleRoundTripTest.moc"
MUDLET_GROUPED_TEST_MAIN(PackageToggleRoundTripTest)
