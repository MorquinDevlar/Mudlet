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
 * Every row of the editor's six item trees is one height, and what a row leads
 * with is a tinted glyph rather than a bitmap.
 *
 * Rows used to be as tall as whatever picture they carried. A folder's was the
 * tree's icon size - 24px at the default preference - a plain item's was the
 * 9px state dot, and a tree's heading row took the sidebar's 18px. So the hover
 * fill on one row and the selection pill on the next were visibly different
 * heights on the same list, which reads as a rendering fault rather than as a
 * design.
 *
 * The delegate now draws every row's leading mark itself, at one size, from the
 * same monochrome set the rest of the window is drawn from. The three cases
 * below check the three halves of that: that a heading, a folder and an item
 * are the same height in all six trees; that a folder's mark really is the
 * folder glyph inked in the muted tone and a broken item's the errors glyph;
 * and that choosing or hovering a row changes its ink but not its height.
 *
 * Run with: ctest -R EditorTreeRowHeightTest -V
 */

#include <QImage>
#include <QMouseEvent>
#include <QStyleOptionViewItem>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest/QtTest>
#include <chrono>

#include "EditorTreeDelegate.h"
#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TAction.h"
#include "TAlias.h"
#include "TKey.h"
#include "TScript.h"
#include "TTimer.h"
#include "TTreeWidget.h"
#include "TTrigger.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorTreeRowHeightTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorTreeRowHeight-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    // The size the delegate draws every row's mark at, which is also the size
    // the tree's heading glyph is drawn at. Written out here rather than shared:
    // a test that reads the number out of the code under test cannot fail when
    // that number moves.
    static constexpr int scmMarkSize = 16;
    // Two pictures composed through the same painter at the same ratio round to
    // the same pixels, but not necessarily to the last unit in each channel
    static constexpr int scmInkTolerance = 2;
    // How far in from the trailing end of a row the painted shape is measured.
    // The pill's corner is 8px, so a reading has to be further in than that to
    // land on the fill rather than on the arc.
    static constexpr int scmClearOfTheCorners = 20;

    // What the fixture below put in each tree, by the id the rows carry
    int mTriggerFolderId = 0;
    int mTriggerItemId = 0;
    int mFilterChainId = 0;
    int mFilteredTriggerId = 0;
    int mBrokenTriggerId = 0;
    int mTimerFolderId = 0;
    int mTimerItemId = 0;
    int mOffsetTimerId = 0;
    int mAliasFolderId = 0;
    int mAliasItemId = 0;
    int mScriptFolderId = 0;
    int mScriptItemId = 0;
    int mActionFolderId = 0;
    int mActionItemId = 0;
    int mKeyFolderId = 0;
    int mKeyItemId = 0;

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

    // Built into the profile before the editor is opened, so that the rows come
    // out of the editor's own fillout_form() rather than being posted into the
    // trees by hand - which is the only way a row carries the accessible
    // description a saved item has, and so the only way it is read as anything
    // but a fresh addition
    void buildTheProfilesItems()
    {
        auto* pTriggerFolder = new TTrigger(qsl("A group of triggers"), QStringList(), QList<int>(), false, mpHost);
        pTriggerFolder->setIsFolder(true);
        pTriggerFolder->setIsActive(true);
        pTriggerFolder->registerTrigger();
        mTriggerFolderId = pTriggerFolder->getID();

        auto* pTrigger = new TTrigger(pTriggerFolder, mpHost);
        pTrigger->setName(qsl("A trigger"));
        pTrigger->setRegexCodeList(QStringList{qsl("hello")}, QList<int>{REGEX_SUBSTRING}, false);
        pTrigger->setIsActive(true);
        pTrigger->registerTrigger();
        mTriggerItemId = pTrigger->getID();

        // A filter chain is a trigger with a pattern that holds other triggers,
        // so it takes a child to be one at all
        auto* pFilterChain = new TTrigger(qsl("A filter chain"), QStringList{qsl("chapter")}, QList<int>{REGEX_SUBSTRING}, false, mpHost);
        pFilterChain->setIsActive(true);
        pFilterChain->registerTrigger();
        mFilterChainId = pFilterChain->getID();
        auto* pFiltered = new TTrigger(pFilterChain, mpHost);
        pFiltered->setName(qsl("Matched inside the chain"));
        pFiltered->setRegexCodeList(QStringList{qsl("verse")}, QList<int>{REGEX_SUBSTRING}, false);
        pFiltered->registerTrigger();
        mFilteredTriggerId = pFiltered->getID();

        // An unterminated group is a pattern PCRE cannot compile, which is what
        // leaves the trigger in the state the errors glyph stands for
        auto* pBroken = new TTrigger(qsl("A broken trigger"), QStringList{qsl("(")}, QList<int>{REGEX_PERL}, false, mpHost);
        pBroken->registerTrigger();
        mBrokenTriggerId = pBroken->getID();

        auto* pTimerFolder = new TTimer(qsl("A group of timers"), QTime(), mpHost);
        pTimerFolder->setIsFolder(true);
        mpHost->getTimerUnit()->registerTimer(pTimerFolder);
        mTimerFolderId = pTimerFolder->getID();

        auto* pTimer = new TTimer(pTimerFolder, mpHost);
        pTimer->setName(qsl("A timer"));
        pTimer->setTime(QTime(0, 0, 5));
        mpHost->getTimerUnit()->registerTimer(pTimer);
        mTimerItemId = pTimer->getID();

        // A timer held inside another timer rather than inside a folder is an
        // offset timer, which is the whole of what makes one
        auto* pOffsetTimer = new TTimer(pTimer, mpHost);
        pOffsetTimer->setName(qsl("An offset timer"));
        pOffsetTimer->setTime(QTime(0, 0, 1));
        mpHost->getTimerUnit()->registerTimer(pOffsetTimer);
        mOffsetTimerId = pOffsetTimer->getID();

        auto* pAliasFolder = new TAlias(qsl("A group of aliases"), mpHost);
        pAliasFolder->setIsFolder(true);
        pAliasFolder->setIsActive(true);
        pAliasFolder->registerAlias();
        mAliasFolderId = pAliasFolder->getID();
        auto* pAlias = new TAlias(pAliasFolder, mpHost);
        pAlias->setName(qsl("An alias"));
        pAlias->setRegexCode(qsl("^greet$"));
        pAlias->setIsActive(true);
        pAlias->registerAlias();
        mAliasItemId = pAlias->getID();

        auto* pScriptFolder = new TScript(qsl("A group of scripts"), mpHost);
        pScriptFolder->setIsFolder(true);
        pScriptFolder->setIsActive(true);
        pScriptFolder->registerScript();
        mScriptFolderId = pScriptFolder->getID();
        auto* pScript = new TScript(pScriptFolder, mpHost);
        pScript->setName(qsl("A script"));
        pScript->setScript(qsl("-- nothing to do\n"));
        pScript->setIsActive(true);
        pScript->registerScript();
        mScriptItemId = pScript->getID();

        auto* pActionFolder = new TAction(qsl("A toolbar"), mpHost);
        pActionFolder->setIsFolder(true);
        pActionFolder->setIsActive(true);
        pActionFolder->registerAction();
        mActionFolderId = pActionFolder->getID();
        auto* pAction = new TAction(pActionFolder, mpHost);
        pAction->setName(qsl("A button"));
        pAction->setIsActive(true);
        pAction->registerAction();
        mActionItemId = pAction->getID();

        auto* pKeyFolder = new TKey(qsl("A group of keys"), mpHost);
        pKeyFolder->setIsFolder(true);
        pKeyFolder->setIsActive(true);
        pKeyFolder->registerKey();
        mKeyFolderId = pKeyFolder->getID();
        auto* pKey = new TKey(pKeyFolder, mpHost);
        pKey->setName(qsl("A key"));
        pKey->setIsActive(true);
        pKey->registerKey();
        mKeyItemId = pKey->getID();
    }

    static QTreeWidgetItem* rowFor(QTreeWidgetItem* pParent, const int id)
    {
        for (int row = 0, rows = pParent->childCount(); row < rows; ++row) {
            QTreeWidgetItem* pChild = pParent->child(row);
            if (pChild->data(0, Qt::UserRole).toInt() == id) {
                return pChild;
            }
            if (QTreeWidgetItem* pFound = rowFor(pChild, id)) {
                return pFound;
            }
        }
        return nullptr;
    }

    // The six trees, each with the heading row of that tree and the rows the
    // fixture put under it - named, so a failure says which row of which tree
    QList<QPair<QString, QPair<TTreeWidget*, QList<QPair<QString, int>>>>> trees() const
    {
        return {{qsl("Triggers"),
                 {mpEditor->treeWidget_triggers,
                  {{qsl("folder"), mTriggerFolderId},
                   {qsl("item"), mTriggerItemId},
                   {qsl("filter chain"), mFilterChainId},
                   {qsl("filtered item"), mFilteredTriggerId},
                   {qsl("broken item"), mBrokenTriggerId}}}},
                {qsl("Timers"), {mpEditor->treeWidget_timers, {{qsl("folder"), mTimerFolderId}, {qsl("item"), mTimerItemId}, {qsl("offset timer"), mOffsetTimerId}}}},
                {qsl("Aliases"), {mpEditor->treeWidget_aliases, {{qsl("folder"), mAliasFolderId}, {qsl("item"), mAliasItemId}}}},
                {qsl("Scripts"), {mpEditor->treeWidget_scripts, {{qsl("folder"), mScriptFolderId}, {qsl("item"), mScriptItemId}}}},
                {qsl("Buttons"), {mpEditor->treeWidget_actions, {{qsl("folder"), mActionFolderId}, {qsl("item"), mActionItemId}}}},
                {qsl("Keys"), {mpEditor->treeWidget_keys, {{qsl("folder"), mKeyFolderId}, {qsl("item"), mKeyItemId}}}}};
    }

    QTreeWidgetItem* headingOf(const TTreeWidget* pTree) const
    {
        if (pTree == mpEditor->treeWidget_triggers) {
            return mpEditor->mpTriggerBaseItem;
        }
        if (pTree == mpEditor->treeWidget_timers) {
            return mpEditor->mpTimerBaseItem;
        }
        if (pTree == mpEditor->treeWidget_aliases) {
            return mpEditor->mpAliasBaseItem;
        }
        if (pTree == mpEditor->treeWidget_scripts) {
            return mpEditor->mpScriptsBaseItem;
        }
        if (pTree == mpEditor->treeWidget_actions) {
            return mpEditor->mpActionBaseItem;
        }
        return mpEditor->mpKeyBaseItem;
    }

    // What the view would lay the row out from, with the mark's ink chosen the
    // way the view chooses it. The delegate is asked rather than the window
    // grabbed: the mark is a 16px square inside a row, and a reading taken off
    // a screenshot could only say what colour is somewhere near it.
    QImage markOf(TTreeWidget* pTree, QTreeWidgetItem* pItem, const bool selected) const
    {
        auto* pDelegate = qobject_cast<uiDesign::EditorTreeDelegate*>(pTree->itemDelegateForIndex(QModelIndex()));
        if (!pDelegate || !pItem) {
            return {};
        }
        const QRect rowRect = pTree->visualItemRect(pItem);
        if (rowRect.isEmpty()) {
            return {};
        }
        QStyleOptionViewItem option = pTree->viewItemOption();
        option.rect = rowRect;
        if (selected) {
            option.state |= QStyle::State_Selected;
        }
        pDelegate->initStyleOption(&option, pTree->indexAt(rowRect.center()));

        const qreal ratio = pTree->devicePixelRatioF();
        const QImage decoration = option.icon.pixmap(option.decorationSize, ratio).toImage().convertToFormat(QImage::Format_ARGB32);
        const int side = qRound(scmMarkSize * ratio);
        if (decoration.width() < side || decoration.height() < side) {
            return {};
        }
        // The mark is the last thing in the composed leading edge, and the slot
        // is exactly its height
        return decoration.copy(QRect(decoration.width() - side, decoration.height() - side, side, side));
    }

    QImage markDrawnFrom(const TTreeWidget* pTree, const QString& glyphFile, const QColor& ink) const
    {
        const qreal ratio = pTree->devicePixelRatioF();
        return uiDesign::tintedGlyph(QPixmap(glyphFile), ink)
                .scaled(QSize(scmMarkSize, scmMarkSize) * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                .toImage()
                .convertToFormat(QImage::Format_ARGB32);
    }

    // How far apart two pictures are in their worst pixel, so a failure can say
    // whether the mark is the wrong shape or merely the wrong colour
    static int worstDifference(const QImage& one, const QImage& other)
    {
        if (one.isNull() || other.isNull() || one.size() != other.size()) {
            return 255;
        }
        int worst = 0;
        for (int y = 0; y < one.height(); ++y) {
            for (int x = 0; x < one.width(); ++x) {
                const QColor here = one.pixelColor(x, y);
                const QColor there = other.pixelColor(x, y);
                worst = std::max({worst, std::abs(here.red() - there.red()), std::abs(here.green() - there.green()), std::abs(here.blue() - there.blue()), std::abs(here.alpha() - there.alpha())});
            }
        }
        return worst;
    }

    // The colour a picture is actually inked in, read off its most opaque pixel
    static QString inkOf(const QImage& glyph)
    {
        if (glyph.isNull()) {
            return qsl("nothing at all");
        }
        QColor strongestColor;
        int strongest = -1;
        for (int y = 0; y < glyph.height(); ++y) {
            for (int x = 0; x < glyph.width(); ++x) {
                const QColor pixel = glyph.pixelColor(x, y);
                if (pixel.alpha() > strongest) {
                    strongest = pixel.alpha();
                    strongestColor = pixel;
                }
            }
        }
        return strongest <= 0 ? qsl("nothing at all") : qsl("%1 at alpha %2").arg(strongestColor.name(), QString::number(strongest));
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

        buildTheProfilesItems();

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(1000, 800);
        // The last case here reads shapes off a shot of the window, and a
        // window the compositor has not shown yet is one whose first paint has
        // not run. Not fatal if it never comes: grab() renders the widget tree
        // itself, so an unexposed window is only a reason to wait rather than a
        // reason to stop.
        if (!QTest::qWaitForWindowExposed(mpEditor, 2000)) {
            qInfo().noquote() << qsl("  the editor window was never exposed; the readings below are off a rendered widget tree alone");
        }

        // The editor is built with the profile rather than on demand, so its
        // trees were filled before the items above existed. Emptied and filled
        // again, they come out of the editor's own reading of the profile -
        // which is what puts a saved item's accessible description on its row,
        // and so the only way a row reads as anything but a fresh addition.
        for (const auto& tree : trees()) {
            tree.second.first->clear();
        }
        mpEditor->fillout_form();
        mpEditor->slot_showTriggers();
        QTest::qWait(50ms);

        // Every row has to be laid out to be measured, and the trees open only
        // their heading row by themselves
        for (const auto& tree : trees()) {
            tree.second.first->expandAll();
        }
        QTest::qWait(50ms);

        // The fixture is the whole of what these cases read, so it is checked
        // before any of them: a tree missing a row would otherwise leave a case
        // comparing nothing at all and passing
        QStringList missing;
        for (const auto& tree : trees()) {
            QTreeWidgetItem* pHeading = headingOf(tree.second.first);
            if (!pHeading || tree.second.first->visualItemRect(pHeading).isEmpty()) {
                missing << qsl("%1: no heading row").arg(tree.first);
                continue;
            }
            for (const auto& row : tree.second.second) {
                QTreeWidgetItem* pRow = rowFor(pHeading, row.second);
                if (!pRow || tree.second.first->visualItemRect(pRow).isEmpty()) {
                    missing << qsl("%1: no %2 row").arg(tree.first, row.first);
                }
            }
        }
        QVERIFY2(missing.isEmpty(), qPrintable(qsl("the profile the cases below read was not built: %1").arg(missing.join(qsl("; ")))));

        // ...and that the three rows whose mark is not simply "folder" really
        // are what the fixture meant them to be
        TTrigger* pFilterChain = mpHost->getTriggerUnit()->getTrigger(mFilterChainId);
        QVERIFY2(pFilterChain && pFilterChain->isFilterChain(), "The trigger the fixture meant as a filter chain is not one");
        TTrigger* pBroken = mpHost->getTriggerUnit()->getTrigger(mBrokenTriggerId);
        QVERIFY2(pBroken && !pBroken->state(), "The trigger the fixture meant to leave broken compiles after all");
        TTimer* pOffset = mpHost->getTimerUnit()->getTimer(mOffsetTimerId);
        QVERIFY2(pOffset && pOffset->isOffsetTimer(), "The timer the fixture meant as an offset timer is not one");
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

    // The whole point: a tree's heading, a folder in it and an item in that
    // folder are one height, in all six trees. A folder used to be as tall as
    // the tree's icon size and a plain item as tall as its 9px dot.
    void test_everyRowOfEveryTreeIsTheSameHeight()
    {
        QStringList uneven;
        QStringList measured;
        for (const auto& tree : trees()) {
            TTreeWidget* pTree = tree.second.first;
            QTreeWidgetItem* pHeading = headingOf(pTree);
            const int headingHeight = pTree->visualItemRect(pHeading).height();
            QStringList heights{qsl("heading %1").arg(headingHeight)};
            for (const auto& row : tree.second.second) {
                const int height = pTree->visualItemRect(rowFor(pHeading, row.second)).height();
                heights << qsl("%1 %2").arg(row.first, QString::number(height));
                if (height != headingHeight) {
                    uneven << qsl("%1: the heading row is %2px and the %3 row %4px").arg(tree.first, QString::number(headingHeight), row.first, QString::number(height));
                }
            }
            measured << qsl("%1 - %2").arg(tree.first, heights.join(qsl(", ")));
        }
        qInfo().noquote() << qsl("  row heights: %1").arg(measured.join(qsl("; ")));

        QVERIFY2(uneven.isEmpty(), qPrintable(qsl("rows in the panel of items are not all one height: %1").arg(uneven.join(qsl("; ")))));
    }

    // ...and what each row leads with is the glyph that says what it is, inked
    // in the tone the rest of the editor's chrome is drawn in
    void test_eachKindOfRowCarriesItsOwnGlyph()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        TTreeWidget* pTriggers = mpEditor->treeWidget_triggers;
        TTreeWidget* pTimers = mpEditor->treeWidget_timers;
        QTreeWidgetItem* pTriggerHeading = mpEditor->mpTriggerBaseItem;
        QTreeWidgetItem* pTimerHeading = mpEditor->mpTimerBaseItem;

        const QList<QPair<QString, QPair<QImage, QImage>>> readings{
                {qsl("a folder"), {markOf(pTriggers, rowFor(pTriggerHeading, mTriggerFolderId), false), markDrawnFrom(pTriggers, qsl(":/icons/editor-folder.png"), tokens.mutedText)}},
                {qsl("a filter chain"), {markOf(pTriggers, rowFor(pTriggerHeading, mFilterChainId), false), markDrawnFrom(pTriggers, qsl(":/icons/editor-filter.png"), tokens.mutedText)}},
                {qsl("a broken item"), {markOf(pTriggers, rowFor(pTriggerHeading, mBrokenTriggerId), false), markDrawnFrom(pTriggers, qsl(":/icons/editor-errors.png"), tokens.mutedText)}},
                {qsl("an offset timer"), {markOf(pTimers, rowFor(pTimerHeading, mOffsetTimerId), false), markDrawnFrom(pTimers, qsl(":/icons/editor-offset-timer.png"), tokens.mutedText)}}};

        QStringList wrong;
        QStringList measured;
        for (const auto& reading : readings) {
            const int difference = worstDifference(reading.second.first, reading.second.second);
            measured << qsl("%1 %2").arg(reading.first, inkOf(reading.second.first));
            if (difference > scmInkTolerance) {
                wrong << qsl("%1: the row is drawn %2 where the glyph it should carry is %3, %4 apart at their worst pixel")
                                 .arg(reading.first, inkOf(reading.second.first), inkOf(reading.second.second), QString::number(difference));
            }
        }
        qInfo().noquote() << qsl("  marks at %1px: %2; the muted tone is %3").arg(QString::number(scmMarkSize), measured.join(qsl(", ")), tokens.mutedText.name());

        QVERIFY2(wrong.isEmpty(), qPrintable(qsl("a row does not lead with the glyph that says what it is: %1").arg(wrong.join(qsl("; ")))));
    }

    // A chosen row's name is written in the accent, and its mark is inked to
    // match rather than staying the muted tone the rest of the list is in
    void test_aChosenRowsMarkIsInkedWithItsName()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        TTreeWidget* pTree = mpEditor->treeWidget_triggers;
        QTreeWidgetItem* pFolder = rowFor(mpEditor->mpTriggerBaseItem, mTriggerFolderId);

        const QImage unchosen = markOf(pTree, pFolder, false);
        const QImage chosen = markOf(pTree, pFolder, true);
        const QImage wantedChosen = markDrawnFrom(pTree, qsl(":/icons/editor-folder.png"), tokens.accentText);
        qInfo().noquote() << qsl("  the folder's mark is %1 unchosen and %2 chosen; the muted tone is %3 and the chosen text colour %4")
                                     .arg(inkOf(unchosen), inkOf(chosen), tokens.mutedText.name(), tokens.accentText.name());

        QVERIFY2(worstDifference(unchosen, chosen) > scmInkTolerance, qPrintable(qsl("a chosen row's mark is inked the same as an unchosen one's: both are %1").arg(inkOf(chosen))));
        QVERIFY2(worstDifference(chosen, wantedChosen) <= scmInkTolerance,
                 qPrintable(qsl("a chosen row's mark is not inked in the colour its name is written in: it is %1 where the name's colour would give %2").arg(inkOf(chosen), inkOf(wantedChosen))));
    }

    // The reader's own reading of it: the wash under the row the pointer is on
    // and the pill under the chosen row are the same height, measured off a shot
    // of the window. A folder used to be drawn eight pixels taller than the item
    // below it, so hovering one and choosing the other put two differently sized
    // shapes on the same short list.
    void test_theHoverFillAndTheSelectionPillAreOneHeight()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        TTreeWidget* pTree = mpEditor->treeWidget_triggers;
        QTreeWidgetItem* pHeading = mpEditor->mpTriggerBaseItem;
        // A row that carries a mark and a row that carries none, far enough
        // apart that the two shapes cannot run into one another and be measured
        // as one
        QTreeWidgetItem* pHovered = rowFor(pHeading, mTriggerFolderId);
        QTreeWidgetItem* pChosen = rowFor(pHeading, mFilteredTriggerId);
        QVERIFY2(pHovered && pChosen, "One of the two rows this case reads is not in the tree");

        pTree->setCurrentItem(pChosen);
        // The panel is a column in a window a profile's worth of items scrolls
        // through, and a row off the bottom of it is painted nowhere
        pTree->scrollToItem(pHovered, QAbstractItemView::PositionAtTop);
        mpEditor->activateWindow();
        pTree->setFocus();
        QTest::qWait(50ms);

        QVERIFY2(pTree->viewport()->rect().contains(pTree->visualItemRect(pHovered)) && pTree->viewport()->rect().contains(pTree->visualItemRect(pChosen)),
                 "Both rows have to be on screen to be read off a shot of the window");
        QVERIFY2(std::abs(pTree->visualItemRect(pHovered).top() - pTree->visualItemRect(pChosen).top()) > pTree->visualItemRect(pHovered).height() + pTree->visualItemRect(pChosen).height(),
                 "The hovered and the chosen row are neighbours, so their two shapes would be measured as one");

        // The unbroken run of painted pixels through the middle of the row,
        // read near its trailing end - clear of the arc its corner is cut with,
        // and past where any of these rows' names reach, so the run stops at the
        // shape's own edge rather than running on into the next row's letters
        const auto paintedHeightAt = [&](const QImage& shot, const QRect& band) {
            // A shot of a window on a screen that doubles its pixels is twice
            // the size the window measures, so a point on the window has to be
            // taken there before it can be read
            const qreal shotRatio = shot.devicePixelRatio();
            const int x = band.right() - scmClearOfTheCorners;
            const auto painted = [&](const int y) {
                const QPoint inWindow = pTree->viewport()->mapTo(mpEditor, QPoint(x, y));
                const QPoint inShot(qRound(inWindow.x() * shotRatio), qRound(inWindow.y() * shotRatio));
                return shot.rect().contains(inShot) && shot.pixelColor(inShot).rgb() != tokens.pane.rgb();
            };
            const int middle = band.center().y();
            if (!painted(middle)) {
                return 0;
            }
            int top = middle;
            int bottom = middle;
            const int reach = 3 * band.height();
            while (middle - top < reach && painted(top - 1)) {
                --top;
            }
            while (bottom - middle < reach && painted(bottom + 1)) {
                ++bottom;
            }
            return bottom - top + 1;
        };

        // Both readings come off one shot, and the shot is taken again if
        // either shape is missing from it. The hover is a state the view holds
        // rather than anything in the model: a leave, or the window losing its
        // activation - which another process opening a window of its own is
        // enough to cause - drops it, and the wash goes with it. So the move is
        // sent again on each attempt rather than once before the first.
        constexpr int scmAttempts = 5;
        QRect hoveredBand;
        QRect chosenBand;
        int hoveredHeight = 0;
        int chosenHeight = 0;
        int attempts = 0;
        while (attempts < scmAttempts && (hoveredHeight == 0 || chosenHeight == 0)) {
            ++attempts;
            hoveredBand = pTree->visualItemRect(pHovered);
            chosenBand = pTree->visualItemRect(pChosen);
            const QPoint hoveredAt = hoveredBand.center();
            QMouseEvent move(QEvent::MouseMove, QPointF(hoveredAt), QPointF(hoveredAt), pTree->viewport()->mapToGlobal(hoveredAt), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(pTree->viewport(), &move);
            QCoreApplication::sendPostedEvents();
            QTest::qWait(50ms);

            const QImage shot = mpEditor->grab().toImage();
            hoveredHeight = paintedHeightAt(shot, hoveredBand);
            chosenHeight = paintedHeightAt(shot, chosenBand);
        }
        qInfo().noquote() << qsl("  the hover fill is %1px tall and the selection pill %2px, on rows of %3px and %4px, read on attempt %5; the panel is %6")
                                     .arg(QString::number(hoveredHeight),
                                          QString::number(chosenHeight),
                                          QString::number(hoveredBand.height()),
                                          QString::number(chosenBand.height()),
                                          QString::number(attempts),
                                          tokens.pane.name());

        QVERIFY2(hoveredHeight > 0, qPrintable(qsl("the row under the pointer is not tinted at all after %1 attempts, so there is nothing to compare the pill with").arg(QString::number(attempts))));
        QVERIFY2(chosenHeight > 0, qPrintable(qsl("the chosen row is not painted at all after %1 attempts, so there is nothing to compare the hover with").arg(QString::number(attempts))));
        QVERIFY2(hoveredHeight == chosenHeight,
                 qPrintable(qsl("the wash under the hovered row is %1px tall and the pill under the chosen row %2px").arg(QString::number(hoveredHeight), QString::number(chosenHeight))));
    }
};

#include "EditorTreeRowHeightTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorTreeRowHeightTest)
