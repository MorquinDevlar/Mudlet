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
 * A broken item's mark and dot are inked uiDesign::errorInk() rather than mutedText,
 * and the WantedOn dot reads by shape as well as ink. Read off a grab in both
 * appearances: a red that cannot be read on the rows' surface is no better than grey.
 *
 * Run with: ctest -R EditorTreeErrorMarkTest -V
 */

#include <QImage>
#include <QStyleOptionViewItem>
#include <QTreeWidget>
#include <QtTest/QtTest>
#include <chrono>

#include "EditorTreeDelegate.h"
#include "EditorTreeRowMetrics.h"
#include "Host.h"
#include "HostDialogs.h"
#include "PixelTestHelper.h"
#include "ProfileSessionTestHelper.h"
#include "TScript.h"
#include "TTreeWidget.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorTreeErrorMarkTest : public QObject
{
    Q_OBJECT

private:
    TestProfile::Session mSession;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorTreeErrorMark-Test-Profile");

    int mBrokenScriptId = 0;
    int mWorkingScriptId = 0;
    int mScriptFolderId = 0;
    int mQuietFolderId = 0;
    int mWantedOnScriptId = 0;

    // Summed over three channels: a glyph composed onto the row at the screen's
    // ratio has its most opaque pixel near the ink rather than exactly it
    static constexpr int scmInkSlack = 30;

    void buildTheProfilesItems()
    {
        auto* pFolder = new TScript(qsl("A group of scripts"), mpHost);
        pFolder->setIsFolder(true);
        pFolder->setIsActive(true);
        pFolder->registerScript();
        mScriptFolderId = pFolder->getID();

        auto* pWorking = new TScript(pFolder, mpHost);
        pWorking->setName(qsl("A working script"));
        pWorking->setScript(qsl("-- nothing to do\n"));
        pWorking->setIsActive(true);
        pWorking->registerScript();
        mWorkingScriptId = pWorking->getID();

        auto* pBroken = new TScript(pFolder, mpHost);
        pBroken->setName(qsl("A broken script"));
        // Failing to compile is what puts an item in the errors glyph's state
        pBroken->setScript(qsl("this is not lua ((\n"));
        pBroken->registerScript();
        mBrokenScriptId = pBroken->getID();

        // A switched-on script inside a switched-off group is the one WantedOn dot
        auto* pQuietFolder = new TScript(qsl("A group that is switched off"), mpHost);
        pQuietFolder->setIsFolder(true);
        pQuietFolder->setIsActive(false);
        pQuietFolder->registerScript();
        mQuietFolderId = pQuietFolder->getID();

        auto* pWantedOn = new TScript(pQuietFolder, mpHost);
        pWantedOn->setName(qsl("A script switched on inside it"));
        pWantedOn->setScript(qsl("-- nothing to do\n"));
        pWantedOn->setIsActive(true);
        pWantedOn->registerScript();
        mWantedOnScriptId = pWantedOn->getID();
    }

    // The style is asked because the trees' stylesheet padding cannot be guessed;
    // the mark is the decoration's last square, after the depth room and the dot
    QRect markRectOf(TTreeWidget* pTree, QTreeWidgetItem* pItem) const
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
        pDelegate->initStyleOption(&option, pTree->indexAt(rowRect.center()));
        const QRect decoration = pTree->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, pTree);
        if (decoration.width() < uiDesign::scmTreeMarkSize) {
            return {};
        }
        return QRect(
                decoration.right() + 1 - uiDesign::scmTreeMarkSize, decoration.top() + (decoration.height() - uiDesign::scmTreeMarkSize) / 2, uiDesign::scmTreeMarkSize, uiDesign::scmTreeMarkSize);
    }

    // dotHitRect() is grown by scmTreeMarkHitSlack for the pointer; taking that
    // back off leaves the dot's own scmTreeDotDiameter square
    QRect dotSquareOf(TTreeWidget* pTree, QTreeWidgetItem* pItem) const
    {
        auto* pDelegate = qobject_cast<uiDesign::EditorTreeDelegate*>(pTree->itemDelegateForIndex(QModelIndex()));
        if (!pDelegate || !pItem) {
            return {};
        }
        const QRect rowRect = pTree->visualItemRect(pItem);
        if (rowRect.isEmpty()) {
            return {};
        }
        const QModelIndex index = pTree->indexAt(rowRect.center());
        const QRect hit = pDelegate->dotHitRect(pDelegate->rowOption(index), index);
        if (hit.isEmpty()) {
            return {};
        }
        const int slack = uiDesign::scmTreeMarkHitSlack;
        return hit.adjusted(slack, slack, -slack, -slack);
    }

    static QColor pixelAt(const QImage& shot, const QPointF& point, const qreal ratio)
    {
        const QPoint inShot(static_cast<int>(point.x() * ratio), static_cast<int>(point.y() * ratio));
        return shot.rect().contains(inShot) ? shot.pixelColor(inShot) : QColor();
    }

    static QPair<int, QColor> nearest(const QImage& shot, const QRect& square, const qreal ratio, const QColor& wanted)
    {
        int best = 3 * 255;
        QColor read;
        for (int y = square.top(); y <= square.bottom(); ++y) {
            for (int x = square.left(); x <= square.right(); ++x) {
                const QPoint inShot(qRound(x * ratio), qRound(y * ratio));
                if (!shot.rect().contains(inShot)) {
                    continue;
                }
                const QColor sample = shot.pixelColor(inShot);
                const int away = TestPixels::distance(sample, wanted);
                if (away < best) {
                    best = away;
                    read = sample;
                }
            }
        }
        return {best, read};
    }

    // The 0.2px margin keeps the band clear of both the point's and the ring's
    // antialiasing, so what is read is the surface
    static QPair<int, QColor> nearestInTheRingsGap(const QImage& shot, const QRect& square, const qreal ratio, const QColor& wanted)
    {
        constexpr qreal margin = 0.2;
        const qreal centrePointReach = uiDesign::scmTreeDotCentreDiameter / 2.0 + margin;
        const qreal ringInnerEdge = (uiDesign::scmTreeDotDiameter - 2.0 * uiDesign::scmTreeHollowPenWidth) / 2.0 - margin;
        const QPointF middle(square.left() + uiDesign::scmTreeDotDiameter / 2.0, square.top() + uiDesign::scmTreeDotDiameter / 2.0);

        int best = 3 * 255;
        QColor read;
        const QPoint topLeft(static_cast<int>(square.left() * ratio), static_cast<int>(square.top() * ratio));
        const int extent = static_cast<int>(uiDesign::scmTreeDotDiameter * ratio);
        for (int y = topLeft.y(); y < topLeft.y() + extent; ++y) {
            for (int x = topLeft.x(); x < topLeft.x() + extent; ++x) {
                if (!shot.rect().contains(QPoint(x, y))) {
                    continue;
                }
                const qreal away = std::hypot((x + 0.5) / ratio - middle.x(), (y + 0.5) / ratio - middle.y());
                if (away < centrePointReach || away > ringInnerEdge) {
                    continue;
                }
                const QColor sample = shot.pixelColor(QPoint(x, y));
                const int off = TestPixels::distance(sample, wanted);
                if (off < best) {
                    best = off;
                    read = sample;
                }
            }
        }
        return {best, read};
    }

    static QList<QPair<QString, enums::Appearance>> appearances() { return {{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}; }

private slots:
    void initTestCase()
    {
        if (TestProfile::Session::configCannotBeRedirected()) {
            QSKIP(TestProfile::Session::configCannotBeRedirectedReason());
        }

        QVERIFY2(mSession.start(mProfileName), qPrintable(mSession.why()));
        mpHost = mSession.host();

        buildTheProfilesItems();

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = HostDialogs::of(mpHost).mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(1000, 800);
        if (!QTest::qWaitForWindowExposed(mpEditor, 2000)) {
            qInfo().noquote() << qsl("  the editor window was never exposed; the readings below are off a rendered widget tree alone");
        }

        // The editor is built with the profile, so its trees were filled before
        // the scripts above existed
        mpEditor->treeWidget_scripts->clear();
        mpEditor->fillout_form();
        mpEditor->slot_showScripts();
        QTest::qWait(50ms);
        mpEditor->treeWidget_scripts->expandAll();
        QTest::qWait(50ms);

        // Checked first: a script that compiled after all would be reported as a
        // fault of the ink rather than of the fixture
        TScript* pBroken = mpHost->getScriptUnit()->getScript(mBrokenScriptId);
        QVERIFY2(pBroken && !pBroken->state(), "The script the fixture meant to leave broken compiles after all");
        QVERIFY2(TestPixels::rowFor(mpEditor->mpScriptsBaseItem, mBrokenScriptId) != nullptr, "The broken script has no row in the Scripts tree");
        QVERIFY2(TestPixels::rowFor(mpEditor->mpScriptsBaseItem, mScriptFolderId) != nullptr, "The folder holding it has no row in the Scripts tree");

        auto* pDelegate = qobject_cast<uiDesign::EditorTreeDelegate*>(mpEditor->treeWidget_scripts->itemDelegateForIndex(QModelIndex()));
        QVERIFY2(pDelegate != nullptr, "The Scripts tree is not drawn by an EditorTreeDelegate");
        const auto readingOf = [this, pDelegate](const int id) {
            QTreeWidgetItem* pRow = TestPixels::rowFor(mpEditor->mpScriptsBaseItem, id);
            return pRow ? pDelegate->stateOf(mpEditor->treeWidget_scripts->indexAt(mpEditor->treeWidget_scripts->visualItemRect(pRow).center())).dot : uiDesign::EditorTreeDelegate::DotState::Off;
        };
        QCOMPARE(readingOf(mWorkingScriptId), uiDesign::EditorTreeDelegate::DotState::Running);
        QVERIFY2(TestPixels::rowFor(mpEditor->mpScriptsBaseItem, mQuietFolderId) != nullptr, "The switched-off group has no row in the Scripts tree");
        QCOMPARE(readingOf(mWantedOnScriptId), uiDesign::EditorTreeDelegate::DotState::WantedOn);
    }

    void cleanupTestCase()
    {
        mpEditor = nullptr;
        mpHost = nullptr;
        mSession.stop();
    }

    void test_aBrokenItemsMarkIsPaintedInTheErrorInk()
    {
        const auto appearanceBefore = mudlet::self()->mAppearance;
        auto restore = qScopeGuard([appearanceBefore]() {
            mudlet::self()->setAppearance(appearanceBefore);
        });

        TTreeWidget* pTree = mpEditor->treeWidget_scripts;
        QStringList misses;
        QStringList measured;
        for (const auto& appearance : appearances()) {
            TestPixels::setAppearanceAndWait(appearance.second);
            const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
            const QColor red = uiDesign::errorInk(tokens);

            const QRect brokenMark = markRectOf(pTree, TestPixels::rowFor(mpEditor->mpScriptsBaseItem, mBrokenScriptId));
            const QRect folderMark = markRectOf(pTree, TestPixels::rowFor(mpEditor->mpScriptsBaseItem, mScriptFolderId));
            QVERIFY2(!brokenMark.isEmpty() && !folderMark.isEmpty(), qPrintable(qsl("%1: one of the two marks has no rectangle to sample").arg(appearance.first)));

            const QImage shot = pTree->viewport()->grab().toImage();
            const qreal ratio = shot.devicePixelRatio();
            const auto redOnTheBroken = nearest(shot, brokenMark, ratio, red);
            const auto greyOnTheBroken = nearest(shot, brokenMark, ratio, tokens.mutedText);
            const auto greyOnTheFolder = nearest(shot, folderMark, ratio, tokens.mutedText);
            const auto redOnTheFolder = nearest(shot, folderMark, ratio, red);
            measured
                    << qsl("%1: the broken row's mark comes within %2 of the red %3 (nearest pixel %4) and within %5 of the muted %6; the folder's comes within %7 of the muted tone and %8 of the red")
                               .arg(appearance.first, QString::number(redOnTheBroken.first), red.name(), redOnTheBroken.second.name())
                               .arg(greyOnTheBroken.first)
                               .arg(tokens.mutedText.name())
                               .arg(greyOnTheFolder.first)
                               .arg(redOnTheFolder.first);

            if (redOnTheBroken.first > scmInkSlack) {
                misses << qsl("%1: no pixel of the broken row's mark is the error ink %2 - the nearest is %3, %4 away")
                                  .arg(appearance.first, red.name(), redOnTheBroken.second.name(), QString::number(redOnTheBroken.first));
            }
            if (greyOnTheBroken.first <= scmInkSlack) {
                misses << qsl("%1: the broken row's mark holds a pixel within %2 of the chrome's %3, so it cannot be told from a folder's")
                                  .arg(appearance.first, QString::number(greyOnTheBroken.first), tokens.mutedText.name());
            }
            // Nearer of two tones rather than a floor: a 1.5px stroke scaled
            // into a 16px square never reaches the full ink at any one pixel
            if (greyOnTheFolder.first >= redOnTheFolder.first) {
                misses << qsl("%1: the folder's mark is nearer the red %2 (%3 away) than the chrome's %4 (%5 away), so this case is reading the wrong squares")
                                  .arg(appearance.first, red.name(), QString::number(redOnTheFolder.first), tokens.mutedText.name(), QString::number(greyOnTheFolder.first));
            }
        }
        qInfo().noquote() << qsl("  %1").arg(measured.join(qsl("\n  ")));
        QVERIFY2(misses.isEmpty(), qPrintable(qsl("\n  %1").arg(misses.join(qsl("\n  ")))));
    }

    void test_theErrorInkCanBeReadOnThePanelTheRowsSitOn()
    {
        const auto appearanceBefore = mudlet::self()->mAppearance;
        auto restore = qScopeGuard([appearanceBefore]() {
            mudlet::self()->setAppearance(appearanceBefore);
        });

        TTreeWidget* pTree = mpEditor->treeWidget_scripts;
        QStringList misses;
        QStringList measured;
        for (const auto& appearance : appearances()) {
            TestPixels::setAppearanceAndWait(appearance.second);
            const QColor red = uiDesign::errorInk(uiDesign::themeTokens());

            // Below the rows, where nothing is painted over the viewport's own surface
            const QImage shot = pTree->viewport()->grab().toImage();
            const qreal ratio = shot.devicePixelRatio();
            const QRect lastRow = pTree->visualItemRect(pTree->topLevelItem(0));
            const QPoint below(pTree->viewport()->width() / 2, pTree->viewport()->height() - qMax(4, lastRow.height() / 2));
            const QPoint inShot(qRound(below.x() * ratio), qRound(below.y() * ratio));
            QVERIFY2(shot.rect().contains(inShot), qPrintable(qsl("%1: the tree has no empty room in it to read the surface off").arg(appearance.first)));
            const QColor surface = shot.pixelColor(inShot);
            const qreal ratioAgainstSurface = uiDesign::contrastRatio(red, surface);
            measured << qsl("%1: the error ink %2 reads at %3:1 on the panel's %4").arg(appearance.first, red.name(), QString::number(ratioAgainstSurface, 'f', 2), surface.name());
            if (ratioAgainstSurface < uiDesign::scmQuietMinimumRatio) {
                misses << qsl("%1: the error ink %2 reads at only %3:1 against the %4 the rows sit on").arg(appearance.first, red.name(), QString::number(ratioAgainstSurface, 'f', 2), surface.name());
            }
        }
        qInfo().noquote() << qsl("  %1").arg(measured.join(qsl("\n  ")));
        QVERIFY2(misses.isEmpty(), qPrintable(qsl("\n  %1").arg(misses.join(qsl("\n  ")))));
    }

    void test_aBrokenItemsDotIsPaintedInTheErrorInk()
    {
        const auto appearanceBefore = mudlet::self()->mAppearance;
        auto restore = qScopeGuard([appearanceBefore]() {
            mudlet::self()->setAppearance(appearanceBefore);
        });

        TTreeWidget* pTree = mpEditor->treeWidget_scripts;
        QStringList misses;
        QStringList measured;
        for (const auto& appearance : appearances()) {
            TestPixels::setAppearanceAndWait(appearance.second);
            const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
            const QColor red = uiDesign::errorInk(tokens);
            const QColor okTone = uiDesign::stateColor(uiDesign::scmStateHue_ok, tokens.darkPage);

            const QRect brokenDot = dotSquareOf(pTree, TestPixels::rowFor(mpEditor->mpScriptsBaseItem, mBrokenScriptId));
            const QRect workingDot = dotSquareOf(pTree, TestPixels::rowFor(mpEditor->mpScriptsBaseItem, mWorkingScriptId));
            QVERIFY2(!brokenDot.isEmpty() && !workingDot.isEmpty(), qPrintable(qsl("%1: one of the two dots has no rectangle to sample").arg(appearance.first)));

            const QImage shot = pTree->viewport()->grab().toImage();
            const qreal ratio = shot.devicePixelRatio();
            const auto redOnTheBroken = nearest(shot, brokenDot, ratio, red);
            const auto greyOnTheBroken = nearest(shot, brokenDot, ratio, tokens.mutedText);
            const auto okOnTheWorking = nearest(shot, workingDot, ratio, okTone);
            const auto redOnTheWorking = nearest(shot, workingDot, ratio, red);
            measured << qsl("%1: the broken row's dot comes within %2 of the red %3 (nearest pixel %4) and within %5 of the muted %6; the working row's comes within %7 of the ok tone %8 "
                            "(nearest pixel %9) and %10 of the red")
                                .arg(appearance.first, QString::number(redOnTheBroken.first), red.name(), redOnTheBroken.second.name())
                                .arg(greyOnTheBroken.first)
                                .arg(tokens.mutedText.name())
                                .arg(okOnTheWorking.first)
                                .arg(okTone.name(), okOnTheWorking.second.name())
                                .arg(redOnTheWorking.first);

            if (redOnTheBroken.first > scmInkSlack) {
                misses << qsl("%1: no pixel of the broken row's dot is the error ink %2 - the nearest is %3, %4 away")
                                  .arg(appearance.first, red.name(), redOnTheBroken.second.name(), QString::number(redOnTheBroken.first));
            }
            if (greyOnTheBroken.first <= redOnTheBroken.first) {
                misses << qsl("%1: the broken row's dot is nearer the chrome's %2 (%3 away) than the red %4 (%5 away), so it is still drawn in the quiet ink")
                                  .arg(appearance.first, tokens.mutedText.name(), QString::number(greyOnTheBroken.first), red.name(), QString::number(redOnTheBroken.first));
            }
            if (okOnTheWorking.first > scmInkSlack) {
                misses << qsl("%1: no pixel of the working row's dot is the ok tone %2 - the nearest is %3, %4 away")
                                  .arg(appearance.first, okTone.name(), okOnTheWorking.second.name(), QString::number(okOnTheWorking.first));
            }
            if (redOnTheWorking.first <= scmInkSlack) {
                misses << qsl("%1: the working row's dot holds a pixel within %2 of the red %3, so the red has reached a row that is not broken")
                                  .arg(appearance.first, QString::number(redOnTheWorking.first), red.name());
            }
        }
        qInfo().noquote() << qsl("  %1").arg(measured.join(qsl("\n  ")));
        QVERIFY2(misses.isEmpty(), qPrintable(qsl("\n  %1").arg(misses.join(qsl("\n  ")))));
    }

    void test_aWantedOnDotIsARingRoundAPoint()
    {
        const auto appearanceBefore = mudlet::self()->mAppearance;
        auto restore = qScopeGuard([appearanceBefore]() {
            mudlet::self()->setAppearance(appearanceBefore);
        });

        TTreeWidget* pTree = mpEditor->treeWidget_scripts;
        QStringList misses;
        QStringList measured;
        for (const auto& appearance : appearances()) {
            TestPixels::setAppearanceAndWait(appearance.second);
            const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
            const QColor okTone = uiDesign::stateColor(uiDesign::scmStateHue_ok, tokens.darkPage);

            const QRect dot = dotSquareOf(pTree, TestPixels::rowFor(mpEditor->mpScriptsBaseItem, mWantedOnScriptId));
            QVERIFY2(!dot.isEmpty(), qPrintable(qsl("%1: the switched-on row's dot has no rectangle to sample").arg(appearance.first)));

            const QImage shot = pTree->viewport()->grab().toImage();
            const qreal ratio = shot.devicePixelRatio();
            // Read off the row rather than assumed: the depth room before the
            // dot is unpainted on a row with nothing folded inside it
            const QColor surface = pixelAt(shot, QPointF(dot.left() - uiDesign::scmTreeDotGap, dot.center().y()), ratio);
            QVERIFY2(surface.isValid(), qPrintable(qsl("%1: there is no room beside the dot to read the row's own surface off").arg(appearance.first)));

            const QColor middle = pixelAt(shot, QPointF(dot.left() + uiDesign::scmTreeDotDiameter / 2.0, dot.top() + uiDesign::scmTreeDotDiameter / 2.0), ratio);
            QVERIFY2(middle.isValid(), qPrintable(qsl("%1: the middle of the dot is outside the grab").arg(appearance.first)));
            const int middleFromQuiet = TestPixels::distance(middle, tokens.mutedText);
            const int middleFromSurface = TestPixels::distance(middle, surface);
            const auto surfaceInTheGap = nearestInTheRingsGap(shot, dot, ratio, surface);
            const auto quietOnTheRing = nearest(shot, dot, ratio, tokens.mutedText);
            const auto okOnTheRing = nearest(shot, dot, ratio, okTone);
            measured << qsl("%1: the middle reads %2, %3 off the muted %4 and %5 off the row's %6; the gap comes within %7 of that surface (nearest pixel %8); the dot comes within %9 of the "
                            "muted tone and %10 of the ok tone %11")
                                .arg(appearance.first, middle.name())
                                .arg(middleFromQuiet)
                                .arg(tokens.mutedText.name())
                                .arg(middleFromSurface)
                                .arg(surface.name())
                                .arg(surfaceInTheGap.first)
                                .arg(surfaceInTheGap.second.name())
                                .arg(quietOnTheRing.first)
                                .arg(okOnTheRing.first)
                                .arg(okTone.name());

            // Nearer of two tones rather than a floor: the dot sits half a pixel
            // down its slot, so a 3px point never reaches full ink at any pixel
            if (middleFromQuiet * 2 >= middleFromSurface) {
                misses << qsl("%1: the middle of the dot reads %2 - %3 off the muted %4 and %5 off the row's own %6, so there is no point standing in the ring")
                                  .arg(appearance.first, middle.name(), QString::number(middleFromQuiet), tokens.mutedText.name(), QString::number(middleFromSurface), surface.name());
            }
            if (surfaceInTheGap.first > scmInkSlack) {
                misses << qsl("%1: no pixel between that point and the ring is the row's own %2 - the nearest is %3, %4 away, so the dot is filled rather than a ring round a point")
                                  .arg(appearance.first, surface.name(), surfaceInTheGap.second.name(), QString::number(surfaceInTheGap.first));
            }
            if (okOnTheRing.first <= quietOnTheRing.first) {
                misses << qsl("%1: the dot is nearer the ok tone %2 (%3 away) than the muted %4 (%5 away), so a row that is not running is drawn as though it were")
                                  .arg(appearance.first, okTone.name(), QString::number(okOnTheRing.first), tokens.mutedText.name(), QString::number(quietOnTheRing.first));
            }
        }
        qInfo().noquote() << qsl("  %1").arg(measured.join(qsl("\n  ")));
        QVERIFY2(misses.isEmpty(), qPrintable(qsl("\n  %1").arg(misses.join(qsl("\n  ")))));
    }
};

MUDLET_GROUPED_TEST_MAIN(EditorTreeErrorMarkTest)
#include "EditorTreeErrorMarkTest.moc"
