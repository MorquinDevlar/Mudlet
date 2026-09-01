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
 * Loading a profile builds a dlgTriggerEditor for it whether or not anybody
 * asks to see one - mudlet::slot_connectionDialogueFinished() does it as part
 * of opening the tab. The editor's placement, though, is one pair of keys for
 * the whole application: script_editor_pos and script_editor_size, in the
 * global QSettings rather than the profile.
 *
 * writeSettings() wrote both on close whatever the window had been doing, and
 * closeEvent() reaches every editor on quit - so a second profile's editor,
 * built and never shown, stored the geometry that restoreWindowGeometry() and
 * the layout happened to leave it at and overwrote the placement the user had
 * actually chosen in the first profile's editor. With two profiles open, the
 * placement was lost every quit. Upstream PR #10317 reports the same defect.
 *
 * The seeded size is deliberately smaller than the editor's own minimum, so an
 * editor that has never been shown holds a size nobody asked for: what it
 * stores without the guard is not the seeded figure but the minimum its layout
 * floors it to, which is what makes this test fail loudly rather than by a few
 * pixels.
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
#include "TelnetServerStub.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorGeometryWriteGuardTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorGeometryWriteGuard-Test-Profile");
    QString mPort; // assigned the stub's actual ephemeral port in initTestCase()
    const QString mLocalhost = qsl("localhost");

    // Standing in for the placement a user chose in another profile's editor
    QPoint mSeededPos;
    QSize mSeededSize;
    // ...and for the drag that would replace it
    QPoint mDraggedPos;

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

    static QString describe(const QPoint& point) { return qsl("(%1,%2)").arg(QString::number(point.x()), QString::number(point.y())); }

    static QString describe(const QSize& size) { return qsl("%1x%2").arg(QString::number(size.width()), QString::number(size.height())); }

    static QPoint storedPos() { return mudlet::getQSettings()->value(qsl("script_editor_pos")).toPoint(); }

    static QSize storedSize() { return mudlet::getQSettings()->value(qsl("script_editor_size")).toSize(); }

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

        const QScreen* pScreen = QGuiApplication::primaryScreen();
        QVERIFY(pScreen);
        const QRect availableArea = pScreen->availableGeometry();
        mSeededPos = availableArea.topLeft() + QPoint(37, 29);
        mDraggedPos = availableArea.topLeft() + QPoint(116, 83);
        mSeededSize = QSize(640, 460).boundedTo(availableArea.size());
        QVERIFY2(mSeededSize.height() >= 300, "The screen is too small for this test to say anything");

        mudlet::getQSettings()->setValue(qsl("script_editor_pos"), mSeededPos);
        mudlet::getQSettings()->setValue(qsl("script_editor_size"), mSeededSize);
        qInfo().noquote() << qsl("  seeded: pos %1 size %2").arg(describe(mSeededPos), describe(mSeededSize));

        // Building the profile is what builds the editor, and nothing here ever
        // asks to see it - which is the whole of the case below
        startProfile(mProfileName, mLocalhost, mPort);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Loading the profile should have built an editor");
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

    // The second profile's editor, in miniature: built by the load, never put
    // on screen, and closed on quit
    void test_anEditorNobodyOpenedLeavesThePlacementAlone()
    {
        QVERIFY2(!mpEditor->isVisible(), "The editor was shown, so this is no longer the case being covered");
        qInfo().noquote() << qsl("  the unshown editor holds: pos %1 size %2").arg(describe(mpEditor->pos()), describe(mpEditor->size()));

        mpEditor->close();
        QCoreApplication::processEvents();
        qInfo().noquote() << qsl("  in QSettings after its close: pos %1 size %2").arg(describe(storedPos()), describe(storedSize()));

        QVERIFY2(storedSize() == mSeededSize, qPrintable(qsl("An editor nobody opened overwrote the stored size %1 with %2").arg(describe(mSeededSize), describe(storedSize()))));
        QVERIFY2(storedPos() == mSeededPos, qPrintable(qsl("An editor nobody opened overwrote the stored position %1 with %2").arg(describe(mSeededPos), describe(storedPos()))));
    }

    // ...and the other half, which the guard must not have taken with it: an
    // editor the user has actually worked in still stores where they left it
    void test_anEditorTheUserOpenedStoresWhereItWasLeft()
    {
        mudlet::self()->slot_showEditorDialog();
        QTest::qWait(100ms);
        QVERIFY2(mpHost->mpEditorDialog == mpEditor, "Showing the editor replaced it, so the two halves are not about the same window");

        mpEditor->move(mDraggedPos);
        QCoreApplication::processEvents();
        const QSize sizeBefore = mpEditor->size();
        qInfo().noquote() << qsl("  the shown editor was left at: pos %1 size %2").arg(describe(mpEditor->pos()), describe(sizeBefore));

        mpEditor->close();
        QCoreApplication::processEvents();
        qInfo().noquote() << qsl("  in QSettings after its close: pos %1 size %2").arg(describe(storedPos()), describe(storedSize()));

        QVERIFY2(storedPos() == mDraggedPos, qPrintable(qsl("The editor was left at %1 and stored %2").arg(describe(mDraggedPos), describe(storedPos()))));
        QCOMPARE(storedSize(), sizeBefore);
    }
};

#include "EditorGeometryWriteGuardTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorGeometryWriteGuardTest)
