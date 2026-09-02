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
 * The keystroke a key answers, set in a field that listens for it.
 *
 * The "Grab New Key" button is gone - the first case holds it gone, since a
 * form still carrying it would pass every other case here while offering two
 * ways to do the one thing - and the field is the control: a click arms the
 * grab, the next keystroke is taken whole (the keypad modifier included, which
 * is what numpad walking is bound through), and Escape or the focus going
 * elsewhere leaves the keystroke as it was.
 *
 * Run with: ctest -R EditorKeyCaptureTest -V
 */

#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QtTest/QtTest>
#include <chrono>

#include "Host.h"
#include "KeyUnit.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TKey.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgKeysMainArea.h"
#include "dlgSourceEditorArea.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorKeyCaptureTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    QTreeWidgetItem* mpKeyItem = nullptr;
    // An action the editor's shortcuts can be seen going away and coming back
    // on, since a grab is what takes them away
    QAction* mpShortcutAction = nullptr;
    QKeySequence mShortcut;
    const QString mProfileName = qsl("EditorKeyCapture-Test-Profile");
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

    QLineEdit* field() const { return mpEditor->mpKeysMainArea->lineEdit_key_binding; }
    QLabel* hint() const { return mpEditor->mpLabel_keyHint; }
    QToolButton* clearButton() const { return mpEditor->mpButton_keyClear; }
    TKey* key() const { return mpKeyItem ? mpHost->getKeyUnit()->getKey(mpKeyItem->data(0, Qt::UserRole).toInt()) : nullptr; }

    void armTheGrab()
    {
        QTest::mouseClick(field(), Qt::LeftButton);
        QCoreApplication::processEvents();
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

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(1100, 900);

        // One the editor puts back afterwards: setShortcuts() restores an
        // action from mButtonShortcuts, keyed by the word on it
        for (QAction* pAction : mpEditor->toolBar->actions()) {
            if (!pAction->shortcut().isEmpty() && mpEditor->mButtonShortcuts.contains(pAction->text())) {
                mpShortcutAction = pAction;
                mShortcut = pAction->shortcut();
                break;
            }
        }
        QVERIFY2(mpShortcutAction != nullptr, "no action of the editor's carries a shortcut, so there is nothing to watch a grab take away");

        mpEditor->slot_showKeys();
        mpEditor->addKey(false);
        QTest::qWait(100ms);
        mpKeyItem = mpEditor->mpCurrentKeyItem;
        QVERIFY2(mpKeyItem != nullptr, "addKey() left no current key item");
    }

    void cleanupTestCase()
    {
        mpKeyItem = nullptr;
        mpShortcutAction = nullptr;
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

    // The button that used to arm the grab is gone, and the field it stood
    // beside does the arming
    void test_clickingTheFieldStartsListening()
    {
        QVERIFY2(mpEditor->mpKeysMainArea->findChild<QWidget*>(qsl("pushButton_key_grabKey")) == nullptr, "the keys form still carries the button that grabbed a key");
        QVERIFY2(!mpEditor->mIsGrabKey, "the editor is listening for a keystroke before anything asked it to");

        armTheGrab();

        QVERIFY2(mpEditor->mIsGrabKey, "clicking the key binding field did not start the grab");
        QCOMPARE(field()->property("editorListening").toBool(), true);
        QCOMPARE(field()->placeholderText(), qsl("Press a key combination"));
        QVERIFY2(field()->text().isEmpty(), "the field still shows the old keystroke while it waits for a new one");
        QCOMPARE(hint()->text(), qsl("Escape keeps the current key"));
        QVERIFY2(mpShortcutAction->shortcut().isEmpty(), "the editor kept its own shortcuts while listening for a keystroke to bind");
    }

    // The keystroke is taken whole: the keypad modifier is what numpad walking
    // is bound through, so narrowing the modifiers would lose it
    void test_theKeystrokeIsStoredWithItsModifiers()
    {
        QVERIFY2(mpEditor->mIsGrabKey, "the grab is not armed, so there is nothing to capture");

        QTest::keyClick(mpEditor, Qt::Key_8, Qt::ControlModifier | Qt::KeypadModifier);
        QCoreApplication::processEvents();

        TKey* pKey = key();
        QVERIFY2(pKey != nullptr, "the key is not in the key unit");
        QCOMPARE(pKey->getKeyCode(), Qt::Key_8);
        QVERIFY2(pKey->getKeyModifiers().testFlag(Qt::KeypadModifier), "the keypad modifier was dropped from the captured keystroke");
        QVERIFY2(pKey->getKeyModifiers().testFlag(Qt::ControlModifier), "the control modifier was dropped from the captured keystroke");
        QVERIFY2(!mpEditor->mIsGrabKey, "the grab is still armed after a keystroke was taken");
        QCOMPARE(field()->text(), mpHost->getKeyUnit()->getKeyName(pKey->getKeyCode(), pKey->getKeyModifiers()));
        QCOMPARE(field()->property("editorListening").toBool(), false);
        QCOMPARE(hint()->text(), qsl("Click to change"));
        QVERIFY2(!clearButton()->isHidden(), "a key with a keystroke is not offered the cross that forgets it");
    }

    // Escape leaves the keystroke as it was, and gives the editor its own
    // shortcuts back
    void test_escapeKeepsTheKeystroke()
    {
        const QString before = field()->text();
        QVERIFY2(!before.isEmpty(), "there is no keystroke to keep");

        armTheGrab();
        QVERIFY2(mpEditor->mIsGrabKey, "clicking the field did not start the grab");
        QTest::keyClick(mpEditor, Qt::Key_Escape);
        QCoreApplication::processEvents();

        QVERIFY2(!mpEditor->mIsGrabKey, "Escape left the editor listening");
        QCOMPARE(field()->text(), before);
        QCOMPARE(field()->property("editorListening").toBool(), false);
        QCOMPARE(mpShortcutAction->shortcut(), mShortcut);
    }

    // ...and so does going somewhere else with the grab still armed, which is
    // the same thing by another route
    void test_leavingTheFieldGivesUpOnTheGrab()
    {
        const QString before = field()->text();
        armTheGrab();
        QVERIFY2(mpEditor->mIsGrabKey, "clicking the field did not start the grab");

        mpEditor->mpKeysMainArea->lineEdit_key_name->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();

        QVERIFY2(!mpEditor->mIsGrabKey, "the grab is still armed after the field lost the keyboard");
        QCOMPARE(field()->text(), before);
        QCOMPARE(field()->property("editorListening").toBool(), false);
        QCOMPARE(mpShortcutAction->shortcut(), mShortcut);
    }

    // The cross takes the keystroke away, and takes itself away with it
    void test_theCrossForgetsTheKeystroke()
    {
        QVERIFY2(!clearButton()->isHidden(), "there is no cross to forget the keystroke with");

        clearButton()->click();
        QCoreApplication::processEvents();

        TKey* pKey = key();
        QVERIFY2(pKey != nullptr, "the key is not in the key unit");
        QCOMPARE(pKey->getKeyCode(), Qt::Key_unknown);
        QVERIFY2(field()->text().isEmpty(), "the field still shows a keystroke the key no longer answers");
        QCOMPARE(field()->placeholderText(), qsl("No key chosen"));
        QCOMPARE(hint()->text(), qsl("Click to set"));
        QVERIFY2(clearButton()->isHidden(), "the cross is still offered for a key with nothing to forget");
    }

    // Saving the item is a way out of a grab too. Nothing on the toolbar takes
    // the keyboard, so the field's own FocusOut never fires - and the name a
    // key with no name of its own is given is read off the very field the grab
    // has emptied.
    void test_savingWhileListeningEndsTheGrab()
    {
        mpEditor->mpKeysMainArea->lineEdit_key_name->setText(qsl("New key"));
        armTheGrab();
        QVERIFY2(mpEditor->mIsGrabKey, "clicking the field did not start the grab");
        QVERIFY2(field()->text().isEmpty(), "the field still shows a keystroke while it waits for one");

        mpEditor->slot_saveSelectedItem();
        QCoreApplication::processEvents();

        QVERIFY2(!mpEditor->mIsGrabKey, "saving the item left the editor listening for a keystroke");
        QCOMPARE(mpShortcutAction->shortcut(), mShortcut);

        // The filter a grab puts on the application takes the arrows wherever
        // in Mudlet they are pressed, so one pressed somewhere else says
        // whether that filter is still there
        QLineEdit elsewhere;
        elsewhere.setText(qsl("abc"));
        elsewhere.setCursorPosition(0);
        QTest::keyClick(&elsewhere, Qt::Key_Right);
        QCOMPARE(elsewhere.cursorPosition(), 1);

        TKey* pKey = key();
        QVERIFY2(pKey != nullptr, "the key is not in the key unit");
        QVERIFY2(!pKey->getName().isEmpty(), "the key was saved with no name at all");
        QVERIFY2(!mpKeyItem->text(0).isEmpty(), "the key's row in the tree is blank");
    }

    // ...and so is going to another view, which the sidebar does without taking
    // the keyboard either
    void test_switchingViewsEndsTheGrab()
    {
        armTheGrab();
        QVERIFY2(mpEditor->mIsGrabKey, "clicking the field did not start the grab");

        mpEditor->slot_showTimers();
        QCoreApplication::processEvents();
        QTest::qWait(50ms);

        QVERIFY2(!mpEditor->mIsGrabKey, "switching views left the editor listening for a keystroke");
        QCOMPARE(mpShortcutAction->shortcut(), mShortcut);

        mpEditor->slot_showKeys();
        mpEditor->slot_keySelected(mpKeyItem);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
    }

    // A key group never matches a keystroke - TKey::match() answers no for a
    // folder - so none of the row is a setting it has, and the row widget goes
    // with the three things on it: left showing, it holds a row of the form's
    // grid open for nothing, which is dead height in a capped column
    void test_aKeyGroupIsOfferedNoneOfTheRow()
    {
        QWidget* pRow = mpEditor->mpKeysMainArea->findChild<QWidget*>(qsl("editorKeyBindingRow"));
        QVERIFY2(pRow != nullptr, "the keys form has no key binding row");
        QVERIFY2(pRow->isVisible(), "the key binding row is not showing for a key that can hold a keystroke");
        // What the row asks for rather than what it was given: the form's own
        // hint is what the two readings below are, and a row stretched to fill
        // the column is not what it costs that hint
        const int rowHeight = pRow->sizeHint().height();
        const int withRow = mpEditor->mpKeysMainArea->sizeHint().height();

        mpEditor->addKey(true);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);

        QVERIFY2(!field()->isVisible(), "a key group is still offered a keystroke it can never match");
        QVERIFY2(!hint()->isVisible(), "a key group is still told how to set a keystroke");
        QVERIFY2(!clearButton()->isVisible(), "a key group is still offered the cross that forgets a keystroke");
        QVERIFY2(!pRow->isVisible(), "the row those three sit on is still showing for a key group");

        const int withoutRow = mpEditor->mpKeysMainArea->sizeHint().height();
        qInfo().noquote()
                << qsl("  the keys form asks for %1 with the key row and %2 without it, the row asking for %3").arg(QString::number(withRow), QString::number(withoutRow), QString::number(rowHeight));
        QVERIFY2(withRow - withoutRow > rowHeight,
                 qPrintable(qsl("the form gave back only %1 for a key group while the row it stopped showing asks for %2 - the grid row it stood in is still open")
                                    .arg(QString::number(withRow - withoutRow), QString::number(rowHeight))));
    }
};

#include "EditorKeyCaptureTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorKeyCaptureTest)
