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
 * The editor's status bar carries how many items the view holds and when the
 * profile last saved itself. Where the caret is in the code pane is not on it:
 * that is a fact about one pane, and it is read at the end of that pane's own
 * heading - see EditorCodeHeadingTest.
 *
 * It used to go out through showMessage(), the channel a status bar keeps for
 * something it will take away again. A message on that channel hides the bar's
 * plain widgets for as long as it stands and is painted in the space they
 * leave. The caret report was sent with no timeout, so it stood for the life of
 * the window and the counts could never be read again once the code pane had
 * said anything. Worse, the first report arrives before the window is on
 * screen, and QStatusBar::hideOrShow() passes over a widget that is not visible
 * yet - so the counts label was never hidden at all, and the message was
 * painted over the top of it. That is what the reader saw: "Line 1, Column 1,
 * Offset 0" laid over "14 triggers - 12 active" in the bottom left corner.
 *
 * What is left on the bar is the counts on the message side and the last save
 * at the trailing edge, and the message channel is left to the editor's
 * genuinely temporary messages - the ones that borrow the counts label's place
 * for the two or three seconds they are shown and then give it back.
 *
 * Run with: ctest -R EditorStatusBarTest -V
 */

#include <QLabel>
#include <QStatusBar>
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

class EditorStatusBarTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorStatusBar-Test-Profile");
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

    QStatusBar* statusBar() const { return mpEditor->statusBar(); }

    // Where a label sits in the bar, so that two of them can be asked whether
    // they are in the same place
    static QRect placeInTheBar(const QWidget* pLabel, const QWidget* pBar) { return QRect(pLabel->mapTo(pBar, QPoint(0, 0)), pLabel->size()); }

    static QString describe(const QRect& rect) { return qsl("%1,%2 %3x%4").arg(QString::number(rect.x()), QString::number(rect.y()), QString::number(rect.width()), QString::number(rect.height())); }

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
        mpEditor->resize(1100, 800);
        // Everything below reads which of the bar's widgets are on show, and a
        // window the compositor has not put up yet has none of them showing -
        // so the cases say so rather than reading nothing and failing
        if (!QTest::qWaitForWindowExposed(mpEditor, 2000)) {
            QSKIP("the editor window was never put on screen, so nothing in its status bar is showing");
        }
        QVERIFY2(statusBar() != nullptr, "The editor window has no status bar");
    }

    void init()
    {
        // A trigger to look at, so the counts have something to count and the
        // code pane holds a document the caret can be moved about in
        mpEditor->slot_showTriggers();
        mpEditor->addTrigger(false);
        mpEditor->mpSourceEditorEdbee->textDocument()->setText(qsl("-- somewhere for the caret to be\n"));
        QTest::qWait(50ms);
    }

    void cleanup()
    {
        if (mpEditor) {
            statusBar()->clearMessage();
        }
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

    // Nothing the code pane says goes out as a message, and nothing on the bar
    // reports the caret: both belong to the code pane's own heading now.
    void test_theCaretReadingIsNotAMessageOnTheBar()
    {
        edbee::TextEditorController* pController = mpEditor->mpSourceEditorEdbee->controller();
        QVERIFY2(pController != nullptr, "The code pane has no controller, so it can report no caret position");

        QSignalSpy reports(pController, &edbee::TextEditorController::updateStatusTextSignal);
        pController->moveCaretToOffset(3, false);
        QTRY_VERIFY2(!reports.isEmpty(), "the code pane reported no caret position, so there is nothing to keep off the bar");

        const QString message = statusBar()->currentMessage();
        QVERIFY2(message.isEmpty(), qPrintable(qsl("the caret position went out as a temporary message, which is painted over the counts: \"%1\"").arg(message)));

        QLabel* pCounts = mpEditor->mpLabel_statusCounts;
        QVERIFY2(pCounts != nullptr, "The status bar has no label for the item counts");
        QTRY_VERIFY2(pCounts->isVisible(), "the counts label is not on show");
        // Filled behind a 200ms wait, so that a fillout is one walk of the tree
        QTRY_VERIFY2(!pCounts->text().isEmpty(), "the counts label never filled");
        qInfo().noquote() << qsl("  the counts read \"%1\" at %2, with the bar carrying no message").arg(pCounts->text(), describe(placeInTheBar(pCounts, statusBar())));
    }

    // ...and what the message channel is left for: the editor's own messages,
    // which take the counts label's place for the seconds they are shown and
    // then hand it back. The counts label is laid out on the message side
    // precisely so that this happens, so it is worth having it written down.
    void test_timedMessageBorrowsTheCountsThenReturnsThem()
    {
        QLabel* pCounts = mpEditor->mpLabel_statusCounts;
        QLabel* pAutosave = mpEditor->mpLabel_statusAutosave;
        QVERIFY2(pCounts != nullptr, "The status bar has no label for the item counts");
        QVERIFY2(pAutosave != nullptr, "the status bar has no label for the last save");
        QTRY_VERIFY2(pCounts->isVisible(), "the counts label is not on show, so there is nothing for a message to borrow");

        statusBar()->showMessage(qsl("x"), 150);
        QTRY_VERIFY2(!pCounts->isVisible(), "a timed message did not take the counts label's place");
        QVERIFY2(pAutosave->isVisible(), "a timed message took the last save away as well, so it is not a permanent widget");

        QTRY_VERIFY2(pCounts->isVisible(), "the counts label never came back once the message had timed out");
    }
};

#include "EditorStatusBarTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorStatusBarTest)
