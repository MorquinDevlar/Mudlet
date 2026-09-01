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
 * Each view of the script editor puts the right hand splitter back to the sizes
 * it was last left at. A split saved when the form column was taller - a view
 * with more fields in it, or the trigger options panel open - then hands the
 * trigger view a form pane it has nothing to fill with, and the code editor
 * starts that far down the window. A reported profile carried 630px of form
 * above 220px of code, against a form that needs about 160px to show a name row
 * and one pattern: some 470px of empty pane between the Add pattern button and
 * the Lua heading.
 *
 * restoreRightSplitterState() only ever measured the form when there was no
 * saved state at all; with one it called restoreState() and took whatever was
 * in it. The restore is now clamped to what the form asks for at that moment,
 * with the surplus going to the code pane. Only the restore - a drag afterwards
 * is the user asking for a taller form, and nothing here runs then.
 */

#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest/QtTest>
#include <algorithm>
#include <chrono>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TTrigger.h"
#include "TelnetServerStub.h"
#include "dlgTriggerEditor.h"
#include "dlgTriggersMainArea.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorSplitterRestoreTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorSplitterRestore-Test-Profile");
    QString mPort; // assigned the stub's actual ephemeral port in initTestCase()
    const QString mLocalhost = qsl("localhost");

    // The sizes off the reported profile, written to settings before the editor
    // is built so it reads them the way a returning user's would arrive
    static constexpr int scmSavedFormHeight = 630;
    static constexpr int scmSavedCodeHeight = 220;
    static constexpr int scmSavedErrorConsoleHeight = 30;
    QByteArray mSeededState;

    QSplitter* mpSplitter = nullptr;
    QWidget* mpFormPane = nullptr;

    void deleteProfileDirectory(const QString& profileName)
    {
        const QString path = mudlet::getMudletPath(enums::profileHomePath, profileName);
        QDir dir(path);
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
        if (!spy.wait(1000)) {
            QFAIL("Could not connect with the host.");
        }
    }

    // A QSplitter stores the sizes it was asked for, not the ones it ended up
    // drawing, so a stand-in splitter sized to hold them exactly is enough to
    // produce the byte array a profile would have carried
    static QByteArray splitterStateFor(const QList<int>& sizes)
    {
        QSplitter splitter(Qt::Vertical);
        int total = 0;
        for (const int size : sizes) {
            splitter.addWidget(new QWidget(&splitter));
            total += size;
        }
        splitter.resize(400, total + splitter.handleWidth() * (sizes.size() - 1));
        splitter.setSizes(sizes);
        return splitter.saveState();
    }

    static QString describe(const QList<int>& sizes)
    {
        QStringList parts;
        for (const int size : sizes) {
            parts << QString::number(size);
        }
        return parts.join(qsl(" / "));
    }

    // The sizes the saved state alone puts back, for comparison against the
    // ones the editor actually settled on. Measured on the editor's own
    // splitter so both go through the same geometry.
    QList<int> sizesFromAnUnclampedRestore()
    {
        mpSplitter->restoreState(mSeededState);
        QCoreApplication::processEvents();
        return mpSplitter->sizes();
    }

    void enterTheTriggerView()
    {
        mpEditor->slot_showTriggers();
        QCoreApplication::processEvents();
    }

private slots:
    void initTestCase()
    {
        if (portableMarkerPresent()) {
            QSKIP("portable.txt present - it takes precedence over XDG_CONFIG_HOME, "
                  "so the config dir cannot be redirected");
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
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>("MudletInstanceCoordinator"));
        mudlet::self()->init();
        mudlet::self()->setStorePasswordsSecurely(false);
        deleteProfileDirectory(mProfileName);

        // Seeded before the profile is up: the editor's constructor is what
        // reads this, and the trigger view is restored from it on the way in
        mSeededState = splitterStateFor({scmSavedFormHeight, scmSavedCodeHeight, scmSavedErrorConsoleHeight});
        QVERIFY(!mSeededState.isEmpty());
        mudlet::getQSettings()->setValue(qsl("mTriggerEditorSplitterState"), mSeededState);
        mudlet::getQSettings()->remove(qsl("script_editor_pos"));

        startProfile(mProfileName, mLocalhost, mPort);

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");

        // Tall enough that the saved split fits without the splitter having to
        // share out a shortfall of its own - the offscreen platform's screen is
        // smaller than the window this scenario needs, and nothing here is
        // shown to a window manager that would refuse the size
        mpEditor->resize(1000, scmSavedFormHeight + scmSavedCodeHeight + 400);
        QTest::qWait(100ms);

        mpSplitter = mpEditor->findChild<QSplitter*>(qsl("splitter_right"));
        QVERIFY2(mpSplitter != nullptr, "The editor should have a splitter_right");
        QCOMPARE(mpSplitter->count(), 3);
        mpFormPane = mpSplitter->widget(0);
        QVERIFY(mpFormPane != nullptr);

        QVERIFY2(mpEditor->mTriggerEditorSplitterState == mSeededState, "The editor did not read the seeded splitter state - this test no longer covers the path it was written for");

        // A trigger to select, so the form has a name row and a pattern in it
        // rather than the empty-view placeholder
        mpEditor->slot_showTriggers();
        mpEditor->addTrigger(false);
        QTest::qWait(100ms);
    }

    void cleanupTestCase()
    {
        mpSplitter = nullptr;
        mpFormPane = nullptr;
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

    // The reported symptom: a saved split of 630 above 220 leaves the trigger
    // form with hundreds of pixels of nothing under its last field, and starts
    // the code editor there
    void test_aStaleSavedSplitDoesNotStrandTheCodeEditor()
    {
        enterTheTriggerView();
        const QList<int> restored = mpSplitter->sizes();
        const int wanted = mpFormPane->sizeHint().height();
        const QList<int> raw = sizesFromAnUnclampedRestore();
        qInfo().noquote() << qsl("  form asks for %1; saved state alone gives %2; the editor settled on %3").arg(QString::number(wanted), describe(raw), describe(restored));

        QVERIFY2(raw.at(0) > wanted + 100,
                 qPrintable(qsl("The saved state put %1 into a form pane asking for %2 - too close for this test to say anything. "
                                "Seeded %3, splitter is %4 tall.")
                                    .arg(QString::number(raw.at(0)),
                                         QString::number(wanted),
                                         describe({scmSavedFormHeight, scmSavedCodeHeight, scmSavedErrorConsoleHeight}),
                                         QString::number(mpSplitter->height()))));
        QVERIFY2(raw.at(0) + raw.at(1) - wanted > 200, "Not enough room between the two panes for the code pane's own floor to be out of the picture");

        QVERIFY2(restored.at(0) == wanted,
                 qPrintable(qsl("The form pane asks for %1 but was restored to %2 - %3 of it has nothing in it, and the code editor starts under all of it")
                                    .arg(QString::number(wanted), QString::number(restored.at(0)), QString::number(restored.at(0) - wanted))));
        QVERIFY2(restored.at(1) == raw.at(0) + raw.at(1) - wanted,
                 qPrintable(qsl("The code pane should have been handed the whole surplus and sat at %1, it is at %2")
                                    .arg(QString::number(raw.at(0) + raw.at(1) - wanted), QString::number(restored.at(1)))));
        QVERIFY2(restored.at(1) > raw.at(1), "The code pane did not grow at all");
        QCOMPARE(restored.at(2), raw.at(2));
    }

    // Only the restore is clamped. Once the panes are on screen the handle is
    // the user's, and a form dragged taller than its fields need stays there.
    void test_theFormCanStillBeDraggedLarger()
    {
        enterTheTriggerView();
        const QList<int> clamped = mpSplitter->sizes();
        const int grown = clamped.at(0) + 150;
        QVERIFY2(clamped.at(1) > 300, "Not enough code pane to take 150px off for this test");

        mpSplitter->setSizes({grown, clamped.at(1) - 150, clamped.at(2)});
        QTest::qWait(100ms);

        QVERIFY2(mpSplitter->sizes().at(0) == grown,
                 qPrintable(qsl("The form was dragged to %1 and something pulled it back to %2").arg(QString::number(grown), QString::number(mpSplitter->sizes().at(0)))));
    }

    // With the trigger options panel open the form legitimately is taller, so
    // the clamp has to measure it as it stands at that moment. Measured as it
    // was with the panel closed, entering the view would cut the form to a
    // height the panel does not fit in - and leave
    // refitSplitterForTriggerOptions() borrowing the difference straight back
    // off the code pane the clamp had just handed it.
    void test_theClampMeasuresTheFormWithTheOptionsPanelOpen()
    {
        mpEditor->setTriggerOptionsShown(false);
        enterTheTriggerView();
        const int closedForm = mpSplitter->sizes().at(0);

        mpEditor->setTriggerOptionsShown(true);
        QTest::qWait(100ms);
        enterTheTriggerView();
        const QList<int> withPanel = mpSplitter->sizes();
        const int wanted = mpFormPane->sizeHint().height();
        const int savedForm = sizesFromAnUnclampedRestore().at(0);
        qInfo().noquote() << qsl("  form is %1 with the options panel closed, asks for %2 with it open, saved state gives %3, restored to %4")
                                     .arg(QString::number(closedForm), QString::number(wanted), QString::number(savedForm), describe(withPanel));

        QVERIFY2(wanted > closedForm, "The options panel did not make the form any taller - this test no longer covers what it was written for");
        QVERIFY2(withPanel.at(0) > closedForm,
                 qPrintable(qsl("The form was cut to %1, which is what it needs with the options panel closed - the clamp measured a form that is not the one on screen")
                                    .arg(QString::number(withPanel.at(0)))));
        // Only ever taken off, never added: the saved height stands when the
        // form asks for more than it, and the panel scrolls inside what there is
        QCOMPARE(withPanel.at(0), std::min(wanted, savedForm));
        QVERIFY2(mpEditor->mTriggerOptionsBorrowedHeight == 0,
                 qPrintable(qsl("The restore left a loan of %1px against geometry it had just thrown away").arg(QString::number(mpEditor->mTriggerOptionsBorrowedHeight))));

        mpEditor->setTriggerOptionsShown(false);
        QTest::qWait(100ms);
    }

    // The other half of the same fault, and the one the user reported: the
    // stored split was never the wrong size for the view, it was the wrong size
    // for the item. A trigger with three pattern rows opened after one with a
    // single row was given the height the single row needed, so the rows
    // scrolled inside a pane a third of the window tall while the code pane
    // under it sat nearly empty.
    void test_choosingATallerItemGivesItsFormTheRoom()
    {
        // Two triggers of known height, built on the profile and read back
        // through the editor's own fill of the tree
        auto* pOne = new TTrigger(qsl("One pattern"), QStringList{qsl("alpha")}, QList<int>{REGEX_SUBSTRING}, false, mpHost);
        pOne->registerTrigger();
        auto* pThree = new TTrigger(qsl("Three patterns"), QStringList{qsl("alpha"), qsl("beta"), qsl("gamma")}, QList<int>{REGEX_SUBSTRING, REGEX_SUBSTRING, REGEX_SUBSTRING}, false, mpHost);
        pThree->registerTrigger();
        mpEditor->treeWidget_triggers->clear();
        mpEditor->fillout_form();
        enterTheTriggerView();
        QTest::qWait(50ms);

        const auto rowFor = [this](const int id) -> QTreeWidgetItem* {
            QTreeWidgetItem* pBase = mpEditor->mpTriggerBaseItem;
            for (int row = 0; pBase && row < pBase->childCount(); ++row) {
                if (pBase->child(row)->data(0, Qt::UserRole).toInt() == id) {
                    return pBase->child(row);
                }
            }
            return nullptr;
        };
        QTreeWidgetItem* pRowOne = rowFor(pOne->getID());
        QTreeWidgetItem* pRowThree = rowFor(pThree->getID());
        QVERIFY2(pRowOne && pRowThree, "The two triggers this case reads are not in the tree");

        const auto choose = [this](QTreeWidgetItem* pRow) {
            mpEditor->treeWidget_triggers->setCurrentItem(pRow);
            mpEditor->slot_triggerSelected(pRow);
            QCoreApplication::processEvents();
            QTest::qWait(50ms);
        };

        choose(pRowOne);
        const QList<int> forOne = mpSplitter->sizes();
        const int panesForOne = forOne.at(0) + forOne.at(1);
        choose(pRowThree);
        const QList<int> sizes = mpSplitter->sizes();

        auto* pPatterns = mpEditor->findChild<QScrollArea*>(qsl("editorPatternScroll"));
        QVERIFY2(pPatterns != nullptr && pPatterns->widget() != nullptr, "The trigger form has no pattern list to scroll");
        const int rowsWant = pPatterns->widget()->sizeHint().height();
        const int rowsShown = pPatterns->viewport()->height();
        const bool scrolling = pPatterns->verticalScrollBar() && pPatterns->verticalScrollBar()->isVisible();
        qInfo().noquote() << qsl("  one pattern row left the panes at %1; three rows want %2px and are shown %3px, with the panes at %4 - the rows %5")
                                     .arg(describe(forOne), QString::number(rowsWant), QString::number(rowsShown), describe(sizes), scrolling ? qsl("scroll") : qsl("all fit"));

        QVERIFY2(rowsWant > forOne.at(0) - rowsShown, "The three-row list is no taller than the pane it is in, so this case says nothing about growing it");
        QVERIFY2(sizes.at(0) > forOne.at(0),
                 qPrintable(qsl("the form pane stayed at %1 for an item whose rows want %2px of the %3px it shows them in")
                                    .arg(QString::number(sizes.at(0)), QString::number(rowsWant), QString::number(rowsShown))));
        QVERIFY2(rowsShown >= rowsWant,
                 qPrintable(qsl("the pattern rows want %1px and are shown %2px, so the last of them is off the bottom of the form").arg(QString::number(rowsWant), QString::number(rowsShown))));
        QVERIFY2(!scrolling, "the pattern rows are still scrolling inside the form pane");
        // ...and what the form grew by came off the code pane, which keeps its
        // own floor - the editor is no use with a code pane too short to type in
        QVERIFY2(sizes.at(1) >= 120, qPrintable(qsl("the code pane was left at %1, under the floor it keeps").arg(QString::number(sizes.at(1)))));
        // The two of them still add up to what they did: the form took its room
        // off the code pane rather than off the error console under it
        QCOMPARE(sizes.at(0) + sizes.at(1), panesForOne);
    }
};

#include "EditorSplitterRestoreTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorSplitterRestoreTest)
