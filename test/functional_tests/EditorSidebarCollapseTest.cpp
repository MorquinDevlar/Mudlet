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
 * The editor's sidebar has two ways of losing its names, and they must not be
 * able to overwrite each other.
 *
 * The chevron on the line between the sidebar and the rest of the window -
 * where Finder and VS Code put the control that shows and hides a sidebar - is
 * the user's own choice, kept across sessions under editorSidebarLabelsShown. A
 * labels-shown preference, since the sidebar has no closed state to remember:
 * it minimises to a rail of icons and the rows go on being reachable, with
 * their names as tooltips.
 *
 * A window too narrow to draw the names takes them away regardless, and that
 * one is transient: it must never reach the stored preference, or a stretch of
 * work in a small window would decide what every later session opens with. This
 * is the split slot_rightSplitterMoved() already makes for the trigger options
 * panel, where only the explicit click writes anything down.
 *
 * Where the control sits and which way it points are checked here too. It was
 * for a while the leading button of the actions toolbar, which is a bar the
 * user can drag to another edge of the window or float - taking the sidebar's
 * only control with it. It now rides on the seam itself, centred on the line
 * down the sidebar's trailing edge and halfway down the pane, and its chevron
 * points the way the sidebar will go.
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
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <chrono>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "SidebarToggle.h"
#include "TelnetServerStub.h"
#include "dlgTriggerEditor.h"
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
// of the way from no difference at all to the 1.64x measured here - a name
// drawn at one weight in both states comes to 1.16x, which is the whole of what
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

class EditorSidebarCollapseTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorSidebarCollapse-Test-Profile");
    QString mPort; // assigned the stub's actual ephemeral port in initTestCase()
    const QString mLocalhost = qsl("localhost");

    // Measured off the editor rather than written down here: the breakpoint is
    // the widest of the translated names plus the narrowest the body can be
    // drawn at, neither of which is a number in the source
    int mWideEnough = 0;
    int mTooNarrow = 0;
    int mHeight = 700;

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
        if (!spy.wait(1000)) {
            QFAIL("Could not connect with the host.");
        }
    }

    // The mode is the property the shared delegate and the sidebar's rules both
    // read, so it is what the window is actually drawing rather than a copy of
    // the intent
    bool railShowing() const { return mpEditor->mpListWidget_editorSidebar->property(uiDesign::scmProp_rail).toBool(); }

    bool storedLabelsShown() const { return mudlet::getQSettings()->value(qsl("editorSidebarLabelsShown"), true).toBool(); }

    // Whether there is a stored answer at all, as against the default the
    // reading above falls back to: only the toolbar's toggle writes one
    static bool anythingStored() { return mudlet::getQSettings()->contains(qsl("editorSidebarLabelsShown")); }

    // The chevron on the seam, which is the widget a pointer and a screen
    // reader both reach
    uiDesign::SidebarToggle* toggle() const { return mpEditor->mpToggle_editorSidebar; }

    // The shell holding the sidebar and everything beside it, which is what the
    // chevron is a child of and so the frame both are measured in
    QWidget* shell() const { return mpEditor->centralWidget(); }

    // How far the chevron's middle is from the line the sidebar's pane ends on
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

    // What each row leaves for its name against what the name comes to, in the
    // bold a chosen row is drawn in. The text rect is not the answer on its
    // own: QCommonStyle draws inside it shrunk by PM_FocusFrameHMargin + 1 on
    // each side, so a name that only just fits the rect is still elided.
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
                            .arg(QString::number(mpEditor->mpWidget_editorSidebarPane->width()),
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

    // The selectable row with the longest name, where a change of weight has the
    // most to show for itself
    int longestNamedRow() const
    {
        QListWidget* pList = mpEditor->mpListWidget_editorSidebar;
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
        QListWidget* pList = mpEditor->mpListWidget_editorSidebar;
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
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>(qsl("MudletInstanceCoordinator")));
        mudlet::self()->init();
        mudlet::self()->setStorePasswordsSecurely(false);
        deleteProfileDirectory(mProfileName);

        startProfile(mProfileName, mLocalhost, mPort);

        mudlet::self()->slot_showEditorDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");

        // Only measurable with the window on screen: the breakpoint is taken off
        // splitter_main's minimum size hint, and a splitter that has never been
        // shown reports the hint of a layout that has never been run
        const dlgTriggerEditor::EditorSidebarWidths widths = mpEditor->editorSidebarWidths();
        mWideEnough = widths.collapseBelow + 120;
        mTooNarrow = widths.collapseBelow - 40;
        qInfo().noquote() << qsl("  sidebar expanded at %1px, names given up below %2px; editor minimum width %3px")
                                     .arg(QString::number(widths.expanded), QString::number(widths.collapseBelow), QString::number(mpEditor->minimumWidth()));
        // A layout's total minimum is the sidebar's fixed width plus the body's,
        // so a breakpoint at or below that is one no drag can ever reach. It is
        // what this one used to be - see editorSidebarWidths().
        QVERIFY2(mTooNarrow >= mpEditor->minimumWidth(),
                 qPrintable(qsl("The editor cannot be dragged below its own breakpoint (minimum %1px, breakpoint %2px), so the forced rail is unreachable")
                                    .arg(QString::number(mpEditor->minimumWidth()), QString::number(widths.collapseBelow))));

        resizeEditor(mWideEnough);
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

    // Nothing stored yet, and a window with room: the names are on show, and
    // the toggle is pressable
    void test_aFreshProfileStartsWithTheNamesShowing()
    {
        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(!railShowing(), qPrintable(qsl("A fresh profile opened as a rail: %1").arg(state())));
        // The property above is what the rules read; this is what the reader
        // sees, and a sidebar that kept the rail's width while saying it was
        // not one would pass on the property alone
        QVERIFY2(mpEditor->mpWidget_editorSidebarPane->width() > uiDesign::scmSidebarRailWidth,
                 qPrintable(qsl("the sidebar is at the rail's %1px with its names showing: %2").arg(QString::number(uiDesign::scmSidebarRailWidth), state())));
        QVERIFY2(!anythingStored(), "opening the editor wrote a sidebar preference nobody chose, so what a later run opens with is not the default this case reads");
        QVERIFY2(toggle() != nullptr, "The editor has no sidebar toggle");
        QVERIFY2(toggle()->isEnabled(), "The sidebar toggle cannot be pressed on a window with room for the names");
    }

    // Where it is: on the line the sidebar's pane ends on, halfway down that
    // pane - not up under the toolbar, and not inside either of the two things
    // it lies between. Read in both modes, because the line moves when the
    // sidebar gives its names up and the control has to follow it.
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
        // The second of the two presses above put the names back, which is the
        // state the next case starts from
        qInfo().noquote() << qsl("  %1").arg(measured.join(qsl("; ")));

        QVERIFY2(adrift.isEmpty(), qPrintable(qsl("the sidebar toggle is not on the seam: %1").arg(adrift.join(qsl("; ")))));
    }

    // Which way it points is what pressing it will do: into the seam while
    // there are names to give up, out of it while there are not. Read off the
    // control's own picture as well as off the flag, so that a chevron that
    // stopped following the mode would be caught.
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

    // The toggle says what happens rather than open or close, and a screen
    // reader hears the same words - which matters most in the rail, where the
    // rows have given their names up to their tooltips
    void test_theToggleNamesWhatItDoesToTheLabels()
    {
        const QString expanded = toggle()->accessibleName();
        QVERIFY2(!expanded.isEmpty(), "The sidebar toggle has no accessible name");
        qInfo().noquote() << qsl("  with the names showing: \"%1\"").arg(expanded);

        pressTheToggle();
        const QString collapsed = toggle()->accessibleName();
        qInfo().noquote() << qsl("  as a rail: \"%1\"").arg(collapsed);
        QVERIFY2(collapsed != expanded, "The sidebar toggle says the same thing in both modes");

        // Whatever the translation, neither wording may be about opening or
        // closing the sidebar - it is always there
        for (const QString& wording : {expanded, collapsed}) {
            QVERIFY2(!wording.contains(qsl("close"), Qt::CaseInsensitive) && !wording.contains(qsl("open"), Qt::CaseInsensitive),
                     qPrintable(qsl("The sidebar toggle reads as opening or closing the sidebar: \"%1\"").arg(wording)));
        }
    }

    // Pressed once, on a window with all the room it needs: the rail is the
    // user's choice and the width has nothing to say about it
    void test_aManualRailStaysOnAWideWindow()
    {
        QVERIFY2(railShowing(), qPrintable(qsl("The toggle did not minimise the sidebar: %1").arg(state())));

        resizeEditor(mWideEnough + 200);
        QVERIFY2(railShowing(), qPrintable(qsl("A wider window put the names back over the user's choice: %1").arg(state())));
        resizeEditor(mWideEnough);
    }

    // ...and pressed again, the names come back
    void test_theTogglePutsTheNamesBack()
    {
        pressTheToggle();
        QVERIFY2(!railShowing(), qPrintable(qsl("The toggle did not bring the names back: %1").arg(state())));
    }

    // The width's own collapse, over a preference that says otherwise. It is
    // the sidebar that changes and nothing else: the preference is untouched,
    // and so is what a close would write out
    void test_aNarrowWindowForcesTheRailWithoutStoringIt()
    {
        QVERIFY2(!railShowing(), "This case starts from the names showing");

        resizeEditor(mTooNarrow);
        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(railShowing(), qPrintable(qsl("A window too narrow for the names kept them: %1").arg(state())));
        QVERIFY2(mpEditor->mEditorSidebarLabelsShown, qPrintable(qsl("Running out of room rewrote the user's preference: %1").arg(state())));
        // The toggle cannot be pressed here, so it says why rather than doing
        // nothing
        QVERIFY2(!toggle()->isEnabled(), "The toggle offers labels a window this narrow cannot draw");
    }

    // ...so widening it again is settled by the preference alone
    void test_wideningTheWindowBringsTheNamesBack()
    {
        resizeEditor(mWideEnough);
        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(!railShowing(), qPrintable(qsl("The names did not come back when the window had room again: %1").arg(state())));
    }

    // The round trip across instances: the toggle's choice is written on
    // close, and a brand new editor opens with it
    void test_aManualRailSurvivesAFreshEditor()
    {
        pressTheToggle();
        QVERIFY2(railShowing(), "The toggle did not minimise the sidebar");

        mpEditor->close();
        QCoreApplication::processEvents();
        QVERIFY2(!storedLabelsShown(), "Closing the editor did not write the toggle's choice down");

        delete mpEditor;
        mpEditor = nullptr;
        QVERIFY2(mpHost->mpEditorDialog.isNull(), "The editor should have been destroyed");

        mudlet::self()->slot_showEditorDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "A fresh editor should have been created");
        resizeEditor(mWideEnough);

        qInfo().noquote() << qsl("  %1").arg(state());
        QVERIFY2(railShowing(), qPrintable(qsl("A fresh editor forgot that the names had been given up: %1").arg(state())));
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

        QListWidget* pList = mpEditor->mpListWidget_editorSidebar;
        const int chosen = longestNamedRow();
        const int other = aNamedRowOtherThan(chosen);
        QVERIFY2(chosen >= 0 && other >= 0, "the editor's sidebar has too few named rows to tell a chosen one from the rest");
        const QString name = pList->item(chosen)->text();
        const QRect nameArea = nameAreaOf(chosen);

        // The pane is already measured against a bold name, so drawing one must
        // not move it - a width that changed with the selection would shift
        // every row under the pointer
        const int paneBefore = mpEditor->mpWidget_editorSidebarPane->width();
        const int rowBefore = pList->visualItemRect(pList->item(chosen)).width();

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        chooseRow(chosen);
        QCOMPARE(pList->currentRow(), chosen);
        const int inkWhenChosen = inkPixels(pList->grab().toImage(), nameArea, tokens.accentText);
        const int paneChosen = mpEditor->mpWidget_editorSidebarPane->width();
        const int rowChosen = pList->visualItemRect(pList->item(chosen)).width();

        // The editor's unchosen names are written in the quiet tone the rest of
        // its chrome takes, which is what sidebarStyleSheet() is handed as the
        // item colour
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

        QStringList squeezed;
        const auto walkTheRows = [this, &squeezed](const QString& drawnBy) {
            QCoreApplication::sendPostedEvents();
            QTest::qWait(100ms);
            // The width the names are drawn at moves with the style, so the
            // window is given whatever room that width now asks for before the
            // rows are read - a window left too narrow would answer with a rail
            resizeEditor(mpEditor->editorSidebarWidths().collapseBelow + 120);
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
