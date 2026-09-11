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
 * The main window's toolbar is drawn by the same flat-bar recipe the script
 * editor's is, and every part of that claim is about pixels a platform style
 * would otherwise own.
 *
 * Left to the platform, the bar reads two ways that are both wrong: on the
 * native macOS style a hovered button shows nothing at all, and on the
 * Fusion-based dark theme it shows a lighter box with a hairline round it. A
 * checkable button that is switched on - Sound, Full Screen, MultiView - is
 * drawn as a sunken block that swallows the menu half beside it.
 *
 * So three things are read off a grab here: a hovered button is washed in the
 * hover tone over the bar's own surface, a switched-on one in the accent's, and
 * a split button's trailing half is still told from its body by the seam the
 * recipe draws on it. Both appearances, because an ink that reads in one and
 * vanishes in the other is the defect the tokens exist to prevent.
 *
 * The fourth claim is about composition rather than colour. A profile's Lua
 * stylesheet is assigned to this very widget, so the design's rules and the
 * profile's have to live on one sheet: the design's first, the profile's last,
 * or a profile that has always painted its own bar stops being able to.
 *
 * ...and a detached profile window builds a bar of its own from its own
 * actions, which is a separate place for all of it to be missing.
 *
 * Run with: ctest -R MainToolBarStyleTest -V
 */

#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QHoverEvent>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QtTest/QtTest>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "ProfileTestHelper.h"
#include "TDetachedWindow.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class MainToolBarStyleTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    QTemporaryDir mCacheDir;
    QByteArray mSavedXdgCache;
    TelnetServerStub* mpServer = nullptr;
    QString mPort;
    const QString mLocalhost = qsl("localhost");
    const QString mFirstHostname = qsl("MainToolBarStyle-First");
    const QString mSecondHostname = qsl("MainToolBarStyle-Second");

    // How far a sampled pixel may sit from the colour the tokens mix, per
    // channel: a wash is composited rather than blitted, so the two agree to a
    // level or two rather than exactly
    static constexpr int scmInkTolerance = 2;
    // Wide enough that every button on the bar is still on the bar: past the
    // window's own length Qt posts the tail of the bar into the overflow menu,
    // where nothing can be sampled
    static constexpr int scmWindowWidth = 1600;
    static constexpr int scmWindowHeight = 600;

    // setupConfig() consults portable.txt before the XDG logic
    static bool portableMarkerPresent()
    {
        return QFileInfo::exists(qsl("%1/portable.txt").arg(QCoreApplication::applicationDirPath())) || QFileInfo::exists(qsl("%1/.config/mudlet/portable.txt").arg(QDir::homePath()));
    }

    void deleteProfileDirectory(const QString& profileName)
    {
        QDir dir(mudlet::getMudletPath(enums::profileHomePath, profileName));
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }

    void startProfile(const QString& hostname)
    {
        auto host = TestProfile::create(hostname, mLocalhost, mPort);
        if (!host) {
            QFAIL("No active host available for the test.");
        }

        QSignalSpy connectionSpy(&(host->mTelnet), &cTelnet::signal_connected);
        if (!connectionSpy.wait(2000)) {
            QFAIL("Could not connect with the host.");
        }
    }

    // Reached by object name rather than by member: the bar is the window's own
    // and every button on it carries the name of the action it was made from
    static QToolBar* theBar() { return mudlet::self()->findChild<QToolBar*>(qsl("mpMainToolBar")); }

    static QToolButton* buttonNamed(const QString& objectName) { return theBar() ? theBar()->findChild<QToolButton*>(objectName) : nullptr; }

    static QString saidAs(const QColor& colour) { return colour.name(QColor::HexArgb); }

    static QString saidAs(const QRect& box) { return qsl("%1,%2 %3x%4").arg(QString::number(box.left()), QString::number(box.top()), QString::number(box.width()), QString::number(box.height())); }

    static QString saidAs(const QPoint& at) { return qsl("%1,%2").arg(QString::number(at.x()), QString::number(at.y())); }

    // A wash carries its own alpha, so what a grab shows is it over whatever it
    // was laid on
    static QColor composited(const QColor& ink, const QColor& surface)
    {
        if (ink.alpha() == 255) {
            return ink;
        }
        const qreal weight = ink.alphaF();
        return QColor::fromRgbF(
                ink.redF() * weight + surface.redF() * (1.0 - weight), ink.greenF() * weight + surface.greenF() * (1.0 - weight), ink.blueF() * weight + surface.blueF() * (1.0 - weight));
    }

    static bool readsAs(const QColor& read, const QColor& wanted, const int tolerance = scmInkTolerance)
    {
        return std::abs(read.red() - wanted.red()) <= tolerance && std::abs(read.green() - wanted.green()) <= tolerance && std::abs(read.blue() - wanted.blue()) <= tolerance;
    }

    static QImage grabTheBar()
    {
        QCoreApplication::processEvents();
        return theBar()->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    }

    // A grab is in device pixels and everything measured here is in the logical
    // ones a rectangle is given in
    static QColor sampled(const QImage& image, const QPoint& at)
    {
        const qreal ratio = image.devicePixelRatio();
        const QPoint device(qRound(at.x() * ratio), qRound(at.y() * ratio));
        if (!image.rect().contains(device)) {
            return QColor();
        }
        return QColor(image.pixel(device));
    }

    // Inside the button's face, past the corner the recipe rounds it to and
    // above the glyph and the word, where nothing but the fill is drawn
    static QPoint washSpotIn(const QRect& button) { return QPoint(button.left() + uiDesign::scmToolBarButtonRadius + 2, button.top() + 2); }

    // Where splitButtonMenuHalfStyleSheet() draws the seam: the leading edge of
    // a menu half that is scmInputDropDownWidth wide and stands at the trailing
    // end of the button's padding box, inside the one-pixel border the bar's
    // own rule gives every button
    static int seamColumnOf(const QRect& button) { return button.right() - uiDesign::scmInputDropDownWidth; }

    // The pointer over a button, said the way a style reads it: QToolButton asks
    // QStyleOption::initFrom(), which takes State_MouseOver from WA_UnderMouse -
    // an attribute QApplication sets as it dispatches enter and leave, and
    // nothing sets on a platform that composites nothing.
    static void hoverOver(QWidget* pWidget, const bool over)
    {
        const QPoint at = pWidget->rect().center();
        if (over) {
            QTest::mouseMove(pWidget, at);
        }
        QCoreApplication::processEvents();
        pWidget->setAttribute(Qt::WA_UnderMouse, over);
        if (over) {
            QEnterEvent entered(QPointF(at), QPointF(at), QPointF(pWidget->mapToGlobal(at)));
            QApplication::sendEvent(pWidget, &entered);
            QHoverEvent hovered(QEvent::HoverEnter, QPointF(at), QPointF(at), QPointF(-1, -1));
            QApplication::sendEvent(pWidget, &hovered);
        } else {
            QEvent left(QEvent::Leave);
            QApplication::sendEvent(pWidget, &left);
            QHoverEvent unhovered(QEvent::HoverLeave, QPointF(-1, -1), QPointF(-1, -1), QPointF(at));
            QApplication::sendEvent(pWidget, &unhovered);
        }
        pWidget->update();
        QCoreApplication::processEvents();
    }

    // The one path an appearance change takes while a window is open
    static void setAppearance(const enums::Appearance appearance)
    {
        mudlet::self()->setAppearance(appearance);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        QCoreApplication::processEvents();
    }

    static QString appearanceSaidAs(const enums::Appearance appearance) { return appearance == enums::Appearance::dark ? qsl("dark") : qsl("light"); }

    // The two claims that are about colour rather than about the sheet, so that
    // the appearance case below makes them a second time against the other
    // palette rather than restating them
    void aHoveredButtonIsWashed(const QString& appearance)
    {
        QToolButton* pHovered = buttonNamed(qsl("triggers_action"));
        QToolButton* pResting = buttonNamed(qsl("aliases_action"));
        QVERIFY2(pHovered && pResting, "the bar has no Triggers or Aliases button to point at");
        QVERIFY2(pHovered->isVisible() && pResting->isVisible(), "the window is too narrow for the bar, so the buttons this case reads are in the overflow menu rather than on it");

        hoverOver(pHovered, true);
        const QImage bar = grabTheBar();
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();

        const QColor washed = sampled(bar, washSpotIn(pHovered->geometry()));
        const QColor wanted = composited(tokens.hoverWash, tokens.page);
        const QColor resting = sampled(bar, washSpotIn(pResting->geometry()));
        hoverOver(pHovered, false);

        qInfo().noquote() << qsl("  %1: the hovered button reads %2 against an expected %3, and one at rest %4 against the bar's %5")
                                     .arg(appearance, saidAs(washed), saidAs(wanted), saidAs(resting), saidAs(tokens.page));

        QVERIFY2(readsAs(washed, wanted),
                 qPrintable(qsl("%1: a button under the pointer is not washed in the hover tone: %2 at %3 where the wash over the bar's own surface is %4")
                                    .arg(appearance, saidAs(washed), saidAs(washSpotIn(pHovered->geometry())), saidAs(wanted))));
        QVERIFY2(
                readsAs(resting, tokens.page),
                qPrintable(qsl("%1: a button nothing is pointing at is drawn on something other than the bar's own surface: %2 rather than %3").arg(appearance, saidAs(resting), saidAs(tokens.page))));
    }

    void aSwitchedOnSplitButtonReadsAsTwoAreas(const QString& appearance)
    {
        QToolButton* pButton = buttonNamed(qsl("mute"));
        QAction* pAction = mudlet::self()->findChild<QAction*>(qsl("muteMedia"));
        QVERIFY2(pButton && pAction, "the bar has no Sound button, or no muteMedia action behind it");
        QVERIFY2(pButton->isVisible(), "the window is too narrow for the bar, so the Sound button is in the overflow menu rather than on it");
        QVERIFY2(pButton->popupMode() == QToolButton::MenuButtonPopup, "the Sound button no longer keeps a menu half, so there is nothing here to tell from its body");

        pAction->setChecked(true);
        QCoreApplication::processEvents();
        QVERIFY2(pButton->isChecked(), "checking the mute action did not switch the button it is the default action of on");

        const QImage bar = grabTheBar();
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QRect box = pButton->geometry();

        const QColor body = sampled(bar, washSpotIn(box));
        const QColor wantedBody = composited(tokens.accentWash, tokens.page);

        // A pixel either side of the computed column as well, since the seam is
        // one pixel wide and a rounding of the sub-control's box would move it
        const int seamAt = seamColumnOf(box);
        QColor seam;
        int seamFoundAt = -1;
        QStringList aroundTheSeam;
        for (int x = seamAt - 1; x <= seamAt + 1; ++x) {
            const QColor read = sampled(bar, QPoint(x, box.center().y()));
            aroundTheSeam << qsl("%1:%2").arg(QString::number(x), saidAs(read));
            if (seamFoundAt < 0 && readsAs(read, tokens.border)) {
                seam = read;
                seamFoundAt = x;
            }
        }

        pAction->setChecked(false);
        QCoreApplication::processEvents();

        qInfo().noquote() << qsl("  %1: the switched-on Sound button reads %2 against an expected %3, with the seam wanted at x=%4 in %5 and the columns around it %6")
                                     .arg(appearance, saidAs(body), saidAs(wantedBody), QString::number(seamAt), saidAs(tokens.border), aroundTheSeam.join(qsl(", ")));

        QVERIFY2(readsAs(body, wantedBody),
                 qPrintable(qsl("%1: a switched-on button on the bar is not washed in the accent: %2 at %3 where the accent's wash over the bar's surface is %4")
                                    .arg(appearance, saidAs(body), saidAs(washSpotIn(box)), saidAs(wantedBody))));
        QVERIFY2(seamFoundAt >= 0,
                 qPrintable(qsl("%1: the Sound button's menu half is not told from its body at all - the seam the recipe draws in %2 is nowhere within a pixel of x=%3 on a button at %4; the "
                                "columns there read %5")
                                    .arg(appearance, saidAs(tokens.border), QString::number(seamAt), saidAs(box), aroundTheSeam.join(qsl(", ")))));
        QVERIFY2(!readsAs(seam, wantedBody),
                 qPrintable(qsl("%1: the seam on the Sound button's menu half is the same wash its body is, %2, so the two halves read as one target").arg(appearance, saidAs(seam))));
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
        // The chevron on a split button's menu half is written into the cache
        // directory, and the rule pointing at it is left out when it cannot be
        // written - so a run of this test writes nowhere the machine keeps
        QVERIFY(mCacheDir.isValid());
        mSavedXdgCache = qgetenv("XDG_CACHE_HOME");
        qputenv("XDG_CACHE_HOME", mCacheDir.path().toUtf8());

        mpServer = new TelnetServerStub(qApp);
        mpServer->start(mLocalhost, 0);
        QVERIFY2(mpServer->isListening(), qPrintable(qsl("TelnetServerStub failed to start: %1").arg(mpServer->errorString())));
        mPort = QString::number(mpServer->serverPort());
        mudlet::start();
        mudlet::self()->setupConfig();
        QCOMPARE(mudlet::getMudletPath(enums::mainPath), qsl("%1/mudlet").arg(mConfigDir.path()));
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>(qsl("MudletInstanceCoordinator")));
        mudlet::self()->init();
        mudlet::self()->setInterfaceLanguage(qsl("en_US"));
        mudlet::self()->setStorePasswordsSecurely(false);

        deleteProfileDirectory(mFirstHostname);
        deleteProfileDirectory(mSecondHostname);

        // Two of them, because slot_tabDetachRequested() refuses index 0
        startProfile(mFirstHostname);
        if (QTest::currentTestFailed()) {
            return;
        }
        startProfile(mSecondHostname);
        if (QTest::currentTestFailed()) {
            return;
        }

        // Nothing on a bar that is not on show can be sampled, and with a
        // profile loaded the shipped default is to keep it off
        mudlet::self()->setToolBarVisibility(enums::visibleAlways);
        mudlet::self()->resize(scmWindowWidth, scmWindowHeight);
        mudlet::self()->show();
        QVERIFY(QTest::qWaitForWindowExposed(mudlet::self()));
        QCoreApplication::processEvents();

        QVERIFY2(theBar(), "the main window has no toolbar named mpMainToolBar");
        QVERIFY2(theBar()->isVisible() && theBar()->width() > 0, "the main window's toolbar is not on show, so nothing below can be read off a grab of it");
    }

    void cleanupTestCase()
    {
        delete mpServer;
        mpServer = nullptr;
        if (mudlet::self()) {
            mudlet::self()->setAppearance(enums::Appearance::systemSetting);
            deleteProfileDirectory(mFirstHostname);
            deleteProfileDirectory(mSecondHostname);
            delete mudlet::self();
        }
        mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME") : qputenv("XDG_CONFIG_HOME", mSavedXdg);
        mSavedXdgCache.isNull() ? qunsetenv("XDG_CACHE_HOME") : qputenv("XDG_CACHE_HOME", mSavedXdgCache);
    }

    // The design's rules are on the bar, and a profile's own sheet is composed
    // after them rather than in place of them
    void test_theBarCarriesTheRecipeAndTheProfilesSheetComesLast()
    {
        const QString sheet = theBar()->styleSheet();
        QVERIFY2(sheet.contains(qsl("QToolBar#mpMainToolBar {")), qPrintable(qsl("the bar's sheet does not draw the bar itself: %1").arg(sheet.left(200))));
        QVERIFY2(sheet.contains(qsl("QToolButton:hover")), "the bar's sheet says nothing about a button under the pointer, so the platform style draws that");
        QVERIFY2(sheet.contains(qsl("QToolButton:checked")), "the bar's sheet says nothing about a button that is switched on, so Sound and Full Screen are drawn by the platform style");
        QVERIFY2(sheet.contains(qsl("[popupMode=\"1\"]")), "the bar's sheet never picks out the buttons whose trailing half opens a menu");
        QVERIFY2(sheet.contains(qsl("::menu-button")), "the bar's sheet draws no menu half on a split button, so its two click areas read as one");

        const QString marker = qsl("/* profile-sheet-marker */");
        mudlet::self()->setGlobalStyleSheet(marker);
        const QString composed = theBar()->styleSheet();
        mudlet::self()->setGlobalStyleSheet(QString());

        QVERIFY2(composed.endsWith(marker),
                 qPrintable(qsl("a profile's own stylesheet does not come last on the bar, so it cannot override the design's rules on the widget they share: the sheet ends \"%1\"")
                                    .arg(composed.right(120))));
        QVERIFY2(composed.contains(qsl("QToolBar#mpMainToolBar {")), "a profile's stylesheet replaced the design's rules on the bar rather than being composed after them");
    }

    void test_aHoveredButtonIsWashed() { aHoveredButtonIsWashed(qsl("as started")); }

    void test_aSwitchedOnSplitButtonReadsAsTwoAreas() { aSwitchedOnSplitButtonReadsAsTwoAreas(qsl("as started")); }

    // One look on both, which is the whole reason the bar stopped being the
    // platform's to draw
    void test_bothAppearances()
    {
        for (const enums::Appearance appearance : {enums::Appearance::dark, enums::Appearance::light}) {
            setAppearance(appearance);
            aHoveredButtonIsWashed(appearanceSaidAs(appearance));
            if (QTest::currentTestFailed()) {
                break;
            }
            aSwitchedOnSplitButtonReadsAsTwoAreas(appearanceSaidAs(appearance));
            if (QTest::currentTestFailed()) {
                break;
            }
        }
        setAppearance(enums::Appearance::systemSetting);
    }

    // A detached window builds its own bar from its own actions
    void test_aDetachedWindowsBarCarriesTheRecipe()
    {
        QVERIFY2(mudlet::self()->getDetachedWindows().isEmpty(), "a detached window was left over from an earlier test");
        mudlet::self()->slot_tabDetachRequested(1, QPoint(200, 200));
        TDetachedWindow* pDetachedWindow = mudlet::self()->getDetachedWindows().value(mSecondHostname);
        QVERIFY2(pDetachedWindow, qPrintable(qsl("detaching tab 1 produced no window for '%1' - the tab order is not what this test assumes").arg(mSecondHostname)));

        QToolBar* pDetachedBar = pDetachedWindow->findChild<QToolBar*>(qsl("detachedMainToolBar"));
        const QString sheet = pDetachedBar ? pDetachedBar->styleSheet() : QString();
        mudlet::self()->slot_tabReattachRequested(mSecondHostname);

        QVERIFY2(pDetachedBar, "the detached window has no toolbar named detachedMainToolBar");
        QVERIFY2(sheet.contains(qsl("QToolBar#detachedMainToolBar {")), qPrintable(qsl("a detached window's bar is left for the platform style to draw: %1").arg(sheet.left(200))));
        QVERIFY2(sheet.contains(qsl("QToolButton:checked")), "a detached window's bar says nothing about a button that is switched on, so its Sound button is drawn by the platform style");
    }
};

#include "MainToolBarStyleTest.moc"
MUDLET_GROUPED_TEST_MAIN(MainToolBarStyleTest)
