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
 * The trigger form's options panel is a disclosure rather than a preference: it
 * is closed every time the editor opens, whatever the last session did with it,
 * and the strip that summarises it stands in for it until the reader asks for it
 * back.
 *
 * It used to be stored under showAllTriggerControls, so an editor closed with
 * the panel open reopened with four cards of options over the form and the code
 * pane pushed down under them. The key is seeded here before the editor is
 * built, which is what a configuration written by any earlier version carries.
 *
 * Run with: ctest -R EditorOptionsPanelDefaultTest -V
 */

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

    QToolButton* toggle() const { return mpEditor->mpTriggersMainArea->toolButton_toggleExtraControls; }
    QWidget* optionsPanel() const { return mpEditor->mpTriggersMainArea->widget_right; }
    QWidget* summaryStrip() const { return mpEditor->mpButton_triggerOptionsSummary; }

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

        // What a configuration written by a version that stored the panel's
        // state carries. Seeded before the profile is started rather than before
        // the editor is asked for: loading a profile builds its editor, so this
        // is the last moment the editor has not read its settings yet. The
        // window is given room for the panel in the same breath, so that a
        // panel on show is not folded away again for want of it.
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

    void test_theOptionsPanelOpensClosedDespiteTheStoredSetting()
    {
        QVERIFY2(mudlet::getQSettings()->value(qsl("showAllTriggerControls")).toBool(),
                 "the stored setting this case is about was cleared before the editor read it, so nothing was proved");
        qInfo().noquote() << qsl("with showAllTriggerControls=true in the configuration the editor opened: panel shown %1, Options button pressed %2, session asking for the panel %3")
                                     .arg(optionsPanel()->isVisible() ? qsl("yes") : qsl("no"), toggle()->isChecked() ? qsl("yes") : qsl("no"),
                                          mpEditor->mShowAllTriggerControls ? qsl("yes") : qsl("no"));

        // What the session is holding, which is the half a window too short to
        // fit the panel does not show: the space-driven auto-collapse hides the
        // panel without changing what the reader asked for, so a stored
        // preference can be read back in and still leave the panel away
        QVERIFY2(!mpEditor->mShowAllTriggerControls, "the editor started the session asking for the options panel, which is the retired setting being read back in");
        QVERIFY2(optionsPanel() && !optionsPanel()->isVisible(), "the editor opened with the trigger options panel on show");
        QVERIFY2(!toggle()->isChecked(), "the Options button opened pressed, so it disagrees with the panel it stands for");
        QVERIFY2(summaryStrip() && summaryStrip()->isVisible(), "the strip that stands in for the panel is not on show while the panel is away");
    }

    // Closed on open is not closed for good: the button still opens it, and the
    // strip goes away while it is open
    void test_theToggleStillOpensAndClosesThePanel()
    {
        toggle()->click();
        QTest::qWait(50ms);
        QVERIFY2(optionsPanel()->isVisible(), "the Options button did not open the panel");
        QVERIFY2(!summaryStrip()->isVisible(), "the strip that stands in for the panel is still on show while the panel is open");

        toggle()->click();
        QTest::qWait(50ms);
        QVERIFY2(!optionsPanel()->isVisible(), "the Options button did not close the panel again");
        QVERIFY2(summaryStrip()->isVisible(), "the strip did not come back when the panel was closed");
    }

    // ...and nothing writes the retired key back. Last of the cases, because it
    // closes the editor, which is what stores what a session leaves behind.
    void test_closingTheEditorClearsTheRetiredKey()
    {
        toggle()->click();
        QTest::qWait(50ms);
        QVERIFY2(optionsPanel()->isVisible(), "the panel was not open, so closing the editor cannot show the key is not written");

        mpEditor->writeSettings();
        QVERIFY2(!mudlet::getQSettings()->contains(qsl("showAllTriggerControls")),
                 qPrintable(qsl("the editor still keeps showAllTriggerControls, which it is now %1")
                                    .arg(mudlet::getQSettings()->value(qsl("showAllTriggerControls")).toString())));
    }
};

#include "EditorOptionsPanelDefaultTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorOptionsPanelDefaultTest)
