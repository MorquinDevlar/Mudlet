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
 * A trigger's options are one row of its form, and the Options button at the
 * right end of the head row is what opens and closes it. That is a disclosure
 * rather than a preference: the editor opens with the row closed every time,
 * whatever the last session left it at, and whatever an older configuration
 * stored under showAllTriggerControls - a key nothing reads any more and which
 * is cleared on write rather than left behind.
 *
 * The key is seeded to true here before the editor is built, which is what such
 * a configuration carries, so that a default that answered to it would show.
 *
 * What is held here:
 *
 * - The strip and the word leading it are away when the editor opens, and the
 *   button is there and unchecked.
 * - The button stands at the end of the head row, level with the command field
 *   and at its height.
 * - With the strip away the pattern list follows the head row at the grid's own
 *   spacing, so the hidden row costs no gap.
 * - The button opens and closes the strip, and the pattern list moves with it.
 * - The choice is kept for the session: it survives another trigger being
 *   chosen and the view being switched away and back.
 * - Writing the settings leaves no showAllTriggerControls behind.
 *
 * Run with: ctest -R EditorOptionsPanelDefaultTest -V
 */

#include <QGridLayout>
#include <QLineEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest/QtTest>
#include <chrono>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgTriggerEditor.h"
#include "dlgTriggersMainArea.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorOptionsPanelDefaultTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorOptionsPanelDefault-Test-Profile");
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

    void settle()
    {
        QCoreApplication::processEvents();
        QTest::qWait(80ms);
        QCoreApplication::processEvents();
    }

    dlgTriggersMainArea* form() const { return mpEditor->mpTriggersMainArea; }

    QToolButton* optionsButton() const { return form()->toolButton_toggleExtraControls; }

    int topEdgeOf(const QWidget* pWidget) const { return pWidget->mapTo(mpEditor, QPoint(0, 0)).y(); }

    int bottomEdgeOf(const QWidget* pWidget) const { return topEdgeOf(pWidget) + pWidget->height(); }

    int leftEdgeOf(const QWidget* pWidget) const { return pWidget->mapTo(mpEditor, QPoint(0, 0)).x(); }

    int rightEdgeOf(const QWidget* pWidget) const { return leftEdgeOf(pWidget) + pWidget->width(); }

    int verticalCentreOf(const QWidget* pWidget) const { return pWidget->mapTo(mpEditor, pWidget->rect().center()).y(); }

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

        // What a configuration written by a version that stored the strip's
        // state carries. Seeded before the profile is started rather than before
        // the editor is asked for: loading a profile builds its editor, so this
        // is the last moment the editor has not read its settings yet. The
        // window is given room for the strip in the same breath, so that what
        // is measured below is a choice rather than a want of room.
        mudlet::getQSettings()->setValue(qsl("showAllTriggerControls"), true);
        mudlet::getQSettings()->setValue(qsl("script_editor_size"), QSize(1100, 900));
        mudlet::getQSettings()->sync();

        startProfile(mProfileName, mLocalhost, mPort);
        if (QTest::currentTestFailed()) {
            return;
        }

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(1000, 800);
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

    // The editor opens with the options away whatever the stored key said
    void test_theOptionsOpenClosedDespiteTheStoredSetting()
    {
        QVERIFY2(mpEditor->mpWidget_triggerOptionsRow != nullptr, "the trigger form has no options strip");
        QVERIFY2(!mpEditor->mpWidget_triggerOptionsRow->isVisible(), "the options strip is on show in a freshly opened editor");
        QVERIFY2(!mpEditor->mpLabel_optionsRow->isVisible(), "the word leading the options strip is on show with nothing beside it");
        QVERIFY2(optionsButton()->isVisible(), "the Options button is not on the head row, so there is no way to open the strip");
        QVERIFY2(!optionsButton()->isChecked(), "the Options button says the strip is open where it is not");
        QVERIFY2(!mpEditor->mShowAllTriggerControls, "the editor is holding the strip as open in a session that has not opened it");
    }

    // It stands at the end of the row the name and the command are typed on,
    // level with the field beside it
    void test_theButtonSitsOnTheHeadRowInLineWithTheCommandField()
    {
        QLineEdit* pCommand = form()->lineEdit_trigger_command;
        qInfo().noquote() << qsl("  the button is %1px tall at y=%2..%3, the command field %4px at y=%5..%6")
                                     .arg(optionsButton()->height())
                                     .arg(topEdgeOf(optionsButton()))
                                     .arg(bottomEdgeOf(optionsButton()))
                                     .arg(pCommand->height())
                                     .arg(topEdgeOf(pCommand))
                                     .arg(bottomEdgeOf(pCommand));

        QCOMPARE(optionsButton()->height(), pCommand->height());
        QVERIFY2(std::abs(verticalCentreOf(optionsButton()) - verticalCentreOf(pCommand)) <= 1,
                 qPrintable(qsl("the button's middle is at %1 and the command field's at %2, so the two are not on one line").arg(verticalCentreOf(optionsButton())).arg(verticalCentreOf(pCommand))));

        QVERIFY2(leftEdgeOf(optionsButton()) > rightEdgeOf(pCommand),
                 qPrintable(qsl("the button starts at x=%1, which is not past the command field's end at x=%2").arg(leftEdgeOf(optionsButton())).arg(rightEdgeOf(pCommand))));
        QVERIFY2(std::abs(rightEdgeOf(optionsButton()) - rightEdgeOf(form()->widget_top)) <= 1,
                 qPrintable(qsl("the button ends at x=%1 and the head row at x=%2, so it is not the last thing on the row").arg(rightEdgeOf(optionsButton())).arg(rightEdgeOf(form()->widget_top))));

        QCOMPARE(optionsButton()->text(), qsl("Options"));
    }

    // A grid row with nothing on it takes no height and no spacing, so the
    // patterns sit exactly one gap under the head row while the strip is away
    void test_withTheStripAwayThePatternsFollowTheHeadRowAtTheGridsSpacing()
    {
        mpEditor->setTriggerOptionsShown(false);
        settle();

        auto* pGrid = qobject_cast<QGridLayout*>(form()->layout());
        QVERIFY2(pGrid != nullptr, "the trigger form is not laid out in a grid");
        const int gap = topEdgeOf(form()->widget_left) - bottomEdgeOf(form()->widget_top);
        qInfo().noquote() << qsl("  the head row ends at y=%1, the patterns start at y=%2, a gap of %3 where the grid's spacing is %4")
                                     .arg(bottomEdgeOf(form()->widget_top))
                                     .arg(topEdgeOf(form()->widget_left))
                                     .arg(gap)
                                     .arg(pGrid->verticalSpacing());
        QCOMPARE(gap, pGrid->verticalSpacing());
    }

    // The button is the whole of what opens and closes the strip
    void test_theButtonOpensAndClosesTheStrip()
    {
        mpEditor->setTriggerOptionsShown(false);
        settle();
        const int patternsWhenAway = topEdgeOf(form()->widget_left);

        QTest::mouseClick(optionsButton(), Qt::LeftButton, Qt::NoModifier, optionsButton()->rect().center());
        settle();
        QVERIFY2(mpEditor->mpWidget_triggerOptionsRow->isVisible(), "clicking the Options button did not put the strip on show");
        QVERIFY2(mpEditor->mpLabel_optionsRow->isVisible(), "the strip is on show without the word leading it");
        QVERIFY2(optionsButton()->isChecked(), "the button does not say the strip it opened is open");
        QVERIFY2(mpEditor->mShowAllTriggerControls, "the click did not leave the session holding the strip as open");
        const int patternsWhenShown = topEdgeOf(form()->widget_left);
        qInfo().noquote() << qsl("  the patterns start at y=%1 with the strip away and y=%2 with it on show").arg(patternsWhenAway).arg(patternsWhenShown);
        QVERIFY2(patternsWhenShown > patternsWhenAway,
                 qPrintable(qsl("the pattern list is at y=%1 with the strip on show, where it was at y=%2 without it - the strip is taking no room").arg(patternsWhenShown).arg(patternsWhenAway)));

        QTest::mouseClick(optionsButton(), Qt::LeftButton, Qt::NoModifier, optionsButton()->rect().center());
        settle();
        QVERIFY2(!mpEditor->mpWidget_triggerOptionsRow->isVisible(), "clicking the Options button again did not put the strip away");
        QVERIFY2(!mpEditor->mpLabel_optionsRow->isVisible(), "the word leading the strip stayed behind after the strip went");
        QVERIFY2(!optionsButton()->isChecked(), "the button says the strip is open after it closed it");
        QVERIFY2(!mpEditor->mShowAllTriggerControls, "closing the strip left the session still holding it as open");
        QCOMPARE(topEdgeOf(form()->widget_left), patternsWhenAway);
    }

    // Nothing is stored, but nothing puts it back either: within the one
    // session the strip stays where it was put
    void test_theChoiceIsKeptForTheSession()
    {
        mpEditor->setTriggerOptionsShown(true);
        settle();

        mpEditor->addTrigger(false);
        settle();
        QVERIFY2(mpEditor->mpWidget_triggerOptionsRow->isVisible(), "choosing another trigger put the options strip away");
        QVERIFY2(optionsButton()->isChecked(), "choosing another trigger left the button saying the strip is closed");

        mpEditor->slot_showAliases();
        mpEditor->slot_showTriggers();
        settle();
        QVERIFY2(mpEditor->mpWidget_triggerOptionsRow->isVisible(), "leaving the Triggers view and coming back put the options strip away");
        QVERIFY2(optionsButton()->isChecked(), "coming back to the Triggers view left the button saying the strip is closed");

        mpEditor->setTriggerOptionsShown(false);
        settle();
    }

    // Nothing writes the retired key back, and opening the editor clears it
    void test_writingSettingsLeavesNoRetiredKey()
    {
        mpEditor->writeSettings();
        QVERIFY2(!mudlet::getQSettings()->contains(qsl("showAllTriggerControls")),
                 qPrintable(qsl("the editor still keeps showAllTriggerControls, which it is now %1").arg(mudlet::getQSettings()->value(qsl("showAllTriggerControls")).toString())));
    }
};

#include "EditorOptionsPanelDefaultTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorOptionsPanelDefaultTest)
