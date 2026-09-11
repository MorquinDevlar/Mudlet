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
 * The two menus the script editor owns - the one Save Profile drops and the
 * search options behind the search field - are drawn by the design language
 * rather than by the platform: the card surface, the hairline and the corner a
 * combo box's dropped-down list takes, with the one mark on a row that is
 * switched on or off.
 *
 * A menu is a window of its own, filled before anything in it is drawn, so the
 * corner has to be cut out of that window as well as off the frame - which is
 * read here the way the settings dialog's combo box popup is read, off a grab
 * that keeps its alpha.
 *
 * Run with: ctest -R EditorMenuStyleTest -V
 */

#include <QAction>
#include <QImage>
#include <QMenu>
#include <QScopeGuard>
#include <QStyleOptionToolButton>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QtTest/QtTest>
#include <chrono>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorMenuStyleTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorMenuStyle-Test-Profile");
    QString mPort; // assigned the stub's actual ephemeral port in initTestCase()
    const QString mLocalhost = qsl("localhost");

    // How far a pixel may be from a colour and still be read as that colour: a
    // rounded edge and a glyph are both drawn against what is behind them, and
    // a screen that doubles its pixels blends more of it again
    static constexpr int scmMarkInkSlack = 40;

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

    QToolButton* saveProfileButton() const { return qobject_cast<QToolButton*>(mpEditor->toolBar->widgetForAction(mpEditor->mProfileSaveAction)); }

    QMenu* searchOptionsMenu() const { return mpEditor->findChild<QMenu*>(qsl("pMenu_searchOptions")); }

    // Every sheet standing over the menu, since the rules that draw one are
    // carried by whatever it hangs off - the toolbar for the button's menu, the
    // menu itself for the one hanging off the window
    static QString sheetsReaching(const QWidget* pMenu)
    {
        QString sheets;
        for (const QObject* pAncestor = pMenu; pAncestor; pAncestor = pAncestor->parent()) {
            if (const auto* pWidget = qobject_cast<const QWidget*>(pAncestor)) {
                sheets += pWidget->styleSheet();
            }
        }
        return sheets;
    }

    // Shown rather than only asked for: a menu is polished against the sheets
    // over it when it is popped up, and the window it is drawn in is only made
    // at that first show
    QImage grabOfMenu(QMenu* pMenu, const QPoint& where) const
    {
        pMenu->popup(where);
        QTest::qWaitForWindowExposed(pMenu);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        const QImage shot = pMenu->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        pMenu->hide();
        QCoreApplication::processEvents();
        return shot;
    }

    // Where the pointer would drop each menu: under the button for the one it
    // hangs off, and under the search field for the other
    QPoint underTheSaveProfileButton() const
    {
        const QToolButton* pButton = saveProfileButton();
        return pButton ? pButton->mapToGlobal(QPoint(0, pButton->height())) : mpEditor->mapToGlobal(QPoint(20, 60));
    }

    // The rectangle the style hands the half that opens the menu. Asked for
    // rather than clicked: a popup runs an event loop of its own that a case
    // cannot get back out of, and the option is filled the way QToolButton
    // fills its own for a button in MenuButtonPopup mode.
    static QRect menuHalfOf(QToolButton* pButton)
    {
        QStyleOptionToolButton option;
        option.initFrom(pButton);
        option.rect = pButton->rect();
        option.text = pButton->text();
        option.icon = pButton->icon();
        option.iconSize = pButton->iconSize();
        option.toolButtonStyle = pButton->toolButtonStyle();
        option.features = QStyleOptionToolButton::Menu | QStyleOptionToolButton::MenuButtonPopup;
        option.subControls = QStyle::SC_ToolButton | QStyle::SC_ToolButtonMenu;
        return pButton->style()->subControlRect(QStyle::CC_ToolButton, &option, QStyle::SC_ToolButtonMenu, pButton);
    }

    QImage grabOfTheBar() const { return mpEditor->toolBar->grab().toImage().convertToFormat(QImage::Format_ARGB32); }

    // Where the pointer is, said in the way a widget hears it, and left long
    // enough for the button to have taken the state and been asked to paint
    static void pointAt(QWidget* pWidget, const QPoint& where)
    {
        QTest::mouseMove(pWidget, where);
        QCoreApplication::processEvents();
        QTest::qWait(30ms);
    }

    // What a patch of the grab comes to, rather than one pixel of it: a wash
    // over a face is a few levels' difference, and a single pixel that landed
    // on a glyph or on an antialiased edge is not that difference
    static QColor averageColour(const QImage& shot, const QRect& box)
    {
        const QRect inside = box.intersected(shot.rect());
        if (inside.isEmpty()) {
            return QColor();
        }
        qint64 red = 0;
        qint64 green = 0;
        qint64 blue = 0;
        for (int y = inside.top(); y <= inside.bottom(); ++y) {
            for (int x = inside.left(); x <= inside.right(); ++x) {
                const QColor sample = shot.pixelColor(x, y);
                red += sample.red();
                green += sample.green();
                blue += sample.blue();
            }
        }
        const qint64 pixels = static_cast<qint64>(inside.width()) * inside.height();
        return QColor(static_cast<int>(red / pixels), static_cast<int>(green / pixels), static_cast<int>(blue / pixels));
    }

    static QRect inTheGrabsPixels(const QImage& shot, const QRect& box)
    {
        const qreal ratio = shot.devicePixelRatio();
        return QRect(QPoint(qRound(box.left() * ratio), qRound(box.top() * ratio)), QPoint(qRound((box.right() + 1) * ratio) - 1, qRound((box.bottom() + 1) * ratio) - 1)).intersected(shot.rect());
    }

    static int distanceBetween(const QColor& one, const QColor& other) { return std::abs(one.red() - other.red()) + std::abs(one.green() - other.green()) + std::abs(one.blue() - other.blue()); }

    // How many pixels well inside the mark's box are not the fill it is drawn
    // on: none in an empty box, a good few once the tick is in it
    static int markedPixelsIn(const QImage& shot, const QRect& box, const QColor& fill)
    {
        const QRect inside = box.adjusted(box.width() / 4, box.height() / 4, -box.width() / 4, -box.height() / 4);
        int marked = 0;
        for (int y = inside.top(); y <= inside.bottom(); ++y) {
            for (int x = inside.left(); x <= inside.right(); ++x) {
                if (distanceBetween(shot.pixelColor(x, y), fill) > scmMarkInkSlack) {
                    ++marked;
                }
            }
        }
        return marked;
    }

    // The square the mark on a checkable row is drawn in: at the size every
    // choice indicator is drawn at, standing in the room the row leaves at its
    // leading end. Found rather than named - where in that room it stands is
    // the recipe's own business - by walking in from the row's edge until the
    // surface gives way to the mark's outline.
    static QRect markBoxOf(const QMenu* pMenu, QAction* pAction, const QImage& shot)
    {
        const QRect row = inTheGrabsPixels(shot, pMenu->actionGeometry(pAction));
        const int middle = row.center().y();
        const QColor surface = shot.pixelColor(row.left(), middle);
        const int size = qRound(uiDesign::scmChoiceIndicatorSize * shot.devicePixelRatio());
        for (int x = row.left(); x < std::min(row.left() + 3 * size, row.right()); ++x) {
            if (distanceBetween(shot.pixelColor(x, middle), surface) > scmMarkInkSlack) {
                return QRect(x, middle - size / 2, size, size).intersected(shot.rect());
            }
        }
        return QRect();
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
        mpEditor->resize(1200, 700);
        mpEditor->slot_showTriggers();
        QVERIFY(QTest::qWaitForWindowExposed(mpEditor));
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

    // Saving the profile under another name hangs off the button beside it, so
    // the bar's own sheet is what has to reach it
    void test_theSaveProfileMenuIsDrawnFromTheDesign()
    {
        QToolButton* pButton = saveProfileButton();
        QVERIFY2(pButton, "the Save Profile action has no button on the bar to hang a menu from");
        QMenu* pMenu = pButton->menu();
        QVERIFY2(pMenu, "the Save Profile button drops no menu");

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QString sheets = sheetsReaching(pMenu);
        QVERIFY2(sheets.contains(qsl("QMenu")),
                 qPrintable(qsl("nothing over the Save Profile menu says what a QMenu is drawn like: the bar's sheet is \"%1\"").arg(mpEditor->toolBar->styleSheet().left(120))));
        QVERIFY2(sheets.contains(tokens.card.name()), qPrintable(qsl("the Save Profile menu is not drawn on the design's card surface (%1)").arg(tokens.card.name())));
    }

    // ...and it is opened round the same corner the combo boxes' lists are. The
    // menu is its own window, and a window is filled before what is in it is
    // drawn, so a radius on the frame alone leaves square corners showing
    // through in the fill.
    void test_theSaveProfileMenuIsOpenAtTheCorner()
    {
        QToolButton* pButton = saveProfileButton();
        QVERIFY2(pButton, "the Save Profile action has no button on the bar to hang a menu from");
        QMenu* pMenu = pButton->menu();
        QVERIFY2(pMenu, "the Save Profile button drops no menu");
        QVERIFY2(pMenu->testAttribute(Qt::WA_TranslucentBackground), "the menu's window was never asked to be see-through, so it is opaque behind whatever corner the frame is given");

        const QImage shot = grabOfMenu(pMenu, underTheSaveProfileButton());
        QVERIFY2(shot.width() > 16 && shot.height() > 8, qPrintable(qsl("the menu came up %1x%2, which is nothing to read").arg(shot.width()).arg(shot.height())));

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const qreal ratio = shot.devicePixelRatio();
        const int middle = shot.height() / 2;
        const QColor corner = QColor::fromRgba(shot.pixel(0, 0));
        // The hairline at the menu's left edge halfway down, where it is a
        // straight run of the line rather than the corner it is rounded
        // through, and the surface a couple of pixels inside it - which is the
        // band the frame leaves round the rows, so no row is being read here
        const QColor hairline = QColor::fromRgba(shot.pixel(0, middle));
        const QColor surface = QColor::fromRgba(shot.pixel(qRound(2 * ratio), middle));
        QVERIFY2(corner.alpha() == 0, qPrintable(qsl("the menu's corner is painted %1 rather than cut away, so its radius has a square window behind it").arg(corner.name(QColor::HexArgb))));
        QVERIFY2(hairline.alpha() == 255 && distanceBetween(hairline, tokens.border) <= scmMarkInkSlack,
                 qPrintable(qsl("the menu's edge reads %1 rather than the design's hairline of %2").arg(hairline.name(QColor::HexArgb), tokens.border.name())));
        QVERIFY2(surface.alpha() == 255 && distanceBetween(surface, tokens.card) <= scmMarkInkSlack,
                 qPrintable(qsl("the menu is painted %1 rather than the card surface's %2").arg(surface.name(QColor::HexArgb), tokens.card.name())));
    }

    // Save Profile is two click areas, and has to read as two before the
    // pointer arrives: the seam down the leading edge of the trailing half says
    // so at rest, the design's chevron stands on that half rather than the
    // platform's filled triangle, and the half lights on its own when it is
    // what is being pointed at.
    void test_theSaveProfileButtonShowsItsTwoHalves()
    {
        QToolButton* pButton = saveProfileButton();
        QVERIFY2(pButton, "the Save Profile action has no button on the bar to read");
        QCOMPARE(pButton->popupMode(), QToolButton::MenuButtonPopup);
        QVERIFY2(pButton->isVisible() && pButton->width() > 3 * uiDesign::scmInputDropDownWidth, "the Save Profile button is not on show wide enough to be read");

        const QString sheet = mpEditor->toolBar->styleSheet();
        const int arrowAt = sheet.indexOf(qsl("::menu-arrow"));
        QVERIFY2(arrowAt >= 0, "the bar's sheet says nothing about the chevron on the menu half, so the platform's filled triangle is still on it");
        QVERIFY2(sheet.mid(arrowAt, 80).contains(qsl("image:")), qPrintable(qsl("the menu half's chevron is named without a picture: \"%1\"").arg(sheet.mid(arrowAt, 80))));

        const QRect half = menuHalfOf(pButton);
        QVERIFY2(half.width() > 4 && half.height() > 8, qPrintable(qsl("the style hands the menu a %1x%2 rectangle, which is nothing to read").arg(half.width()).arg(half.height())));
        QVERIFY2(half.left() > pButton->width() / 2, "the half that opens the menu is not the trailing one");

        // The pointer is put somewhere neither half can claim before the button
        // is read at rest, since a case run after one that left it hovered
        // would otherwise read a lit button as its resting state
        pointAt(mpEditor->toolBar, QPoint(2, 2));
        const QImage rest = grabOfTheBar();
        const QRect halfInGrab = inTheGrabsPixels(rest, QRect(pButton->mapTo(mpEditor->toolBar, half.topLeft()), half.size()));
        QVERIFY2(halfInGrab.width() > 4 && halfInGrab.height() > 8, "the menu half does not land inside the grab of the bar");

        // The seam, read down the straight run of it rather than through the
        // ends, where the half's own corners round away from the line
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const int from = halfInGrab.top() + halfInGrab.height() / 5;
        const int to = halfInGrab.bottom() - halfInGrab.height() / 5;
        int onTheLine = 0;
        int rows = 0;
        QColor readAs;
        for (int y = from; y <= to; ++y) {
            ++rows;
            const QColor sample = rest.pixelColor(halfInGrab.left(), y);
            if (!readAs.isValid()) {
                readAs = sample;
            }
            if (distanceBetween(sample, tokens.border) <= scmMarkInkSlack) {
                ++onTheLine;
            }
        }
        // ...and a couple of pixels in from it, which has to be something else
        // entirely: a seam is a line, and a half filled in the hairline's tone
        // would pass the reading above at every column
        const int inside = halfInGrab.left() + qMax(3, qRound(3 * rest.devicePixelRatio()));
        const QColor justInside = rest.pixelColor(inside, halfInGrab.center().y() - halfInGrab.height() / 4);

        qInfo().noquote() << qsl("  the menu half is %1px wide at x %2; its leading edge reads %3 against the hairline's %4 on %5 of %6 rows, and %7 a few pixels in")
                                     .arg(half.width())
                                     .arg(half.left())
                                     .arg(readAs.name(), tokens.border.name())
                                     .arg(onTheLine)
                                     .arg(rows)
                                     .arg(justInside.name());
        QVERIFY2(rows > 2 && onTheLine * 5 >= rows * 4,
                 qPrintable(qsl("the half's leading edge carries no seam: %1 of %2 rows read the design's hairline of %3, the first of them %4")
                                    .arg(onTheLine)
                                    .arg(rows)
                                    .arg(tokens.border.name(), readAs.name())));
        QVERIFY2(distanceBetween(justInside, tokens.border) > scmMarkInkSlack,
                 qPrintable(qsl("the half is filled with the hairline's own tone (%1 a few pixels in), so what was read at its edge is a fill rather than a seam").arg(justInside.name())));

        // A band across the top of the half, clear of the seam at one edge and
        // of the chevron standing in the middle of it, and one of the same
        // height in the gap the padding leaves between the words and the seam,
        // which is the button's own face with nothing drawn over it
        const int inset = qMax(3, qRound(3 * rest.devicePixelRatio()));
        const int bandHeight = qMax(2, halfInGrab.height() / 5);
        const QRect wash(halfInGrab.left() + inset, halfInGrab.top() + qMax(2, qRound(2 * rest.devicePixelRatio())), halfInGrab.width() - qMax(5, qRound(5 * rest.devicePixelRatio())), bandHeight);
        const QRect face(halfInGrab.left() - 2 * inset, wash.top(), inset, bandHeight);
        const QColor atRest = averageColour(rest, wash);

        // Pointed at, the half has to be drawn as something other than the face
        // beside it. Which of the two the pointer is actually over is not asked:
        // a tool button's hover is tracked through QStyle::hitTestComplexControl,
        // which QStyleSheetStyle does not answer for CC_ToolButton, so the
        // button reports SC_ToolButton for a point anywhere on it and no sheet
        // can tell the halves apart. What the wash says is that the half is a
        // target of its own, not that it is the one under the pointer.
        pointAt(pButton, half.center());
        const QImage lit = grabOfTheBar();
        const QColor litHalf = averageColour(lit, wash);
        const QColor litFace = averageColour(lit, face);

        const int faceToAccent = distanceBetween(litFace, tokens.accent);
        const int halfToAccent = distanceBetween(litHalf, tokens.accent);
        qInfo().noquote() << qsl("  the half reads %1 at rest and %2 pointed at, against the %3 of the face beside it; those two are %4 and %5 from the accent's %6")
                                     .arg(atRest.name(), litHalf.name(), litFace.name())
                                     .arg(halfToAccent)
                                     .arg(faceToAccent)
                                     .arg(tokens.accent.name());
        QVERIFY2(distanceBetween(litHalf, atRest) > 2,
                 qPrintable(qsl("pointing at the button changed nothing on the half (%1 against %2 at rest), so the pointer never reached it and nothing below can be read")
                                    .arg(litHalf.name(), atRest.name())));
        QVERIFY2(distanceBetween(litHalf, litFace) > 8,
                 qPrintable(qsl("the pointed-at half reads %1 against the %2 of the face beside it, so it is drawn as one piece with the body").arg(litHalf.name(), litFace.name())));
        QVERIFY2(halfToAccent < faceToAccent,
                 qPrintable(qsl("the pointed-at half is %1 from the accent against the %2 of the face beside it, so it does not light towards the accent").arg(halfToAccent).arg(faceToAccent)));

        pointAt(mpEditor->toolBar, QPoint(2, 2));
    }

    // The search options hang off the window rather than off a button, so they
    // are drawn and opened up on their own
    void test_theSearchOptionsMenuIsDrawnFromTheDesign()
    {
        QMenu* pMenu = searchOptionsMenu();
        QVERIFY2(pMenu, "the search options menu is not there to be read");
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QString sheets = sheetsReaching(pMenu);
        QVERIFY2(sheets.contains(qsl("QMenu")) && sheets.contains(tokens.card.name()),
                 qPrintable(qsl("the search options menu is not drawn on the design's card surface (%1)").arg(tokens.card.name())));
        QVERIFY2(sheets.contains(qsl("QMenu::indicator")), "nothing says what the mark on a checkable search option looks like, so the platform draws it");
        QVERIFY2(pMenu->testAttribute(Qt::WA_TranslucentBackground), "the search options menu's window was never asked to be see-through, so its corner is opaque");

        const QImage shot = grabOfMenu(pMenu, underTheSaveProfileButton());
        const QColor corner = QColor::fromRgba(shot.pixel(0, 0));
        QVERIFY2(corner.alpha() == 0, qPrintable(qsl("the search options menu's corner is painted %1 rather than cut away").arg(corner.name(QColor::HexArgb))));
    }

    // ...and a search option that is switched on shows the one mark every other
    // choice in the window is made with, in the ink that mark is drawn in
    void test_aCheckedSearchOptionCarriesTheOneMark()
    {
        QMenu* pMenu = searchOptionsMenu();
        QVERIFY2(pMenu, "the search options menu is not there to be read");
        QAction* pOption = mpEditor->mpAction_searchCaseSensitive;
        QVERIFY2(pOption && pOption->isCheckable(), "the case-sensitive search option is not a checkable row");

        const bool optionWas = pOption->isChecked();
        auto putItBack = qScopeGuard([pOption, optionWas]() {
            pOption->setChecked(optionWas);
        });

        pOption->setChecked(false);
        const QImage empty = grabOfMenu(pMenu, underTheSaveProfileButton());
        const QRect emptyBox = markBoxOf(pMenu, pOption, empty);
        QVERIFY2(emptyBox.width() > 4 && emptyBox.height() > 4, qPrintable(qsl("the option's mark has no rectangle to sample: %1x%2").arg(emptyBox.width()).arg(emptyBox.height())));
        const QColor fill = empty.pixelColor(emptyBox.center());
        const int nothingInIt = markedPixelsIn(empty, emptyBox, fill);

        pOption->setChecked(true);
        const QImage checked = grabOfMenu(pMenu, underTheSaveProfileButton());
        const int aTick = markedPixelsIn(checked, emptyBox, fill);

        // The ink the mark is drawn in, which is measured against the fill it
        // lies on rather than against the accent framing the box
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QColor markInk = uiDesign::readableOn(tokens.field, tokens.accent, tokens.text, uiDesign::scmQuietMinimumRatio);
        int nearest = 3 * 255;
        for (int y = emptyBox.top(); y <= emptyBox.bottom(); ++y) {
            for (int x = emptyBox.left(); x <= emptyBox.right(); ++x) {
                nearest = std::min(nearest, distanceBetween(checked.pixelColor(x, y), markInk));
            }
        }

        qInfo().noquote() << qsl("  the option's mark is %1px on a fill of %2, with %3 pixels in it empty and %4 checked; its nearest ink to %5 is %6 away")
                                     .arg(emptyBox.width())
                                     .arg(fill.name())
                                     .arg(nothingInIt)
                                     .arg(aTick)
                                     .arg(markInk.name())
                                     .arg(nearest);
        QVERIFY2(nothingInIt == 0, qPrintable(qsl("an unchecked option already has %1 pixels in its box that are not the fill").arg(nothingInIt)));
        QVERIFY2(aTick > 0, "a checked option paints nothing inside its box, so there is no mark on it");
        QVERIFY2(nearest <= scmMarkInkSlack, qPrintable(qsl("the mark's nearest ink is %1 away from the %2 every other choice is marked in").arg(nearest).arg(markInk.name())));
    }
};

#include "EditorMenuStyleTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorMenuStyleTest)
