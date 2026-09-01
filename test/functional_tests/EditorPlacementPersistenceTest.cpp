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
 * The script editor has to open where the user last left it, and stay there
 * when it is toggled away and back. It remembered its size but not its
 * position, and the asymmetry named the culprit: the size was only ever read
 * from settings, while the position was overwritten after every show.
 *
 * The editor is a singleton that is hidden and shown rather than built and
 * destroyed, and the slots that show it re-centred it on the profile's screen
 * each time - mudlet::slot_showEditorDialog() with
 * utils::forceRepositionDialogOnParentScreen(), which centres unconditionally,
 * and mudlet::slot_showTriggerDialog() with utils::positionDialogOnParentScreen(),
 * which centres any dialog that is not currently visible, and the singleton is
 * hidden at that point. Neither touched the size, which is exactly the shape of
 * the bug that was reported.
 *
 * This has to be driven through those slots. A test that constructs the editor
 * itself never runs them, which is how the defect survived the geometry work.
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

class EditorPlacementPersistenceTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorPlacementPersistence-Test-Profile");
    QString mPort; // assigned the stub's actual ephemeral port in initTestCase()
    const QString mLocalhost = qsl("localhost");

    QPoint mSeededPos;  // written to settings before the editor is built
    QPoint mDraggedPos; // where the test moves it to, standing in for a drag
    QSize mSeededSize;

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

        QSignalSpy spy(&(mpHost->mTelnet), &cTelnet::signal_connected);
        if (!spy.wait(1000)) {
            QFAIL("Could not connect with the host.");
        }
    }

    static QString describe(const QPoint& point) { return qsl("(%1,%2)").arg(QString::number(point.x()), QString::number(point.y())); }

    static QString describe(const QSize& size) { return qsl("%1x%2").arg(QString::number(size.width()), QString::number(size.height())); }

    // Every measurement the report asks for goes through here, so a run says
    // where the window was at each step whether it passes or fails
    void report(const char* step) const { qInfo().noquote() << qsl("  %1: pos %2 size %3").arg(QString::fromLatin1(step), describe(mpEditor->pos()), describe(mpEditor->size())); }

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
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>("MudletInstanceCoordinator"));
        mudlet::self()->init();
        mudlet::self()->setStorePasswordsSecurely(false);
        deleteProfileDirectory(mProfileName);

        const QScreen* pScreen = QGuiApplication::primaryScreen();
        QVERIFY(pScreen);
        const QRect availableArea = pScreen->availableGeometry();

        // Both placements sit a little way inside the desktop, so the editor is
        // reachable at either of them and showEvent() has no reason to move it.
        // They are far enough apart, and far enough from the centre this used to
        // snap to, that no rounding can make one look like the other
        mSeededPos = availableArea.topLeft() + QPoint(24, 18);
        mDraggedPos = availableArea.topLeft() + QPoint(103, 71);
        mSeededSize = QSize(640, 460).boundedTo(availableArea.size());
        QVERIFY2(mSeededSize.height() >= 300, "The screen is too small for this test to say anything");

        mudlet::getQSettings()->setValue(qsl("script_editor_pos"), mSeededPos);
        mudlet::getQSettings()->setValue(qsl("script_editor_size"), mSeededSize);

        startProfile(mProfileName, mLocalhost, mPort);
        qInfo().noquote() << qsl("  seeded: pos %1 size %2").arg(describe(mSeededPos), describe(mSeededSize));
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

    // Shown the way the application shows it - through the slot the menu item
    // and the keyboard shortcut are wired to, not by constructing it here
    void test_editorOpensAtTheStoredPosition()
    {
        mudlet::self()->slot_showEditorDialog();
        QTest::qWait(100ms);

        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        report("after first show");

        QVERIFY2(mpEditor->pos() == mSeededPos, qPrintable(qsl("The editor was stored at %1 but opened at %2 - showing it moved it").arg(describe(mSeededPos), describe(mpEditor->pos()))));
    }

    // The move stands in for a drag: moveEvent() cannot tell the two apart, and
    // it is what marks the placement as the user's own
    void test_draggingTheEditorSticks()
    {
        mpEditor->move(mDraggedPos);
        QCoreApplication::processEvents();
        report("after move");

        QVERIFY2(mpEditor->pos() == mDraggedPos, qPrintable(qsl("Asked the editor to move to %1, it went to %2").arg(describe(mDraggedPos), describe(mpEditor->pos()))));
    }

    // The reported symptom: toggle the editor away and back, and it is centred
    // again. The size always survived this, which is why it is asserted too -
    // the two have to behave the same way
    void test_hidingAndShowingKeepsThePlacement()
    {
        const QSize sizeBefore = mpEditor->size();

        mpEditor->hide();
        QCoreApplication::processEvents();
        mudlet::self()->slot_showEditorDialog();
        QTest::qWait(100ms);
        report("after hide and show");

        QVERIFY2(mpEditor->pos() == mDraggedPos, qPrintable(qsl("The editor was left at %1 and came back at %2 - it is moved on every show").arg(describe(mDraggedPos), describe(mpEditor->pos()))));
        QVERIFY2(mpEditor->size() == sizeBefore, qPrintable(qsl("The editor was left at %1 and came back at %2").arg(describe(sizeBefore), describe(mpEditor->size()))));
    }

    // The other slot that shows the singleton reached the same helper by a
    // different route: it repositions any dialog that is not visible, and the
    // editor is hidden when it runs
    void test_theTriggerSlotKeepsThePlacementToo()
    {
        mpEditor->hide();
        QCoreApplication::processEvents();
        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        report("after hide and show via the trigger slot");

        QVERIFY2(mpEditor->pos() == mDraggedPos, qPrintable(qsl("The editor was left at %1 and the trigger slot brought it back at %2").arg(describe(mDraggedPos), describe(mpEditor->pos()))));
    }

    // The round trip across instances: closing writes the placement out, and a
    // brand new editor reads it back
    void test_thePlacementSurvivesAFreshEditor()
    {
        const QSize sizeBefore = mpEditor->size();

        mpEditor->close();
        QCoreApplication::processEvents();

        const QPoint storedPos = mudlet::getQSettings()->value(qsl("script_editor_pos")).toPoint();
        const QSize storedSize = mudlet::getQSettings()->value(qsl("script_editor_size")).toSize();
        qInfo().noquote() << qsl("  in QSettings on close: pos %1 size %2").arg(describe(storedPos), describe(storedSize));
        QVERIFY2(storedPos == mDraggedPos, qPrintable(qsl("The editor was at %1 when it closed but stored %2").arg(describe(mDraggedPos), describe(storedPos))));
        QCOMPARE(storedSize, sizeBefore);

        delete mpEditor;
        mpEditor = nullptr;
        QVERIFY2(mpHost->mpEditorDialog.isNull(), "The editor should have been destroyed");

        mudlet::self()->slot_showEditorDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "A fresh editor should have been created");
        report("after a fresh construction");

        QVERIFY2(mpEditor->pos() == mDraggedPos, qPrintable(qsl("A fresh editor should have opened at the stored %1, it opened at %2").arg(describe(mDraggedPos), describe(mpEditor->pos()))));
        QCOMPARE(mpEditor->size(), sizeBefore);
    }
};

#include "EditorPlacementPersistenceTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorPlacementPersistenceTest)
