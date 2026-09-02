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
 * Tab with the caret at the very start of the code pane took the client down.
 *
 * edbee's TabCommand walks back from the caret to decide whether everything
 * before it on the line is whitespace. The offsets it walks are size_t, and it
 * stepped back one before checking there was anywhere to step back to - so a
 * caret at offset zero wrapped to npos, which reads as "far past the start of
 * the line" and sent charAt() an index no document has. A debug build stops on
 * the assertion inside charAt(); a release build has no assertion to stop on
 * and reads out of bounds instead.
 *
 * An empty document is the case a user meets: a new variable, alias or script
 * opens with nothing typed and the caret at the only offset there is.
 *
 * Run with: ctest -R EditorTabAtStartTest -V
 */

#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <chrono>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorTabAtStartTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorTabAtStart-Test-Profile");
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
        if (!spy.wait(2000)) {
            QFAIL("Could not connect with the host.");
        }
    }

    edbee::TextEditorController* controller() const { return mpEditor->mpSourceEditorEdbee->controller(); }

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
        if (QTest::currentTestFailed()) {
            return;
        }

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->slot_showTriggers();
        mpEditor->addTrigger(false);
        QTest::qWait(50ms);
        QVERIFY2(controller() != nullptr, "the code pane has no controller to type into");
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

    // The offset before the first one does not exist, and the walk back from
    // the caret has to notice that rather than wrap round to npos
    void test_tabAtTheStartOfAnEmptyDocumentDoesNotTakeTheClientDown()
    {
        controller()->textDocument()->setText(QString());
        controller()->moveCaretToOffset(0, false);
        QCOMPARE(controller()->textSelection()->range(0).caret(), static_cast<size_t>(0));

        // Through the command edbee binds Tab to, which is what the key press
        // reaches - the crash was inside it rather than in the key handling
        controller()->executeCommand(qsl("tab"));

        qInfo().noquote() << qsl("  after Tab the document holds %1 characters").arg(QString::number(controller()->textDocument()->length()));
        QVERIFY2(controller()->textDocument()->length() > 0, "Tab at the start of an empty document inserted nothing at all");
    }

    // ...and the same at the start of a document that has text in it, which is
    // the other way to be standing on offset zero
    void test_tabAtTheStartOfALineOfTextDoesNotTakeTheClientDown()
    {
        controller()->textDocument()->setText(qsl("local a = 1\n"));
        controller()->moveCaretToOffset(0, false);
        QCOMPARE(controller()->textSelection()->range(0).caret(), static_cast<size_t>(0));

        controller()->executeCommand(qsl("tab"));

        const QString after = controller()->textDocument()->text();
        qInfo().noquote() << qsl("  the first line is now \"%1\"").arg(after.section(QChar::LineFeed, 0, 0));
        QVERIFY2(after.contains(qsl("local a = 1")), "Tab at the start of the line lost the line it was on");
    }
};

#include "EditorTabAtStartTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorTabAtStartTest)
