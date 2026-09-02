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
 * Where the caret is is a fact about the code pane, not about the window, so
 * it is read at the trailing end of that pane's own heading.
 *
 * It used to go out through QStatusBar::showMessage() with no timeout. A
 * message with no timeout never comes down, and QStatusBar::hideOrShow() hides
 * a plain widget for a message only if that widget is already visible - the
 * first caret report arrives before the window is shown, so the item counts
 * were never hidden and the message was painted over them.
 *
 * On the heading the reading also comes and goes with the pane, because the
 * strip carrying it does, rather than being hidden and shown by hand.
 *
 * The label is looked up by object name rather than through the member holding
 * it, so a build without it fails saying the heading has no caret reading on it
 * rather than failing to compile.
 *
 * Run with: ctest -R EditorCaretReadingTest -V
 */

#include <QLabel>
#include <QSplitterHandle>
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
#include "dlgTriggerPatternEdit.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorCaretReadingTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorCaretReading-Test-Profile");
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

    // The handle over the code pane, which is the second of the three the right
    // hand splitter stacks, and the strip it carries
    QSplitterHandle* codeHeadingHandle() const { return mpEditor->splitter_right->handle(1); }
    QWidget* heading() const { return mpEditor->findChild<QWidget*>(qsl("editorCodeHeader")); }
    QLabel* caretReading() const { return mpEditor->findChild<QLabel*>(qsl("editorCodeCaret")); }

    edbee::TextEditorController* controller() const { return mpEditor->mpSourceEditorEdbee->controller(); }

    // Where a piece of the heading sits on the strip
    QRect placeOnTheStrip(const QWidget* pPiece) const { return QRect(pPiece->mapTo(heading(), QPoint(0, 0)), pPiece->size()); }

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
        mpEditor->resize(1200, 800);
        // Everything below reads what is on show and where it is, and a window
        // the compositor has not put up yet has none of it
        if (!QTest::qWaitForWindowExposed(mpEditor, 2000)) {
            QSKIP("the editor window was never put on screen, so nothing on its code pane's heading is showing");
        }
        QVERIFY2(codeHeadingHandle() != nullptr, "The right hand splitter has no handle over the code pane");
        QVERIFY2(heading() != nullptr, "The handle over the code pane carries no heading");
    }

    void init()
    {
        mpEditor->slot_showTriggers();
        mpEditor->addTrigger(false);
        // A pattern, or the item cannot be saved clean whatever its Lua says
        mpEditor->mTriggerPatternEdit.at(0)->singleLineTextEdit_pattern->setPlainText(qsl("EditorCaretReading"));
        mpEditor->mpSourceEditorEdbee->textDocument()->setText(qsl("-- somewhere for the caret to be\n"));
        QTest::qWait(50ms);
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

    // The reading is on the strip, it is the line and column rather than the
    // byte offset the pane used to add for its own debugging, and it does not
    // go out as a status bar message
    void test_theCaretReadingSitsOnTheHeading()
    {
        QVERIFY2(controller() != nullptr, "The code pane has no controller, so it can report no caret position");

        QSignalSpy reports(controller(), &edbee::TextEditorController::updateStatusTextSignal);
        controller()->moveCaretToOffset(3, false);
        QTRY_VERIFY2(!reports.isEmpty(), "the code pane reported no caret position, so there is nothing for the heading to show");

        QLabel* pCaret = caretReading();
        QVERIFY2(pCaret != nullptr, "the code pane's heading has no label named editorCodeCaret on it");
        QTRY_VERIFY2(pCaret->isVisible(), "the caret reading is not on show");

        const QString lastReport = reports.last().first().toString();
        const QString caretText = pCaret->text();
        qInfo().noquote() << qsl("  the pane reported \"%1\" and the heading reads \"%2\"").arg(lastReport, caretText);
        QVERIFY2(!caretText.isEmpty() && lastReport.contains(caretText) && !caretText.contains(qsl(" | ")),
                 qPrintable(qsl("the caret reading is \"%1\", which is not the front of the code pane's report \"%2\"").arg(caretText, lastReport)));
        // Only what the pane says before its own debugging tail, since the name
        // of the command it last ran is in that tail and has "Offset" in it
        const QString reported = lastReport.section(qsl(" | "), 0, 0);
        QVERIFY2(!reported.contains(qsl("Offset")),
                 qPrintable(qsl("the code pane is still reporting a byte offset, which is a debugging reading rather than one a script author uses: \"%1\"").arg(reported)));

        const QRect place = placeOnTheStrip(pCaret);
        QVERIFY2(heading()->rect().contains(place), qPrintable(qsl("the caret reading is at %1, which is not on the %2 strip").arg(describe(place), describe(heading()->rect()))));

        const QString message = mpEditor->statusBar()->currentMessage();
        QVERIFY2(message.isEmpty(), qPrintable(qsl("the caret position went out as a status bar message, which is painted over the counts: \"%1\"").arg(message)));
    }

    // ...and being part of the pane, it comes and goes with it rather than
    // being hidden and shown by hand
    void test_theHeadingGoesWithThePane()
    {
        QSplitterHandle* pHandle = codeHeadingHandle();
        QTRY_VERIFY2(pHandle->isVisible(), "the code pane's heading is not on show while a trigger is being edited");

        mpEditor->clearTriggerForm();
        QTRY_VERIFY2(!pHandle->isVisible(), "the heading stayed behind after the code pane it belongs to was taken away");

        mpEditor->addTrigger(false);
        QTRY_VERIFY2(pHandle->isVisible(), "the heading did not come back with the code pane");
    }
};

#include "EditorCaretReadingTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorCaretReadingTest)
