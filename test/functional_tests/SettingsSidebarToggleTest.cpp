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
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QSettings>
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
};

#include "SettingsSidebarToggleTest.moc"
MUDLET_GROUPED_TEST_MAIN(SettingsSidebarToggleTest)
