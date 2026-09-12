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
 * The notepad drawn in the design language, and the things about that which can
 * go wrong without anything else noticing.
 *
 * Every sheet goes on a widget rather than on the window, because a profile's
 * Lua stylesheet is assigned to the window on every show - so a sheet set on
 * the window would simply be replaced by it, and the window would come back
 * platform-drawn with nothing failing.
 *
 * The strip at the foot of the window has three readings and every one of them
 * is a claim something can break: idle, it says how far each send would reach
 * and shows no way to stop what is not running; sending, the buttons give way
 * to the progress of the send and to Stop; and with no prefix over a long note
 * it says what sending would come to, in the warning tone, with the field
 * carrying the same hairline and a cue inside it.
 *
 * That last reading is fitted to the room the bar has left, which is usually
 * none: the whole sentence, the short form, or no words at all beside the cue
 * in the field - and never a sentence cut off mid-word, which is what a reader
 * used to be left with at the width the warning matters most at.
 *
 * The pictures on the add-tab button and the three the find bar is worked from
 * are set in the style pass rather than in the .ui file, which is the only
 * thing that re-inks them when the appearance moves. A pass that stopped
 * setting one leaves an empty square.
 *
 * A tab is drawn as a chip on the page rather than as the folder tab a platform
 * cuts, which is a claim about pixels and is read as pixels here.
 *
 * Closing a note destroys it: save() writes the tabs that are left and nothing
 * keeps a copy of the rest. So a note with text in it is asked about first, once
 * per act however many notes the act takes, with Cancel as both the default and
 * the escape - and an empty note goes without a word.
 *
 * ...and an appearance change has to rebuild both sheets. The window is not a
 * QDialog and gets no restyle from anybody else.
 *
 * Run with: ctest -R NotepadShellTest -V
 */

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QtTest/QtTest>

#include <memory>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgNotepad.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class NotepadShellTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    QTemporaryDir mCacheDir;
    QByteArray mSavedXdgCache;
    TelnetServerStub* mpServer = nullptr;
    Host* mpHost = nullptr;
    dlgNotepad* mpNotepad = nullptr;
    QString mPort;
    const QString mLocalhost = qsl("localhost");
    const QString mProfileName = qsl("NotepadShell-Test-Profile");

    // How far a sampled pixel may sit from the colour the tokens mix, per
    // channel. A styled fill is composited rather than blitted, so the two
    // agree to a level or two rather than exactly.
    static constexpr int scmFillTolerance = 6;

    // setupConfig() consults portable.txt before the XDG logic
    static bool portableMarkerPresent()
    {
        return QFileInfo::exists(qsl("%1/portable.txt").arg(QCoreApplication::applicationDirPath())) || QFileInfo::exists(qsl("%1/.config/mudlet/portable.txt").arg(QDir::homePath()));
    }

    void deleteProfileDirectory(const QString& profileName)
    {
        QDir dir(mudlet::getMudletPath(enums::profileHomePath, profileName));
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }

    QString centralSheet() const { return mpNotepad->centralwidget->styleSheet(); }
    QString toolBarSheet() const { return mpNotepad->toolBar->styleSheet(); }

    QPlainTextEdit* currentNote() const { return qobject_cast<QPlainTextEdit*>(mpNotepad->tabWidget->currentWidget()); }

    // A note of that many lines, none of them empty - which is what the counts
    // on the two buttons are counts of
    void writeNote(const int lines)
    {
        QStringList text;
        for (int line = 1; line <= lines; ++line) {
            text << qsl("line %1").arg(QString::number(line));
        }
        currentNote()->setPlainText(text.join(QChar::LineFeed));
        QCoreApplication::processEvents();
    }

    // The first lines of that note, chosen the way a reader drags over them
    void selectFromTheTop(const int lines)
    {
        QTextCursor cursor = currentNote()->textCursor();
        cursor.movePosition(QTextCursor::Start);
        cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, lines);
        currentNote()->setTextCursor(cursor);
        QCoreApplication::processEvents();
    }

    void clearTheSelection()
    {
        QTextCursor cursor = currentNote()->textCursor();
        cursor.movePosition(QTextCursor::Start);
        currentNote()->setTextCursor(cursor);
        QCoreApplication::processEvents();
    }

    QString wordsOn(const QString& labelName) const
    {
        auto* pLabel = mpNotepad->findChild<QLabel*>(labelName);
        return pLabel ? pLabel->text() : QString();
    }

    // What the bar shows for an action, which is what a reader clicks: an action
    // hidden while a send runs is still an action, and only the button says so
    static bool onShow(const QAction* pAction) { return pAction && pAction->isVisible(); }

    // The cue the warning is carried by inside the prefix field, found where a
    // reader meets it - on the field - rather than through the member holding it
    QAction* fieldCue() const
    {
        const QList<QAction*> onTheField = mpNotepad->lineEdit_prependText->actions();
        for (QAction* pAction : onTheField) {
            if (pAction->objectName() == qsl("notepadPrefixWarning")) {
                return pAction;
            }
        }
        return nullptr;
    }

    // Whatever the strip is showing of the warning, it is never the tail end of
    // a sentence that ran out of room
    void nothingIsAFragment() const
    {
        const QString words = wordsOn(qsl("notepadNoPrefixText"));
        QVERIFY2(!words.contains(QChar(0x2026)) && !words.endsWith(qsl("...")), qPrintable(qsl("the warning is cut to a fragment rather than shown whole or not at all: \"%1\"").arg(words)));
    }

    // The window at that width, and the strip given its chance to measure again
    void atWidth(const int width) const
    {
        mpNotepad->resize(width, 520);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        QCoreApplication::processEvents();
    }

    // The one path an appearance change takes while a window is open
    void setAppearance(const enums::Appearance appearance)
    {
        mudlet::self()->setAppearance(appearance);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        QCoreApplication::processEvents();
        mpNotepad->grab();
    }

    static bool readsAs(const QColor& read, const QColor& wanted)
    {
        return std::abs(read.red() - wanted.red()) <= scmFillTolerance && std::abs(read.green() - wanted.green()) <= scmFillTolerance && std::abs(read.blue() - wanted.blue()) <= scmFillTolerance;
    }

    // The cross on a tab: the box the recipe draws it in, standing inside the
    // chip's padding on whichever side the strip puts it - the same line the
    // word is laid to - and on the chip's own centre line.
    void theCrossOnTabIsTheRecipes(const int index) const
    {
        QTabBar* pTabBar = mpNotepad->tabWidget->tabBar();
        QWidget* pTrailing = pTabBar->tabButton(index, QTabBar::RightSide);
        QWidget* pCross = pTrailing ? pTrailing : pTabBar->tabButton(index, QTabBar::LeftSide);
        QVERIFY2(pCross, qPrintable(qsl("tab %1 carries no cross on either side of its word").arg(QString::number(index))));

        const QSize wanted(uiDesign::scmTabCloseBoxSize, uiDesign::scmTabCloseBoxSize);
        QVERIFY2(pCross->size() == wanted,
                 qPrintable(qsl("tab %1: a cross drawn into a %2px box the platform sized is stretched past the %3px mark the recipe draws")
                                    .arg(QString::number(index), QString::number(pCross->size().width()), QString::number(uiDesign::scmTabCloseGlyphSize))));

        // The gap that tells two chips apart comes off the trailing side, so the
        // chip is the tab's rectangle less that gap
        const QRect tab = pTabBar->tabRect(index);
        const QRect chip = tab.adjusted(0, 0, -uiDesign::scmTabGap, 0);
        const int padding = uiDesign::scmInputBorderWidth + uiDesign::scmTabPaddingHorizontal;
        const int stands = pTrailing ? chip.right() - pCross->geometry().right() : pCross->geometry().left() - chip.left();
        QVERIFY2(stands == padding,
                 qPrintable(qsl("the cross on tab %1 sits %2px from the chip's edge where the word has %3px of padding")
                                    .arg(QString::number(index), QString::number(stands), QString::number(uiDesign::scmTabPaddingHorizontal))));

        const int offCentre = std::abs(pCross->geometry().center().y() - tab.center().y());
        QVERIFY2(offCentre <= 1, qPrintable(qsl("the cross on tab %1 sits %2px off the chip's centre line, where the word is").arg(QString::number(index), QString::number(offCentre))));
    }

    // How many 20ms ticks the answerer below will wait for a box to turn up, and
    // how long it puts up with one that did not go away after it was answered.
    // It has to give up: the modal loop runs inside the call under test, so a
    // box nothing presses would hang the case until ctest kills it rather than
    // failing it with a message.
    static constexpr int scmBoxAnswerTicks = 100;

    // What the answerer presses: the button that keeps the note, or the one in
    // the destructive role that takes it
    enum class TheAnswer { Keep, Discard };

    QTimer* mpBoxAnswerer = nullptr;
    QPointer<QMessageBox> mpAnsweredBox;
    int mBoxesSeen = 0;
    QString mBoxComplaint;

    // The box a close puts to the reader. activeModalWidget() is Qt's own
    // bookkeeping rather than the platform plugin's, so it works offscreen; the
    // sweep over the top levels keeps this from resting on that alone.
    static QMessageBox* visibleMessageBox()
    {
        if (auto* pModal = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            return pModal;
        }
        const QList<QWidget*> topLevels = QApplication::topLevelWidgets();
        for (QWidget* pWidget : topLevels) {
            if (auto* pBox = qobject_cast<QMessageBox*>(pWidget); pBox && pBox->isVisible()) {
                return pBox;
            }
        }
        return nullptr;
    }

    // QMessageBox::exec() spins a loop of its own, so the answer is armed before
    // the close and presses from inside it. The box just answered is held by a
    // QPointer rather than by address: it is a local of the call under test, so
    // the pointer empties when it goes and a second question - which is the
    // thing these cases are counting - is told apart from the first.
    void armTheAnswer(const TheAnswer answer)
    {
        mBoxesSeen = 0;
        mBoxComplaint.clear();
        mpAnsweredBox.clear();
        delete mpBoxAnswerer;
        mpBoxAnswerer = new QTimer(this);
        mpBoxAnswerer->setInterval(20ms);
        auto ticks = std::make_shared<int>(0);
        connect(mpBoxAnswerer, &QTimer::timeout, this, [this, answer, ticks]() {
            QMessageBox* pBox = visibleMessageBox();
            if (!pBox) {
                if (++*ticks > scmBoxAnswerTicks) {
                    mpBoxAnswerer->stop();
                }
                return;
            }

            if (pBox == mpAnsweredBox) {
                // The one just pressed, on its way out
                if (++*ticks > scmBoxAnswerTicks) {
                    mBoxComplaint = qsl("the box stayed up after it was answered");
                    mpBoxAnswerer->stop();
                    pBox->close();
                }
                return;
            }

            *ticks = 0;
            ++mBoxesSeen;
            mpAnsweredBox = pBox;

            QAbstractButton* pKeep = pBox->button(QMessageBox::Cancel);
            if (!pKeep) {
                mBoxComplaint = qsl("the box carries no Cancel, so there is no way to keep the note");
                pBox->close();
                return;
            }
            if (pBox->defaultButton() != pKeep || pBox->escapeButton() != pKeep) {
                mBoxComplaint = qsl("Cancel is not both the default and the escape button, so Return or Escape destroys the note");
            }

            if (answer == TheAnswer::Keep) {
                pKeep->click();
                return;
            }

            const QList<QAbstractButton*> onTheBox = pBox->buttons();
            for (QAbstractButton* pButton : onTheBox) {
                if (pBox->buttonRole(pButton) == QMessageBox::DestructiveRole) {
                    pButton->click();
                    return;
                }
            }
            mBoxComplaint = qsl("the box carries no button in the destructive role, so nothing on it closes the note");
            pBox->close();
        });
        mpBoxAnswerer->start();
    }

    void disarmTheAnswer()
    {
        if (mpBoxAnswerer) {
            mpBoxAnswerer->stop();
        }
    }

private slots:
    // A case that stops at a failed assertion must not leave the answerer
    // pressing its way through the next one's dialogs
    void cleanup() { disarmTheAnswer(); }

    void initTestCase()
    {
        if (portableMarkerPresent()) {
            QSKIP("portable.txt present - it takes precedence over XDG_CONFIG_HOME, so the config dir cannot be redirected");
        }

        QVERIFY(mConfigDir.isValid());
        QVERIFY(QDir().mkpath(qsl("%1/mudlet/profiles").arg(mConfigDir.path())));
        mSavedXdg = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", mConfigDir.path().toUtf8());
        // The glyphs a stylesheet points at are written into the cache
        // directory, so a run of this test writes nowhere the machine keeps
        QVERIFY(mCacheDir.isValid());
        mSavedXdgCache = qgetenv("XDG_CACHE_HOME");
        qputenv("XDG_CACHE_HOME", mCacheDir.path().toUtf8());

        mpServer = new TelnetServerStub(qApp);
        mpServer->start(mLocalhost, 0);
        QVERIFY2(mpServer->isListening(), qPrintable(qsl("TelnetServerStub failed to start: %1").arg(mpServer->errorString())));
        mPort = QString::number(mpServer->serverPort());
        mudlet::start();
        mudlet::self()->setupConfig();
        QCOMPARE(mudlet::getMudletPath(enums::mainPath), qsl("%1/mudlet").arg(mConfigDir.path()));
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>(qsl("MudletInstanceCoordinator")));
        mudlet::self()->init();
        // The words below are read as words rather than as pictures
        mudlet::self()->setInterfaceLanguage(qsl("en_US"));
        mudlet::self()->setStorePasswordsSecurely(false);

        deleteProfileDirectory(mProfileName);
        mpHost = TestProfile::create(mProfileName, mLocalhost, mPort);
        QVERIFY2(mpHost, "No active host after profile creation");
        QSignalSpy connected(&(mpHost->mTelnet), &cTelnet::signal_connected);
        QVERIFY2(connected.wait(2000), "Could not connect with the host.");

        // The way mudlet's Notes button opens it
        mudlet::self()->slot_notes();
        mpNotepad = mpHost->mpNotePad;
        QVERIFY2(mpNotepad, "the notepad did not open at all");
        mpNotepad->resize(760, 520);
        QVERIFY(QTest::qWaitForWindowExposed(mpNotepad));

        // A second note, so that the strip below has both a chosen tab and one
        // that is not
        if (mpNotepad->tabWidget->count() < 2) {
            mpNotepad->addTab(qsl("Second"));
        }
        QCOMPARE(mpNotepad->tabWidget->count(), 2);
        mpNotepad->tabWidget->setCurrentIndex(0);
        QCoreApplication::processEvents();
    }

    void cleanupTestCase()
    {
        mpNotepad = nullptr;
        mpHost = nullptr;
        delete mpServer;
        mpServer = nullptr;
        if (mudlet::self()) {
            mudlet::self()->setAppearance(enums::Appearance::systemSetting);
            deleteProfileDirectory(mProfileName);
            delete mudlet::self();
        }
        mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME") : qputenv("XDG_CONFIG_HOME", mSavedXdg);
        mSavedXdgCache.isNull() ? qunsetenv("XDG_CACHE_HOME") : qputenv("XDG_CACHE_HOME", mSavedXdgCache);
    }

    // Both sheets exist and are set where they have to be: on the central
    // widget and on the bar, never on the window, whose own stylesheet a
    // profile's Lua one is assigned to on every show
    void test_theShellsTwoSheetsAreSetOnWidgetsRatherThanOnTheWindow()
    {
        const QString central = centralSheet();
        QVERIFY2(central.contains(qsl("QTabBar::tab")), qPrintable(qsl("the central widget's sheet does not draw the strip of tabs at all: %1").arg(central.left(200))));
        QVERIFY2(central.contains(uiDesign::themeTokens().field.name()),
                 qPrintable(qsl("the central widget's sheet never names the field tone %1, so the notes are not drawn as the fields they are typed into").arg(uiDesign::themeTokens().field.name())));

        const QString bar = toolBarSheet();
        QVERIFY2(bar.contains(qsl("QToolBar#toolBar {")), qPrintable(qsl("the bar's sheet does not draw the bar itself: %1").arg(bar.left(200))));
        QVERIFY2(bar.contains(qsl("notepadFieldState")), "the bar's sheet draws no hairline on a field the strip has warned about");
        QVERIFY2(bar.contains(qsl("notepadSendingBar")), "the bar's sheet says nothing about the thread a send's progress is drawn as");
    }

    // Idle, the strip says how far each of the three sends would reach and shows
    // no way to stop what is not running. The fold that used to hide all of it
    // is gone: it saved no space and guarded nothing.
    void test_theStripShowsItsReachAndNoStopWhileIdle()
    {
        QVERIFY2(!mpNotepad->findChild<QToolButton*>(qsl("notepadOptionsToggle")), "the bar still carries the Options button the send controls were folded behind");

        const QList<QPair<QAction*, QString>> sends{
                {mpNotepad->action_sendLine, qsl("Send line")}, {mpNotepad->action_sendSelection, qsl("Send selection")}, {mpNotepad->action_sendAll, qsl("Send all")}};
        for (const auto& send : sends) {
            QVERIFY2(onShow(send.first), qPrintable(qsl("'%1' is not on the strip at all").arg(send.second)));
            QVERIFY2(!send.first->icon().isNull(), qPrintable(qsl("'%1' carries no glyph, so it is a word with a gap beside it").arg(send.second)));
            QVERIFY2(!send.first->toolTip().isEmpty(), qPrintable(qsl("'%1' says nothing about what it will do").arg(send.second)));
        }

        writeNote(12);
        selectFromTheTop(3);
        QCOMPARE(mpNotepad->action_sendAll->text(), qsl("Send all (12 lines)"));
        QCOMPARE(mpNotepad->action_sendSelection->text(), qsl("Send selection (3 lines)"));
        QVERIFY2(mpNotepad->action_sendSelection->isEnabled(), "Send selection is unavailable with three lines selected");
        QCOMPARE(mpNotepad->action_sendLine->text(), qsl("Send line"));

        clearTheSelection();
        QCOMPARE(mpNotepad->action_sendSelection->text(), qsl("Send selection"));
        QVERIFY2(!mpNotepad->action_sendSelection->isEnabled(), "Send selection is available with nothing selected");

        QVERIFY2(!onShow(mpNotepad->action_stop), "Stop is on the strip with nothing being sent");

        // The word leading the prefix field and the field itself are set in
        // the size the bar's buttons are, or on macOS they sit a size larger
        // than the buttons beside them and off their line
        const QWidget* pSendLine = mpNotepad->toolBar->widgetForAction(mpNotepad->action_sendLine);
        QVERIFY2(pSendLine, "the bar made no button for Send line");
        // Rounded: the platform's tool button font is a fraction on some of
        // them, where the sheet can only name a whole point
        const int buttonSize = qRound(pSendLine->font().pointSizeF());
        for (const QWidget* pWord : {static_cast<QWidget*>(mpNotepad->label_prependText),
                                     static_cast<QWidget*>(mpNotepad->lineEdit_prependText),
                                     static_cast<QWidget*>(mpNotepad->mpLabel_noPrefixText),
                                     static_cast<QWidget*>(mpNotepad->mpLabel_sendingText)}) {
            QVERIFY2(qRound(pWord->font().pointSizeF()) == buttonSize,
                     qPrintable(qsl("%1 is set at %2pt beside buttons set at %3pt").arg(pWord->objectName(), QString::number(pWord->font().pointSizeF()), QString::number(buttonSize))));
        }
    }

    // The seam between the send buttons and the prefix is centred between the
    // two words it parts, not between the two widgets. A tool button holds its
    // word the bar's own padding in from its edge while a label's word starts
    // at the label's edge - so the seam, whose margins are the same either
    // side, sat nearly twice as far from "Send all (12 lines)" as from "Before
    // each line" until the word was given the same room a button gives its own.
    //
    // Read as geometry: what a word's own ink reaches is not measurable, but
    // the box each of them is held in is. The two readings together put the
    // seam within the button's one transparent border pixel of centred.
    void test_theSeamOnTheStripIsCentredBetweenTheTwoWordsItParts()
    {
        atWidth(1400);
        writeNote(12);
        clearTheSelection();

        const QWidget* pSendAll = mpNotepad->toolBar->widgetForAction(mpNotepad->action_sendAll);
        QVERIFY2(pSendAll, "the bar made no button for Send all");
        const QWidget* pSeam = nullptr;
        for (QAction* pAction : mpNotepad->toolBar->actions()) {
            if (pAction->isSeparator()) {
                pSeam = mpNotepad->toolBar->widgetForAction(pAction);
                break;
            }
        }
        QVERIFY2(pSeam, "the bar carries no seam between the send buttons and the prefix");
        const QLabel* pWord = mpNotepad->label_prependText;
        QVERIFY2(pWord && pWord->isVisible(), "the word leading the prefix field is not on the strip");
        QVERIFY2(pSendAll->x() < pSeam->x() && pSeam->x() < pWord->x(),
                 qPrintable(qsl("the strip is not laid out button, seam, word: they start at %1, %2 and %3")
                                    .arg(QString::number(pSendAll->x()), QString::number(pSeam->x()), QString::number(pWord->x()))));

        const int beforeTheSeam = pSeam->x() - (pSendAll->x() + pSendAll->width());
        const int afterTheSeam = pWord->x() - (pSeam->x() + pSeam->width());
        QVERIFY2(beforeTheSeam == afterTheSeam, qPrintable(qsl("the bar leaves %1px before the seam and %2px after it").arg(QString::number(beforeTheSeam), QString::number(afterTheSeam))));

        // ...and the word's own box, which is where the rule this case is about
        // does its work: a label with no padding holds its word at its edge and
        // the seam reads as belonging to the field rather than parting a pair
        // Read off the pixels rather than off the margins: what the reader sees
        // is ink either side of a line, and the button's own slack after its
        // word is what a margin alone cannot account for
        const QImage shot = mpNotepad->toolBar->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        const qreal ratio = shot.width() / qreal(mpNotepad->toolBar->width());
        const QRgb bar = shot.pixel(qRound(2 * ratio), qRound(2 * ratio));
        const auto inkIn = [&](const QWidget* pWidget, int& first, int& last) {
            first = -1;
            last = -1;
            const QRect area = QRect(pWidget->mapTo(mpNotepad->toolBar, QPoint(0, 0)), pWidget->size());
            for (int x = area.left(); x <= area.right(); ++x) {
                for (int y = area.top() + 2; y < area.bottom() - 2; ++y) {
                    const QRgb px = shot.pixel(qRound(x * ratio), qRound(y * ratio));
                    if (qAbs(qRed(px) - qRed(bar)) + qAbs(qGreen(px) - qGreen(bar)) + qAbs(qBlue(px) - qBlue(bar)) > 90) {
                        if (first < 0) {
                            first = x;
                        }
                        last = x;
                        break;
                    }
                }
            }
        };
        int buttonFirst = 0, buttonLast = 0, wordFirst = 0, wordLast = 0;
        inkIn(pSendAll, buttonFirst, buttonLast);
        inkIn(pWord, wordFirst, wordLast);
        QVERIFY2(buttonLast > 0 && wordFirst > 0, "no ink was found in the Send all button or in the lead word");
        const int seamLine = pSeam->x() + pSeam->width() / 2;
        const int leftOfTheLine = seamLine - buttonLast;
        const int rightOfTheLine = wordFirst - seamLine;
        qInfo().noquote() << qsl("  the seam stands %1px after Send all's last ink and %2px before the lead word's first").arg(QString::number(leftOfTheLine), QString::number(rightOfTheLine));
        QVERIFY2(qAbs(leftOfTheLine - rightOfTheLine) <= 2,
                 qPrintable(qsl("the seam is %1px from the word on its left and %2px from the word on its right").arg(QString::number(leftOfTheLine), QString::number(rightOfTheLine))));
    }

    // Sending, the three buttons, the word, the field and the warning leave the
    // strip and the progress of the send takes their place - with Stop the one
    // button on it. Stopping puts the strip back the way it was.
    void test_aSendShowsProgressAndStopThenReturns()
    {
        writeNote(12);
        clearTheSelection();
        mpNotepad->lineEdit_prependText->setText(qsl("say"));
        QCoreApplication::processEvents();

        mpNotepad->action_sendAll->trigger();
        QCoreApplication::processEvents();

        QVERIFY2(!onShow(mpNotepad->action_sendAll), "Send all is still on the strip while a send runs");
        QVERIFY2(!onShow(mpNotepad->action_sendLine) && !onShow(mpNotepad->action_sendSelection), "the other two send buttons stayed on the strip while a send runs");
        QVERIFY2(!onShow(mpNotepad->action_prependText) && !onShow(mpNotepad->action_prependTextLabel), "the prefix and the word leading it stayed on the strip while a send runs");
        QVERIFY2(onShow(mpNotepad->action_stop), "there is no way to stop a send that is running");
        QCOMPARE(wordsOn(qsl("notepadSendingText")), qsl("Sending 0 of 12"));

        auto* pBar = mpNotepad->findChild<QProgressBar*>(qsl("notepadSendingBar"));
        QVERIFY2(pBar, "the strip carries no thread of progress");
        QCOMPARE(pBar->maximum(), 12);
        QCOMPARE(pBar->value(), 0);

        QTRY_VERIFY_WITH_TIMEOUT(wordsOn(qsl("notepadSendingText")) == qsl("Sending 1 of 12"), 2000);
        QCOMPARE(pBar->value(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(wordsOn(qsl("notepadSendingText")) == qsl("Sending 2 of 12"), 2000);
        QCOMPARE(pBar->value(), 2);

        mpNotepad->slot_stopSending();
        QCoreApplication::processEvents();
        QTRY_VERIFY_WITH_TIMEOUT(onShow(mpNotepad->action_sendAll), 1000);
        QVERIFY2(!onShow(mpNotepad->action_stop), "Stop is still on the strip after the send was stopped");
        QVERIFY2(onShow(mpNotepad->action_prependText), "the prefix field did not come back after the send was stopped");

        mpNotepad->lineEdit_prependText->clear();
        QCoreApplication::processEvents();
    }

    // The reading the strip is really for: an empty prefix over a note long
    // enough to be prose, which would go to the game a command at a time
    void test_anEmptyPrefixOverALongNoteWarns()
    {
        // Wide enough for the whole sentence: which of the warning's forms the
        // strip has room for is the case after this one
        mpNotepad->resize(1400, 520);
        QCoreApplication::processEvents();
        mpNotepad->lineEdit_prependText->clear();
        writeNote(12);
        clearTheSelection();

        QWidget* pNote = mpNotepad->findChild<QWidget*>(qsl("notepadNoPrefixNote"));
        QVERIFY2(pNote, "the strip has no 'notepadNoPrefixNote' at all");
        QVERIFY2(pNote->isVisible(), "a 12 line note with no prefix is sent without a word about it");
        QVERIFY2(wordsOn(qsl("notepadNoPrefixText")).contains(qsl("12")), qPrintable(qsl("the warning does not say how many lines would go: \"%1\"").arg(wordsOn(qsl("notepadNoPrefixText")))));
        QCOMPARE(mpNotepad->lineEdit_prependText->property("notepadFieldState").toString(), qsl("warning"));
        QVERIFY2(mpNotepad->action_sendAll->isEnabled(), "the warning took the send away rather than saying what it would do");

        // ...and the cue that costs the bar no room, so it is there at every
        // width the window is dragged to: inside the field, at its trailing end
        QAction* pCue = fieldCue();
        QVERIFY2(pCue, "the prefix field carries no 'notepadPrefixWarning' at all");
        QVERIFY2(pCue->isVisible(), "the field carries no cue while the strip is warning about what a send would do");
        QVERIFY2(!pCue->icon().isNull(), "the field's cue carries no picture, so it is a gap at the end of the field");
        QCOMPARE(pCue->toolTip(), mpNotepad->mNoPrefixNoteText);
        QVERIFY2(pCue->toolTip().contains(qsl("12")), qPrintable(qsl("the cue does not say how many lines would go: \"%1\"").arg(pCue->toolTip())));

        mpNotepad->lineEdit_prependText->setText(qsl("say"));
        QCoreApplication::processEvents();
        QVERIFY2(!pNote->isVisible(), "the warning stayed up after a prefix was typed");
        QVERIFY2(!pCue->isVisible(), "the field kept its cue after a prefix was typed");
        QVERIFY2(mpNotepad->lineEdit_prependText->property("notepadFieldState").toString().isEmpty(), "the field kept its warning hairline after a prefix was typed");

        mpNotepad->lineEdit_prependText->clear();
        writeNote(3);
        QCoreApplication::processEvents();
        QVERIFY2(!pNote->isVisible(), "a three line note with no prefix is warned about, which is a warning about nothing");
        QVERIFY2(!pCue->isVisible(), "a three line note put the cue in the field");
        QVERIFY2(mpNotepad->lineEdit_prependText->property("notepadFieldState").toString().isEmpty(), "a three line note put the warning hairline on the field");

        // ...and on a window narrower than the strip's own content the whole
        // sentence is still there to be read, whatever the strip has room for
        writeNote(12);
        atWidth(760);
        QVERIFY2(pCue->isVisible(), "the field's cue went with the words the narrower strip had no room for");
        QVERIFY2(pNote->toolTip().contains(qsl("12")), qPrintable(qsl("the whole sentence is nowhere to be read: \"%1\"").arg(pNote->toolTip())));
        QVERIFY2(mpNotepad->lineEdit_prependText->toolTip().contains(qsl("12")),
                 qPrintable(qsl("the field, which is what a reader is looking at, does not carry the sentence: \"%1\"").arg(mpNotepad->lineEdit_prependText->toolTip())));
        // The action rather than the widget: a bar that has posted its tail
        // into its drop-down leaves the widget saying it is visible
        if (mpNotepad->action_noPrefixNote->isVisible()) {
            QVERIFY2(pNote->x() + pNote->width() <= mpNotepad->toolBar->width(), "the warning is drawn off the end of the strip");
        }
    }

    // The words come only whole. The strip's own content wants more room than
    // the window opens at, so a sentence at its end is the first thing to go:
    // it is the whole sentence where there is room for the whole sentence, the
    // short form where there is not, and nothing at all rather than a fragment -
    // with the cue in the field standing at every one of the three.
    //
    // The widths are pinned to what this window measures on the platform these
    // run on: the strip with the words off it asks for 712px, the sentence is
    // 298px wide and the short form 54px - so the sentence wants 1010px of
    // window and the short form 766px, and below that neither is shown.
    void test_theWarningIsShownWholeOrShortOrNotAtAll()
    {
        mpNotepad->lineEdit_prependText->clear();
        writeNote(12);
        clearTheSelection();

        QWidget* pNote = mpNotepad->findChild<QWidget*>(qsl("notepadNoPrefixNote"));
        QAction* pCue = fieldCue();
        QVERIFY2(pNote && pCue, "the strip is missing the warning's reading or the field's cue");
        const QString whole = mpNotepad->mNoPrefixNoteText;
        QVERIFY2(whole.contains(qsl("12")), qPrintable(qsl("the sentence to be fitted is not the warning: \"%1\"").arg(whole)));

        atWidth(1400);
        QTRY_COMPARE(wordsOn(qsl("notepadNoPrefixText")), whole);
        QVERIFY2(pNote->isVisible(), "the warning is not on the strip on a window with room for the whole sentence");
        QVERIFY2(pCue->isVisible(), "the field's cue is gone at the width where everything fits");
        nothingIsAFragment();

        atWidth(800);
        QTRY_COMPARE(wordsOn(qsl("notepadNoPrefixText")), qsl("No prefix"));
        QVERIFY2(pNote->isVisible(), "the short form is not on the strip at the width it is for");
        QVERIFY2(pCue->isVisible(), "the field's cue went with the sentence the strip had no room for");
        nothingIsAFragment();

        // Read as the action being off the bar rather than as isVisible() on the
        // widget: at a width this narrow the bar is over its own room as well and
        // posts its tail into the drop-down, which leaves that flag saying what it
        // said before the fit ran. What the reader sees is what the bar carries.
        atWidth(520);
        QTRY_VERIFY2(!onShow(mpNotepad->action_noPrefixNote), "the reading is still on the strip at a width neither form fits in");
        QVERIFY2(pNote->isHidden(), "the warning's widget was left on the strip, so it is drawn as a dot with nothing after it");
        QVERIFY2(wordsOn(qsl("notepadNoPrefixText")).isEmpty(), qPrintable(qsl("words were left on the label at the narrowest width: \"%1\"").arg(wordsOn(qsl("notepadNoPrefixText")))));
        QVERIFY2(pCue->isVisible(), "with no words on the strip the field's cue is gone as well, so nothing warns at all");
        QVERIFY2(pNote->toolTip().contains(qsl("12")) && pCue->toolTip().contains(qsl("12")), "the whole sentence is nowhere to be read at the narrowest width");
        nothingIsAFragment();

        // ...and back, since a form that only ever narrows is half a fit
        atWidth(1400);
        QTRY_COMPARE(wordsOn(qsl("notepadNoPrefixText")), whole);
        QVERIFY2(pNote->isVisible(), "the warning did not come back when the window was widened again");

        mpNotepad->resize(760, 520);
        QCoreApplication::processEvents();
    }

    // Both readings are drawn in the design's own colours: the send in the
    // accent, since it is the window doing what it was asked, and the warning in
    // words walked off the page until they can be read on it
    void test_theStripsInksAreTheDesigns()
    {
        auto* pDot = mpNotepad->findChild<QLabel*>(qsl("notepadSendingDot"));
        auto* pWords = mpNotepad->findChild<QLabel*>(qsl("notepadNoPrefixText"));
        QVERIFY2(pDot && pWords, "the strip is missing one of its two readings");

        mpNotepad->lineEdit_prependText->clear();
        writeNote(12);
        clearTheSelection();

        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();

            QVERIFY2(pDot->styleSheet().contains(tokens.accent.name()),
                     qPrintable(qsl("on the %1 appearance the sending dot is not drawn in the accent %2: %3").arg(appearance.first, tokens.accent.name(), pDot->styleSheet())));

            const QColor ink = pWords->palette().color(QPalette::Active, QPalette::WindowText);
            const qreal ratio = uiDesign::contrastRatio(ink, tokens.page);
            qInfo().noquote() << qsl("  %1: the warning reads %2 on the page %3 = %4:1").arg(appearance.first, ink.name(), tokens.page.name(), QString::number(ratio, 'f', 2));
            QVERIFY2(ratio >= uiDesign::scmTextMinimumRatio,
                     qPrintable(qsl("on the %1 appearance the warning reads %2 on the page %3 = %4:1, under the %5:1 every word of this design is walked to")
                                        .arg(appearance.first, ink.name(), tokens.page.name(), QString::number(ratio, 'f', 2), QString::number(uiDesign::scmTextMinimumRatio, 'f', 1))));
        }

        // The cases after this one read pixels against whichever appearance the
        // machine is in, which is where they began
        setAppearance(enums::Appearance::systemSetting);
    }

    // The four buttons that are a picture and nothing else, and the cross on a
    // tab, which is a picture a rule points at rather than an icon on a widget
    void test_everyPictureInTheWindowComesFromTheGlyphs()
    {
        const QStringList pictureButtons{qsl("notepadAddTab"), qsl("notepadFindPrevious"), qsl("notepadFindNext"), qsl("notepadFindClose")};
        for (const QString& name : pictureButtons) {
            auto* pButton = mpNotepad->findChild<QToolButton*>(name);
            QVERIFY2(pButton, qPrintable(qsl("there is no '%1'").arg(name)));
            QVERIFY2(!pButton->icon().isNull(), qPrintable(qsl("'%1' carries no glyph, so it is an empty square").arg(name)));
            QVERIFY2(pButton->text().isEmpty(), qPrintable(qsl("'%1' still carries a word beside its glyph").arg(name)));
        }

        const QString central = centralSheet();
        QVERIFY2(central.contains(qsl("close-button")), "the strip of tabs draws no cross of its own, so a tab keeps the platform's");
        const int at = central.indexOf(qsl("::close-button"));
        QVERIFY2(central.mid(at, 200).contains(qsl("image:")), "the tab's close-button rule points at no picture");
    }

    // A tab is a chip on the page: the chosen one filled with the accent's wash
    // and the rest showing the page through. Read as pixels, since that is the
    // whole of the claim.
    void test_theChosenTabIsLitAndTheRestAreThePage()
    {
        QTabBar* pTabBar = mpNotepad->tabWidget->tabBar();
        QVERIFY2(pTabBar->count() == 2, "the strip does not hold the two tabs this reads");
        mpNotepad->tabWidget->setCurrentIndex(0);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QColor lit = uiDesign::blend(tokens.page, tokens.accent, uiDesign::scmSoftWashStrength);
        const QImage shot = mpNotepad->grab().toImage();

        // Inside the fill and clear of the word: the tab's own padding leaves
        // room at its leading edge that nothing is drawn in
        const auto insideTab = [pTabBar, this](const int index) {
            const QRect tab = pTabBar->tabRect(index);
            return pTabBar->mapTo(mpNotepad, QPoint(tab.left() + 4, tab.center().y()));
        };

        const QColor chosen = shot.pixelColor(insideTab(0));
        const QColor other = shot.pixelColor(insideTab(1));
        qInfo().noquote() << qsl("  chosen tab %1, the other %2; the accent wash on the page is %3 and the page is %4").arg(chosen.name(), other.name(), lit.name(), tokens.page.name());

        QVERIFY2(readsAs(chosen, lit), qPrintable(qsl("the chosen tab reads %1 rather than the accent wash %2 - it is not drawn as a lit chip").arg(chosen.name(), lit.name())));
        QVERIFY2(readsAs(other, tokens.page),
                 qPrintable(qsl("the tab that is not chosen reads %1 rather than the page %2 - the strip is still drawing folder tabs").arg(other.name(), tokens.page.name())));
    }

    // An appearance change has to rebuild both sheets. Nothing else restyles
    // this window: it is not a QDialog, and the profile stylesheet it is handed
    // on show says nothing about either.
    void test_bothSheetsAreRebuiltWhenTheAppearanceMoves()
    {
        QMap<QString, QPair<QString, QString>> sheets;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const QString field = uiDesign::themeTokens().field.name();
            QVERIFY2(centralSheet().contains(field),
                     qPrintable(qsl("on the %1 appearance the field tone is %2, which the central widget's sheet does not name - the shell did not restyle").arg(appearance.first, field)));
            QVERIFY2(toolBarSheet().contains(uiDesign::themeTokens().page.name()),
                     qPrintable(qsl("on the %1 appearance the bar's sheet does not name the page tone %2").arg(appearance.first, uiDesign::themeTokens().page.name())));
            sheets.insert(appearance.first, {centralSheet(), toolBarSheet()});
        }

        QVERIFY2(sheets.value(qsl("dark")).first != sheets.value(qsl("light")).first, "both appearances built the same central sheet, so the reading above would hold whatever it said");
        QVERIFY2(sheets.value(qsl("dark")).second != sheets.value(qsl("light")).second, "both appearances built the same toolbar sheet");
    }

    // The fold the send controls used to hide behind was remembered per
    // profile. Nothing reads that key now, so a profile that still carries it
    // from an older notepad has it taken out the next time this one saves.
    void test_theRetiredFoldKeyIsClearedOnSave()
    {
        const QString key = qsl("Notepad/SendControlsVisible");
        mpHost->writeProfileIniData(key, qsl("true"));
        QCOMPARE(mpHost->readProfileIniData(key), qsl("true"));

        mpNotepad->saveSettings();

        QVERIFY2(mpHost->readProfileIniData(key).isEmpty(), qPrintable(qsl("the profile still carries %1 after the notepad saved its settings").arg(key)));
        QVERIFY2(!mpHost->readProfileIniData(qsl("Notepad/WindowState")).isEmpty(), "the save that was meant to clear the old key did not write the window state either, so it did not run");
    }

    // A closed note is gone: save() writes the tabs that are left and nothing
    // keeps a copy of the rest, so a cross caught by a slip of the finger used
    // to take the work with it. A note with text in it is asked about first,
    // with Cancel as both the default and the escape so that Return and Escape
    // alike keep it - and an empty note, which is no loss, goes without a word.
    void test_aNoteWithTextAsksBeforeItIsClosed()
    {
        QCOMPARE(mpNotepad->tabWidget->count(), 2);
        auto* pSecond = qobject_cast<QPlainTextEdit*>(mpNotepad->tabWidget->widget(1));
        QVERIFY2(pSecond, "the second note is not a field at all");
        pSecond->setPlainText(qsl("kept"));
        QCoreApplication::processEvents();

        armTheAnswer(TheAnswer::Keep);
        mpNotepad->closeTab(1);
        disarmTheAnswer();
        QVERIFY2(mBoxComplaint.isEmpty(), qPrintable(mBoxComplaint));
        QVERIFY2(mBoxesSeen == 1, qPrintable(qsl("closing a note with text in it put %1 questions to the reader rather than one").arg(QString::number(mBoxesSeen))));
        QCOMPARE(mpNotepad->tabWidget->count(), 2);
        QCOMPARE(qobject_cast<QPlainTextEdit*>(mpNotepad->tabWidget->widget(1))->toPlainText(), qsl("kept"));

        armTheAnswer(TheAnswer::Discard);
        mpNotepad->closeTab(1);
        disarmTheAnswer();
        QVERIFY2(mBoxComplaint.isEmpty(), qPrintable(mBoxComplaint));
        QCOMPARE(mBoxesSeen, 1);
        QCOMPARE(mpNotepad->tabWidget->count(), 1);

        // ...and a note with nothing in it: had it asked, the answerer would
        // have been let in by the loop the box spins and would have kept it
        mpNotepad->addTab(qsl("Second"));
        QCoreApplication::processEvents();
        QCOMPARE(mpNotepad->tabWidget->count(), 2);
        armTheAnswer(TheAnswer::Keep);
        mpNotepad->closeTab(1);
        disarmTheAnswer();
        QVERIFY2(!mBoxesSeen, "closing an empty note asked the reader about text that was not there");
        QCOMPARE(mpNotepad->tabWidget->count(), 1);

        mpNotepad->addTab(qsl("Second"));
        mpNotepad->tabWidget->setCurrentIndex(0);
        QCoreApplication::processEvents();
        QCOMPARE(mpNotepad->tabWidget->count(), 2);
    }

    // Close Other Tabs is one act rather than a run of closes, so it is put to
    // the reader once - and Cancel keeps every one of the notes it named
    void test_closingTheOtherNotesAsksOnceForAllOfThem()
    {
        QCOMPARE(mpNotepad->tabWidget->count(), 2);
        qobject_cast<QPlainTextEdit*>(mpNotepad->tabWidget->widget(1))->setPlainText(qsl("kept"));
        const int third = mpNotepad->addTab(qsl("Third"), qsl("also kept"));
        QCoreApplication::processEvents();
        QCOMPARE(third, 2);
        QCOMPARE(mpNotepad->tabWidget->count(), 3);

        armTheAnswer(TheAnswer::Keep);
        mpNotepad->closeOtherTabs(0);
        disarmTheAnswer();
        QVERIFY2(mBoxComplaint.isEmpty(), qPrintable(mBoxComplaint));
        QVERIFY2(mBoxesSeen == 1, qPrintable(qsl("closing the other notes put %1 questions to the reader rather than one").arg(QString::number(mBoxesSeen))));
        QVERIFY2(mpNotepad->tabWidget->count() == 3, qPrintable(qsl("Cancel left %1 of the 3 notes standing").arg(QString::number(mpNotepad->tabWidget->count()))));

        armTheAnswer(TheAnswer::Discard);
        mpNotepad->closeOtherTabs(0);
        disarmTheAnswer();
        QVERIFY2(mBoxComplaint.isEmpty(), qPrintable(mBoxComplaint));
        QCOMPARE(mBoxesSeen, 1);
        QCOMPARE(mpNotepad->tabWidget->count(), 1);

        mpNotepad->addTab(qsl("Second"));
        mpNotepad->tabWidget->setCurrentIndex(0);
        QCoreApplication::processEvents();
        QCOMPARE(mpNotepad->tabWidget->count(), 2);
    }

    // A cross that cannot close anything is a control that does nothing. The
    // last note left is kept by closeTab() and the context menu offers no close
    // for it, so the strip carries no crosses while there is one tab - and both
    // tabs carry one again the moment a second note is made.
    void test_theLastTabCarriesNoCrossSinceItCannotClose()
    {
        QTabBar* pTabBar = mpNotepad->tabWidget->tabBar();
        const auto crossOn = [pTabBar](const int index) {
            QWidget* pTrailing = pTabBar->tabButton(index, QTabBar::RightSide);
            return pTrailing ? pTrailing : pTabBar->tabButton(index, QTabBar::LeftSide);
        };

        QCOMPARE(mpNotepad->tabWidget->count(), 2);
        for (int i = 0; i < 2; ++i) {
            QVERIFY2(crossOn(i), qPrintable(qsl("tab %1 carries no cross while there are two notes to close one of").arg(QString::number(i))));
        }

        mpNotepad->closeTab(1);
        QCoreApplication::processEvents();
        QCOMPARE(mpNotepad->tabWidget->count(), 1);
        QVERIFY2(!crossOn(0), "the sole tab still carries a cross, though closeTab() will not close it");

        // The rule the cross is hidden for: the last note stays whatever is
        // asked of it
        mpNotepad->closeTab(0);
        QCoreApplication::processEvents();
        QCOMPARE(mpNotepad->tabWidget->count(), 1);

        const int second = mpNotepad->addTab(qsl("Second"));
        QCoreApplication::processEvents();
        QCOMPARE(mpNotepad->tabWidget->count(), 2);
        for (int i = 0; i < 2; ++i) {
            QVERIFY2(crossOn(i), qPrintable(qsl("tab %1 carries no cross after a second note brought the crosses back").arg(QString::number(i))));
        }

        // ...and the crosses the strip made again are the recipe's, which is
        // the keeper on the bar catching them rather than the pass over the
        // tabs that were there when it was styled
        theCrossOnTabIsTheRecipes(0);
        if (QTest::currentTestFailed()) {
            return;
        }
        theCrossOnTabIsTheRecipes(second);

        mpNotepad->tabWidget->setCurrentIndex(0);
        QCoreApplication::processEvents();
    }

    // The cross on a tab is sized by the widget, not by the rule that draws it:
    // Qt's close button asks the style for its own box in its constructor, and
    // a ::close-button rule is never asked. The mark is drawn at
    // scmTabCloseGlyphSize inside a picture scmTabCloseBoxSize across, and that
    // picture is painted into whatever rect the button ended up with - so a box
    // the platform made larger stretches the mark by the same ratio, and the
    // reader gets a heavier x here than on the profile strip.
    //
    // Where that box stands is the second half of the same claim, and Qt gets it
    // from the tab's raw rectangle rather than from the padding box the rule
    // lays the word in - so the cross came out flush against the chip's edge
    // while the word beside it had ten pixels of air.
    void test_everyCrossIsTheRecipesBoxInsideTheChipsPadding()
    {
        for (int i = 0; i < mpNotepad->tabWidget->tabBar()->count(); ++i) {
            theCrossOnTabIsTheRecipes(i);
            if (QTest::currentTestFailed()) {
                return;
            }
        }

        // ...and a tab made after the strip was prepared, under the appearance
        // whose base style is Fusion rather than the platform's own: the two
        // answer different numbers for a tab's cross and only one of them is
        // the recipe's. This one is kept by the filter watching the bar rather
        // than by the pass over the tabs that were already there.
        setAppearance(enums::Appearance::dark);
        const int third = mpNotepad->addTab(qsl("Third"));
        QCoreApplication::processEvents();
        theCrossOnTabIsTheRecipes(third);
    }
};

#include "NotepadShellTest.moc"
MUDLET_GROUPED_TEST_MAIN(NotepadShellTest)
