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
 * The editor's three columns start on one line.
 *
 * The top edge of the sidebar's first row, of the search field heading the panel
 * of items, and of whatever leads the column an item is edited in - the notice
 * when one is showing, the name row when it is not - are the same y. The
 * measured drift before this was guarded: the search field sat at 0 while the
 * sidebar's first row sat at 12, and the notice at 21, because each column
 * carried its own top margin and one of them was the style's default.
 *
 * All three now come off scmEditorColumnTopInset, so the cases below compare the
 * columns against each other rather than against that number: a column that
 * stops using it is what this is here to catch.
 *
 * Run with: ctest -R EditorColumnAlignmentTest -V
 */

#include <QComboBox>
#include <QFrame>
#include <QListWidget>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <chrono>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "dlgSystemMessageArea.h"
#include "dlgTriggerEditor.h"
#include "dlgTriggersMainArea.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorColumnAlignmentTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorColumnAlignment-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

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

    // In the window's own coordinates, which is the one frame all three columns
    // can be compared in - each of them is inset from a different parent
    int topEdgeOf(QWidget* pWidget) const { return pWidget->mapTo(mpEditor, QPoint(0, 0)).y(); }

    // The pill the first row is drawn as, which is what the reader sees the
    // column start at - not the list widget carrying it
    int sidebarFirstRowTop() const
    {
        QListWidget* pSidebar = mpEditor->mpListWidget_editorSidebar;
        if (!pSidebar || pSidebar->count() < 1) {
            return -1;
        }
        return pSidebar->viewport()->mapTo(mpEditor, pSidebar->visualItemRect(pSidebar->item(0)).topLeft()).y();
    }

    int searchFieldTop() const { return topEdgeOf(mpEditor->comboBox_searchTerms); }

    int noticeTop() const { return topEdgeOf(mpEditor->mpSystemMessageArea->frame_notificationArea); }

    // The row an item's name is typed on, which leads the column when there is
    // no notice over it
    int nameRowTop() const { return topEdgeOf(mpEditor->mpTriggersMainArea->widget_top); }

    static QString drift(const QString& what, const int measured, const int sidebar) { return qsl("%1 starts at %2 and the sidebar's first row at %3").arg(what).arg(measured).arg(sidebar); }

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
        mpEditor->resize(1000, 800);

        // A trigger to edit, so the column an item is edited in is the one a
        // user looks at rather than an empty form
        mpEditor->slot_showTriggers();
        mpEditor->addTrigger(false);
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

    // The two columns that are always there, whatever the third one is showing
    void test_theSearchFieldStartsWhereTheSidebarDoes()
    {
        mpEditor->hideSystemMessageArea();
        QTest::qWait(50ms);

        const int sidebar = sidebarFirstRowTop();
        QVERIFY2(sidebar > 0, "the sidebar has no first row to measure");
        qInfo().noquote() << qsl("sidebar %1, search field %2, name row %3").arg(sidebar).arg(searchFieldTop()).arg(nameRowTop());

        QVERIFY2(searchFieldTop() == sidebar, qPrintable(drift(qsl("the search field"), searchFieldTop(), sidebar)));
    }

    // ...and the column an item is edited in, with nothing over the form
    void test_theNameRowStartsWhereTheSidebarDoesWithNoNotice()
    {
        mpEditor->hideSystemMessageArea();
        QTest::qWait(50ms);
        QVERIFY2(!mpEditor->mpSystemMessageArea->isVisible(), "the notice is still showing, so the name row is not what leads the column");

        const int sidebar = sidebarFirstRowTop();
        QVERIFY2(nameRowTop() == sidebar, qPrintable(drift(qsl("the name row"), nameRowTop(), sidebar)));
    }

    // ...and with one, since the notice is then what leads it
    void test_theNoticeStartsWhereTheSidebarDoes()
    {
        mpEditor->showInfo(qsl("A notice, so that the column an item is edited in is led by one"));
        QTest::qWait(50ms);
        QVERIFY2(mpEditor->mpSystemMessageArea->isVisible(), "the notice did not come up, so there is nothing to measure");

        const int sidebar = sidebarFirstRowTop();
        qInfo().noquote() << qsl("sidebar %1, search field %2, notice %3").arg(sidebar).arg(searchFieldTop()).arg(noticeTop());

        QVERIFY2(noticeTop() == sidebar, qPrintable(drift(qsl("the notice"), noticeTop(), sidebar)));
        QVERIFY2(searchFieldTop() == sidebar, qPrintable(drift(qsl("the search field"), searchFieldTop(), sidebar)));

        mpEditor->hideSystemMessageArea();
        QTest::qWait(50ms);
    }
};

#include "EditorColumnAlignmentTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorColumnAlignmentTest)
