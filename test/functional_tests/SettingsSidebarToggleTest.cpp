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
 * Guards the settings sidebar's chevron and its stored labels-shown preference:
 * a window too narrow for the names forces the rail but must never write it to
 * the preference, or one small window would decide every later session.
 *
 * Run with: ctest -R SettingsSidebarToggleTest -V
 */

#include <QAbstractButton>
#include <QColor>
#include <QComboBox>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QProxyStyle>
#include <QScopeGuard>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionViewItem>
#include <QtTest/QtTest>
#include <chrono>

#include "Host.h"
#include "MudletApp.h"
#include "ProfileSessionTestHelper.h"
#include "SettingsTestHelper.h"
#include "SidebarToggle.h"
#include "dlgProfilePreferences.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

// On a desktop the appearances' styles leave different text margins (2px under
// dark's Fusion proxy, 4px under macOS's style), deciding whether a name elides.
// Offscreen both appearances are Fusion, so the difference is installed instead.
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

// Measured: a bold chosen name inks 1.59x its unchosen self, one weight in both
// states 1.11x (colours and pill alone); 1.40 is 2/3 of the way from 1.0 to 1.59.
constexpr double scmBoldInkFloor = 1.40;

// A solid pen leaves stems opaque on any background; only antialiased edges
// fall below, and those follow the background rather than the weight.
constexpr int scmOpaqueInk = 192;
constexpr int scmInkSpread = 24;

// A font weight cannot be read back off a widget, so the grab is the only witness.
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

class SettingsSidebarToggleTest : public QObject
{
    Q_OBJECT

private:
    TestProfile::Session mSession;
    QByteArray mSavedNoThemeDownload;
    Host* mpHost = nullptr;
    dlgProfilePreferences* mpPreferences = nullptr;
    const QString mProfileName = qsl("SettingsSidebarToggle-Test");
    // The dialog's minimum size, well under the width the names need
    const QSize mTooNarrow = QSize(780, 560);
    const QSize mWideEnough = QSize(1060, 760);

    QWidget* sidebar() const { return mpPreferences->findChild<QWidget*>(qsl("settingsSidebar")); }
    QWidget* shell() const { return mpPreferences->findChild<QWidget*>(qsl("settingsShell")); }
    QListWidget* categories() const { return TestSettings::sidebar(mpPreferences); }
    uiDesign::SidebarToggle* toggle() const { return mpPreferences->findChild<uiDesign::SidebarToggle*>(qsl("settingsSidebarToggle")); }

    // The property the delegate and the stylesheet read: what is drawn, not a copy of the intent
    bool railShowing() const { return categories()->property(uiDesign::scmProp_rail).toBool(); }

    static bool storedLabelsShown() { return MudletApp::getQSettings()->value(qsl("settingsSidebarLabelsShown"), true).toBool(); }

    static bool anythingStored() { return MudletApp::getQSettings()->contains(qsl("settingsSidebarLabelsShown")); }

    int widestBoldName() const
    {
        QFont nameFont = categories()->font();
        nameFont.setBold(true);
        const QFontMetrics nameMetrics(nameFont);
        int widest = 0;
        for (int row = 0, rows = categories()->count(); row < rows; ++row) {
            widest = std::max(widest, nameMetrics.horizontalAdvance(categories()->item(row)->text()));
        }
        return widest;
    }

    // QCommonStyle draws inside the text rect shrunk by PM_FocusFrameHMargin + 1
    // on each side, so a name that only just fits the rect is still elided.
    QStringList namesTooNarrowForTheirRows(QStringList& measured) const
    {
        QListWidget* pList = categories();
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
        // Any blank of the right size: a row's cost is the glyph's space, not the glyph
        QPixmap blank(pList->iconSize());
        blank.fill(Qt::transparent);
        option.icon = QIcon(blank);

        const int drawingMargin = 2 * (pList->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, &option, pList) + 1);
        QStringList squeezed;
        int tightest = 0;
        QString tightestName;
        for (int row = 0, rows = pList->count(); row < rows; ++row) {
            QListWidgetItem* pItem = pList->item(row);
            // The divider rows, which have a frame in place of a name
            if (pItem->text().isEmpty()) {
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
                            .arg(QString::number(sidebar()->width()),
                                 QString::number(pList->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, &option, pList)),
                                 QString::number(drawingMargin),
                                 tightestName,
                                 QString::number(tightest));
        return squeezed;
    }

    // In bold, so the same rect covers the name chosen or not and both grabs read the same place
    QRect nameAreaOf(const int row) const
    {
        QListWidget* pList = categories();
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

    // The longest name shows a change of weight most. The support link at the
    // foot of the list is named but never selectable, so its flags keep it out.
    int longestNamedRow() const
    {
        QListWidget* pList = categories();
        int longest = -1;
        for (int row = 0, rows = pList->count(); row < rows; ++row) {
            QListWidgetItem* pItem = pList->item(row);
            if (pItem->text().isEmpty() || !(pItem->flags() & Qt::ItemIsSelectable)) {
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
        QListWidget* pList = categories();
        for (int other = 0, rows = pList->count(); other < rows; ++other) {
            QListWidgetItem* pItem = pList->item(other);
            if (other != row && !pItem->text().isEmpty() && (pItem->flags() & Qt::ItemIsSelectable)) {
                return other;
            }
        }
        return -1;
    }

    void chooseRow(const int row)
    {
        categories()->setCurrentRow(row);
        QCoreApplication::sendPostedEvents();
        QTest::qWait(50ms);
    }

    // The dialog's own control: the path that restyles every open window, not only this one
    void takeTheDialogTo(const enums::Appearance appearance)
    {
        if (mpPreferences->comboBox_appearance->currentIndex() == appearance) {
            mpPreferences->comboBox_appearance->setCurrentIndex(appearance == enums::Appearance::dark ? enums::Appearance::light : enums::Appearance::dark);
            QCoreApplication::sendPostedEvents();
            QTest::qWait(100ms);
        }
        mpPreferences->comboBox_appearance->setCurrentIndex(appearance);
        QCoreApplication::sendPostedEvents();
        QTest::qWait(100ms);
    }

    void openPreferences()
    {
        mpPreferences = new dlgProfilePreferences(mudlet::self(), mpHost);
        mpPreferences->resize(mWideEnough);
        mpPreferences->show();
        QVERIFY(QTest::qWaitForWindowExposed(mpPreferences));
        QVERIFY2(categories(), "the settings shell has no category sidebar");
        QVERIFY2(toggle(), "the settings sidebar has no chevron on its seam");
    }

    void resizeDialog(const QSize& size)
    {
        mpPreferences->resize(size);
        QCoreApplication::sendPostedEvents();
        QTest::qWait(50ms);
    }

    void pressTheToggle()
    {
        QVERIFY2(toggle() != nullptr, "the settings dialog has no sidebar toggle");
        QVERIFY2(toggle()->isEnabled(), "the sidebar toggle is not pressable at this width");
        QTest::mouseClick(toggle(), Qt::LeftButton);
        QCoreApplication::sendPostedEvents();
        QTest::qWait(50ms);
    }

    QString state() const
    {
        return qsl("window %1, sidebar %2, rail %3, stored labelsShown %4")
                .arg(QString::number(mpPreferences->width()),
                     QString::number(sidebar()->width()),
                     railShowing() ? qsl("yes") : qsl("no"),
                     anythingStored() ? (storedLabelsShown() ? qsl("true") : qsl("false")) : qsl("nothing"));
    }

private slots:
    void initTestCase()
    {
        if (TestProfile::Session::configCannotBeRedirected()) {
            QSKIP(TestProfile::Session::configCannotBeRedirectedReason());
        }

        mSavedNoThemeDownload = qgetenv("MUDLET_TEST_NO_THEME_DOWNLOAD");
        qputenv("MUDLET_TEST_NO_THEME_DOWNLOAD", "1");

        QVERIFY2(mSession.start(mProfileName), qPrintable(mSession.why()));
        mpHost = mSession.host();
        QVERIFY2(!anythingStored(), "the sidebar preference was already written before a dialog was ever opened, so the default below proves nothing");

        openPreferences();
    }

    void cleanupTestCase()
    {
        delete mpPreferences;
        mpPreferences = nullptr;
        mpHost = nullptr;
        mSession.stop();
        mSavedNoThemeDownload.isNull() ? qunsetenv("MUDLET_TEST_NO_THEME_DOWNLOAD") : qputenv("MUDLET_TEST_NO_THEME_DOWNLOAD", mSavedNoThemeDownload);
    }

    void test_aDialogNobodyHasToldOpensWithItsNames()
    {
        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(!railShowing(), qPrintable(qsl("the settings dialog opened as a rail: %1").arg(state())));
        QVERIFY2(sidebar()->width() > uiDesign::scmSidebarRailWidth, qPrintable(qsl("the sidebar has the rail's width with its names showing: %1").arg(state())));
        QVERIFY2(!anythingStored(), "opening the dialog wrote a preference nobody chose");
        QVERIFY2(toggle()->isEnabled(), "the chevron cannot be pressed on a window with room for the names");
        QVERIFY2(toggle()->pointingLeft(), "the names are showing and the chevron does not point at the sidebar");
        QListWidgetItem* pItem = categories()->item(TestSettings::rowOf(mpPreferences, qsl("mapper")));
        QVERIFY2(!pItem->text().isEmpty(), "a row lost the text that is its accessible name");
        QVERIFY2(pItem->toolTip().isEmpty(), "a row with its name on show still repeats it as a tooltip");
        QVERIFY2(!mpPreferences->findChild<QLabel*>(qsl("settingsWordmark"))->isHidden(), "the wordmark is hidden beside a sidebar with room for it");
    }

    // Read in both modes: the seam moves when the sidebar becomes a rail
    void test_theToggleRidesTheSeamBesideTheSidebar()
    {
        QVERIFY2(toggle()->parentWidget() == shell(), "the toggle is a child of one of the two things it lies between, which would clip it");

        QStringList measured;
        QStringList adrift;
        for (const QString& mode : {qsl("with the names showing"), qsl("as a rail")}) {
            const QPoint paneTopLeft = sidebar()->mapTo(shell(), QPoint(0, 0));
            const int seamX = paneTopLeft.x() + sidebar()->width();
            const int fromTheSeam = qRound(toggle()->x() + toggle()->width() / 2.0) - seamX;
            const int paneMiddle = paneTopLeft.y() + sidebar()->height() / 2;
            const int fromTheMiddle = qRound(toggle()->y() + toggle()->height() / 2.0) - paneMiddle;
            measured << qsl("%1: the sidebar ends at x=%2 and the chevron's middle is %3px off it, %4px off the pane's own middle")
                                .arg(mode, QString::number(seamX), QString::number(fromTheSeam), QString::number(fromTheMiddle));
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
        qInfo().noquote() << qsl("  %1").arg(measured.join(qsl("; ")));
        QVERIFY2(adrift.isEmpty(), qPrintable(qsl("the settings sidebar's toggle is not on the seam: %1").arg(adrift.join(qsl("; ")))));
        // The next case starts from the names showing
        QVERIFY2(!railShowing(), qPrintable(qsl("the second press did not put the names back: %1").arg(state())));
    }

    // The rail keeps each row's text, which a screen reader announces, and shows it on hover
    void test_theToggleTakesTheNamesAwayAndWritesItDown()
    {
        pressTheToggle();
        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(railShowing(), qPrintable(qsl("the toggle did not take the names away: %1").arg(state())));
        QCOMPARE(sidebar()->width(), uiDesign::scmSidebarRailWidth);
        QVERIFY2(anythingStored() && !storedLabelsShown(), qPrintable(qsl("pressing the toggle did not write the choice down: %1").arg(state())));
        QVERIFY2(!toggle()->pointingLeft(), "the sidebar is a rail and the chevron still points into it");
        QListWidgetItem* pItem = categories()->item(TestSettings::rowOf(mpPreferences, qsl("mapper")));
        QCOMPARE(pItem->toolTip(), pItem->text());
        QVERIFY2(mpPreferences->findChild<QLabel*>(qsl("settingsWordmark"))->isHidden(), "the wordmark is drawn beside a rail that has no room for it");

        // Back to the names, where the cases below start
        pressTheToggle();
        QVERIFY2(!railShowing(), qPrintable(qsl("the second press did not put the names back: %1").arg(state())));
        QVERIFY2(storedLabelsShown(), qPrintable(qsl("putting the names back did not write the choice down: %1").arg(state())));
    }

    void test_theSidebarIsAsWideAsItsNamesAndNoWider()
    {
        QVERIFY2(!railShowing(), "this case starts from the names showing");
        const int measured = sidebar()->width();
        const int widest = widestBoldName();
        qInfo().noquote() << qsl("  the sidebar is %1px wide for a widest bold name of %2px").arg(QString::number(measured), QString::number(widest));
        QVERIFY2(measured >= widest + uiDesign::scmSidebarRailWidth,
                 qPrintable(qsl("the sidebar is %1px wide, which leaves the widest name (%2px) less than a rail's width of chrome around it").arg(measured).arg(widest)));
        QVERIFY2(measured <= 232, qPrintable(qsl("the measured sidebar is %1px, wider than the flat 232px it replaced").arg(measured)));
        QWidget* pWordmarkRow = mpPreferences->findChild<QLabel*>(qsl("settingsWordmark"))->parentWidget();
        QVERIFY2(pWordmarkRow->width() >= pWordmarkRow->sizeHint().width(),
                 qPrintable(qsl("the wordmark row wants %1px and was given %2px").arg(pWordmarkRow->sizeHint().width()).arg(pWordmarkRow->width())));
    }

    void test_aNarrowWindowForcesTheRailWithoutStoringIt()
    {
        QVERIFY2(!railShowing(), "this case starts from the names showing");

        resizeDialog(mTooNarrow);
        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(railShowing(), qPrintable(qsl("a window too narrow for the names kept them: %1").arg(state())));
        QVERIFY2(storedLabelsShown(), qPrintable(qsl("running out of room rewrote the user's preference: %1").arg(state())));
        QVERIFY2(!toggle()->isEnabled(), "the toggle offers labels a window this narrow cannot draw");
        QVERIFY2(toggle()->toolTip() != toggle()->accessibleName(), "the toggle a narrow window has disabled still offers to show the labels");

        resizeDialog(mWideEnough);
        QVERIFY2(!railShowing(), qPrintable(qsl("the names did not come back when the window had room again: %1").arg(state())));
        QVERIFY2(toggle()->isEnabled(), "the toggle is still refusing on a window with room for the names");
    }

    void test_theToggleNamesWhatItDoesToTheLabels()
    {
        const QString showing = toggle()->accessibleName();
        QVERIFY2(!showing.isEmpty(), "the sidebar toggle has no accessible name");
        pressTheToggle();
        const QString rail = toggle()->accessibleName();
        qInfo().noquote() << qsl("  with the names showing: \"%1\"; as a rail: \"%2\"").arg(showing, rail);
        QVERIFY2(rail != showing, "the sidebar toggle says the same thing in both modes");
        for (const QString& wording : {showing, rail}) {
            QVERIFY2(!wording.contains(qsl("close"), Qt::CaseInsensitive) && !wording.contains(qsl("open"), Qt::CaseInsensitive),
                     qPrintable(qsl("the sidebar toggle reads as opening or closing the sidebar: \"%1\"").arg(wording)));
        }
        QVERIFY2(!storedLabelsShown(), qPrintable(qsl("the press above did not write the rail down: %1").arg(state())));
    }

    void test_theChoiceSurvivesAFreshDialog()
    {
        QVERIFY2(railShowing(), "this case starts from the rail the case above left");
        delete mpPreferences;
        mpPreferences = nullptr;
        openPreferences();
        QVERIFY2(railShowing(), qPrintable(qsl("a fresh dialog forgot that the names had been given up: %1").arg(state())));

        pressTheToggle();
        QVERIFY2(!railShowing(), "the toggle did not put the names up");
        delete mpPreferences;
        mpPreferences = nullptr;
        openPreferences();
        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(!railShowing(), qPrintable(qsl("a fresh dialog forgot that the names had been asked for: %1").arg(state())));
    }

    // sidebarRowWidth() measures in bold, so a regular chosen row means a pane wider
    // than it needs. An ::item font-weight never reaches SidebarItemDelegate's
    // painter, so nothing structural proves the weight and the grab is asked.
    void test_theChosenRowIsDrawnBold()
    {
        if (railShowing()) {
            pressTheToggle();
        }
        QVERIFY2(!railShowing(), qPrintable(qsl("this case needs the names showing: %1").arg(state())));
        resizeDialog(mWideEnough);

        QListWidget* pList = categories();
        const int chosen = longestNamedRow();
        const int other = aNamedRowOtherThan(chosen);
        QVERIFY2(chosen >= 0 && other >= 0, "the settings sidebar has too few named rows to tell a chosen one from the rest");
        const QString name = pList->item(chosen)->text();
        const QRect nameArea = nameAreaOf(chosen);

        // A width that changed with the selection would shift every row under the pointer
        const int paneBefore = sidebar()->width();
        const int rowBefore = pList->visualItemRect(pList->item(chosen)).width();

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        chooseRow(chosen);
        QCOMPARE(pList->currentRow(), chosen);
        const int inkWhenChosen = inkPixels(pList->grab().toImage(), nameArea, tokens.accentText);
        const int paneChosen = sidebar()->width();
        const int rowChosen = pList->visualItemRect(pList->item(chosen)).width();

        // tokens.text, not the editor's muted tone: the item colour this sidebar
        // hands sidebarStyleSheet()
        chooseRow(other);
        QCOMPARE(pList->currentRow(), other);
        const int inkWhenNot = inkPixels(pList->grab().toImage(), nameArea, tokens.text);

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
            // The loading flag makes setAppearance rebuild its own style for an
            // appearance already in force, replacing WiderTextMarginStyle
            mudlet::self()->setAppearance(appearanceBefore, true);
        });
        if (railShowing()) {
            pressTheToggle();
        }
        QVERIFY2(!railShowing(), qPrintable(qsl("this case needs the names showing: %1").arg(state())));
        resizeDialog(mWideEnough);

        QStringList squeezed;
        const auto walkTheRows = [this, &squeezed](const QString& drawnBy) {
            QCoreApplication::sendPostedEvents();
            QTest::qWait(100ms);
            // Reported, not asserted: QVERIFY2 would only return from the lambda,
            // leaving the walk unrun and the case passing on an empty list
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
            takeTheDialogTo(appearance.second);
            walkTheRows(appearance.first);
        }

        qApp->setStyle(new WiderTextMarginStyle(QStyleFactory::create(qsl("Fusion"))));
        walkTheRows(qsl("a style with wider text margins"));

        QVERIFY2(squeezed.isEmpty(), qPrintable(qsl("the settings dialog's sidebar does not hold its names under every style: %1").arg(squeezed.join(qsl("; ")))));
    }
};

#include "SettingsSidebarToggleTest.moc"
MUDLET_GROUPED_TEST_MAIN(SettingsSidebarToggleTest)
