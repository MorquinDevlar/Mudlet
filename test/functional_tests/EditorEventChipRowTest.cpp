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
 * The events a script is registered for, drawn as a row of chips: what the row
 * shows, what typing in it does, and what its height does to the column it is
 * on.
 *
 * The old pair of controls - a list in a box, a field under it and a "+" and a
 * "-" beside them - is gone, so the first case also holds them gone: a form
 * still carrying them would pass every other case here while showing the user
 * two ways to do the same thing.
 *
 * Run with: ctest -R EditorEventChipRowTest -V
 */

#include <QFontInfo>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest/QtTest>
#include <chrono>

#include "ChipRow.h"
#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "ScriptUnit.h"
#include "TScript.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgScriptsMainArea.h"
#include "dlgSourceEditorArea.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorEventChipRowTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    QTreeWidgetItem* mpScriptItem = nullptr;
    const QString mProfileName = qsl("EditorEventChipRow-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    // The three the script is opened with, and the fourth typed into the field
    static QStringList startingEvents() { return {qsl("sysConnectionEvent"), qsl("MyEvent"), qsl("OneMoreEvent")}; }

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

    uiDesign::ChipRow* row() const { return mpEditor->mpChipRow_scriptEvents; }
    // The field is only there while a name is being typed, so this answers null
    // for "closed" rather than for "missing"
    QLineEdit* field() const
    {
        QLineEdit* pField = row()->findChild<QLineEdit*>(qsl("editorChipEditor"));
        return pField && !pField->isHidden() ? pField : nullptr;
    }
    QToolButton* addButton() const { return row()->findChild<QToolButton*>(qsl("editorChipAdd")); }
    QLabel* note() const
    {
        QLabel* pNote = row()->findChild<QLabel*>(qsl("editorChipNote"));
        return pNote && !pNote->isHidden() ? pNote : nullptr;
    }

    QStringList savedEvents() const
    {
        if (!mpScriptItem) {
            return {};
        }
        TScript* pScript = mpHost->getScriptUnit()->getScript(mpScriptItem->data(0, Qt::UserRole).toInt());
        return pScript ? pScript->getEventHandlerList() : QStringList{};
    }

    // What the chips read, in the order they are shown, taken off the widgets
    // rather than off the row's own list
    QStringList shownEvents() const
    {
        QStringList shown;
        for (int i = 0; i < row()->count(); ++i) {
            QWidget* pChip = row()->chipAt(i);
            if (!pChip) {
                continue;
            }
            if (QLabel* pLabel = pChip->findChild<QLabel*>(); pLabel) {
                shown << pLabel->text();
            }
        }
        return shown;
    }

    void typeIntoField(const QString& text)
    {
        QLineEdit* pField = field();
        if (!pField) {
            QTest::qFail("the field is not open, so there is nothing to type into", __FILE__, __LINE__);
            return;
        }
        pField->setText(text);
        QTest::keyClick(pField, Qt::Key_Return);
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

        mudlet::self()->slot_showScriptDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(1100, 900);
        mpEditor->slot_showScripts();
        mpEditor->addScript(false);
        QTest::qWait(100ms);

        mpScriptItem = mpEditor->mpCurrentScriptItem;
        QVERIFY2(mpScriptItem != nullptr, "addScript() left no current script item");

        // Put the events on the script itself and re-select it, so the row is
        // filled the way it is for any script a profile was loaded with
        TScript* pScript = mpHost->getScriptUnit()->getScript(mpScriptItem->data(0, Qt::UserRole).toInt());
        QVERIFY2(pScript != nullptr, "the new script is not in the script unit");
        pScript->setEventHandlerList(startingEvents());
        mpEditor->slot_scriptsSelected(mpScriptItem);
        QTest::qWait(50ms);

        // Named and saved after that reselect, which fills the form from the
        // script and would put the old name back over anything typed before it.
        // The search below looks the script up by the name on its tree row.
        mpEditor->mpScriptsMainArea->lineEdit_script_name->setText(qsl("ChipRowScript"));
        mpEditor->slot_saveSelectedItem();
        QTest::qWait(50ms);
        QCOMPARE(mpScriptItem->text(0), qsl("ChipRowScript"));
    }

    void cleanupTestCase()
    {
        mpScriptItem = nullptr;
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

    // What the script is registered for is what the row shows, in that order -
    // and the controls it used to be shown through are not on the form any more
    void test_theScriptsEventsAreItsChips()
    {
        QVERIFY2(row() != nullptr, "the scripts form has no row of event chips");
        QCOMPARE(row()->count(), 3);
        QCOMPARE(shownEvents(), startingEvents());
        QCOMPARE(row()->items(), startingEvents());

        for (const QString& gone : {qsl("listWidget_script_registered_event_handlers"),
                                    qsl("lineEdit_script_event_handler_entry"),
                                    qsl("toolButton_script_add_event_handler"),
                                    qsl("toolButton_script_remove_event_handler"),
                                    qsl("label_script_event_handler_entry")}) {
            QVERIFY2(mpEditor->mpScriptsMainArea->findChild<QWidget*>(gone) == nullptr, qPrintable(qsl("the scripts form still carries %1").arg(gone)));
        }
        QVERIFY2(addButton() != nullptr, "the row has no button to add an event with");
        QVERIFY2(field() == nullptr, "the row opens with its field already showing");
    }

    // A name typed into the field is a fourth chip, and saving the script puts
    // all four on it
    void test_typingANameAddsAChip()
    {
        row()->beginAdd();
        QCoreApplication::processEvents();
        QVERIFY2(field() != nullptr, "beginAdd() did not open the field");
        QVERIFY2(addButton()->isHidden(), "the add button is still showing beside the field it opened");

        typeIntoField(qsl("FourthEvent"));

        QCOMPARE(row()->count(), 4);
        QCOMPARE(row()->items().at(3), qsl("FourthEvent"));

        mpEditor->saveScript();
        QTest::qWait(50ms);
        QCOMPARE(savedEvents(), startingEvents() << qsl("FourthEvent"));
    }

    // A name the script already has is refused with a note rather than silently,
    // and the field is left open on what was typed
    void test_aNameAlreadyListedIsRefused()
    {
        QVERIFY2(field() != nullptr, "the field closed after the last name was taken, so there is nothing to type the duplicate into");
        QSignalSpy refusals(row(), &uiDesign::ChipRow::duplicateRefused);

        typeIntoField(qsl("MyEvent"));

        QCOMPARE(row()->count(), 4);
        QCOMPARE(refusals.count(), 1);
        QLabel* pNote = note();
        QVERIFY2(pNote != nullptr, "nothing said why the name was not taken");
        QVERIFY2(pNote->text().contains(qsl("MyEvent")), qPrintable(qsl("the note does not name what was refused: %1").arg(pNote->text())));
        QVERIFY2(field() != nullptr, "the field was closed on a name it refused, so the typing is lost");
    }

    // Escape gives up on the name being typed and puts the button back
    void test_escapeClosesTheField()
    {
        QVERIFY2(field() != nullptr, "the field is not open, so there is nothing for Escape to close");
        const int before = row()->count();

        QTest::keyClick(field(), Qt::Key_Escape);
        QCoreApplication::processEvents();

        QVERIFY2(field() == nullptr, "Escape left the field open");
        QVERIFY2(!addButton()->isHidden(), "Escape closed the field without putting the add button back");
        QCOMPARE(row()->count(), before);
    }

    // The cross on a chip takes that event off the script
    void test_theCrossTakesAnEventAway()
    {
        QCOMPARE(row()->count(), 4);
        QWidget* pChip = row()->chipAt(1);
        QVERIFY2(pChip != nullptr, "the row has no second chip to take away");
        QCOMPARE(pChip->findChild<QLabel*>()->text(), qsl("MyEvent"));

        QToolButton* pCross = pChip->findChild<QToolButton*>(qsl("editorChipRemove"));
        QVERIFY2(pCross != nullptr, "a chip has no cross to take it away with");
        pCross->click();
        QCoreApplication::processEvents();

        QCOMPARE(row()->count(), 3);
        QVERIFY2(!row()->items().contains(qsl("MyEvent")), "the event the cross was clicked on is still listed");
        QCOMPARE(row()->items(), QStringList({qsl("sysConnectionEvent"), qsl("OneMoreEvent"), qsl("FourthEvent")}));
    }

    // A search result for an event handler jumps to the chip showing it
    void test_theSearchJumpPointsAtTheChip()
    {
        // Somewhere else first, or the chip the last case left the keyboard on
        // would answer this whether the jump pointed at anything or not
        row()->focusItem(0);
        QCoreApplication::processEvents();
        QCOMPARE(row()->focusWidget(), row()->chipAt(0));

        QTreeWidgetItem* pResult = new QTreeWidgetItem(QStringList{qsl("ChipRowScript")});
        pResult->setData(0, dlgTriggerEditor::ItemRole, static_cast<int>(EditorViewType::cmScriptView));
        pResult->setData(0, dlgTriggerEditor::TypeRole, dlgTriggerEditor::SearchResultIsEventHandler);
        pResult->setData(0, dlgTriggerEditor::NameRole, qsl("ChipRowScript"));
        pResult->setData(0, dlgTriggerEditor::IdRole, mpScriptItem->data(0, Qt::UserRole).toInt());
        // The second of the three the row is showing now
        pResult->setData(0, dlgTriggerEditor::PatternOrLineRole, 1);
        mpEditor->treeWidget_searchResults->addTopLevelItem(pResult);

        mpEditor->slot_itemSelectedInSearchResults(pResult);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);

        // focusWidget() rather than hasFocus(): the latter also asks whether the
        // window is the active one, which a test run headlessly cannot promise
        // Jumping to the script fills the row from what was saved on it, so the
        // event taken off above is back - and index 1 is a name again
        QVERIFY2(row()->count() > 1, "the jump left the row with fewer chips than the result pointed into");
        QCOMPARE(row()->focusWidget(), row()->chipAt(1));
    }

    // A row too narrow for its chips wraps, and the column the form is in takes
    // the height that costs off the code pane rather than clipping it
    void test_wrappingGrowsTheFormColumn()
    {
        mpEditor->resize(1100, 900);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);
        const int oneLine = mpEditor->mpNonCodeWidgets->height();
        const int codeOneLine = mpEditor->mpSourceEditorArea->height();
        const int lineHeight = row()->lineHeight();
        QVERIFY2(row()->height() < 2 * lineHeight,
                 qPrintable(qsl("the chips already take more than one line at 1100px, so this case has nothing to widen from - the row is %1 tall").arg(row()->height())));

        mpEditor->resize(620, 900);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);

        const int wrapped = mpEditor->mpNonCodeWidgets->height();
        qInfo().noquote() << qsl("  the column is %1 on one line of chips and %2 on two, a chip line being %3").arg(QString::number(oneLine), QString::number(wrapped), QString::number(lineHeight));
        QVERIFY2(row()->height() >= 2 * lineHeight,
                 qPrintable(qsl("the chips did not wrap at 620px - the row is %1 tall and a line of it is %2").arg(QString::number(row()->height()), QString::number(lineHeight))));
        QVERIFY2(wrapped >= oneLine + lineHeight,
                 qPrintable(qsl("the form column stayed at %1 while the chips wrapped onto a second line, up from %2").arg(QString::number(wrapped), QString::number(oneLine))));
        QVERIFY2(mpEditor->mpSourceEditorArea->height() < codeOneLine,
                 qPrintable(qsl("the code pane is still %1 tall, so the room the second line of chips took came from somewhere else").arg(QString::number(mpEditor->mpSourceEditorArea->height()))));

        mpEditor->resize(1100, 900);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);
    }

    // The cap on the column comes down as well as up: a script with nothing in
    // its row gives back the room the last one's chips took
    void test_fewerEventsGiveTheRoomBack()
    {
        mpEditor->resize(620, 900);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);
        mpEditor->hideSystemMessageArea();
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        QVERIFY2(row()->count() >= 3, "the script under test has lost the events this case needs it to be showing");
        const int withChips = mpEditor->mpNonCodeWidgets->height();

        mpEditor->addScript(false);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);
        mpEditor->hideSystemMessageArea();
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        QCOMPARE(row()->count(), 0);
        const int withNone = mpEditor->mpNonCodeWidgets->height();
        qInfo().noquote() << qsl("  the column is %1 with the chips and %2 without them").arg(QString::number(withChips), QString::number(withNone));
        QVERIFY2(withNone < withChips, qPrintable(qsl("the column stayed at %1 for a script with no events, which is what the last one's chips needed").arg(QString::number(withNone))));

        mpEditor->treeWidget_scripts->setCurrentItem(mpScriptItem);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);
        mpEditor->hideSystemMessageArea();
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        QVERIFY2(row()->count() >= 3, "the script's events did not come back with it");
        QCOMPARE(mpEditor->mpNonCodeWidgets->height(), withChips);

        mpEditor->resize(1100, 900);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);
    }

    // The cross takes no focus, so a name half typed into the open field is
    // settled by the removal itself rather than by whatever the focus lands on
    // afterwards - which is nothing at all when the row is emptied
    void test_removingAChipSettlesTheOpenFieldFirst()
    {
        row()->setItems({qsl("AlphaEvent"), qsl("BetaEvent")});
        QCoreApplication::processEvents();
        QSignalSpy changes(row(), &uiDesign::ChipRow::itemsChanged);

        row()->beginAdd();
        QCoreApplication::processEvents();
        QVERIFY2(field() != nullptr, "beginAdd() did not open the field");
        field()->setText(qsl("GammaEvent"));

        QWidget* pChip = row()->chipAt(1);
        QVERIFY2(pChip != nullptr, "the row has no second chip to take away");
        QToolButton* pCross = pChip->findChild<QToolButton*>(qsl("editorChipRemove"));
        QVERIFY2(pCross != nullptr, "a chip has no cross to take it away with");
        pCross->click();
        QCoreApplication::processEvents();

        QCOMPARE(row()->items(), QStringList({qsl("AlphaEvent"), qsl("GammaEvent")}));
        QCOMPARE(changes.count(), 2);
        QVERIFY2(field() == nullptr, "the field is still open after a chip was taken away");
        QVERIFY2(!addButton()->isHidden(), "the add button did not come back when the field closed");

        // ...and again with one chip, where taking it away leaves nothing for
        // the keyboard to move to and so nothing to commit the name in passing
        row()->setItems({qsl("AlphaEvent")});
        QCoreApplication::processEvents();
        changes.clear();

        row()->beginAdd();
        QCoreApplication::processEvents();
        QVERIFY2(field() != nullptr, "beginAdd() did not open the field");
        field()->setText(qsl("DeltaEvent"));

        QToolButton* pOnlyCross = row()->chipAt(0)->findChild<QToolButton*>(qsl("editorChipRemove"));
        QVERIFY2(pOnlyCross != nullptr, "the one chip has no cross to take it away with");
        pOnlyCross->click();
        QCoreApplication::processEvents();

        QCOMPARE(row()->items(), QStringList{qsl("DeltaEvent")});
        QCOMPARE(changes.count(), 2);
        QVERIFY2(field() == nullptr, "the field is still open after the last chip was taken away");
        QVERIFY2(!addButton()->isHidden(), "the add button did not come back when the field closed");
    }

    // A name already listed cannot be taken, and on the way out there is nobody
    // left to type it over - so the field goes with the refusal rather than
    // standing open and unfocused over the button that reopens it
    void test_aRefusedNameOnTheWayOutClosesTheField()
    {
        row()->setItems({qsl("sysLoadEvent")});
        QCoreApplication::processEvents();
        const int before = row()->count();
        QSignalSpy refusals(row(), &uiDesign::ChipRow::duplicateRefused);

        row()->beginAdd();
        QCoreApplication::processEvents();
        QVERIFY2(field() != nullptr, "beginAdd() did not open the field");
        field()->setText(qsl("sysLoadEvent"));

        mpEditor->mpScriptsMainArea->lineEdit_script_name->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();

        QCOMPARE(refusals.count(), 1);
        QVERIFY2(field() == nullptr, "the field was left open on a name that was refused, with nothing left to type over it");
        QVERIFY2(!addButton()->isHidden(), "the add button is still hidden behind a field that has been refused");
        QVERIFY2(note() != nullptr, "nothing is left saying why the name was not taken");
        QCOMPARE(row()->count(), before);
    }

    // The search counts through the script's own list, which can hold the same
    // name twice - the row draws it once. So the jump goes by the name rather
    // than by the number the search counted to.
    void test_theSearchJumpFindsTheChipByName()
    {
        mpEditor->addScript(false);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);
        QTreeWidgetItem* pOtherItem = mpEditor->mpCurrentScriptItem;
        QVERIFY2(pOtherItem != nullptr && pOtherItem != mpScriptItem, "addScript() left no second script to search into");
        mpEditor->mpScriptsMainArea->lineEdit_script_name->setText(qsl("SearchJumpScript"));
        mpEditor->slot_saveSelectedItem();
        QTest::qWait(50ms);
        QCOMPARE(pOtherItem->text(0), qsl("SearchJumpScript"));
        const int otherID = pOtherItem->data(0, Qt::UserRole).toInt();

        // Back to the first script before the list is written, so that leaving
        // the second one is not what saves the row's chips over it
        mpEditor->slot_scriptsSelected(mpScriptItem);
        QTest::qWait(50ms);
        TScript* pOther = mpHost->getScriptUnit()->getScript(otherID);
        QVERIFY2(pOther != nullptr, "the second script is not in the script unit");
        pOther->setEventHandlerList({qsl("MyEvent"), qsl("MyEvent"), qsl("OtherEvent")});
        QCOMPARE(pOther->getEventHandlerList().count(), 3);

        // Something inside the row holds the keyboard first, or a jump that
        // pointed at nothing would leave the answer below to whatever had it
        addButton()->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
        QCOMPARE(row()->focusWidget(), addButton());

        auto* pResult = new QTreeWidgetItem(QStringList{qsl("SearchJumpScript")});
        pResult->setData(0, dlgTriggerEditor::ItemRole, static_cast<int>(EditorViewType::cmScriptView));
        pResult->setData(0, dlgTriggerEditor::TypeRole, dlgTriggerEditor::SearchResultIsEventHandler);
        pResult->setData(0, dlgTriggerEditor::NameRole, qsl("SearchJumpScript"));
        pResult->setData(0, dlgTriggerEditor::IdRole, otherID);
        // Where OtherEvent stands in the script's own list, which is one along
        // from where it stands in the row
        pResult->setData(0, dlgTriggerEditor::PatternOrLineRole, 2);
        mpEditor->treeWidget_searchResults->addTopLevelItem(pResult);

        mpEditor->slot_itemSelectedInSearchResults(pResult);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);

        QCOMPARE(row()->items(), QStringList({qsl("MyEvent"), qsl("OtherEvent")}));
        QCOMPARE(row()->focusWidget(), row()->chipAt(1));
    }

    // A chip is as tall as the type in it, and the word leading the row is
    // pinned to one chip's height - both measured off the interface font rather
    // than written down, so both have to be measured again when it changes.
    //
    // The change is handed to the row and to the window rather than set on the
    // application: trigger_editor.ui pins a font on the frame these forms are
    // in, and Qt does not carry a font past a widget a stylesheet has been
    // applied to - so an application font never reaches either of them by
    // itself. What is walked here is that each of the two answers the change it
    // does hear.
    void test_aLargerFontReMeasuresTheChips()
    {
        row()->setItems({qsl("AlphaEvent"), qsl("BetaEvent")});
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        QCOMPARE(row()->count(), 2);

        QLabel* pLead = mpEditor->mpScriptsMainArea->label_script_registered_event_handlers;
        const int wasLine = row()->lineHeight();
        const QFont was = row()->font();

        QFont bigger = was;
        bigger.setPointSizeF(QFontInfo(was).pointSizeF() + 4.0);
        row()->setFont(bigger);
        mpEditor->setFont(bigger);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);

        const int line = row()->lineHeight();
        QVERIFY2(line > wasLine, qPrintable(qsl("a font four points larger left a line of chips at %1, which is what it was").arg(QString::number(line))));
        for (int i = 0; i < row()->count(); ++i) {
            QVERIFY2(row()->chipAt(i)->height() == line,
                     qPrintable(qsl("chip %1 is %2 tall while a line of chips is now %3").arg(QString::number(i), QString::number(row()->chipAt(i)->height()), QString::number(line))));
        }
        QVERIFY2(addButton()->height() == line, qPrintable(qsl("the button that adds an event is %1 tall while a chip is now %2").arg(QString::number(addButton()->height()), QString::number(line))));
        QVERIFY2(pLead->height() == line,
                 qPrintable(qsl("the word leading the row is %1 tall while a line of chips is now %2, so it no longer sits level with the first of them")
                                    .arg(QString::number(pLead->height()), QString::number(line))));

        row()->setFont(was);
        mpEditor->setFont(was);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);
    }
};

#include "EditorEventChipRowTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorEventChipRowTest)
