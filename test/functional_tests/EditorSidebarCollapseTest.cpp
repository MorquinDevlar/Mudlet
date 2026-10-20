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
 * Guards the sidebar's two collapses against each other: the seam chevron's choice
 * persists under editorSidebarLabelsShown, while a too-narrow window's forced rail
 * must never reach it, or one small-window session decides every later one.
 */

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QListWidget>
#include <QPixmap>
#include <QProxyStyle>
#include <QScopeGuard>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionViewItem>
#include <QtTest/QtTest>
#include <chrono>

#include "Host.h"
#include "HostDialogs.h"
#include "MudletApp.h"
#include "ProfileSessionTestHelper.h"
#include "SidebarToggle.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

// On a desktop the appearances' styles leave different text margins (2px a side
// under the dark Fusion proxy, 4px under macOS's own), which decides elision;
// offscreen both are Fusion, so the difference is installed
namespace {
class WiderTextMarginStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;

    int pixelMetric(PixelMetric metric, const QStyleOption* pOption, const QWidget* pWidget) const override
    {
        const int given = QProxyStyle::pixelMetric(metric, pOption, pWidget);
        return metric == PM_FocusFrameHMargin ? given + 4 : given;
    }
};

// Two thirds of the way from 1.0 to the 1.64x a bold name measured; one weight
// in both states comes to 1.16x from the colours and the pill alone
constexpr double scmBoldInkFloor = 1.40;

// A solid pen's stems come back opaque; only antialiased edges are partial, and
// those follow the background rather than the weight
constexpr int scmOpaqueInk = 192;
constexpr int scmInkSpread = 24;

// A weight cannot be read back off a widget, so the picture is the only witness
int inkPixels(const QImage& picture, const QRect& area, const QColor& ink)
{
    int found = 0;
    const QRect inPicture = area.intersected(picture.rect());
    for (int y = inPicture.top(); y <= inPicture.bottom(); ++y) {
        for (int x = inPicture.left(); x <= inPicture.right(); ++x) {
            const QColor pixel = picture.pixelColor(x, y);
            if (pixel.alpha() < scmOpaqueInk) {
                continue;
            }
            const int spread = qMax(qMax(qAbs(pixel.red() - ink.red()), qAbs(pixel.green() - ink.green())), qAbs(pixel.blue() - ink.blue()));
            if (spread <= scmInkSpread) {
                ++found;
            }
        }
    }
    return found;
}
} // namespace

class EditorSidebarCollapseTest : public QObject
{
    Q_OBJECT

private:
    TestProfile::Session mSession;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorSidebarCollapse-Test-Profile");

    // Measured off the editor: the breakpoint is the widest translated name plus
    // the narrowest body, neither of which is a number in the source
    int mWideEnough = 0;
    int mTooNarrow = 0;
    int mHeight = 700;

    // The property the delegate and the sidebar's rules read, so it is what is
    // drawn rather than a copy of the intent
    bool railShowing() const { return mpEditor->mpListWidget_editorSidebar->property(uiDesign::scmProp_rail).toBool(); }

    bool storedLabelsShown() const { return MudletApp::getQSettings()->value(qsl("editorSidebarLabelsShown"), true).toBool(); }

    static bool anythingStored() { return MudletApp::getQSettings()->contains(qsl("editorSidebarLabelsShown")); }

    uiDesign::SidebarToggle* toggle() const { return mpEditor->mpToggle_editorSidebar; }

    // The chevron's parent, so the frame both it and the seam are measured in
    QWidget* shell() const { return mpEditor->centralWidget(); }

    int offsetFromTheSeam() const
    {
        QWidget* pPane = mpEditor->mpWidget_editorSidebarPane;
        const int seamX = pPane->mapTo(shell(), QPoint(0, 0)).x() + pPane->width();
        return qRound(toggle()->x() + toggle()->width() / 2.0) - seamX;
    }

    void resizeEditor(const int width)
    {
        mpEditor->resize(width, mHeight);
        QCoreApplication::sendPostedEvents();
        QTest::qWait(50ms);
    }

    void pressTheToggle()
    {
        QVERIFY2(toggle() != nullptr, "The editor has no sidebar toggle");
        QVERIFY2(toggle()->isEnabled(), "The sidebar toggle is not pressable at this width");
        QTest::mouseClick(toggle(), Qt::LeftButton);
        QCoreApplication::sendPostedEvents();
        QTest::qWait(50ms);
    }

    // In bold, as a chosen row is drawn. QCommonStyle draws inside the text rect
    // shrunk by PM_FocusFrameHMargin + 1 a side, so a name that just fits it is elided.
    QStringList namesTooNarrowForTheirRows(QStringList& measured) const
    {
        QListWidget* pList = mpEditor->mpListWidget_editorSidebar;
        QFont nameFont = pList->font();
        nameFont.setBold(true);
        const QFontMetrics nameMetrics(nameFont);

        QStyleOptionViewItem option;
        option.initFrom(pList);
        option.font = nameFont;
        option.fontMetrics = nameMetrics;
        option.features |= QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration;
        option.decorationSize = pList->iconSize();
        option.decorationPosition = QStyleOptionViewItem::Left;
        option.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
        // Any picture of the right size: a row reserves the glyph's space, not the glyph
        QPixmap blank(pList->iconSize());
        blank.fill(Qt::transparent);
        option.icon = QIcon(blank);

        const int drawingMargin = 2 * (pList->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, &option, pList) + 1);
        QStringList squeezed;
        int tightest = 0;
        QString tightestName;
        for (int row = 0, rows = pList->count(); row < rows; ++row) {
            QListWidgetItem* pItem = pList->item(row);
            // Divider rows have no name; the Folders row hides while the profile lacks that view
            if (pItem->text().isEmpty() || pItem->isHidden()) {
                continue;
            }
            option.text = pItem->text();
            option.rect = pList->visualItemRect(pItem);
            const int room = pList->style()->subElementRect(QStyle::SE_ItemViewItemText, &option, pList).width() - drawingMargin;
            const int needed = nameMetrics.horizontalAdvance(pItem->text());
            if (tightestName.isEmpty() || room - needed < tightest) {
                tightest = room - needed;
                tightestName = pItem->text();
            }
            if (room < needed) {
                squeezed << qsl("\"%1\" has %2px of a row %3px wide for a name %4px across").arg(pItem->text(), QString::number(room), QString::number(option.rect.width()), QString::number(needed));
            }
        }
        measured << qsl("sidebar %1px, style %2, %3px of margin, tightest row \"%4\" with %5px to spare")
                            .arg(QString::number(mpEditor->mpWidget_editorSidebarPane->width()),
                                 QString::number(pList->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, &option, pList)),
                                 QString::number(drawingMargin),
                                 tightestName,
                                 QString::number(tightest));
        return squeezed;
    }

    // Taken in bold, so the same rect covers the name in either state and both
    // pictures are read at the same place
    QRect nameAreaOf(const int row) const
    {
        QListWidget* pList = mpEditor->mpListWidget_editorSidebar;
        QFont nameFont = pList->font();
        nameFont.setBold(true);

        QStyleOptionViewItem option;
        option.initFrom(pList);
        option.font = nameFont;
        option.fontMetrics = QFontMetrics(nameFont);
        option.features |= QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration;
        option.decorationSize = pList->iconSize();
        option.decorationPosition = QStyleOptionViewItem::Left;
        option.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
        QPixmap blank(pList->iconSize());
        blank.fill(Qt::transparent);
        option.icon = QIcon(blank);
        option.text = pList->item(row)->text();
        option.rect = pList->visualItemRect(pList->item(row));
        // visualItemRect answers in the viewport's coordinates and grab() takes
        // the whole list, frame included
        return pList->style()->subElementRect(QStyle::SE_ItemViewItemText, &option, pList).translated(pList->viewport()->mapTo(pList, QPoint(0, 0)));
    }

    // The longest name shows a change of weight most
    int longestNamedRow() const
    {
        QListWidget* pList = mpEditor->mpListWidget_editorSidebar;
        int longest = -1;
        for (int row = 0, rows = pList->count(); row < rows; ++row) {
            QListWidgetItem* pItem = pList->item(row);
            if (pItem->text().isEmpty() || pItem->isHidden() || !(pItem->flags() & Qt::ItemIsSelectable)) {
                continue;
            }
            if (longest < 0 || pItem->text().length() > pList->item(longest)->text().length()) {
                longest = row;
            }
        }
        return longest;
    }

    int aNamedRowOtherThan(const int row) const
    {
        QListWidget* pList = mpEditor->mpListWidget_editorSidebar;
        for (int other = 0, rows = pList->count(); other < rows; ++other) {
            QListWidgetItem* pItem = pList->item(other);
            if (other != row && !pItem->text().isEmpty() && !pItem->isHidden() && (pItem->flags() & Qt::ItemIsSelectable)) {
                return other;
            }
        }
        return -1;
    }

    void chooseRow(const int row)
    {
        mpEditor->mpListWidget_editorSidebar->setCurrentRow(row);
        QCoreApplication::sendPostedEvents();
        QTest::qWait(50ms);
    }

    QString state() const
    {
        return qsl("window %1, sidebar %2, rail %3, stored labelsShown %4")
                .arg(QString::number(mpEditor->width()),
                     QString::number(mpEditor->mpWidget_editorSidebarPane->width()),
                     railShowing() ? qsl("yes") : qsl("no"),
                     storedLabelsShown() ? qsl("true") : qsl("false"));
    }

private slots:
    void initTestCase()
    {
        if (TestProfile::Session::configCannotBeRedirected()) {
            QSKIP(TestProfile::Session::configCannotBeRedirectedReason());
        }

        QVERIFY2(mSession.start(mProfileName), qPrintable(mSession.why()));
        mpHost = mSession.host();

        mudlet::self()->slot_showEditorDialog();
        QTest::qWait(100ms);
        mpEditor = HostDialogs::of(mpHost).mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");

        // Only measurable once shown: the breakpoint comes from splitter_main's
        // minimum size hint, which is stale until its layout has run
        const dlgTriggerEditor::EditorSidebarWidths widths = mpEditor->editorSidebarWidths();
        mWideEnough = widths.collapseBelow + 120;
        mTooNarrow = widths.collapseBelow - 40;
        qInfo().noquote() << qsl("  sidebar expanded at %1px, names given up below %2px; editor minimum width %3px")
                                     .arg(QString::number(widths.expanded), QString::number(widths.collapseBelow), QString::number(mpEditor->minimumWidth()));
        // The layout's minimum is the sidebar's fixed width plus the body's, so a
        // breakpoint at or below it is one no drag can reach
        QVERIFY2(mTooNarrow >= mpEditor->minimumWidth(),
                 qPrintable(qsl("The editor cannot be dragged below its own breakpoint (minimum %1px, breakpoint %2px), so the forced rail is unreachable")
                                    .arg(QString::number(mpEditor->minimumWidth()), QString::number(widths.collapseBelow))));

        resizeEditor(mWideEnough);
    }

    void cleanupTestCase()
    {
        mpEditor = nullptr;
        mpHost = nullptr;
        mSession.stop();
    }

    void test_aFreshProfileStartsWithTheNamesShowing()
    {
        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(!railShowing(), qPrintable(qsl("A fresh profile opened as a rail: %1").arg(state())));
        // A sidebar that kept the rail's width while its property said otherwise
        // would pass on the property alone
        QVERIFY2(mpEditor->mpWidget_editorSidebarPane->width() > uiDesign::scmSidebarRailWidth,
                 qPrintable(qsl("the sidebar is at the rail's %1px with its names showing: %2").arg(QString::number(uiDesign::scmSidebarRailWidth), state())));
        QVERIFY2(!anythingStored(), "opening the editor wrote a sidebar preference nobody chose, so what a later run opens with is not the default this case reads");
        QVERIFY2(toggle() != nullptr, "The editor has no sidebar toggle");
        QVERIFY2(toggle()->isEnabled(), "The sidebar toggle cannot be pressed on a window with room for the names");
    }

    // Read in both modes: the seam moves when the sidebar gives its names up
    void test_theToggleRidesTheSeamBesideTheSidebar()
    {
        QVERIFY2(toggle() != nullptr, "The editor has no sidebar toggle");
        QVERIFY2(toggle()->parentWidget() == shell(), "The toggle is a child of one of the two things it lies between, which would clip it");

        QWidget* pPane = mpEditor->mpWidget_editorSidebarPane;
        QStringList measured;
        QStringList adrift;
        for (const QString& mode : {qsl("with the names showing"), qsl("as a rail")}) {
            const int fromTheSeam = offsetFromTheSeam();
            const int paneMiddle = pPane->mapTo(shell(), QPoint(0, 0)).y() + pPane->height() / 2;
            const int fromTheMiddle = qRound(toggle()->y() + toggle()->height() / 2.0) - paneMiddle;
            measured << qsl("%1: the sidebar ends at x=%2 and the chevron's middle is %3px off it, %4px off the pane's own middle")
                                .arg(mode, QString::number(pPane->mapTo(shell(), QPoint(0, 0)).x() + pPane->width()), QString::number(fromTheSeam), QString::number(fromTheMiddle));
            // A pixel either way: the pill straddles a line rather than filling
            // a column, so its middle lands on the boundary between two pixels
            if (std::abs(fromTheSeam) > 1) {
                adrift << qsl("%1, %2px off the seam").arg(mode, QString::number(fromTheSeam));
            }
            if (std::abs(fromTheMiddle) > 1) {
                adrift << qsl("%1, %2px off the middle of the pane").arg(mode, QString::number(fromTheMiddle));
            }
            pressTheToggle();
        }
        // The second press put the names back, the state the next case starts from
        qInfo().noquote() << qsl("  %1").arg(measured.join(qsl("; ")));

        QVERIFY2(adrift.isEmpty(), qPrintable(qsl("the sidebar toggle is not on the seam: %1").arg(adrift.join(qsl("; ")))));
    }

    // Read off the picture as well as the flag, so a chevron that stopped
    // following the mode is caught
    void test_theChevronTurnsRoundWithTheMode()
    {
        QVERIFY2(!railShowing(), "This case starts from the names showing");
        QVERIFY2(toggle()->pointingLeft(), "The names are showing and the chevron does not point at the sidebar");
        const QImage pointingIn = toggle()->grab().toImage();

        pressTheToggle();
        QVERIFY2(railShowing(), "The toggle did not minimise the sidebar");
        QVERIFY2(!toggle()->pointingLeft(), "The sidebar is a rail and the chevron still points at it");
        const QImage pointingOut = toggle()->grab().toImage();
        qInfo().noquote() << qsl("  the chevron is %1px by %2px and is drawn two ways").arg(QString::number(toggle()->width()), QString::number(toggle()->height()));
        QVERIFY2(pointingIn != pointingOut, "The chevron is drawn the same way in both of the sidebar's modes");

        pressTheToggle();
        QVERIFY2(!railShowing() && toggle()->pointingLeft(), "The toggle did not turn back round with the names");
    }

    void test_theToggleNamesWhatItDoesToTheLabels()
    {
        const QString expanded = toggle()->accessibleName();
        QVERIFY2(!expanded.isEmpty(), "The sidebar toggle has no accessible name");
        qInfo().noquote() << qsl("  with the names showing: \"%1\"").arg(expanded);

        pressTheToggle();
        const QString collapsed = toggle()->accessibleName();
        qInfo().noquote() << qsl("  as a rail: \"%1\"").arg(collapsed);
        QVERIFY2(collapsed != expanded, "The sidebar toggle says the same thing in both modes");

        // The sidebar is always there, so no wording may be about opening or closing it
        for (const QString& wording : {expanded, collapsed}) {
            QVERIFY2(!wording.contains(qsl("close"), Qt::CaseInsensitive) && !wording.contains(qsl("open"), Qt::CaseInsensitive),
                     qPrintable(qsl("The sidebar toggle reads as opening or closing the sidebar: \"%1\"").arg(wording)));
        }
    }

    // Starts from the rail the previous case's press left
    void test_aManualRailStaysOnAWideWindow()
    {
        QVERIFY2(railShowing(), qPrintable(qsl("The toggle did not minimise the sidebar: %1").arg(state())));

        resizeEditor(mWideEnough + 200);
        QVERIFY2(railShowing(), qPrintable(qsl("A wider window put the names back over the user's choice: %1").arg(state())));
        resizeEditor(mWideEnough);
    }

    void test_theTogglePutsTheNamesBack()
    {
        pressTheToggle();
        QVERIFY2(!railShowing(), qPrintable(qsl("The toggle did not bring the names back: %1").arg(state())));
    }

    // mEditorSidebarLabelsShown is what a close writes out
    void test_aNarrowWindowForcesTheRailWithoutStoringIt()
    {
        QVERIFY2(!railShowing(), "This case starts from the names showing");

        resizeEditor(mTooNarrow);
        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(railShowing(), qPrintable(qsl("A window too narrow for the names kept them: %1").arg(state())));
        QVERIFY2(mpEditor->mEditorSidebarLabelsShown, qPrintable(qsl("Running out of room rewrote the user's preference: %1").arg(state())));
        QVERIFY2(!toggle()->isEnabled(), "The toggle offers labels a window this narrow cannot draw");
    }

    void test_wideningTheWindowBringsTheNamesBack()
    {
        resizeEditor(mWideEnough);
        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(!railShowing(), qPrintable(qsl("The names did not come back when the window had room again: %1").arg(state())));
    }

    void test_aManualRailSurvivesAFreshEditor()
    {
        pressTheToggle();
        QVERIFY2(railShowing(), "The toggle did not minimise the sidebar");

        mpEditor->close();
        QCoreApplication::processEvents();
        QVERIFY2(!storedLabelsShown(), "Closing the editor did not write the toggle's choice down");

        delete mpEditor;
        mpEditor = nullptr;
        QVERIFY2(HostDialogs::of(mpHost).mpEditorDialog.isNull(), "The editor should have been destroyed");

        mudlet::self()->slot_showEditorDialog();
        QTest::qWait(100ms);
        mpEditor = HostDialogs::of(mpHost).mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "A fresh editor should have been created");
        resizeEditor(mWideEnough);

        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(railShowing(), qPrintable(qsl("A fresh editor forgot that the names had been given up: %1").arg(state())));
    }

    // sidebarRowWidth() measures names in bold. A font-weight on ::item never
    // reaches the painter, which uses the style option's font, so the weight is
    // SidebarItemDelegate's and only the picture can prove it.
    void test_theChosenRowIsDrawnBold()
    {
        if (railShowing()) {
            pressTheToggle();
        }
        QVERIFY2(!railShowing(), qPrintable(qsl("this case needs the names showing: %1").arg(state())));

        QListWidget* pList = mpEditor->mpListWidget_editorSidebar;
        const int chosen = longestNamedRow();
        const int other = aNamedRowOtherThan(chosen);
        QVERIFY2(chosen >= 0 && other >= 0, "the editor's sidebar has too few named rows to tell a chosen one from the rest");
        const QString name = pList->item(chosen)->text();
        const QRect nameArea = nameAreaOf(chosen);

        // A width that changed with the selection would shift every row under
        // the pointer
        const int paneBefore = mpEditor->mpWidget_editorSidebarPane->width();
        const int rowBefore = pList->visualItemRect(pList->item(chosen)).width();

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        chooseRow(chosen);
        QCOMPARE(pList->currentRow(), chosen);
        const int inkWhenChosen = inkPixels(pList->grab().toImage(), nameArea, tokens.accentText);
        const int paneChosen = mpEditor->mpWidget_editorSidebarPane->width();
        const int rowChosen = pList->visualItemRect(pList->item(chosen)).width();

        // Unchosen names take mutedText, the item colour sidebarStyleSheet() is handed
        chooseRow(other);
        QCOMPARE(pList->currentRow(), other);
        const int inkWhenNot = inkPixels(pList->grab().toImage(), nameArea, tokens.mutedText);

        const double ratio = inkWhenNot > 0 ? static_cast<double>(inkWhenChosen) / inkWhenNot : 0.0;
        qInfo().noquote() << qsl("  \"%1\" is inked with %2px chosen against %3px unchosen (%4x, floor %5x); pane %6px throughout, row %7px")
                                     .arg(name,
                                          QString::number(inkWhenChosen),
                                          QString::number(inkWhenNot),
                                          QString::number(ratio, 'f', 2),
                                          QString::number(scmBoldInkFloor, 'f', 2),
                                          QString::number(paneBefore),
                                          QString::number(rowChosen));

        QVERIFY2(inkWhenChosen > 0 && inkWhenNot > 0,
                 qPrintable(qsl("\"%1\" was read in a %2x%3 rect at %4,%5 and holds no ink in either state (%6px chosen, %7px unchosen), so the two pictures say nothing")
                                    .arg(name,
                                         QString::number(nameArea.width()),
                                         QString::number(nameArea.height()),
                                         QString::number(nameArea.x()),
                                         QString::number(nameArea.y()),
                                         QString::number(inkWhenChosen),
                                         QString::number(inkWhenNot))));
        QVERIFY2(ratio >= scmBoldInkFloor,
                 qPrintable(
                         qsl("\"%1\" is drawn with %2px of ink when it is the chosen row and %3px when it is not - %4x, under the %5x a bold name comes to, so the chosen row is not being drawn bold")
                                 .arg(name, QString::number(inkWhenChosen), QString::number(inkWhenNot), QString::number(ratio, 'f', 2), QString::number(scmBoldInkFloor, 'f', 2))));
        QVERIFY2(paneChosen == paneBefore && rowChosen == rowBefore,
                 qPrintable(qsl("choosing \"%1\" moved the sidebar: pane %2px to %3px, row %4px to %5px")
                                    .arg(name, QString::number(paneBefore), QString::number(paneChosen), QString::number(rowBefore), QString::number(rowChosen))));
    }

    void test_everyNameFitsItsRowUnderTheStyleDrawingThem()
    {
        const auto appearanceBefore = mudlet::self()->mAppearance;
        auto restore = qScopeGuard([appearanceBefore]() {
            // The loading flag makes setAppearance() rebuild its own style even
            // for the appearance already in force
            mudlet::self()->setAppearance(appearanceBefore, true);
        });
        if (railShowing()) {
            pressTheToggle();
        }
        QVERIFY2(!railShowing(), qPrintable(qsl("this case needs the names showing: %1").arg(state())));

        QStringList squeezed;
        const auto walkTheRows = [this, &squeezed](const QString& drawnBy) {
            QCoreApplication::sendPostedEvents();
            QTest::qWait(100ms);
            // The breakpoint moves with the style, and a window left too narrow
            // would answer with a rail
            resizeEditor(mpEditor->editorSidebarWidths().collapseBelow + 120);
            // Reported, not asserted: QVERIFY2 in a lambda only returns from it,
            // leaving the case passing on an empty list
            if (railShowing()) {
                squeezed << qsl("%1: the sidebar gave its names up altogether (%2)").arg(drawnBy, state());
                return;
            }

            QStringList measured;
            const QStringList tooNarrow = namesTooNarrowForTheirRows(measured);
            qInfo().noquote() << qsl("  %1: %2").arg(drawnBy, measured.join(qsl("; ")));
            for (const QString& complaint : tooNarrow) {
                squeezed << qsl("%1: %2").arg(drawnBy, complaint);
            }
        };

        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            mudlet::self()->setAppearance(appearance.second);
            walkTheRows(appearance.first);
        }

        qApp->setStyle(new WiderTextMarginStyle(QStyleFactory::create(qsl("Fusion"))));
        walkTheRows(qsl("a style with wider text margins"));

        QVERIFY2(squeezed.isEmpty(), qPrintable(qsl("the editor's sidebar does not hold its names under every style: %1").arg(squeezed.join(qsl("; ")))));
    }
};

#include "EditorSidebarCollapseTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorSidebarCollapseTest)
