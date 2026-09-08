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
 * The settings dialog's sidebar is the editor's sidebar: the same component,
 * the same rail, and now the same chevron on the seam between it and what it
 * selects.
 *
 * Two things decide what that sidebar is drawn as, and only one of them is the
 * user's. The chevron's choice is kept across sessions under
 * settingsSidebarLabelsShown - a labels-shown preference, since the sidebar has
 * no closed state to remember: it minimises to a rail of icons and every
 * category goes on being one click away, with its name as a tooltip. A dialog
 * nobody has told opens with the names, as the editor does. A window too
 * narrow to draw the names
 * takes them away regardless, and that one is transient: it must never reach
 * the stored preference, or a stretch of work in a small window would decide
 * what every later session opens with.
 *
 * The width the names are drawn at is measured off the names themselves rather
 * than written down, the way the editor's is - a flat 232px was what this
 * sidebar used to be held to whatever it held.
 *
 * Run with: ctest -R SettingsSidebarToggleTest -V
 */

#include <QAbstractButton>
#include <QColor>
#include <QComboBox>
#include <QDir>
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
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <chrono>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "SettingsTestHelper.h"
#include "SidebarToggle.h"
#include "TelnetServerStub.h"
#include "dlgProfilePreferences.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

// What a style leaves round an item's text is the whole of what the two
// appearances differ by on a desktop - two pixels either side under the dark
// theme's Fusion proxy, four under the platform's own style on macOS - and it
// is what decides whether a row's name is drawn out or elided. Under the
// offscreen platform these run on both appearances are Fusion, so the
// difference is installed rather than waited for.
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

// How much more of a row's name is inked once it is the chosen one. Bold stems
// cover more pixels than regular ones of the same size, and this is two thirds
// of the way from no difference at all to the 1.59x measured here - a name
// drawn at one weight in both states comes to 1.11x, which is the whole of what
// the colours and the pill under them are worth.
constexpr double scmBoldInkFloor = 1.40;

// A name is painted with a solid pen, so its stems come back opaque whatever
// they are drawn over. Only the antialiased edges are part of the way there,
// and those follow the background rather than the weight.
constexpr int scmOpaqueInk = 192;
constexpr int scmInkSpread = 24;

// The pixels of one colour a rect holds. A weight cannot be read back off a
// widget, so the picture is the only witness there is.
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
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    QTemporaryDir mCacheDir;
    QByteArray mSavedXdgCache;
    QByteArray mSavedNoThemeDownload;
    TelnetServerStub* mpServer = nullptr;
    Host* mpHost = nullptr;
    dlgProfilePreferences* mpPreferences = nullptr;
    const QString mProfileName = qsl("SettingsSidebarToggle-Test");
    QString mPort; // assigned the stub's actual ephemeral port in initTestCase()
    const QString mLocalhost = qsl("localhost");
    // The size the dialog refuses to go below, which is well under the width
    // the names need
    const QSize mTooNarrow = QSize(780, 560);
    const QSize mWideEnough = QSize(1060, 760);

    QWidget* sidebar() const { return mpPreferences->findChild<QWidget*>(qsl("settingsSidebar")); }
    QWidget* shell() const { return mpPreferences->findChild<QWidget*>(qsl("settingsShell")); }
    QListWidget* categories() const { return TestSettings::sidebar(mpPreferences); }
    uiDesign::SidebarToggle* toggle() const { return mpPreferences->findChild<uiDesign::SidebarToggle*>(qsl("settingsSidebarToggle")); }

    // The mode is the property the shared delegate and the sidebar's rules both
    // read, so it is what the window is actually drawing rather than a copy of
    // the intent
    bool railShowing() const { return categories()->property(uiDesign::scmProp_rail).toBool(); }

    static bool storedLabelsShown() { return mudlet::getQSettings()->value(qsl("settingsSidebarLabelsShown"), true).toBool(); }

    static bool anythingStored() { return mudlet::getQSettings()->contains(qsl("settingsSidebarLabelsShown")); }

    // The widest of the names the sidebar has to be able to draw, in the bold a
    // chosen row is written in
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

    // What each row leaves for its name against what the name comes to, in the
    // bold a chosen row is drawn in. The text rect is not the answer on its
    // own: QCommonStyle draws inside it shrunk by PM_FocusFrameHMargin + 1 on
    // each side, so a name that only just fits the rect is still elided.
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
        // Any picture of the right size: what a row costs beside its name is
        // the space the glyph is given, not the glyph
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

    // Where a row's name is drawn, in the list's own coordinates. Taken with the
    // bold the chosen row is written in, so that the same rect covers the name
    // in either state and the two pictures are read at the same place.
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

    // The selectable row with the longest name, where a change of weight has the
    // most to show for itself. The support link at the foot of the list is
    // named but never chosen, so its flags keep it out.
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

    // The dialog's own control, which is the path that restyles every open
    // window rather than only this one
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
        if (portableMarkerPresent()) {
            QSKIP("portable.txt present - it takes precedence over XDG_CONFIG_HOME, so the config dir cannot be redirected");
        }

        // A config root of this process's own, so that the preference this is
        // about starts out unwritten however the machine's own is set
        QVERIFY(mConfigDir.isValid());
        QVERIFY(QDir().mkpath(qsl("%1/mudlet/profiles").arg(mConfigDir.path())));
        mSavedXdg = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", mConfigDir.path().toUtf8());
        QVERIFY(mCacheDir.isValid());
        mSavedXdgCache = qgetenv("XDG_CACHE_HOME");
        qputenv("XDG_CACHE_HOME", mCacheDir.path().toUtf8());
        mSavedNoThemeDownload = qgetenv("MUDLET_TEST_NO_THEME_DOWNLOAD");
        qputenv("MUDLET_TEST_NO_THEME_DOWNLOAD", "1");

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
        TestSettings::deleteProfileDirectory(mProfileName);

        mpHost = TestProfile::create(mProfileName, mLocalhost, mPort);
        QVERIFY2(mpHost, "No active host after profile creation");
        QVERIFY2(!anythingStored(), "the sidebar preference was already written before a dialog was ever opened, so the default below proves nothing");

        openPreferences();
    }

    void cleanupTestCase()
    {
        delete mpPreferences;
        mpPreferences = nullptr;
        mpHost = nullptr;
        delete mpServer;
        mpServer = nullptr;
        if (mudlet::self()) {
            TestSettings::deleteProfileDirectory(mProfileName);
            delete mudlet::self();
        }
        mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME") : qputenv("XDG_CONFIG_HOME", mSavedXdg);
        mSavedXdgCache.isNull() ? qunsetenv("XDG_CACHE_HOME") : qputenv("XDG_CACHE_HOME", mSavedXdgCache);
        mSavedNoThemeDownload.isNull() ? qunsetenv("MUDLET_TEST_NO_THEME_DOWNLOAD") : qputenv("MUDLET_TEST_NO_THEME_DOWNLOAD", mSavedNoThemeDownload);
    }

    // Nothing stored, a window with all the room it needs: the dialog opens
    // with its names showing, the way the editor does
    void test_aDialogNobodyHasToldOpensWithItsNames()
    {
        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(!railShowing(), qPrintable(qsl("the settings dialog opened as a rail: %1").arg(state())));
        QVERIFY2(sidebar()->width() > uiDesign::scmSidebarRailWidth, qPrintable(qsl("the sidebar has the rail's width with its names showing: %1").arg(state())));
        QVERIFY2(!anythingStored(), "opening the dialog wrote a preference nobody chose");
        QVERIFY2(toggle()->isEnabled(), "the chevron cannot be pressed on a window with room for the names");
        // Pointing the way the sidebar will go: into the seam, since pressing
        // it is what takes the names away
        QVERIFY2(toggle()->pointingLeft(), "the names are showing and the chevron does not point at the sidebar");
        // With the names on show a row needs no tooltip to say what it is
        QListWidgetItem* pItem = categories()->item(TestSettings::rowOf(mpPreferences, qsl("mapper")));
        QVERIFY2(!pItem->text().isEmpty(), "a row lost the text that is its accessible name");
        QVERIFY2(pItem->toolTip().isEmpty(), "a row with its name on show still repeats it as a tooltip");
        QVERIFY2(!mpPreferences->findChild<QLabel*>(qsl("settingsWordmark"))->isHidden(), "the wordmark is hidden beside a sidebar with room for it");
    }

    // Where it is: on the line the sidebar's pane ends on, halfway down that
    // pane - not inside either of the two things it lies between. Read in both
    // modes, because the line moves when the sidebar gives its names up and the
    // control has to follow it.
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
        // The second of the two presses above put the names back, which is the
        // state the next case starts from
        QVERIFY2(!railShowing(), qPrintable(qsl("the second press did not put the names back: %1").arg(state())));
    }

    // Pressed, the names go and a rail of icons is left - and the choice is
    // written down as it is made rather than waiting for the dialog to be shut.
    // The names are what a screen reader announces the rows as, so the rail
    // offers them on hover rather than dropping them.
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

        // ...and back, written down again, which is where the cases below start
        pressTheToggle();
        QVERIFY2(!railShowing(), qPrintable(qsl("the second press did not put the names back: %1").arg(state())));
        QVERIFY2(storedLabelsShown(), qPrintable(qsl("putting the names back did not write the choice down: %1").arg(state())));
    }

    // The width is measured off the names rather than written down: wide enough
    // that the longest of them is not squeezed, and no wider than the flat
    // 232px this sidebar was held to before it measured anything
    void test_theSidebarIsAsWideAsItsNamesAndNoWider()
    {
        QVERIFY2(!railShowing(), "this case starts from the names showing");
        const int measured = sidebar()->width();
        const int widest = widestBoldName();
        qInfo().noquote() << qsl("  the sidebar is %1px wide for a widest bold name of %2px").arg(QString::number(measured), QString::number(widest));
        QVERIFY2(measured >= widest + uiDesign::scmSidebarRailWidth,
                 qPrintable(qsl("the sidebar is %1px wide, which leaves the widest name (%2px) less than a rail's width of chrome around it").arg(measured).arg(widest)));
        QVERIFY2(measured <= 232, qPrintable(qsl("the measured sidebar is %1px, wider than the flat 232px it replaced").arg(measured)));
        // ...and the wordmark row over the names is drawn out rather than
        // elided, which is the other thing the pane has to hold
        QWidget* pWordmarkRow = mpPreferences->findChild<QLabel*>(qsl("settingsWordmark"))->parentWidget();
        QVERIFY2(pWordmarkRow->width() >= pWordmarkRow->sizeHint().width(),
                 qPrintable(qsl("the wordmark row wants %1px and was given %2px").arg(pWordmarkRow->sizeHint().width()).arg(pWordmarkRow->width())));
    }

    // The width's own collapse, over a preference that says otherwise. It is
    // the sidebar that changes and nothing else: the preference is untouched,
    // so widening the window again is settled by it alone
    void test_aNarrowWindowForcesTheRailWithoutStoringIt()
    {
        QVERIFY2(!railShowing(), "this case starts from the names showing");

        resizeDialog(mTooNarrow);
        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(railShowing(), qPrintable(qsl("a window too narrow for the names kept them: %1").arg(state())));
        QVERIFY2(storedLabelsShown(), qPrintable(qsl("running out of room rewrote the user's preference: %1").arg(state())));
        // The toggle cannot be pressed here, so it says why rather than doing
        // nothing
        QVERIFY2(!toggle()->isEnabled(), "the toggle offers labels a window this narrow cannot draw");
        QVERIFY2(toggle()->toolTip() != toggle()->accessibleName(), "the toggle a narrow window has disabled still offers to show the labels");

        resizeDialog(mWideEnough);
        QVERIFY2(!railShowing(), qPrintable(qsl("the names did not come back when the window had room again: %1").arg(state())));
        QVERIFY2(toggle()->isEnabled(), "the toggle is still refusing on a window with room for the names");
    }

    // The toggle says what happens to the labels rather than open or close, and
    // a screen reader hears the same words - which matters most in the rail,
    // where the rows have given their names up to their tooltips
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

    // The round trip: the choice is the dialog's rather than this instance's,
    // and a brand new one opens with it
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

    // The chosen row is drawn bold, which is the weight the sidebar's width was
    // measured in - sidebarRowWidth() asks the style for a name in bold, so a
    // row rendered at regular weight is a pane wider than what it holds. The
    // weight is SidebarItemDelegate's: a font-weight on an ::item never reaches
    // the painter, which lays the name out in the style option's own font. So
    // nothing structural can prove it and the picture is asked instead - the
    // same rect of the same row, once while it is chosen and once while it is
    // not.
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

        // The pane is already measured against a bold name, so drawing one must
        // not move it - a width that changed with the selection would shift
        // every row under the pointer
        const int paneBefore = sidebar()->width();
        const int rowBefore = pList->visualItemRect(pList->item(chosen)).width();

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        chooseRow(chosen);
        QCOMPARE(pList->currentRow(), chosen);
        const int inkWhenChosen = inkPixels(pList->grab().toImage(), nameArea, tokens.accentText);
        const int paneChosen = sidebar()->width();
        const int rowChosen = pList->visualItemRect(pList->item(chosen)).width();

        // Full strength rather than the editor's muted tone: this sidebar is the
        // dialog's navigation, and tokens.text is what it hands
        // sidebarStyleSheet() as the item colour
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

    // The width has to hold the names under whichever style is drawing the
    // rows. The two appearances are two different base styles on a desktop - a
    // Fusion proxy on dark, the platform's own on light - and they leave
    // different margins round an item's text, so a sidebar measured with one
    // number for both draws the names out in one appearance and elides them in
    // the other. Under the offscreen platform this runs on both appearances are
    // Fusion, which is why the last pass installs the difference outright.
    void test_everyNameFitsItsRowUnderTheStyleDrawingThem()
    {
        const auto appearanceBefore = mudlet::self()->mAppearance;
        auto restore = qScopeGuard([appearanceBefore]() {
            // The appearance builds a style of its own, whatever was put in its
            // place; the loading flag is what makes it do that for an
            // appearance already in force
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
            // Reported rather than asserted: a return out of a lambda is
            // all QVERIFY2 can do here, and a silent one would leave the walk
            // below unrun and the case passing on an empty list
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
