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
 * The right hand splitter stacks the form column over the code editor, and the
 * form is snapped to the height the item in it asks for - shrinking as readily
 * as growing - whenever the item changes, whenever a view is entered, and once
 * the editor has been shown and the panes have real heights to divide up.
 *
 * Two faults that replaces. A trigger of eleven pattern rows, opened as the
 * first item of a freshly shown editor, scrolled inside a short form under a
 * code pane holding most of the window: the fit ran while the editor was still
 * hidden, when the splitter had nothing to divide. And a form left tall by a
 * long item stayed tall for the three-row item after it, because the fit only
 * ever grew a pane - it could not tell its own growth from a drag.
 *
 * The one exception is a view whose handle the user has dragged in this
 * session. QSplitter emits splitterMoved() for a drag and not for setSizes(),
 * so a drag is the only thing that records a height, and it records it for that
 * view alone. Nothing is stored: the seven settings keys each view's split used
 * to live under are cleared on the way past, so a restart snaps everywhere.
 *
 * Retired with the rule they were written for:
 *  - test_aStaleSavedSplitDoesNotStrandTheCodeEditor, which checked that a
 *    stored split of 630 above 220 was clamped to what the form could fill. A
 *    stored split no longer reaches the layout at all, so the seeded state is
 *    now only the backdrop the first-show case is measured against, and
 *    test_nothingOfASessionsDragSurvivesIntoTheNextOne covers the keys going.
 *  - test_theClampMeasuresTheFormWithTheOptionsPanelOpen, which checked that
 *    the clamp measured a form with the panel in it. There is no clamped
 *    restore left to measure; the two options-panel cases below cover the open
 *    and the close in a form that snaps and in one that was dragged.
 *
 * Run with: ctest -R EditorSplitterRestoreTest -V
 */

#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
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
#include "TAlias.h"
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

    // Room for a form of eleven pattern rows over a code pane well clear of its
    // floor, so that nothing here ends up measuring the clamp instead of the fit
    static constexpr int scmWindowWidth = 1000;
    static constexpr int scmWindowHeight = 1250;
    // Mirrors scmEditorSourcePaneFloor in src/dlgTriggerEditor.cpp, which is
    // file-local to it
    static constexpr int scmSourcePaneFloor = 120;
    static constexpr int scmTallTriggerPatterns = 11;
    static constexpr int scmShortTriggerPatterns = 3;

    // The sizes off the reported profile, written to settings before the editor
    // is built so it reads them the way a returning user's would arrive. Under
    // the rule this file covers nothing reads them at all.
    static constexpr int scmSavedFormHeight = 630;
    static constexpr int scmSavedCodeHeight = 220;
    static constexpr int scmSavedErrorConsoleHeight = 30;
    QByteArray mSeededState;

    QSplitter* mpSplitter = nullptr;
    QScrollArea* mpPatterns = nullptr;

    TTrigger* mpTallTrigger = nullptr;
    TTrigger* mpShortTrigger = nullptr;
    TTrigger* mpOneRowTrigger = nullptr;
    TAlias* mpAlias = nullptr;

    // Everything the very first item shown after the editor was built and put
    // on screen was left at. Taken in initTestCase() because no later case can
    // put the editor back to never having been shown.
    QList<int> mFirstShowSizes;
    int mFirstShowFormWants = 0;
    int mFirstShowRowsWant = 0;
    int mFirstShowRowsShown = 0;
    bool mFirstShowScrolling = false;

    static QStringList retiredSplitterKeys()
    {
        return {qsl("mTriggerEditorSplitterState"),
                qsl("mAliasEditorSplitterState"),
                qsl("mScriptEditorSplitterState"),
                qsl("mActionEditorSplitterState"),
                qsl("mKeyEditorSplitterState"),
                qsl("mTimerEditorSplitterState"),
                qsl("mVarEditorSplitterState")};
    }

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

    static TTrigger* registeredTrigger(const QString& name, const int patternCount, Host* pHost)
    {
        QStringList patterns;
        QList<int> kinds;
        for (int row = 0; row < patternCount; ++row) {
            patterns << qsl("pattern %1").arg(QString::number(row));
            kinds << REGEX_SUBSTRING;
        }
        auto* pTrigger = new TTrigger(name, patterns, kinds, false, pHost);
        pTrigger->registerTrigger();
        return pTrigger;
    }

    QTreeWidgetItem* triggerRowFor(const int id) const
    {
        QTreeWidgetItem* pBase = mpEditor->mpTriggerBaseItem;
        for (int row = 0; pBase && row < pBase->childCount(); ++row) {
            if (pBase->child(row)->data(0, Qt::UserRole).toInt() == id) {
                return pBase->child(row);
            }
        }
        return nullptr;
    }

    void enterTheTriggerView()
    {
        mpEditor->slot_showTriggers();
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
    }

    void chooseTrigger(TTrigger* pTrigger)
    {
        QTreeWidgetItem* pRow = triggerRowFor(pTrigger->getID());
        QVERIFY2(pRow != nullptr, qPrintable(qsl("\"%1\" is not in the tree, so this case cannot choose it").arg(pTrigger->getName())));
        mpEditor->treeWidget_triggers->setCurrentItem(pRow);
        mpEditor->slot_triggerSelected(pRow);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
    }

    void chooseTheAlias()
    {
        mpEditor->slot_showAliases();
        QCoreApplication::processEvents();
        QTreeWidgetItem* pBase = mpEditor->mpAliasBaseItem;
        QVERIFY2(pBase != nullptr && pBase->childCount() > 0, "the aliases tree has nothing in it for this case to choose");
        QTreeWidgetItem* pRow = pBase->child(0);
        mpEditor->treeWidget_aliases->setCurrentItem(pRow);
        mpEditor->slot_aliasSelected(pRow);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
    }

    // QSplitter emits splitterMoved() for a drag and not for setSizes(), so the
    // slot a drag would have reached is called for it. The two panes keep their
    // total between them, as a drag on the handle above the error console does.
    void dragFormPaneTo(const int formHeight)
    {
        QList<int> sizes = mpSplitter->sizes();
        const int panes = sizes.at(0) + sizes.at(1);
        mpSplitter->setSizes({formHeight, panes - formHeight, sizes.at(2)});
        QCoreApplication::processEvents();
        mpEditor->slot_rightSplitterMoved(0, 0);
        QCoreApplication::processEvents();
    }

    int panesTotal() const
    {
        const QList<int> sizes = mpSplitter->sizes();
        return sizes.at(0) + sizes.at(1);
    }

    // What the form column is asking for as it stands, which is what an
    // undragged view is snapped to
    int formWants() const { return mpEditor->formPaneHeightForItsContents(panesTotal()); }

    bool patternsScrolling() const { return mpPatterns->verticalScrollBar() && mpPatterns->verticalScrollBar()->isVisible(); }

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

        // Seeded before the profile is up, which is what builds the editor: the
        // configuration a returning user arrives with, carrying the split their
        // last session was left at. Nothing is to read it.
        mSeededState = splitterStateFor({scmSavedFormHeight, scmSavedCodeHeight, scmSavedErrorConsoleHeight});
        QVERIFY(!mSeededState.isEmpty());
        for (const QString& key : retiredSplitterKeys()) {
            mudlet::getQSettings()->setValue(key, mSeededState);
        }
        mudlet::getQSettings()->remove(qsl("script_editor_pos"));
        mudlet::getQSettings()->sync();

        startProfile(mProfileName, mLocalhost, mPort);
        if (QTest::currentTestFailed()) {
            return;
        }

        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Loading the profile did not build an editor");
        QVERIFY2(!mpEditor->isVisible(), "the editor was already on screen, so the first-show case has nothing left to prove");

        // Three triggers of known height, built on the profile and read back
        // through the editor's own fill of the tree
        mpTallTrigger = registeredTrigger(qsl("Eleven patterns"), scmTallTriggerPatterns, mpHost);
        mpShortTrigger = registeredTrigger(qsl("Three patterns"), scmShortTriggerPatterns, mpHost);
        mpOneRowTrigger = registeredTrigger(qsl("One pattern"), 1, mpHost);
        // ...and one alias, for the view that is never dragged
        mpAlias = new TAlias(qsl("An alias"), mpHost);
        mpAlias->setRegexCode(qsl("^greet$"));
        mpAlias->setCommand(qsl("say hello"));
        mpAlias->registerAlias();
        // fillout_form() inserts a fresh base item into every one of the seven
        // trees, so all seven are emptied first or each gets a second one
        for (TTreeWidget* pTree : {mpEditor->treeWidget_triggers,
                                   mpEditor->treeWidget_aliases,
                                   mpEditor->treeWidget_timers,
                                   mpEditor->treeWidget_scripts,
                                   mpEditor->treeWidget_actions,
                                   mpEditor->treeWidget_keys,
                                   mpEditor->treeWidget_variables}) {
            pTree->clear();
        }
        mpEditor->fillout_form();

        // The eleven-row trigger is made the item the editor will open on, and
        // the form is filled with it while the window is still hidden - which
        // is the order mudlet::slot_showEditorDialog() puts them in, and the
        // whole of why the fit that ran with it had no panes to divide up
        QTreeWidgetItem* pTallRow = triggerRowFor(mpTallTrigger->getID());
        QVERIFY2(pTallRow != nullptr, "the eleven-row trigger is not in the tree");
        mpEditor->treeWidget_triggers->setCurrentItem(pTallRow);
        mpEditor->slot_showTriggers();
        QCoreApplication::processEvents();
        QVERIFY2(!mpEditor->isVisible(), "showing the trigger view put the editor on screen, which this case needs the show itself to do");

        // Tall enough that the eleven-row form fits over a code pane clear of
        // its floor - the offscreen platform's screen is smaller than the
        // window this scenario needs, and nothing here is shown to a window
        // manager that would refuse the size
        mpEditor->resize(scmWindowWidth, scmWindowHeight);
        mpEditor->show();
        QTest::qWait(200ms);

        mpSplitter = mpEditor->findChild<QSplitter*>(qsl("splitter_right"));
        QVERIFY2(mpSplitter != nullptr, "The editor should have a splitter_right");
        QCOMPARE(mpSplitter->count(), 3);
        mpPatterns = mpEditor->findChild<QScrollArea*>(qsl("editorPatternScroll"));
        QVERIFY2(mpPatterns != nullptr && mpPatterns->widget() != nullptr, "The trigger form has no pattern list to scroll");

        mFirstShowSizes = mpSplitter->sizes();
        mFirstShowFormWants = formWants();
        mFirstShowRowsWant = mpPatterns->widget()->sizeHint().height();
        mFirstShowRowsShown = mpPatterns->viewport()->height();
        mFirstShowScrolling = patternsScrolling();
    }

    void cleanupTestCase()
    {
        mpSplitter = nullptr;
        mpPatterns = nullptr;
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

    // Each case is its own session as far as the splitter is concerned: a
    // height is remembered only for a view dragged in that session
    void init()
    {
        if (!mpEditor) {
            return;
        }
        mpEditor->mDraggedFormPaneHeights.clear();
        mpEditor->mTriggerOptionsBorrowedHeight = 0;
    }

    // The first defect from the screenshots: the editor picks its opening item
    // and fills the form with it before the window is put on screen, so the fit
    // that ran with it was dividing up a splitter that had no height yet. A
    // trigger of eleven pattern rows came up scrolling inside a short form,
    // under a code pane holding most of the window.
    void test_theFirstItemShownGetsItsContentsHeight()
    {
        QVERIFY2(!mFirstShowSizes.isEmpty(), "the first show was never measured");
        qInfo().noquote() << qsl("  first item shown is a trigger of %1 pattern rows: the rows want %2px and are shown %3px, the form asks for %4px, the panes are at %5 - the rows %6")
                                     .arg(QString::number(scmTallTriggerPatterns),
                                          QString::number(mFirstShowRowsWant),
                                          QString::number(mFirstShowRowsShown),
                                          QString::number(mFirstShowFormWants),
                                          describe(mFirstShowSizes),
                                          mFirstShowScrolling ? qsl("scroll") : qsl("all fit"));

        QVERIFY2(mFirstShowRowsWant > 0, "the pattern list has no height at all, so this case is measuring nothing");
        // Asked of what the form wanted rather than of where the panes ended
        // up, so that a form given the whole window reads here as the fault it
        // is rather than as a window too short to run the case in
        const int panes = mFirstShowSizes.at(0) + mFirstShowSizes.at(1);
        QVERIFY2(mFirstShowFormWants < panes - scmSourcePaneFloor,
                 qPrintable(qsl("the form asked for %1px of the %2px the two panes have, so it was clamped rather than fitted - the window is too short for this case")
                                    .arg(QString::number(mFirstShowFormWants), QString::number(panes))));

        QVERIFY2(mFirstShowSizes.at(0) == mFirstShowFormWants,
                 qPrintable(qsl("the form asks for %1px and the first show left it at %2px").arg(QString::number(mFirstShowFormWants), QString::number(mFirstShowSizes.at(0)))));
        QVERIFY2(mFirstShowRowsShown >= mFirstShowRowsWant,
                 qPrintable(qsl("the pattern rows want %1px and are shown %2px, so the last of them is off the bottom of the form the editor opened with")
                                    .arg(QString::number(mFirstShowRowsWant), QString::number(mFirstShowRowsShown))));
        QVERIFY2(!mFirstShowScrolling, "the editor opened with the pattern rows scrolling inside the form pane");
        QVERIFY2(mFirstShowSizes.at(1) >= scmSourcePaneFloor,
                 qPrintable(qsl("the code pane opened at %1, under the %2px floor it keeps").arg(QString::number(mFirstShowSizes.at(1)), QString::number(scmSourcePaneFloor))));
    }

    // The second defect: a form left tall by an eleven-row trigger stayed tall
    // for the three-row one after it, because the fit only ever grew a pane. A
    // large empty band between the last pattern row and the Lua heading, with
    // the code pane pushed to the bottom of the window.
    void test_choosingAShorterItemSnapsTheFormBackDown()
    {
        enterTheTriggerView();
        chooseTrigger(mpTallTrigger);
        const QList<int> tall = mpSplitter->sizes();
        chooseTrigger(mpShortTrigger);
        const QList<int> shortened = mpSplitter->sizes();
        const int wanted = formWants();
        qInfo().noquote() << qsl("  eleven rows left the panes at %1; three rows ask for %2px and the panes came out at %3").arg(describe(tall), QString::number(wanted), describe(shortened));

        QVERIFY2(tall.at(0) > wanted + 100,
                 qPrintable(qsl("the eleven-row form was only %1px against the three-row form's %2px - too close for this case to say anything about shrinking")
                                    .arg(QString::number(tall.at(0)), QString::number(wanted))));
        QVERIFY2(shortened.at(0) == wanted,
                 qPrintable(qsl("the form stayed at %1px for an item asking for %2px - %3px of empty pane between its last row and the code editor")
                                    .arg(QString::number(shortened.at(0)), QString::number(wanted), QString::number(shortened.at(0) - wanted))));
        QVERIFY2(shortened.at(1) == tall.at(1) + (tall.at(0) - shortened.at(0)),
                 qPrintable(qsl("the code pane should have grown by the %1px the form gave up and sat at %2px, it is at %3px")
                                    .arg(QString::number(tall.at(0) - shortened.at(0)), QString::number(tall.at(1) + tall.at(0) - shortened.at(0)), QString::number(shortened.at(1)))));
        // The form gave its room to the code pane rather than to the error
        // console under it
        QCOMPARE(shortened.at(0) + shortened.at(1), tall.at(0) + tall.at(1));
        QCOMPARE(shortened.at(2), tall.at(2));
    }

    // The exception to the snap, and the whole of it: a view whose handle the
    // user has dragged keeps that height as the item in it changes, and no
    // other view is given it.
    void test_aDraggedViewKeepsItsHeightAndOnlyThatView()
    {
        enterTheTriggerView();
        chooseTrigger(mpTallTrigger);
        const int tallestFormNeeds = mpSplitter->sizes().at(0);
        chooseTrigger(mpShortTrigger);
        const int panes = panesTotal();
        // As tall as the form is dragged in this case: past what any of the
        // three items fills on its own, and still clear of the code pane's floor
        dragFormPaneTo(std::min(tallestFormNeeds + 80, panes - scmSourcePaneFloor - 40));
        const int draggedTo = mpSplitter->sizes().at(0);
        QVERIFY2(mpEditor->mDraggedFormPaneHeights.value(EditorViewType::cmTriggerView) == draggedTo,
                 qPrintable(qsl("a drag on this view's handle left the form at %1px and was not recorded as the view's own height").arg(QString::number(draggedTo))));
        QVERIFY2(draggedTo > tallestFormNeeds,
                 qPrintable(qsl("the drag left the form at %1px of the %2px the panes have, which the tallest of these items fills on its own at %3px - the window is too short for this case")
                                    .arg(QString::number(draggedTo), QString::number(panes), QString::number(tallestFormNeeds))));

        chooseTrigger(mpOneRowTrigger);
        QVERIFY2(mpSplitter->sizes().at(0) == draggedTo,
                 qPrintable(qsl("a one-row trigger pulled the dragged form from %1px to %2px").arg(QString::number(draggedTo), QString::number(mpSplitter->sizes().at(0)))));
        chooseTrigger(mpTallTrigger);
        QVERIFY2(mpSplitter->sizes().at(0) == draggedTo,
                 qPrintable(qsl("an eleven-row trigger pulled the dragged form from %1px to %2px").arg(QString::number(draggedTo), QString::number(mpSplitter->sizes().at(0)))));

        // ...and only this view. The aliases have never been dragged, so they
        // still snap.
        chooseTheAlias();
        const QList<int> aliasSizes = mpSplitter->sizes();
        const int aliasFormWants = formWants();
        qInfo().noquote() << qsl("  the trigger view was dragged to %1px; the alias form asks for %2px and came out at %3")
                                     .arg(QString::number(draggedTo), QString::number(aliasFormWants), describe(aliasSizes));
        QVERIFY2(aliasSizes.at(0) == aliasFormWants,
                 qPrintable(qsl("the alias form asks for %1px and was given %2px - a view that was never dragged took the trigger view's height")
                                    .arg(QString::number(aliasFormWants), QString::number(aliasSizes.at(0)))));
        QVERIFY2(aliasSizes.at(0) != draggedTo, "the alias form happens to want exactly the height the trigger view was dragged to, so this case proves nothing");

        // ...and the trigger view still has it on the way back in
        enterTheTriggerView();
        chooseTrigger(mpShortTrigger);
        QVERIFY2(mpSplitter->sizes().at(0) == draggedTo,
                 qPrintable(qsl("the trigger view came back at %1px rather than the %2px it was dragged to").arg(QString::number(mpSplitter->sizes().at(0)), QString::number(draggedTo))));
    }

    // The handle is the user's: a form dragged taller than its fields need
    // stays where it was put, and nothing pulls it back once the events the
    // drag posted have run.
    void test_theFormCanStillBeDraggedLarger()
    {
        enterTheTriggerView();
        chooseTrigger(mpShortTrigger);
        const QList<int> snapped = mpSplitter->sizes();
        QVERIFY2(snapped.at(1) > 300, "Not enough code pane to take 150px off for this test");
        const int grown = snapped.at(0) + 150;

        dragFormPaneTo(grown);
        QTest::qWait(100ms);

        QVERIFY2(mpSplitter->sizes().at(0) == grown,
                 qPrintable(qsl("The form was dragged to %1 and something pulled it back to %2").arg(QString::number(grown), QString::number(mpSplitter->sizes().at(0)))));
    }

    // The other half of the same fault as the shrink: the split was never the
    // wrong size for the view, it was the wrong size for the item. A trigger
    // with three pattern rows opened after one with a single row was given the
    // height the single row needed, so the rows scrolled inside a pane a third
    // of the window tall while the code pane under it sat nearly empty.
    void test_choosingATallerItemGivesItsFormTheRoom()
    {
        enterTheTriggerView();
        chooseTrigger(mpOneRowTrigger);
        const QList<int> forOne = mpSplitter->sizes();
        const int panesForOne = forOne.at(0) + forOne.at(1);
        chooseTrigger(mpShortTrigger);
        const QList<int> sizes = mpSplitter->sizes();

        const int rowsWant = mpPatterns->widget()->sizeHint().height();
        const int rowsShown = mpPatterns->viewport()->height();
        const bool scrolling = patternsScrolling();
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
        QVERIFY2(sizes.at(1) >= scmSourcePaneFloor, qPrintable(qsl("the code pane was left at %1, under the floor it keeps").arg(QString::number(sizes.at(1)))));
        // The two of them still add up to what they did: the form took its room
        // off the code pane rather than off the error console under it
        QCOMPARE(sizes.at(0) + sizes.at(1), panesForOne);
    }

    // In a view that snaps, opening the options panel is only a change in what
    // the form holds, and closing it is the same change back - so the snap
    // answers both and there is nothing on loan from the code pane
    void test_theOptionsPanelIsJustMoreFormInAViewThatSnaps()
    {
        enterTheTriggerView();
        chooseTrigger(mpShortTrigger);
        mpEditor->setTriggerOptionsShown(false);
        QTest::qWait(50ms);
        const QList<int> closed = mpSplitter->sizes();

        mpEditor->setTriggerOptionsShown(true);
        QTest::qWait(50ms);
        const QList<int> opened = mpSplitter->sizes();
        const int wantedOpen = formWants();
        qInfo().noquote()
                << qsl("  the form is %1 with the options panel closed and %2 with it open, against the %3px it asks for open").arg(describe(closed), describe(opened), QString::number(wantedOpen));

        QVERIFY2(opened.at(0) > closed.at(0), "opening the options panel did not make the form any taller - this case no longer covers what it was written for");
        QVERIFY2(opened.at(0) == wantedOpen, qPrintable(qsl("the form asks for %1px with the panel open and was given %2px").arg(QString::number(wantedOpen), QString::number(opened.at(0)))));
        QVERIFY2(mpEditor->mTriggerOptionsBorrowedHeight == 0,
                 qPrintable(
                         qsl("a view that snaps took a loan of %1px out of the code pane, which nothing here is keeping the books for").arg(QString::number(mpEditor->mTriggerOptionsBorrowedHeight))));

        mpEditor->setTriggerOptionsShown(false);
        QTest::qWait(50ms);
        QVERIFY2(mpSplitter->sizes().at(0) == closed.at(0),
                 qPrintable(qsl("closing the panel left the form at %1px rather than the %2px it had before it was opened")
                                    .arg(QString::number(mpSplitter->sizes().at(0)), QString::number(closed.at(0)))));
    }

    // In a view that was dragged, the user's height is the base: opening the
    // panel where it does not fit borrows the difference off the code pane and
    // closing it hands back that much and no more, and an item change in
    // between keeps both
    void test_theOptionsPanelBorrowsAndHandsBackInADraggedView()
    {
        enterTheTriggerView();
        chooseTrigger(mpShortTrigger);
        mpEditor->setTriggerOptionsShown(false);
        QTest::qWait(50ms);
        dragFormPaneTo(mpSplitter->sizes().at(0));
        const QList<int> base = mpSplitter->sizes();
        QVERIFY2(mpEditor->mDraggedFormPaneHeights.contains(EditorViewType::cmTriggerView), "the drag was not recorded, so the borrow this case is about will not happen");

        mpEditor->setTriggerOptionsShown(true);
        QTest::qWait(50ms);
        const int borrowed = mpEditor->mTriggerOptionsBorrowedHeight;
        const QList<int> opened = mpSplitter->sizes();
        qInfo().noquote() << qsl("  the form was dragged to %1 and the panel borrowed %2px of the code pane, leaving %3").arg(describe(base), QString::number(borrowed), describe(opened));

        QVERIFY2(borrowed > 0, "the panel was opened over a form dragged too short for it and borrowed nothing");
        QVERIFY2(opened.at(0) == base.at(0) + borrowed,
                 qPrintable(qsl("the form should have gone to %1px, the dragged height plus what was borrowed, and is at %2px")
                                    .arg(QString::number(base.at(0) + borrowed), QString::number(opened.at(0)))));

        // An item change in the middle keeps the user's height and the loan on
        // top of it, rather than closing the room the panel is shown in
        chooseTrigger(mpOneRowTrigger);
        QVERIFY2(mpSplitter->sizes().at(0) == opened.at(0),
                 qPrintable(qsl("choosing another item pulled the form from %1px to %2px with the options panel still open")
                                    .arg(QString::number(opened.at(0)), QString::number(mpSplitter->sizes().at(0)))));

        mpEditor->setTriggerOptionsShown(false);
        QTest::qWait(50ms);
        QVERIFY2(mpSplitter->sizes().at(0) == base.at(0),
                 qPrintable(qsl("closing the panel left the form at %1px rather than handing back the %2px it borrowed and returning to %3px")
                                    .arg(QString::number(mpSplitter->sizes().at(0)), QString::number(borrowed), QString::number(base.at(0)))));
        QCOMPARE(mpEditor->mTriggerOptionsBorrowedHeight, 0);
    }

    // The flicker a screen recording caught: dragging the right hand splitter
    // to make the code pane taller folded the options panel away, and the fold
    // itself changed what the form is made of - so the next move event read a
    // form taller than the one the fold had been recorded against, brought the
    // panel back, and folded it again on the event after. One flip per mouse
    // move, for the length of the drag.
    //
    // What is recorded now is the splitter's own size, which the fold does not
    // touch, and re-opening asks for the pane to be dragged clearly past where
    // the panel stopped fitting. The fold happens during a drag, so it must not
    // be mistaken for the deliberate close that hands a borrowed height back.
    void test_theFoldedOptionsPanelDoesNotFlickerOnEveryMoveEvent()
    {
        // Mirrors scmEditorOptionsRestoreBand in src/dlgTriggerEditor.cpp,
        // which is file-local to it
        static constexpr int scmRestoreBand = 24;

        enterTheTriggerView();
        chooseTrigger(mpShortTrigger);
        mpEditor->setTriggerOptionsShown(true);
        QTest::qWait(100ms);
        QWidget* pPanel = mpEditor->mpTriggersMainArea->widget_right;
        QVERIFY2(pPanel != nullptr && pPanel->isVisible(), "the options panel is not on show, so there is nothing here for a drag to fold away");
        QVERIFY2(mpEditor->mpButton_triggerOptionsSummary != nullptr, "the strip that stands in for the panel is missing");

        const QList<int> opened = mpSplitter->sizes();

        int askedFor = opened.at(0);
        for (int step = 0; step < 200 && pPanel->isVisible(); ++step) {
            askedFor -= 4;
            dragFormPaneTo(askedFor);
        }
        QVERIFY2(!pPanel->isVisible(), qPrintable(qsl("the options panel never folded away, down to a form pane of %1px").arg(QString::number(askedFor))));
        const int foldedAt = mpSplitter->sizes().at(0);
        qInfo().noquote() << qsl("  the panel opened at a form pane of %1px and folded away at %2px; the band before it comes back is %3px")
                                     .arg(QString::number(opened.at(0)), QString::number(foldedAt), QString::number(scmRestoreBand));

        // The flicker itself: a drag is a hand on a mouse, so the pane sits and
        // wobbles a few pixels around wherever the panel stopped fitting - and
        // every one of those events used to bring it back, because the fold had
        // made the form shorter and the height the fold was recorded against
        // was one the form had already passed. Back it came, the spacer was at
        // zero again, and it folded on the event after.
        for (int nudge = 0; nudge <= scmRestoreBand - 4; nudge += 4) {
            dragFormPaneTo(foldedAt + nudge);
            QVERIFY2(!pPanel->isVisible(),
                     qPrintable(qsl("the options panel came back %1px above where it stopped fitting, inside the %2px band - that is the flicker")
                                        .arg(QString::number(nudge), QString::number(scmRestoreBand))));
            QVERIFY2(mpEditor->mpButton_triggerOptionsSummary->isVisible(),
                     qPrintable(qsl("%1px above where the panel stopped fitting, neither it nor the strip that stands in for it is on show").arg(QString::number(nudge))));
        }

        // ...and past the band, which is the drag that asks for it back
        dragFormPaneTo(foldedAt + scmRestoreBand + 4);
        QVERIFY2(
                pPanel->isVisible(),
                qPrintable(
                        qsl("the options panel stayed folded away %1px above where it stopped fitting, past the %2px band").arg(QString::number(scmRestoreBand + 4), QString::number(scmRestoreBand))));

        mpEditor->setTriggerOptionsShown(false);
        QTest::qWait(50ms);
    }

    // A dragged height lives for the session and no longer. Last of the cases,
    // because it writes settings and builds the editor a restart would build.
    void test_nothingOfASessionsDragSurvivesIntoTheNextOne()
    {
        enterTheTriggerView();
        chooseTrigger(mpTallTrigger);
        const int tallestFormNeeds = mpSplitter->sizes().at(0);
        chooseTrigger(mpShortTrigger);
        dragFormPaneTo(tallestFormNeeds + 80);
        const int draggedTo = mpSplitter->sizes().at(0);
        QVERIFY2(mpEditor->mDraggedFormPaneHeights.contains(EditorViewType::cmTriggerView), "the drag was not recorded, so there is nothing here for a restart to lose");

        // The editor a restart builds, on the same profile and a configuration
        // still carrying a split under all seven of the keys that used to hold
        // one - written again here because any save in between will have taken
        // the seeded ones out
        for (const QString& key : retiredSplitterKeys()) {
            mudlet::getQSettings()->setValue(key, mSeededState);
        }
        mudlet::getQSettings()->sync();
        auto* pReopened = new dlgTriggerEditor(mpHost);
        pReopened->fillout_form();
        pReopened->resize(scmWindowWidth, scmWindowHeight);
        QTreeWidgetItem* pBase = pReopened->mpTriggerBaseItem;
        QVERIFY2(pBase != nullptr && pBase->childCount() > 0, "the reopened editor has no triggers in its tree");
        for (int row = 0; row < pBase->childCount(); ++row) {
            if (pBase->child(row)->data(0, Qt::UserRole).toInt() == mpShortTrigger->getID()) {
                pReopened->treeWidget_triggers->setCurrentItem(pBase->child(row));
                break;
            }
        }
        pReopened->slot_showTriggers();
        pReopened->show();
        QTest::qWait(200ms);

        auto* pReopenedSplitter = pReopened->findChild<QSplitter*>(qsl("splitter_right"));
        QVERIFY2(pReopenedSplitter != nullptr && pReopenedSplitter->count() == 3, "the reopened editor has no splitter_right to measure");
        const QList<int> reopenedSizes = pReopenedSplitter->sizes();
        const int reopenedWants = pReopened->formPaneHeightForItsContents(reopenedSizes.at(0) + reopenedSizes.at(1));
        qInfo().noquote() << qsl("  the session dragged the trigger form to %1px; the editor built after it asks for %2px and came up at %3")
                                     .arg(QString::number(draggedTo), QString::number(reopenedWants), describe(reopenedSizes));

        QVERIFY2(pReopened->mDraggedFormPaneHeights.isEmpty(), "the new editor started out remembering a height, which only a drag inside it should give it");
        QVERIFY2(reopenedSizes.at(0) == reopenedWants,
                 qPrintable(qsl("the reopened form asks for %1px and came up at %2px").arg(QString::number(reopenedWants), QString::number(reopenedSizes.at(0)))));
        QVERIFY2(reopenedSizes.at(0) < draggedTo - 20,
                 qPrintable(qsl("the reopened form came up at %1px, which is the %2px the last session dragged to").arg(QString::number(reopenedSizes.at(0)), QString::number(draggedTo))));

        delete pReopened;
        QCoreApplication::processEvents();

        // ...and the keys the split used to be stored under are cleared rather
        // than written back
        mpEditor->writeSettings();
        for (const QString& key : retiredSplitterKeys()) {
            QVERIFY2(!mudlet::getQSettings()->contains(key), qPrintable(qsl("the editor still keeps %1, which it is now %2").arg(key, mudlet::getQSettings()->value(key).toString())));
        }
    }
};

#include "EditorSplitterRestoreTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorSplitterRestoreTest)
