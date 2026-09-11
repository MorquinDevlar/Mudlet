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
 * What the script editor opens at when nothing has been stored for it.
 *
 * It used to be nine tenths of the profile window, which is a size rather than
 * a shape: on a 3008x1692 desktop with the profile window filling it, the
 * editor opened at 2700x1470 and covered everything. What it holds - a form and
 * a page of code - is no more readable for that, so it opens as a companion to
 * the profile window instead, at a fixed size the reader can then drag to
 * whatever they like. A stored size still wins, which is what
 * EditorMinimumSizeTest and EditorPlacementPersistenceTest hold.
 *
 * The profile window is deliberately made larger than that default before the
 * profile is started, so a default that still scaled off it would come out
 * bigger and be caught here.
 *
 * What this cannot catch, on the offscreen platform every ctest run uses: on
 * cocoa the native window must not exist before restoreWindowGeometry() has
 * run, since a resize of a native window that is created but not yet shown is
 * dropped there. The editor's constructor orders its
 * setUnifiedTitleAndToolBarOnMac() call after readSettings() for that reason,
 * and moving it back opens every editor at the .ui file's 636x688 on a Mac
 * while this case stays green.
 *
 * Run with: ctest -R EditorDefaultSizeTest -V
 */

#include <QGuiApplication>
#include <QScreen>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <chrono>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TConsole.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorDefaultSizeTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorDefaultSize-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    // Comfortably larger than the default, so that a default scaled off the
    // profile window would come out larger than the default itself
    static constexpr int scmProfileWindowWidth = 1600;
    static constexpr int scmProfileWindowHeight = 1000;
    // Mirrors scmEditorDefaultWidth and scmEditorDefaultHeight, which are
    // file-local to the editor: written out here so that the size is held to a
    // figure rather than to whatever the editor happens to answer
    static constexpr int scmCompanionWidth = 1300;
    static constexpr int scmCompanionHeight = 690;

    void deleteProfileDirectory(const QString& profileName)
    {
        QDir dir(mudlet::getMudletPath(enums::profileHomePath, profileName));
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }

    static QString describe(const QSize& size) { return qsl("%1x%2").arg(size.width()).arg(size.height()); }

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

        // Nothing has ever been stored for the editor in this configuration, so
        // the two keys are named here only to say what this case is about
        QVERIFY2(!mudlet::getQSettings()->contains(qsl("script_editor_size")), "this configuration already carries a stored editor size, so the default is never reached");
        QVERIFY2(!mudlet::getQSettings()->contains(qsl("script_editor_pos")), "this configuration already carries a stored editor position");

        // The profile window before the profile is started: loading one builds
        // its editor, and the editor reads its geometry as it is built
        mudlet::self()->resize(scmProfileWindowWidth, scmProfileWindowHeight);
        QCoreApplication::processEvents();

        mpHost = TestProfile::create(mProfileName, mLocalhost, mPort);
        QVERIFY2(mpHost != nullptr, "No active host available for the test.");
        QSignalSpy connected(&(mpHost->mTelnet), &cTelnet::signal_connected);
        QVERIFY2(connected.wait(2000), "Could not connect with the host.");

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
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

    // With nothing stored, the editor is the companion size whatever the
    // profile window behind it came to
    void test_theEditorOpensAtItsCompanionSizeRatherThanAtTheProfileWindows()
    {
        const QSize companion(scmCompanionWidth, scmCompanionHeight);
        const QScreen* pScreen = mpEditor->screen() ? mpEditor->screen() : QGuiApplication::primaryScreen();
        const QSize available = pScreen ? pScreen->availableGeometry().size() : QSize();
        const QSize profileWindow = mpHost->mpConsole->window()->size();
        // restoreWindowGeometry() brings whatever it is handed inside the
        // desktop and then up to the layout's floor, so a desktop smaller than
        // the companion size - the 800x800 an offscreen run is given - is held
        // to what those two leave of it rather than skipped
        const QSize expected = companion.boundedTo(available).expandedTo(mpEditor->minimumSizeHint().boundedTo(available));

        qInfo().noquote() << qsl("  the profile window is %1 on a %2 desktop, and the editor opened at %3 against the %4 it defaults to, floored at %5 - so %6 is what it should come to")
                                     .arg(describe(profileWindow), describe(available), describe(mpEditor->size()), describe(companion), describe(mpEditor->minimumSizeHint()), describe(expected));

        QVERIFY2(profileWindow.width() > companion.width() && profileWindow.height() > companion.height(),
                 qPrintable(qsl("the profile window is %1, which is not larger than the %2 the editor opens at - a default scaled off it would come out smaller rather than bigger, so nothing "
                                "here would catch one")
                                    .arg(describe(profileWindow), describe(companion))));
        QCOMPARE(mpEditor->defaultEditorSize(), companion);
        QVERIFY2(mpEditor->size() == expected, qPrintable(qsl("the editor opened at %1 rather than at the %2 it defaults to").arg(describe(mpEditor->size()), describe(expected))));
    }
};

#include "EditorDefaultSizeTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorDefaultSizeTest)
