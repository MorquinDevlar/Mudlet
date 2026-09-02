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
 * The heading over the editor's code pane owns everything that is said about
 * that pane: what language is typed there, where the caret is, and what the
 * last save of it made of the Lua.
 *
 * The caret reading used to be on the window's status bar, which is the wrong
 * end of the window to read it from - it is a fact about one pane, not about
 * the editor - and it had to be hidden and shown by hand whenever the pane came
 * and went. On the heading it comes and goes with the pane because it is part
 * of it.
 *
 * A save that compiled says nothing at all: the heading is the glyph, the
 * title and the caret reading. A save that did not shows a dot and one line
 * naming the line the compiler stopped on, which is a link - clicking it, or
 * reaching it with Tab and pressing Return, puts the caret there. The strip is
 * the splitter's handle and is transparent to the mouse so a drag anywhere on
 * it still resizes, so the click is heard by the handle rather than by the
 * words - and a drag that ended where it started is not one of them.
 *
 * The new pieces are looked up by object name rather than through the members
 * that hold them, so a build without them fails saying the heading has no
 * caret reading or no compile note on it rather than failing to compile.
 *
 * Run with: ctest -R EditorCodeHeadingTest -V
 */

#include <QLabel>
#include <QMouseEvent>
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

class EditorCodeHeadingTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorCodeHeading-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    // Lua reports the line a syntax error is on counting from one, and the item
    // the editor is holding becomes line one of the chunk it is compiled in -
    // so what the note names is the line of the document the reader is looking
    // at. edbee counts its lines from zero, hence the two numbers below.
    static constexpr int scmBrokenLine = 2;
    static constexpr int scmBrokenLineInEdbee = scmBrokenLine - 1;
    // What the caret reading is allowed to sit in from the trailing edge of the
    // strip: the heading's own inset, and a pixel or two of rounding
    static constexpr int scmTrailingEdgeSlack = 8;
    // A compiler's sentence is as long as it likes: an unfinished string is
    // reported with the whole of what the lexer scanned in it, so this is also
    // how long the line the caret is moved along below is
    static constexpr int scmLongStringLength = 300;
    // Which line that string is on, counting from zero as edbee does
    static constexpr int scmLongLineInEdbee = 1;
    // Far enough for a press to be a drag of the panes rather than a click,
    // whatever the platform's threshold is
    static constexpr int scmDragTravel = 60;
    // ...and how far each report of a pointer being dragged is from the one
    // before it: a real drag is a stream of small moves, each of them short of
    // the threshold on its own
    static constexpr int scmDragStep = 3;

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
    QWidget* compileNote() const { return mpEditor->findChild<QWidget*>(qsl("editorCompileNote")); }
    QLabel* compileMessage() const { return mpEditor->findChild<QLabel*>(qsl("editorCompileMessage")); }

    edbee::TextEditorController* controller() const { return mpEditor->mpSourceEditorEdbee->controller(); }

    // Which line of the document the caret is on, counting from zero as edbee
    // does
    int caretLine() const
    {
        const size_t caret = controller()->textSelection()->range(0).caret();
        return static_cast<int>(controller()->textDocument()->lineFromOffset(caret));
    }

    // Where in the document the caret is, which says which column of a line it
    // ended up in as well as which line
    int caretOffset() const { return static_cast<int>(controller()->textSelection()->range(0).caret()); }

    // An item whose compiler's sentence is longer than any strip has room for,
    // on a line long enough that the caret reading takes several more
    // characters to name its end than it does the start of the item
    static QString longBrokenScript() { return qsl("local a = 1\nlocal b = \"%1\n").arg(QString(scmLongStringLength, QLatin1Char('a'))); }

    // A move with the button held, which QTest::mouseMove does not send: it
    // reports no button down, and a splitter only follows a pointer that has
    // one. The global point is what leads, since the handle moves out from
    // under the pointer as the panes resize.
    static void dragTo(QWidget* pHandle, const QPointF& global)
    {
        QMouseEvent move(QEvent::MouseMove, pHandle->mapFromGlobal(global), global, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(pHandle, &move);
    }

    // Where a piece of the heading sits on the strip, so that two of them can be
    // asked whether they are in the same place
    QRect placeOnTheStrip(const QWidget* pPiece) const { return QRect(pPiece->mapTo(heading(), QPoint(0, 0)), pPiece->size()); }

    static QString describe(const QRect& rect) { return qsl("%1,%2 %3x%4").arg(QString::number(rect.x()), QString::number(rect.y()), QString::number(rect.width()), QString::number(rect.height())); }

    static QString describeSizes(const QList<int>& sizes)
    {
        QStringList words;
        for (const int size : sizes) {
            words << QString::number(size);
        }
        return words.join(QLatin1Char('/'));
    }

    void typeAndSave(const QString& script)
    {
        mpEditor->mpSourceEditorEdbee->textDocument()->setText(script);
        mpEditor->slot_saveEdits();
        QTest::qWait(50ms);
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
        // A pattern, or the item cannot be saved clean whatever its Lua says: a
        // trigger with none reports that through the same channel a failed
        // compile does
        mpEditor->mTriggerPatternEdit.at(0)->singleLineTextEdit_pattern->setPlainText(qsl("EditorCodeHeading"));
        mpEditor->mpSourceEditorEdbee->textDocument()->setText(qsl("-- somewhere for the caret to be\n"));
        QTest::qWait(50ms);
    }

    void cleanup()
    {
        if (mpEditor) {
            mpEditor->resize(1200, 800);
            QTest::qWait(20ms);
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

    // Where the caret is is a fact about the code pane, so it is read at the
    // trailing end of that pane's own heading rather than in the corner of the
    // window - and it is the line and column, not the byte offset the pane
    // used to add for its own debugging.
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

    // A save that compiled is said by there being nothing to say: the heading
    // is the glyph, the title and the caret reading, with no note on it
    void test_aCleanSaveShowsNoNote()
    {
        typeAndSave(qsl("-- nothing wrong here\n"));

        QWidget* pNote = compileNote();
        QVERIFY2(pNote != nullptr, "the code pane's heading has no widget named editorCompileNote on it");
        QTRY_VERIFY2(!pNote->isVisible(), qPrintable(qsl("a save that compiled left a note on the heading reading \"%1\"").arg(compileMessage() ? compileMessage()->text() : QString())));
        QVERIFY2(codeHeadingHandle()->toolTip().isEmpty(), qPrintable(qsl("a save that compiled left a tooltip on the heading: \"%1\"").arg(codeHeadingHandle()->toolTip())));

        // ...and the room the note had is held open rather than handed to
        // whatever else on the strip could grow into it
        QLabel* pCaret = caretReading();
        QVERIFY2(pCaret != nullptr, "the code pane's heading has no label named editorCodeCaret on it");
        const QRect place = placeOnTheStrip(pCaret);
        QVERIFY2(place.right() >= heading()->width() - scmTrailingEdgeSlack,
                 qPrintable(qsl("with no note on it the caret reading is at %1 on a %2px strip, which is not its trailing edge").arg(describe(place), QString::number(heading()->width()))));
    }

    // ...and one that did not names the line it stopped on. The item's own name
    // is what the reader is already looking at, so the chunk name the compiler
    // wraps round it says nothing and is left to the tooltip.
    void test_aFailedSaveNamesTheLine()
    {
        typeAndSave(qsl("local a = 1\nlocal b = 1 +* 2\n"));

        QWidget* pNote = compileNote();
        QVERIFY2(pNote != nullptr, "the code pane's heading has no widget named editorCompileNote on it");
        QTRY_VERIFY2(pNote->isVisible(), "a save that did not compile left no note on the heading");

        QLabel* pMessage = compileMessage();
        QVERIFY2(pMessage != nullptr, "the compile note has no label named editorCompileMessage in it");
        const QString said = pMessage->text();
        const QString tip = codeHeadingHandle()->toolTip();
        qInfo().noquote() << qsl("  the note reads \"%1\" and the tooltip \"%2\"").arg(said, tip);

        QVERIFY2(said.contains(qsl("%1:").arg(QString::number(scmBrokenLine))), qPrintable(qsl("the note does not name the line the compiler stopped on: \"%1\"").arg(said)));
        QVERIFY2(said.contains(qsl("unexpected symbol")), qPrintable(qsl("the note does not carry what the compiler said: \"%1\"").arg(said)));
        QVERIFY2(!said.contains(qsl("[string")) && !said.contains(qsl("Lua syntax error")), qPrintable(qsl("the note repeats the compiler's own preamble: \"%1\"").arg(said)));
        QVERIFY2(tip.contains(qsl("[string")), qPrintable(qsl("the whole of what the compiler said is not on the heading's tooltip: \"%1\"").arg(tip)));
    }

    // The line the note names is a link to that line, and the click is heard by
    // the handle rather than by the words - which must not leave the panes
    // resized on the way
    void test_clickingTheNoteMovesTheCaretToTheLine()
    {
        typeAndSave(qsl("local a = 1\nlocal b = 1 +* 2\n"));

        QWidget* pNote = compileNote();
        QVERIFY2(pNote != nullptr, "the code pane's heading has no widget named editorCompileNote on it");
        QTRY_VERIFY2(pNote->isVisible(), "a save that did not compile left no note to click on");

        controller()->moveCaretToOffset(0, false);
        QCOMPARE(caretLine(), 0);

        QSplitterHandle* pHandle = codeHeadingHandle();
        const QList<int> before = mpEditor->splitter_right->sizes();
        QTest::mouseClick(pHandle, Qt::LeftButton, Qt::NoModifier, pNote->mapTo(pHandle, pNote->rect().center()));

        QTRY_COMPARE(caretLine(), scmBrokenLineInEdbee);
        QCOMPARE(mpEditor->splitter_right->sizes(), before);
    }

    // What the compiler said is as long as it likes, and the caret reading is
    // short and always true - so the note is what gives way when the strip runs
    // out of room, never the reading beside it
    // A heading over a form that cannot use the room resizes nothing, and loses
    // its band and its drag with it - but the note on it is a link either way,
    // so the handle still has to hear a click on it
    void test_theNoteIsStillALinkOnAHeadingThatDragsNothing()
    {
        typeAndSave(qsl("local a = 1\nlocal b = 1 +* 2\n"));

        QWidget* pNote = compileNote();
        QVERIFY2(pNote != nullptr, "the code pane's heading has no widget named editorCompileNote on it");
        QTRY_VERIFY2(pNote->isVisible(), "a save that did not compile left no note to click on");

        controller()->moveCaretToOffset(0, false);
        QCOMPARE(caretLine(), 0);

        QSplitterHandle* pHandle = codeHeadingHandle();
        QVERIFY2(mpEditor->splitter_right->setHandleResizes(1, false), "the code pane's heading is not a handle this can be asked of");
        const QList<int> before = mpEditor->splitter_right->sizes();
        QTest::mouseClick(pHandle, Qt::LeftButton, Qt::NoModifier, pNote->mapTo(pHandle, pNote->rect().center()));

        QTRY_COMPARE(caretLine(), scmBrokenLineInEdbee);
        QCOMPARE(mpEditor->splitter_right->sizes(), before);
        QVERIFY(mpEditor->splitter_right->setHandleResizes(1, true));
    }

    void test_theNoteYieldsToTheCaretReading()
    {
        typeAndSave(qsl("local a = 1\nlocal b = 1 +* 2\n"));

        QLabel* pCaret = caretReading();
        QLabel* pMessage = compileMessage();
        QVERIFY2(pCaret != nullptr, "the code pane's heading has no label named editorCodeCaret on it");
        QVERIFY2(pMessage != nullptr, "the compile note has no label named editorCompileMessage in it");

        mpEditor->resize(mpEditor->minimumSizeHint());
        QCoreApplication::processEvents();
        QTest::qWait(50ms);

        QTRY_VERIFY2(pCaret->width() >= pCaret->sizeHint().width(), "the strip never made room for the caret reading's words");

        const QString said = pMessage->text();
        const bool elided = said.endsWith(QChar(0x2026));
        const int wanted = pMessage->fontMetrics().horizontalAdvance(said);
        qInfo().noquote() << qsl("  on a %1px strip the note reads \"%2\" (%3px in a %4px label) and the caret reading \"%5\" (%6px)")
                                     .arg(QString::number(heading()->width()), said, QString::number(wanted), QString::number(pMessage->width()), pCaret->text(), QString::number(pCaret->width()));
        QVERIFY2(elided || wanted <= pMessage->width(),
                 qPrintable(qsl("the note reads \"%1\", which wants %2px of a %3px label without being cut to fit").arg(said, QString::number(wanted), QString::number(pMessage->width()))));

        const QRect caret = placeOnTheStrip(pCaret);
        const QRect note = placeOnTheStrip(pMessage);
        QVERIFY2(!caret.intersects(note), qPrintable(qsl("the note is at %1 and the caret reading at %2, which is the same place").arg(describe(note), describe(caret))));
    }

    // ...and it yields to it as the reading changes, not only as the strip
    // does. A caret that moves somewhere it takes more characters to name
    // widens the reading and narrows the note beside it without the strip
    // itself being resized at all, so the words have to be cut again to what
    // the note is left with or their tail is clipped off mid-letter.
    void test_theNoteIsCutAgainWhenTheCaretReadingGrows()
    {
        typeAndSave(longBrokenScript());

        QLabel* pMessage = compileMessage();
        QLabel* pCaret = caretReading();
        QVERIFY2(pMessage != nullptr, "the compile note has no label named editorCompileMessage in it");
        QVERIFY2(pCaret != nullptr, "the code pane's heading has no label named editorCodeCaret on it");
        QTRY_VERIFY2(compileNote() != nullptr && compileNote()->isVisible(), "a save that did not compile left no note on the heading");

        controller()->moveCaretToOffset(0, false);
        QTRY_VERIFY2(pMessage->text().endsWith(QChar(0x2026)),
                     qPrintable(qsl("the note reads \"%1\" in a %2px label, so it was never cut - there is nothing here to cut again").arg(pMessage->text(), QString::number(pMessage->width()))));
        const QString wide = pMessage->text();
        const int roomWhenWide = pMessage->width();
        const QString shortReading = pCaret->text();

        // The far end of that long line, which takes several more characters to
        // name than the first column of the first line
        controller()->moveCaretTo(static_cast<size_t>(scmLongLineInEdbee), static_cast<size_t>(scmLongStringLength), false);
        QTRY_VERIFY2(pMessage->width() < roomWhenWide,
                     qPrintable(qsl("the caret reading went from \"%1\" to \"%2\" without taking any of the note's %3px").arg(shortReading, pCaret->text(), QString::number(roomWhenWide))));

        const QString narrow = pMessage->text();
        const int wanted = pMessage->fontMetrics().horizontalAdvance(narrow);
        qInfo().noquote() << qsl("  the reading went from \"%1\" to \"%2\", the note's room from %3px to %4px, and the note now wants %5px of it")
                                     .arg(shortReading, pCaret->text(), QString::number(roomWhenWide), QString::number(pMessage->width()), QString::number(wanted));
        QVERIFY2(narrow != wide,
                 qPrintable(qsl("the note still reads what it was cut to for a %1px label in the %2px one it now has").arg(QString::number(roomWhenWide), QString::number(pMessage->width()))));
        QVERIFY2(narrow.endsWith(QChar(0x2026)), qPrintable(qsl("the note reads \"%1\", which does not say it was cut").arg(narrow)));
        QVERIFY2(wanted <= pMessage->width(),
                 qPrintable(qsl("the note wants %1px of a %2px label, so its last words are clipped rather than cut").arg(QString::number(wanted), QString::number(pMessage->width()))));
    }

    // A link a mouse can follow is one the keyboard has to reach as well: the
    // note takes the tab focus and answers Return with the jump a click makes
    void test_theNoteCanBeFollowedFromTheKeyboard()
    {
        typeAndSave(qsl("local a = 1\nlocal b = 1 +* 2\n"));

        QWidget* pNote = compileNote();
        QVERIFY2(pNote != nullptr, "the code pane's heading has no widget named editorCompileNote on it");
        QTRY_VERIFY2(pNote->isVisible(), "a save that did not compile left no note to reach");

        mpEditor->activateWindow();
        pNote->setFocus(Qt::TabFocusReason);
        QTRY_VERIFY2(pNote->hasFocus(), "the note cannot hold the keyboard focus, so the mouse is the only way to the line it names");

        controller()->moveCaretToOffset(0, false);
        QCOMPARE(caretLine(), 0);

        QTest::keyClick(pNote, Qt::Key_Return);
        QTRY_COMPARE(caretLine(), scmBrokenLineInEdbee);
    }

    // An unclosed block is not reported against any line the reader can see:
    // Lua stops at the end of the wrapper the item is compiled inside, which is
    // a line past the last one of the item. The jump lands on the last line
    // there is, at the start of it rather than wherever the document runs out.
    void test_anUnclosedBlockJumpsToTheLastLine()
    {
        typeAndSave(qsl("if true then\n  print('x')"));

        QWidget* pNote = compileNote();
        QVERIFY2(pNote != nullptr, "the code pane's heading has no widget named editorCompileNote on it");
        QTRY_VERIFY2(pNote->isVisible(), "an unclosed block was saved with no note on the heading");

        const int lines = static_cast<int>(controller()->textDocument()->lineCount());
        qInfo().noquote() << qsl("  the note reads \"%1\" and names line %2 of a document that has %3")
                                     .arg(compileMessage() ? compileMessage()->text() : QString(), QString::number(mpEditor->mEditorCompileErrorLine), QString::number(lines));
        QVERIFY2(mpEditor->mEditorCompileErrorLine > lines,
                 qPrintable(qsl("the compiler stopped on line %1 of a %2 line document, which is a line the reader can see - so this case is not the one past the end")
                                    .arg(QString::number(mpEditor->mEditorCompileErrorLine), QString::number(lines))));

        controller()->moveCaretToOffset(0, false);
        QCOMPARE(caretLine(), 0);

        QSplitterHandle* pHandle = codeHeadingHandle();
        QTest::mouseClick(pHandle, Qt::LeftButton, Qt::NoModifier, pNote->mapTo(pHandle, pNote->rect().center()));

        QTRY_COMPARE(caretLine(), lines - 1);
        const int startOfTheLastLine = static_cast<int>(controller()->textDocument()->offsetFromLine(static_cast<size_t>(lines - 1)));
        QVERIFY2(caretOffset() == startOfTheLastLine,
                 qPrintable(qsl("the caret is at offset %1 rather than %2, where the last line starts - the line asked for was past the end of the document and was followed there")
                                    .arg(QString::number(caretOffset()), QString::number(startOfTheLastLine))));
    }

    // A press on the note that dragged the panes and came back is a resize that
    // ended where it started, not a click on the link
    void test_aDragThatCameBackIsNotAClick()
    {
        typeAndSave(qsl("local a = 1\nlocal b = 1 +* 2\n"));

        QWidget* pNote = compileNote();
        QVERIFY2(pNote != nullptr, "the code pane's heading has no widget named editorCompileNote on it");
        QTRY_VERIFY2(pNote->isVisible(), "a save that did not compile left no note to press on");

        controller()->moveCaretToOffset(0, false);
        QCOMPARE(caretLine(), 0);

        QSplitterHandle* pHandle = codeHeadingHandle();
        const QList<int> before = mpEditor->splitter_right->sizes();
        const QPoint on = pNote->mapTo(pHandle, pNote->rect().center());
        const QPointF pressedAt = pHandle->mapToGlobal(QPointF(on));

        QTest::mousePress(pHandle, Qt::LeftButton, Qt::NoModifier, on);
        dragTo(pHandle, pressedAt + QPointF(0, scmDragTravel));
        dragTo(pHandle, pressedAt);
        QTest::mouseRelease(pHandle, Qt::LeftButton, Qt::NoModifier, pHandle->mapFromGlobal(pressedAt).toPoint());
        QTest::qWait(50ms);

        QVERIFY2(caretLine() == 0, qPrintable(qsl("the caret moved to line %1, so a drag of the panes that came back to where it started was heard as a click").arg(QString::number(caretLine()))));
        QCOMPARE(mpEditor->splitter_right->sizes(), before);
    }

    // A real drag arrives as a stream of small moves, and the handle follows
    // each of them - so, measured against the handle, the pointer never gets
    // anywhere. Measured against the screen it has travelled the whole way, and
    // that is what says the press was a drag of the panes and not a click.
    void test_aDragOfSmallStepsIsNotAClickEither()
    {
        typeAndSave(qsl("local a = 1\nlocal b = 1 +* 2\n"));

        QWidget* pNote = compileNote();
        QVERIFY2(pNote != nullptr, "the code pane's heading has no widget named editorCompileNote on it");
        QTRY_VERIFY2(pNote->isVisible(), "a save that did not compile left no note to press on");

        controller()->moveCaretToOffset(0, false);
        QCOMPARE(caretLine(), 0);

        QSplitterHandle* pHandle = codeHeadingHandle();
        const QList<int> before = mpEditor->splitter_right->sizes();
        const QPoint on = pNote->mapTo(pHandle, pNote->rect().center());
        const QPointF pressedAt = pHandle->mapToGlobal(QPointF(on));

        QTest::mousePress(pHandle, Qt::LeftButton, Qt::NoModifier, on);
        QPointF at = pressedAt;
        for (int travelled = 0; travelled < scmDragTravel; travelled += scmDragStep) {
            at += QPointF(0, scmDragStep);
            dragTo(pHandle, at);
        }
        QTest::mouseRelease(pHandle, Qt::LeftButton, Qt::NoModifier, pHandle->mapFromGlobal(at).toPoint());
        QTest::qWait(50ms);

        const QList<int> after = mpEditor->splitter_right->sizes();
        qInfo().noquote() << qsl("  the panes went from %1 to %2 over %3 steps of %4px")
                                     .arg(describeSizes(before), describeSizes(after), QString::number(scmDragTravel / scmDragStep), QString::number(scmDragStep));
        QVERIFY2(after != before, "the panes did not move, so the drag never reached the splitter and the case proves nothing");
        QVERIFY2(caretLine() == 0, qPrintable(qsl("the caret moved to line %1, so a drag of the panes made of small steps was heard as a click").arg(QString::number(caretLine()))));
    }
};

#include "EditorCodeHeadingTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorCodeHeadingTest)
