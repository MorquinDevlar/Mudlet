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
 * The editor's own shortcuts across a key grab. A grab takes them away so that
 * the keystroke being bound reaches the grab rather than the action it would
 * otherwise fire, and ending the grab - by Escape or by taking a keystroke -
 * has to give every one of them back.
 *
 * It used to give back only the shortcuts a label-keyed table listed, so
 * Ctrl+N to add an item, and the others set directly on their actions, were
 * gone for the rest of the session after the first grab.
 *
 * Run with: ctest -R EditorKeyGrabShortcutsTest -V
 */

#include <QAction>
#include <QHash>
#include <QKeySequence>
#include <QToolBar>
#include <QtTest/QtTest>
#include <chrono>

#include "Host.h"
#include "HostDialogs.h"
#include "KeyUnit.h"
#include "ProfileSessionTestHelper.h"
#include "TKey.h"
#include "dlgKeysMainArea.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorKeyGrabShortcutsTest : public QObject
{
    Q_OBJECT

private:
    TestProfile::Session mSession;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    // Every toolbar action's shortcut as it stood before any grab, so that what
    // a grab gives back can be held against all of them
    QHash<QAction*, QKeySequence> mShortcutsBeforeTheGrab;
    const QString mProfileName = qsl("EditorKeyGrabShortcuts-Test-Profile");

    // Add Item and its like carry no text at all, only the icon and the tooltip
    QString actionLabel(const QAction* pAction) const { return pAction->text().isEmpty() ? pAction->toolTip() : pAction->text(); }

    void armTheGrab()
    {
        // The redesign replaced the Grab Key button with the binding field
        // itself: a click on it is what arms the grab
        QTest::mouseClick(mpEditor->mpKeysMainArea->lineEdit_key_binding, Qt::LeftButton);
        QCoreApplication::processEvents();
    }

    QString shortcutsNotGivenBack() const
    {
        QStringList lines;
        for (auto it = mShortcutsBeforeTheGrab.cbegin(); it != mShortcutsBeforeTheGrab.cend(); ++it) {
            const QKeySequence now = it.key()->shortcut();
            if (now == it.value()) {
                continue;
            }
            lines << qsl("%1: had %2, now %3").arg(actionLabel(it.key()), it.value().toString(QKeySequence::PortableText), now.isEmpty() ? qsl("nothing") : now.toString(QKeySequence::PortableText));
        }
        return lines.join(qsl("\n"));
    }

private slots:
    void initTestCase()
    {
        if (TestProfile::Session::configCannotBeRedirected()) {
            QSKIP(TestProfile::Session::configCannotBeRedirectedReason());
        }

        QVERIFY2(mSession.start(mProfileName), qPrintable(mSession.why()));
        mpHost = mSession.host();

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = HostDialogs::of(mpHost).mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(1100, 900);

        // The toolbar and the view switchers, which the redesign moved off a
        // toolbar of their own - the same set the grab suspends
        for (QAction* pAction : mpEditor->toolbarActions()) {
            mShortcutsBeforeTheGrab.insert(pAction, pAction->shortcut());
        }

        QVERIFY2(mShortcutsBeforeTheGrab.contains(mpEditor->mAddItem), "Add Item is not on the editor's toolbar, so the shortcut this test watches is not among the recorded ones");
        QCOMPARE(mpEditor->mAddItem->shortcut(), QKeySequence(QKeySequence::New));
        // Ctrl+S used to come from a label-keyed table, which never matched in
        // a translated UI; it is now set on the action itself
        QCOMPARE(mpEditor->mSaveItem->shortcut(), QKeySequence(QKeySequence::Save));

        mpEditor->slot_showKeys();
        mpEditor->addKey(false);
        QTest::qWait(100ms);
        QVERIFY2(mpEditor->mpCurrentKeyItem != nullptr, "addKey() left no current key item");
    }

    void cleanupTestCase()
    {
        mShortcutsBeforeTheGrab.clear();
        mpEditor = nullptr;
        mpHost = nullptr;
        mSession.stop();
    }

    void test_escapeGivesEveryShortcutBack()
    {
        armTheGrab();
        QVERIFY2(mpEditor->mIsGrabKey, "clicking Grab Key did not start the grab");

        for (auto it = mShortcutsBeforeTheGrab.cbegin(); it != mShortcutsBeforeTheGrab.cend(); ++it) {
            if (it.value().isEmpty()) {
                continue;
            }
            QVERIFY2(it.key()->shortcut().isEmpty(), qPrintable(qsl("%1 kept its shortcut while the editor listened for a keystroke to bind").arg(actionLabel(it.key()))));
        }

        QTest::keyClick(mpEditor, Qt::Key_Escape);
        QCoreApplication::processEvents();
        QVERIFY2(!mpEditor->mIsGrabKey, "Escape left the editor listening for a keystroke");

        const QString report = shortcutsNotGivenBack();
        QVERIFY2(report.isEmpty(), qPrintable(report));
    }

    void test_aCapturedKeystrokeGivesEveryShortcutBack()
    {
        armTheGrab();
        QVERIFY2(mpEditor->mIsGrabKey, "clicking Grab Key did not start the grab");

        QTest::keyClick(mpEditor, Qt::Key_F7, Qt::ControlModifier);
        QCoreApplication::processEvents();
        QVERIFY2(!mpEditor->mIsGrabKey, "the grab is still armed after a keystroke was taken");

        // The keystroke reaching the key proves it was the capturing branch of
        // event(), not the Escape one, that ended this grab
        TKey* pKey = mpHost->getKeyUnit()->getKey(mpEditor->mpCurrentKeyItem->data(0, Qt::UserRole).toInt());
        QVERIFY2(pKey != nullptr, "the key is not in the key unit");
        QCOMPARE(pKey->getKeyCode(), Qt::Key_F7);

        const QString report = shortcutsNotGivenBack();
        QVERIFY2(report.isEmpty(), qPrintable(report));
    }
};

#include "EditorKeyGrabShortcutsTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorKeyGrabShortcutsTest)
