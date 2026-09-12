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
 * The package manager drawn in the design language, and the things about that
 * which can go wrong without anything else noticing.
 *
 * Every sheet goes on one of the two columns rather than on the window, because
 * mudlet assigns a profile's Lua stylesheet to a dialog on show - so a sheet
 * set on the dialog would simply be replaced by it, and the window would come
 * back platform-drawn with nothing failing.
 *
 * The view is chosen from a row of chips now, but the three buttons the .ui
 * file put it on are still there, hidden, and are still what every slot and
 * every other package test presses. The two have to stay in step whichever of
 * them was moved, or the strip says one thing and the list shows another.
 *
 * A row is drawn by PackageItemDelegate on the shared row recipe: the name, the
 * one-line summary under it, the package's own picture or the design's package
 * glyph, and the accent bar down the leading edge of the chosen row - which is
 * read here as pixels, since no other reading of it is possible.
 *
 * Install and Remove moved into the details column and are hidden rather than
 * merely disabled in the view they do not apply to; the notice under the list
 * is the shared one, hidden until the window has something to report.
 *
 * Run with: ctest -R PackageManagerShellTest -V
 */

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QTabBar>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <memory>

#include "EditorTreeRowMetrics.h"
#include "GripSplitter.h"
#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PackageItemDelegate.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgPackageManager.h"
#include "dlgSystemMessageArea.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class PackageManagerShellTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    QTemporaryDir mCacheDir;
    QByteArray mSavedXdgCache;
    TelnetServerStub* mpServer = nullptr;
    Host* mpHost = nullptr;
    dlgPackageManager* mpDialog = nullptr;
    QString mPort;
    const QString mLocalhost = qsl("localhost");
    const QString mProfileName = qsl("PackageManagerShell-Test-Profile");

    // The packages this walks. Seeded rather than taken from whatever a fresh
    // profile preinstalls: what is being read here is how a row is drawn, and a
    // case that changes its answer when the preinstall list changes is a case
    // about the wrong thing.
    const QString mFirstPackage = qsl("aaa-shell-test-package");
    const QString mFirstTitle = qsl("The first package this test lists");
    const QString mSecondPackage = qsl("zzz-shell-test-package");

    // How far a sampled pixel may sit from the colour the tokens mix. A styled
    // fill is composited over what is under it rather than blitted, and the bar
    // is three pixels wide with an antialiased corner at each end, so the other
    // style tests' hairline slack is what is allowed here too.
    static constexpr int scmFillTolerance = 24;

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

    // The cached repository index the window reads the Explore view out of.
    // Handed a version, it also lists the first installed package at that
    // version, which is the only way the Updates view has anything in it: a
    // package has to be installed *and* listed higher on the repository before
    // it counts as waiting for an update.
    bool writeRepositoryIndex(const QString& firstPackageVersion = QString()) const
    {
        QJsonArray packages;
        for (const QString& name : {qsl("explore-only-one"), qsl("explore-only-two")}) {
            QJsonObject entry;
            entry.insert(qsl("mpackage"), name);
            entry.insert(qsl("filename"), qsl("%1.mpackage").arg(name));
            entry.insert(qsl("version"), qsl("1.0.0"));
            entry.insert(qsl("title"), qsl("a package that exists only in this test"));
            entry.insert(qsl("author"), qsl("PackageManagerShellTest"));
            packages.append(entry);
        }
        if (!firstPackageVersion.isEmpty()) {
            QJsonObject entry;
            entry.insert(qsl("mpackage"), mFirstPackage);
            entry.insert(qsl("filename"), qsl("%1.mpackage").arg(mFirstPackage));
            entry.insert(qsl("version"), firstPackageVersion);
            entry.insert(qsl("title"), mFirstTitle);
            entry.insert(qsl("author"), qsl("A Shell Test"));
            packages.append(entry);
        }
        QJsonObject root;
        root.insert(qsl("packages"), packages);

        QFile file(qsl("%1/mpkg.packages.json").arg(mudlet::getMudletPath(enums::profileHomePath, mProfileName)));
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        return file.write(QJsonDocument(root).toJson()) > 0;
    }

    QTabBar* viewBar() const { return mpDialog->findChild<QTabBar*>(qsl("packagesViewBar")); }

    // The view, read the way the window itself reads it: through the hidden
    // button the group holds checked
    int checkedView() const
    {
        if (mpDialog->pushButton_explore->isChecked()) {
            return static_cast<int>(NavigationView::Explore);
        }
        if (mpDialog->pushButton_installed->isChecked()) {
            return static_cast<int>(NavigationView::Installed);
        }
        if (mpDialog->pushButton_updates->isChecked()) {
            return static_cast<int>(NavigationView::Updates);
        }
        return -1;
    }

    void showView(const NavigationView view)
    {
        QAbstractButton* pButton = view == NavigationView::Explore ? static_cast<QAbstractButton*>(mpDialog->pushButton_explore)
                                                                   : (view == NavigationView::Installed ? static_cast<QAbstractButton*>(mpDialog->pushButton_installed)
                                                                                                        : static_cast<QAbstractButton*>(mpDialog->pushButton_updates));
        pButton->click();
        settle();
    }

    void settle() const
    {
        QCoreApplication::processEvents();
        QTest::qWait(30ms);
        QCoreApplication::processEvents();
    }

    // The one path an appearance change takes while a window is open
    void setAppearance(const enums::Appearance appearance)
    {
        mudlet::self()->setAppearance(appearance);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        QCoreApplication::processEvents();
        mpDialog->grab();
    }

    static bool readsAs(const QColor& read, const QColor& wanted)
    {
        return std::abs(read.red() - wanted.red()) <= scmFillTolerance && std::abs(read.green() - wanted.green()) <= scmFillTolerance && std::abs(read.blue() - wanted.blue()) <= scmFillTolerance;
    }

    static int distance(const QColor& a, const QColor& b) { return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue()); }

    // A grab is taken at the screen's own pixel ratio, so a point measured in
    // the widget's coordinates is not where it lands in the image
    static QColor sampled(const QImage& shot, const QPoint& at)
    {
        const qreal ratio = shot.devicePixelRatio() > 0.0 ? shot.devicePixelRatio() : 1.0;
        return shot.pixelColor(qBound(0, qRound(at.x() * ratio), shot.width() - 1), qBound(0, qRound(at.y() * ratio), shot.height() - 1));
    }

    QImage listShot() const { return mpDialog->packageList->viewport()->grab().toImage(); }

    QRect rowRect(const int row) const { return mpDialog->packageList->visualItemRect(mpDialog->packageList->item(row)); }

    // A profile is created with packages of Mudlet's own already installed, and
    // the list is sorted - so which row a name is on is not something this can
    // count out
    int rowOf(const QString& packageName) const
    {
        for (int row = 0; row < mpDialog->packageList->count(); ++row) {
            if (mpDialog->packageList->item(row)->text() == packageName) {
                return row;
            }
        }
        return -1;
    }

    // Where the dot on a row is drawn, which is where a click has to land for
    // the delegate to read it as one on the switch
    QRect dotRect(const int row) const { return PackageItemDelegate::dotHitRect(rowRect(row)); }

    // How much of a row's trailing edge the version stands in. The name's ink
    // is read to the left of this: the version is written in the design's ok
    // tone, which stands further from the surface than any name does, so a scan
    // of the whole line would find the green whatever the name is written in.
    static constexpr int scmRowVersionZone = 56;

    // The strongest ink anywhere in a row's first line of words. A glyph's
    // strokes are antialiased into the surface under them, so the pixel that
    // stands furthest from that surface is the one drawn closest to the pen.
    QColor nameInk(const QImage& shot, const int row, const QColor& surface) const
    {
        const QRect rect = rowRect(row);
        QColor strongest = surface;
        int furthest = -1;
        for (int y = rect.top() + 2; y < rect.center().y(); ++y) {
            for (int x = rect.left() + 30; x < rect.right() - scmRowVersionZone; ++x) {
                const QColor read = sampled(shot, QPoint(x, y));
                if (const int apart = distance(read, surface); apart > furthest) {
                    furthest = apart;
                    strongest = read;
                }
            }
        }
        return strongest;
    }

    // The pixel in the run the version is drawn in that stands nearest the ink
    // it should be written in, with how far off it is. The run is the last of
    // the name's line before what the row leaves clear at its trailing edge.
    QPair<QColor, int> versionPixel(const QImage& shot, const int row, const QColor& ink) const
    {
        const QRect rect = rowRect(row);
        QColor nearest;
        int closest = -1;
        for (int y = rect.top() + 2; y < rect.center().y(); ++y) {
            for (int x = rect.right() - scmRowVersionZone; x < rect.right() - 4; ++x) {
                const QColor read = sampled(shot, QPoint(x, y));
                if (const int apart = distance(read, ink); closest < 0 || apart < closest) {
                    closest = apart;
                    nearest = read;
                }
            }
        }
        return {nearest, closest};
    }

    PackageItemDelegate* rowDelegate() const { return qobject_cast<PackageItemDelegate*>(mpDialog->packageList->itemDelegate()); }

    // What the delegate draws on a row's first line, asked of the delegate
    // itself: the option a view hands it, so the name is cut to the same room
    QString nameDrawn(const int row) const
    {
        QStyleOptionViewItem option;
        option.initFrom(mpDialog->packageList);
        option.rect = rowRect(row);
        return rowDelegate()->nameDrawn(option, mpDialog->packageList->model()->index(row, 0));
    }

    // The switch, thrown the way the Host does rather than through the dialog,
    // so that what is read afterwards is what the dialog made of it
    void setPackageEnabled(const QString& packageName, const bool enabled)
    {
        mpHost->setPackageEnabled(packageName, enabled);
        settle();
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
        // The glyphs a stylesheet points at are written into the cache
        // directory, so a run of this test writes nowhere the machine keeps
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
        // The words below are read as words rather than as pictures
        mudlet::self()->setInterfaceLanguage(qsl("en_US"));
        mudlet::self()->setStorePasswordsSecurely(false);

        deleteProfileDirectory(mProfileName);
        mpHost = TestProfile::create(mProfileName, mLocalhost, mPort);
        QVERIFY2(mpHost, "No active host after profile creation");
        QSignalSpy connected(&(mpHost->mTelnet), &cTelnet::signal_connected);
        QVERIFY2(connected.wait(2000), "Could not connect with the host.");

        // Two installed packages, the first of them carrying a summary: a row
        // with one and a row without are drawn differently and both are read
        // below. Written straight onto the host the way PackageManagerRemovalTest
        // does, since nothing here installs anything.
        mpHost->mInstalledPackages << mFirstPackage << mSecondPackage;
        mpHost->mPackageInfo.insert(mFirstPackage,
                                    QMap<QString, QString>{{qsl("mpackage"), mFirstPackage},
                                                           {qsl("title"), mFirstTitle},
                                                           {qsl("author"), qsl("A Shell Test")},
                                                           {qsl("version"), qsl("1.2.3")},
                                                           {qsl("description"), qsl("# Heading\n\nA package with `code` in its notes.\n")}});
        mpHost->mPackageInfo.insert(mSecondPackage, QMap<QString, QString>{{qsl("mpackage"), mSecondPackage}, {qsl("version"), qsl("0.1")}});
        // The second of the two starts switched off, so that both readings of
        // the dot are on the screen the moment the window opens. Written onto
        // the list the profile saves rather than switched through the Host:
        // there is nothing installed here to switch, and what is being read is
        // how the row is drawn from that list.
        mpHost->mDisabledPackages << mSecondPackage;

        // A repository index of the window's own, so the Explore view has rows
        // in it without a byte crossing the network. Neither name is installed,
        // so nothing here reaches the Installed view or the Updates count.
        QVERIFY(writeRepositoryIndex());

        mpDialog = new dlgPackageManager(nullptr, mpHost);
        QVERIFY2(mpDialog, "the package manager was not built at all");
        // The way mudlet::slot_packageManager() hands the window over, which is
        // what lets the Host reach back into it when a package is switched
        mpHost->mpPackageManager = mpDialog;
        mpDialog->resize(900, 600);
        mpDialog->show();
        QVERIFY(QTest::qWaitForWindowExposed(mpDialog));
        settle();

        QVERIFY2(mpDialog->packageList->count() >= 2,
                 qPrintable(qsl("SETUP: the Installed view lists %1 packages, so the rows below have nothing to be read on").arg(QString::number(mpDialog->packageList->count()))));
    }

    void cleanupTestCase()
    {
        if (mpDialog) {
            mpDialog->close();
            mpDialog = nullptr;
        }
        QCoreApplication::processEvents();
        mpHost = nullptr;
        delete mpServer;
        mpServer = nullptr;
        if (mudlet::self()) {
            mudlet::self()->setAppearance(enums::Appearance::systemSetting);
            deleteProfileDirectory(mProfileName);
            delete mudlet::self();
        }
        mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME") : qputenv("XDG_CONFIG_HOME", mSavedXdg);
        mSavedXdgCache.isNull() ? qunsetenv("XDG_CACHE_HOME") : qputenv("XDG_CACHE_HOME", mSavedXdgCache);
    }

    // The three views are a row of chips, and the chosen chip follows the view
    // whichever of the two the reader moved - the bar itself, or one of the
    // hidden buttons every slot and every other package test still presses.
    // Website opens the package's own page on the repository site, which
    // addresses a package by a slug - lower case, non-alphanumerics folded to
    // one hyphen - rather than the front page scrolled to a fragment; a help
    // page the author named wins over both
    // An install or a switch can show a package's toolbars, which are dock
    // widgets of the main window and bring it forward over this dialog; every
    // act the window starts ends by asking for the front again
    // The window opens on the Installed view, and every one of the three things
    // that say which view it is agrees - the list, the hidden button and the
    // chip: the first tab a bar is given becomes current and said so, which
    // pressed Explore before the window had chosen
    void test_theWindowOpensWithTheInstalledChipLit()
    {
        dlgPackageManager fresh(nullptr, mpHost);
        auto* pBar = fresh.findChild<QTabBar*>(qsl("packagesViewBar"));
        QVERIFY2(pBar, "the fresh window has no chip row");
        qInfo().noquote() << qsl("  a fresh window's chip row reads tab %1, the hidden Installed button is %2checked")
                                     .arg(pBar->currentIndex())
                                     .arg(fresh.pushButton_installed->isChecked() ? QString() : qsl("not "));
        QVERIFY2(pBar->currentIndex() == static_cast<int>(NavigationView::Installed),
                 qPrintable(qsl("a fresh window lights chip %1 where the Installed view (%2) is what it opens on").arg(pBar->currentIndex()).arg(static_cast<int>(NavigationView::Installed))));
        QVERIFY2(fresh.pushButton_installed->isChecked(), "a fresh window's hidden Installed button is not the checked one");
        QVERIFY2(fresh.mCurrentView == NavigationView::Installed, "a fresh window's view is not Installed");
    }

    // The name at the head of the details column is cut to the room its row
    // has, and that room follows the seam - including a seam moved by code,
    // which announces nothing
    void test_theHeadlineFitsTheRoomTheSeamLeavesIt()
    {
        showView(NavigationView::Installed);
        QVERIFY2(mpDialog->mpSplitter, "there is no splitter to move");
        const QList<int> before = mpDialog->mpSplitter->sizes();
        // Long enough to be cut beside a narrow column and whole beside a wide one
        const QString longName = qsl("a-package-name-long-enough-to-be-cut");
        mpDialog->fillPackageDetails(longName, qsl("A summary"), qsl("An author"), qsl("1.0"));
        QCoreApplication::processEvents();

        mpDialog->mpSplitter->setSizes({mpDialog->width() - 330, 330});
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        const QString cramped = mpDialog->label_packageName->text();

        mpDialog->mpSplitter->setSizes({300, mpDialog->width() - 300});
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        const QString roomy = mpDialog->label_packageName->text();
        qInfo().noquote() << qsl("  the headline reads \"%1\" with the details column narrow and \"%2\" with it wide").arg(cramped, roomy);

        QVERIFY2(cramped != longName, qPrintable(qsl("beside a 330px column the headline still reads the whole \"%1\", so the narrow case reads nothing").arg(cramped)));
        QVERIFY2(roomy == longName,
                 qPrintable(qsl("with the details column wide again the headline still reads \"%1\" rather than the whole name \"%2\" - the name was cut for a room the column no longer has")
                                    .arg(roomy, longName)));
        mpDialog->mpSplitter->setSizes(before);
        QCoreApplication::processEvents();
        mpDialog->refreshPackageStates();
    }

    void test_theWindowComesBackToTheFrontAfterItsOwnActs()
    {
        mudlet::self()->show();
        mudlet::self()->raise();
        mudlet::self()->activateWindow();
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        if (QApplication::activeWindow() != mudlet::self()) {
            QSKIP("this platform does not report window activation, so nothing here can be read");
        }

        mpDialog->comeBackToFront();
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        QVERIFY2(QApplication::activeWindow() == mpDialog,
                 qPrintable(qsl("after asking for the front the active window is %1 rather than the package manager")
                                    .arg(QApplication::activeWindow() ? QApplication::activeWindow()->objectName() : qsl("none"))));
    }

    void test_websiteOpensThePackagesOwnPage()
    {
        QCOMPARE(mpDialog->packageWebsiteUrl(qsl("achaea-room-tracking")), qsl("https://packages.mudlet.org/packages/achaea-room-tracking"));
        QCOMPARE(mpDialog->packageWebsiteUrl(qsl("Achaean System")), qsl("https://packages.mudlet.org/packages/achaean-system"));
        QCOMPARE(mpDialog->packageWebsiteUrl(qsl("generic_mapper")), qsl("https://packages.mudlet.org/packages/generic-mapper"));
        QCOMPARE(mpDialog->packageWebsiteUrl(qsl("AchaeaChatTabs")), qsl("https://packages.mudlet.org/packages/achaeachattabs"));
        QCOMPARE(mpDialog->packageWebsiteUrl(qsl("  odd -- name! ")), qsl("https://packages.mudlet.org/packages/odd-name"));

        QMap<QString, QString> withHelp = mpHost->mPackageInfo.value(mFirstPackage);
        withHelp.insert(qsl("helpURL"), qsl("https://example.org/help"));
        mpHost->mPackageInfo.insert(mFirstPackage, withHelp);
        QCOMPARE(mpDialog->packageWebsiteUrl(mFirstPackage), qsl("https://example.org/help"));
        withHelp.remove(qsl("helpURL"));
        mpHost->mPackageInfo.insert(mFirstPackage, withHelp);
    }

    void test_theViewIsARowOfChipsThatStaysInStepWithTheButtons()
    {
        QTabBar* pBar = viewBar();
        QVERIFY2(pBar, "there is no QTabBar named 'packagesViewBar' - the view is not drawn as a row of chips at all");
        QCOMPARE(pBar->count(), 3);
        QVERIFY2(!mpDialog->headerBar->isVisible(), "the old row of navigation buttons is still on show beside the chips");
        QVERIFY2(!pBar->accessibleName().isEmpty(), "the strip of chips has no accessible name, so a screen reader announces an unnamed tab bar");

        showView(NavigationView::Explore);
        qInfo().noquote() << qsl("  the Explore button was pressed; the bar reads tab %1").arg(QString::number(pBar->currentIndex()));
        QVERIFY2(pBar->currentIndex() == static_cast<int>(NavigationView::Explore),
                 qPrintable(qsl("pressing the hidden Explore button left the chip row on tab %1 rather than on %2")
                                    .arg(QString::number(pBar->currentIndex()), QString::number(static_cast<int>(NavigationView::Explore)))));

        pBar->setCurrentIndex(static_cast<int>(NavigationView::Installed));
        settle();
        qInfo().noquote() << qsl("  the Installed chip was chosen; the buttons read view %1").arg(QString::number(checkedView()));
        QVERIFY2(checkedView() == static_cast<int>(NavigationView::Installed),
                 qPrintable(qsl("choosing the Installed chip left the hidden buttons on view %1 rather than on %2")
                                    .arg(QString::number(checkedView()), QString::number(static_cast<int>(NavigationView::Installed)))));
        QVERIFY2(mpDialog->packageList->count() >= 2, "choosing the Installed chip did not bring the installed packages back into the list");
    }

    // The count of waiting updates is on the chip the reader sees, not only on
    // the button nothing shows any more
    void test_theUpdatesChipCarriesWhateverTheButtonSays()
    {
        QTabBar* pBar = viewBar();
        QVERIFY(pBar);
        const int updates = static_cast<int>(NavigationView::Updates);
        qInfo().noquote() << qsl("  the Updates button says \"%1\", the chip says \"%2\"").arg(mpDialog->pushButton_updates->text(), pBar->tabText(updates));
        QVERIFY2(pBar->tabText(updates) == mpDialog->pushButton_updates->text(),
                 qPrintable(qsl("the Updates chip says \"%1\" where the button it stands for says \"%2\"").arg(pBar->tabText(updates), mpDialog->pushButton_updates->text())));
    }

    // Both sheets exist and are set on the columns, never on the dialog, whose
    // own stylesheet a profile's Lua one is assigned to on every show
    void test_theSheetsAreSetOnTheColumnsRatherThanOnTheWindow()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();

        QVERIFY2(mpDialog->styleSheet().isEmpty(), qPrintable(qsl("the dialog itself carries a sheet, which a profile's Lua stylesheet replaces on show: %1").arg(mpDialog->styleSheet().left(120))));

        const QString left = mpDialog->leftPanel->styleSheet();
        QVERIFY2(left.contains(tokens.pane.name()), qPrintable(qsl("the list column's sheet never names the pane tone %1, so it is not told apart from the page beside it").arg(tokens.pane.name())));
        QVERIFY2(left.contains(qsl("QTabBar::tab")), qPrintable(qsl("the list column's sheet draws no chips for the view strip: %1").arg(left.left(200))));
        QVERIFY2(left.contains(qsl("QLineEdit")), qPrintable(qsl("the list column's sheet draws no field, so the search box is platform-drawn: %1").arg(left.left(200))));
        QVERIFY2(left.contains(qsl("QPushButton")), "the list column's sheet draws no button, so Install from a file is platform-drawn");

        const QString rows = mpDialog->packageList->styleSheet();
        const QString chosenWash = uiDesign::blend(tokens.pane, tokens.accent, uiDesign::scmAccentWashStrength).name();
        qInfo().noquote() << qsl("  the chosen row is washed with %1").arg(chosenWash);
        QVERIFY2(rows.contains(chosenWash), qPrintable(qsl("the list's sheet never names %1, the wash the design fills a chosen row with: %2").arg(chosenWash, rows.left(240))));
        QVERIFY2(rows.contains(qsl("QScrollBar")), "the list's own scrollbars are left platform-drawn");

        const QString right = mpDialog->rightPanel->styleSheet();
        QVERIFY2(right.contains(tokens.page.name()), qPrintable(qsl("the details column's sheet never names the page tone %1").arg(tokens.page.name())));
        QVERIFY2(right.contains(qsl("QPushButton")), qPrintable(qsl("the details column's sheet draws no button: %1").arg(right.left(200))));
        QVERIFY2(right.contains(qsl("QTextEdit")), qPrintable(qsl("the details column's sheet draws no field, so the notes are platform-drawn: %1").arg(right.left(200))));
    }

    // Install and Remove stand in the details column now, and the one that
    // cannot apply in the current view is not there at all - a row of buttons
    // with one that can never be pressed is a row with a hole in it
    void test_theActionButtonsFollowTheView()
    {
        showView(NavigationView::Installed);
        QVERIFY2(!mpDialog->pushButton_installRepo->isVisible(), "Install is on show in the Installed view, where there is nothing for it to install");
        QVERIFY2(mpDialog->pushButton_remove->isVisible(), "Remove is not on show in the Installed view, which is the one view it applies to");

        showView(NavigationView::Explore);
        QVERIFY2(mpDialog->pushButton_installRepo->isVisible(), "Install is not on show in the Explore view");
        QVERIFY2(!mpDialog->pushButton_remove->isVisible(), "Remove is on show in the Explore view, where nothing listed is installed");

        showView(NavigationView::Installed);
        mpDialog->packageList->setCurrentRow(0);
        settle();
        for (const QPushButton* pButton : {mpDialog->pushButton_website, mpDialog->pushButton_report}) {
            QVERIFY2(!pButton->text().isEmpty(), qPrintable(qsl("%1 is a picture with no word beside it").arg(pButton->objectName())));
            QVERIFY2(!pButton->icon().isNull(), qPrintable(qsl("%1 carries no glyph").arg(pButton->objectName())));
            QVERIFY2(!pButton->isFlat(), qPrintable(qsl("%1 is still flat, so it reads as decoration rather than as somewhere to press").arg(pButton->objectName())));
            QVERIFY2(!pButton->toolTip().isEmpty(), qPrintable(qsl("%1 says nothing about where it leads").arg(pButton->objectName())));
        }
        QVERIFY2(!mpDialog->pushButton_installFile->icon().isNull(), "Install from a file carries no glyph");
        QCOMPARE(mpDialog->pushButton_installFile->text(), qsl("Install from a file"));
    }

    // A row is two lines and a picture, and the chosen one carries the accent
    // bar down its leading edge that every other list in the design does
    void test_theRowsAreTheDesignsRows()
    {
        showView(NavigationView::Installed);
        mpDialog->packageList->setCurrentRow(0);
        settle();

        QListWidgetItem* pRow = mpDialog->packageList->item(0);
        QVERIFY2(pRow, "the Installed view lists nothing at all");
        QCOMPARE(pRow->text(), mFirstPackage);
        QVERIFY2(pRow->data(Qt::UserRole).toString() == mFirstTitle,
                 qPrintable(qsl("the row's second line reads \"%1\" rather than the package's title \"%2\"").arg(pRow->data(Qt::UserRole).toString(), mFirstTitle)));

        QStyleOptionViewItem option;
        option.initFrom(mpDialog->packageList);
        option.rect = mpDialog->packageList->visualItemRect(pRow);
        const int height = mpDialog->packageList->itemDelegate()->sizeHint(option, mpDialog->packageList->model()->index(0, 0)).height();
        QFont caption = option.font;
        caption.setPointSize(uiDesign::typeSize(uiDesign::TypeStep::Caption));
        const int twoLines = QFontMetrics(option.font).height() + QFontMetrics(caption).height();
        qInfo().noquote() << qsl("  a row measures %1px against %2px of words on two lines").arg(QString::number(height), QString::number(twoLines));
        QVERIFY2(height >= twoLines,
                 qPrintable(qsl("a row is %1px tall where its name and its summary alone come to %2px - the summary has nowhere to be drawn").arg(QString::number(height), QString::number(twoLines))));

        const QRect row = mpDialog->packageList->visualItemRect(pRow);
        QVERIFY2(!row.isEmpty(), "the chosen row has no rectangle on the viewport, so there is nothing to read");
        const QImage shot = mpDialog->packageList->viewport()->grab().toImage();
        const QColor bar = shot.pixelColor(row.left() + 1, row.center().y());
        const QColor accent = uiDesign::themeTokens().accent;
        qInfo().noquote() << qsl("  the chosen row's gutter reads %1 against an accent of %2").arg(bar.name(), accent.name());
        QVERIFY2(readsAs(bar, accent),
                 qPrintable(qsl("the chosen row's leading gutter reads %1 where the accent is %2 - the bar that says which row is chosen is not being painted").arg(bar.name(), accent.name())));
    }

    // The notice under the list is the shared one, and says nothing until the
    // window has something to report
    void test_theNoticeIsTheSharedOneAndIsQuietAtRest()
    {
        auto* pNotice = mpDialog->findChild<dlgSystemMessageArea*>(qsl("packagesNotice"));
        QVERIFY2(pNotice, "there is no dlgSystemMessageArea named 'packagesNotice' - the window reports failures with a bare label");
        QVERIFY2(mpDialog->leftPanel->isAncestorOf(pNotice), "the notice is not in the list column, where what it reports happened");
        QVERIFY2(!pNotice->isVisible(), "the notice is on show with nothing to report");
        QVERIFY2(!mpDialog->label_importStatus->isVisible(), "the label the notice replaced is still on show");
    }

    // The head of the details column: the name a step up the type scale, the
    // author and version as one quiet caption line, and the picture at the size
    // the column was drawn for rather than the .ui file's 96px block
    void test_theHeadOfTheDetailsColumnIsTheTypeScale()
    {
        showView(NavigationView::Installed);
        mpDialog->packageList->setCurrentRow(0);
        settle();

        const QFont nameFont = mpDialog->label_packageName->font();
        qInfo().noquote() << qsl("  the package name is set at %1pt weight %2, against a Title step of %3pt")
                                     .arg(QString::number(nameFont.pointSize()), QString::number(nameFont.weight()), QString::number(uiDesign::typeSize(uiDesign::TypeStep::Title)));
        QVERIFY2(nameFont.pointSize() == uiDesign::typeSize(uiDesign::TypeStep::Title),
                 qPrintable(qsl("the package name is set at %1pt where the type scale's Title step is %2pt")
                                    .arg(QString::number(nameFont.pointSize()), QString::number(uiDesign::typeSize(uiDesign::TypeStep::Title)))));
        QVERIFY2(nameFont.weight() >= QFont::DemiBold,
                 qPrintable(qsl("the package name is set at weight %1, which is lighter than the DemiBold the design sets a title in").arg(QString::number(nameFont.weight()))));

        QCOMPARE(mpDialog->label_icon->size(), QSize(48, 48));
        QVERIFY2(!mpDialog->line->isVisible(), "the rule across the details column is still drawn, beside the notes field's own hairline");
        QVERIFY2(mpDialog->label_author->text().contains(qsl("A Shell Test")), qPrintable(qsl("the caption line does not name the author: \"%1\"").arg(mpDialog->label_author->text())));
        QVERIFY2(mpDialog->label_version->text().contains(qsl("1.2.3")), qPrintable(qsl("the caption line does not name the version: \"%1\"").arg(mpDialog->label_version->text())));
    }

    // Every row of the Installed view leads with the dot the editor's trees
    // lead a row with, in the same two readings: filled in the design's ok tone
    // where the package is running, a hollow ring in the chrome tone where it is
    // switched off. Read as pixels, since a picture has no other reading.
    void test_anInstalledRowLeadsWithTheStateDot()
    {
        showView(NavigationView::Installed);
        mpDialog->packageList->setCurrentRow(0);
        settle();

        const int onRow = rowOf(mFirstPackage);
        const int offRow = rowOf(mSecondPackage);
        QVERIFY2(onRow >= 0 && offRow >= 0, "SETUP: one of the two seeded packages is not listed in the Installed view");
        for (int row = 0; row < mpDialog->packageList->count(); ++row) {
            QVERIFY2(mpDialog->packageList->item(row)->data(PackageItemDelegate::cPackageEnabledRole).isValid(),
                     qPrintable(qsl("row %1 (\"%2\") of the Installed view carries no switch reading, so the delegate has nothing to draw a dot from")
                                        .arg(QString::number(row), mpDialog->packageList->item(row)->text())));
        }
        QCOMPARE(mpDialog->packageList->item(onRow)->data(PackageItemDelegate::cPackageEnabledRole).toBool(), true);
        QCOMPARE(mpDialog->packageList->item(offRow)->data(PackageItemDelegate::cPackageEnabledRole).toBool(), false);

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QColor running = uiDesign::stateColor(uiDesign::scmStateHue_ok, tokens.darkPage);
        const QImage shot = listShot();

        const QRect onDot = dotRect(onRow);
        QVERIFY2(!onDot.isEmpty(), "the running package's row has no dot rectangle at all");
        const QColor filled = sampled(shot, onDot.center());
        qInfo().noquote() << qsl("  the running package's dot reads %1 against an ok tone of %2").arg(filled.name(), running.name());
        QVERIFY2(readsAs(filled, running),
                 qPrintable(
                         qsl("the running package's dot reads %1 where the design's ok tone is %2 - a row of the Installed view is not leading with a filled dot").arg(filled.name(), running.name())));

        const QRect offDot = dotRect(offRow);
        // The ring is drawn on the square's edge and the middle of it is left
        // open, which is the whole difference between the two readings
        const QColor ring = sampled(shot, QPoint(offDot.left() + uiDesign::scmTreeMarkHitSlack, offDot.center().y()));
        const QColor hollow = sampled(shot, offDot.center());
        qInfo().noquote() << qsl("  the switched-off package's ring reads %1 against a chrome tone of %2, its middle %3").arg(ring.name(), tokens.mutedText.name(), hollow.name());
        QVERIFY2(readsAs(ring, tokens.mutedText),
                 qPrintable(qsl("the switched-off package's dot reads %1 on its edge where the chrome tone is %2 - it is not being drawn as a hollow ring").arg(ring.name(), tokens.mutedText.name())));
        QVERIFY2(!readsAs(hollow, running), qPrintable(qsl("the switched-off package's dot is filled with the ok tone %1 - it reads as running").arg(running.name())));
    }

    // ...and its name goes quiet with it, the way a switched-off row in the
    // editor's trees does. Read on the row that is not the chosen one: a chosen
    // row's words are the accent's ink whatever the package is doing.
    void test_aSwitchedOffPackagesNameIsQuiet()
    {
        showView(NavigationView::Installed);
        mpDialog->packageList->setCurrentRow(0);
        settle();

        const int offRow = rowOf(mSecondPackage);
        QVERIFY2(offRow >= 0, "SETUP: the switched-off package is not listed in the Installed view");
        QVERIFY2(offRow != mpDialog->packageList->currentRow(), "SETUP: the switched-off row is the chosen one, whose words are the accent's ink whatever the package is doing");

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QImage shot = listShot();
        const QColor ink = nameInk(shot, offRow, tokens.pane);
        qInfo().noquote() << qsl("  the switched-off row's name reads %1: %2 from the chrome tone %3, %4 from the value tone %5")
                                     .arg(ink.name(), QString::number(distance(ink, tokens.mutedText)), tokens.mutedText.name(), QString::number(distance(ink, tokens.text)), tokens.text.name());
        QVERIFY2(distance(ink, tokens.mutedText) < distance(ink, tokens.text),
                 qPrintable(qsl("the switched-off package's name reads %1, which is nearer the value tone %2 than the chrome tone %3 - the row does not say the package is off")
                                    .arg(ink.name(), tokens.text.name(), tokens.mutedText.name())));
    }

    // The dot is a switch, not a picture: a click on it throws the package, and
    // the details column beside it says so.
    void test_clickingTheDotSwitchesThePackage()
    {
        showView(NavigationView::Installed);
        setPackageEnabled(mFirstPackage, true);
        const int row = rowOf(mFirstPackage);
        QVERIFY2(row >= 0, "SETUP: the package this case switches is not listed in the Installed view");
        mpDialog->packageList->setCurrentRow(row);
        settle();
        QVERIFY2(mpHost->packageEnabled(mFirstPackage), "SETUP: the package this case switches is already off");

        auto* pToggle = mpDialog->findChild<QPushButton*>(qsl("packagesToggleButton"));
        QVERIFY2(pToggle, "there is no QPushButton named 'packagesToggleButton' - the details column offers no switch");
        auto* pNote = mpDialog->findChild<QLabel*>(qsl("packagesOffNote"));
        QVERIFY2(pNote, "there is no QLabel named 'packagesOffNote' - nothing says what a switched-off package is not doing");
        QCOMPARE(pToggle->text(), qsl("Turn off"));
        QVERIFY2(!pNote->isVisible(), "the line about a switched-off package is on show while the package is running");

        QTest::mouseClick(mpDialog->packageList->viewport(), Qt::LeftButton, Qt::NoModifier, dotRect(row).center());
        settle();
        qInfo().noquote() << qsl("  after a click on the dot the package reads %1").arg(mpHost->packageEnabled(mFirstPackage) ? qsl("on") : qsl("off"));
        QVERIFY2(!mpHost->packageEnabled(mFirstPackage), "a click on the row's dot left the package running - the dot is a picture rather than a switch");
        QCOMPARE(mpDialog->packageList->item(row)->data(PackageItemDelegate::cPackageEnabledRole).toBool(), false);
        QVERIFY2(pToggle->text() == qsl("Turn on"), qPrintable(qsl("with the package switched off the button still says \"%1\"").arg(pToggle->text())));
        QVERIFY2(pNote->isVisible(), "the package is switched off and nothing says what that means");
        QCOMPARE(mpDialog->packageList->currentRow(), row);

        QTest::mouseClick(mpDialog->packageList->viewport(), Qt::LeftButton, Qt::NoModifier, dotRect(row).center());
        settle();
        QVERIFY2(mpHost->packageEnabled(mFirstPackage), "a second click on the dot did not switch the package back on");
        QCOMPARE(pToggle->text(), qsl("Turn off"));
        QVERIFY2(!pNote->isVisible(), "the line about a switched-off package stayed up after the package was switched back on");
    }

    // Nothing listed in the Explore view is installed, so there is nothing there
    // to switch - and a button that can never be pressed is a hole in the row
    void test_theSwitchIsOnlyOfferedWhereThereIsSomethingToSwitch()
    {
        auto* pToggle = mpDialog->findChild<QPushButton*>(qsl("packagesToggleButton"));
        QVERIFY(pToggle);

        showView(NavigationView::Installed);
        mpDialog->packageList->setCurrentRow(rowOf(mFirstPackage));
        settle();
        QVERIFY2(pToggle->isVisible(), "the switch is not offered in the Installed view, which is the one view it applies to");

        showView(NavigationView::Explore);
        mpDialog->packageList->setCurrentRow(0);
        settle();
        QVERIFY2(mpDialog->packageList->count() >= 1, "SETUP: the Explore view lists nothing, so there is no row to read");
        QVERIFY2(!pToggle->isVisible(), "the switch is on show in the Explore view, where nothing listed is installed");
        QVERIFY2(!mpDialog->packageList->item(0)->data(PackageItemDelegate::cPackageEnabledRole).isValid(),
                 "a row of the Explore view carries a switch reading, so it is drawn with a dot that stands for nothing");

        showView(NavigationView::Installed);
        mpDialog->packageList->setCurrentRow(0);
        settle();
    }

    // A package being switched on or off is not an install: the window has to
    // stay where the reader left it rather than jumping back to the top of the
    // Installed view with their search cleared
    void test_aSwitchLeavesTheViewAndTheChosenRowAlone()
    {
        showView(NavigationView::Explore);
        mpDialog->packageList->setCurrentRow(1);
        settle();
        const int chosen = mpDialog->packageList->currentRow();
        const QString chosenName = mpDialog->packageList->currentItem()->text();
        QCOMPARE(chosen, 1);

        // Switched the way it is not already: a call that finds the package
        // already in the state asked for returns before it changes anything, and
        // a case built on one of those proves nothing
        QVERIFY2(!mpHost->packageEnabled(mSecondPackage), "SETUP: the package this case switches is already running, so the call below would do nothing");
        setPackageEnabled(mSecondPackage, true);
        qInfo().noquote() << qsl("  after the switch the window shows view %1, row %2 (\"%3\")")
                                     .arg(QString::number(checkedView()), QString::number(mpDialog->packageList->currentRow()), mpDialog->packageList->currentItem()->text());
        QVERIFY2(checkedView() == static_cast<int>(NavigationView::Explore),
                 qPrintable(qsl("switching a package off threw the window from the Explore view to view %1").arg(QString::number(checkedView()))));
        QVERIFY2(mpDialog->packageList->currentRow() == chosen && mpDialog->packageList->currentItem()->text() == chosenName,
                 qPrintable(qsl("switching a package off moved the chosen row from %1 (\"%2\") to %3 (\"%4\")")
                                    .arg(QString::number(chosen), chosenName, QString::number(mpDialog->packageList->currentRow()), mpDialog->packageList->currentItem()->text())));

        setPackageEnabled(mSecondPackage, false);
        showView(NavigationView::Installed);
        mpDialog->packageList->setCurrentRow(0);
        settle();
    }

    // The list's right-click menu carries the acts of the view being looked at,
    // and nothing that could not act on the row it was opened over. Built
    // through buildPackageContextMenu() rather than shown: exec() runs a menu's
    // own event loop, which a test cannot come back out of.
    void test_theListsRightClickMenuCarriesTheViewsActs()
    {
        showView(NavigationView::Installed);
        setPackageEnabled(mFirstPackage, true);
        const int row = rowOf(mFirstPackage);
        QVERIFY2(row >= 0, "SETUP: the package this case switches is not listed in the Installed view");
        mpDialog->packageList->setCurrentRow(row);
        settle();
        QVERIFY2(mpHost->packageEnabled(mFirstPackage), "SETUP: the package this case switches is already off");

        QMenu* pMenu = mpDialog->buildPackageContextMenu(mpDialog->packageList->item(row));
        QVERIFY2(pMenu, "no menu is built at all over an installed package's row");
        QStringList entries;
        for (const QAction* pAction : pMenu->actions()) {
            entries << qsl("%1 (%2)").arg(pAction->text(), pAction->objectName());
        }
        qInfo().noquote() << qsl("  over an installed package the menu carries: %1").arg(entries.join(qsl(", ")));

        auto* pToggle = pMenu->findChild<QAction*>(qsl("packagesMenuToggle"));
        QVERIFY2(pToggle, "the menu over an installed package carries no entry named 'packagesMenuToggle' - the switch is not offered where the pointer already is");
        QVERIFY2(pMenu->findChild<QAction*>(qsl("packagesMenuRemove")), "the menu over an installed package carries no entry named 'packagesMenuRemove'");
        QVERIFY2(pToggle->text() == qsl("Turn off"), qPrintable(qsl("over a running package the menu's switch reads \"%1\" rather than the word the button carries").arg(pToggle->text())));

        pToggle->trigger();
        settle();
        QVERIFY2(!mpHost->packageEnabled(mFirstPackage), "the menu's switch was triggered and the package is still running");
        delete pMenu;

        QMenu* pAgain = mpDialog->buildPackageContextMenu(mpDialog->packageList->item(row));
        QVERIFY(pAgain);
        auto* pToggleAgain = pAgain->findChild<QAction*>(qsl("packagesMenuToggle"));
        QVERIFY2(pToggleAgain, "the menu built over a switched-off package carries no switch");
        QVERIFY2(pToggleAgain->text() == qsl("Turn on"), qPrintable(qsl("with the package switched off the menu's entry still reads \"%1\"").arg(pToggleAgain->text())));
        delete pAgain;

        setPackageEnabled(mFirstPackage, true);

        showView(NavigationView::Explore);
        mpDialog->packageList->setCurrentRow(0);
        settle();
        QVERIFY2(mpDialog->packageList->count() >= 1, "SETUP: the Explore view lists nothing, so there is no row to open a menu over");
        QMenu* pExploreMenu = mpDialog->buildPackageContextMenu(mpDialog->packageList->item(0));
        QVERIFY2(pExploreMenu, "no menu is built over an Explore row");
        QVERIFY2(pExploreMenu->findChild<QAction*>(qsl("packagesMenuInstall")), "the menu over an Explore row carries no entry named 'packagesMenuInstall' - the one act that view offers");
        QVERIFY2(!pExploreMenu->findChild<QAction*>(qsl("packagesMenuToggle")), "the menu over an Explore row offers a switch, where nothing listed is installed to be switched");
        delete pExploreMenu;

        showView(NavigationView::Installed);
        mpDialog->packageList->setCurrentRow(0);
        settle();
    }

    // The version stands beside the name on the headline row rather than on the
    // caption line under the summary, reads as the bare number, and is written
    // in the design's ok tone walked until it can be read on the page
    void test_theVersionStandsBesideTheNameInTheOkTone()
    {
        showView(NavigationView::Installed);
        const int row = rowOf(mFirstPackage);
        QVERIFY2(row >= 0, "SETUP: the package this case reads is not listed in the Installed view");
        mpDialog->packageList->setCurrentRow(row);
        settle();

        QLabel* pName = mpDialog->label_packageName;
        QLabel* pVersion = mpDialog->label_version;
        QVERIFY2(pName->parentWidget() == pVersion->parentWidget(),
                 qPrintable(qsl("the version sits on %1 where the name sits on %2 - they are not on one row")
                                    .arg(pVersion->parentWidget() ? pVersion->parentWidget()->objectName() : qsl("nothing"),
                                         pName->parentWidget() ? pName->parentWidget()->objectName() : qsl("nothing"))));
        qInfo().noquote() << qsl("  the name is at %1,%2 %3x%4 and the version at %5,%6 %7x%8")
                                     .arg(QString::number(pName->x()),
                                          QString::number(pName->y()),
                                          QString::number(pName->width()),
                                          QString::number(pName->height()),
                                          QString::number(pVersion->x()),
                                          QString::number(pVersion->y()),
                                          QString::number(pVersion->width()),
                                          QString::number(pVersion->height()));
        QVERIFY2(std::abs(pName->y() - pVersion->y()) <= 1,
                 qPrintable(qsl("the name starts at y %1 and the version at y %2 - the version is on a line of its own rather than beside the name")
                                    .arg(QString::number(pName->y()), QString::number(pVersion->y()))));
        QVERIFY2(pVersion->x() >= pName->geometry().right(),
                 qPrintable(qsl("the version starts at x %1 where the name ends at x %2 - it does not stand after the name on the row")
                                    .arg(QString::number(pVersion->x()), QString::number(pName->geometry().right()))));

        QVERIFY2(pVersion->text() == qsl("1.2.3"), qPrintable(qsl("the version beside the name reads \"%1\" rather than the bare number").arg(pVersion->text())));
        QVERIFY2(pVersion->font().pointSize() == mpDialog->font().pointSize(),
                 qPrintable(qsl("the version is set at %1pt where the window's body font is %2pt").arg(QString::number(pVersion->font().pointSize()), QString::number(mpDialog->font().pointSize()))));

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QColor walked = uiDesign::readableOn(tokens.page, uiDesign::stateColor(uiDesign::scmStateHue_ok, tokens.darkPage), tokens.text, uiDesign::scmTextMinimumRatio);
        const QString right = mpDialog->rightPanel->styleSheet();
        qInfo().noquote() << qsl("  the version's ink walks the ok tone %1 to %2 against the page %3")
                                     .arg(uiDesign::stateColor(uiDesign::scmStateHue_ok, tokens.darkPage).name(), walked.name(), tokens.page.name());
        QVERIFY2(right.contains(qsl("#label_version { color: %1").arg(walked.name())),
                 qPrintable(qsl("the details column's sheet does not write the version in %1, the design's ok tone walked to the text floor: %2").arg(walked.name(), right.left(400))));

        // ...and in the Updates view it carries the arrow between the two
        // versions with no word in front of it. The fixture's repository index
        // has nothing installed on it, so one is written for this case alone
        // and taken away again.
        QVERIFY2(writeRepositoryIndex(qsl("9.9.9")), "SETUP: the repository index listing an update could not be written");
        QVERIFY2(mpDialog->readPackageRepositoryFile(), "SETUP: the repository index listing an update could not be read back");
        showView(NavigationView::Updates);
        const int updateRow = rowOf(mFirstPackage);
        QVERIFY2(updateRow >= 0, "SETUP: the package listed at a higher version did not reach the Updates view");
        mpDialog->packageList->setCurrentRow(updateRow);
        settle();
        qInfo().noquote() << qsl("  in the Updates view the version reads \"%1\"").arg(pVersion->text());
        QVERIFY2(pVersion->text() == qsl("1.2.3 → 9.9.9"),
                 qPrintable(qsl("in the Updates view the version reads \"%1\" rather than the installed number, the arrow and the waiting one").arg(pVersion->text())));

        QVERIFY(writeRepositoryIndex());
        QVERIFY(mpDialog->readPackageRepositoryFile());
        showView(NavigationView::Installed);
        mpDialog->packageList->setCurrentRow(0);
        settle();
    }

    // ...and every row says the same thing about the package it stands for:
    // the version, right-aligned on the name's own line, in that same green,
    // with the name still drawn in full beside it
    void test_everyRowCarriesItsVersionAtTheTrailingEdge()
    {
        showView(NavigationView::Installed);
        const int row = rowOf(mFirstPackage);
        QVERIFY2(row >= 0, "SETUP: the package this case reads is not listed in the Installed view");
        mpDialog->packageList->setCurrentRow(row);
        settle();

        QListWidgetItem* pRow = mpDialog->packageList->item(row);
        QVERIFY2(pRow->data(PackageItemDelegate::cPackageVersionRole).toString() == qsl("1.2.3"),
                 qPrintable(qsl("the row carries \"%1\" in its version role where the package is installed at 1.2.3 - nothing on the row says which version it is")
                                    .arg(pRow->data(PackageItemDelegate::cPackageVersionRole).toString())));

        QVERIFY2(rowDelegate(), "the list's delegate is not a PackageItemDelegate, so there is no row-version ink to read against");
        const QColor ink = rowDelegate()->versionInk(true);
        const QImage shot = listShot();
        const auto [read, apart] = versionPixel(shot, row, ink);
        qInfo().noquote() << qsl("  the chosen row's trailing run reads nearest %1 against the version ink %2, %3 apart").arg(read.name(), ink.name(), QString::number(apart));
        QVERIFY2(readsAs(read, ink),
                 qPrintable(qsl("nothing in the last %1px of the chosen row's name line reads as %2, the ink the version is written in - the nearest pixel is %3 - so the row draws no version")
                                    .arg(QString::number(scmRowVersionZone), ink.name(), read.name())));

        // ...and the name beside it is not cut to make room: the delegate is
        // asked what it draws on that line rather than the pixels, since an
        // ellipsis is three dots no reading of a grab can tell from a full stop
        qInfo().noquote() << qsl("  the row's name line reads \"%1\"").arg(nameDrawn(row));
        QVERIFY2(nameDrawn(row) == mFirstPackage,
                 qPrintable(qsl("the row's name line reads \"%1\" where the package is called \"%2\" - the version took room the name needed").arg(nameDrawn(row), mFirstPackage)));

        // A switched-off package keeps the green: which version is installed is
        // a fact about the package, not a reading of whether it is running
        const int offRow = rowOf(mSecondPackage);
        QVERIFY2(offRow >= 0 && offRow != row, "SETUP: the switched-off package is not on a row of its own in the Installed view");
        QVERIFY2(mpDialog->packageList->item(offRow)->data(PackageItemDelegate::cPackageVersionRole).toString() == qsl("0.1"),
                 qPrintable(qsl("the switched-off package's row carries \"%1\" rather than its version 0.1")
                                    .arg(mpDialog->packageList->item(offRow)->data(PackageItemDelegate::cPackageVersionRole).toString())));
        const QColor quietInk = rowDelegate()->versionInk(false);
        const auto [offRead, offApart] = versionPixel(shot, offRow, quietInk);
        qInfo().noquote() << qsl("  the switched-off row's trailing run reads nearest %1 against %2, %3 apart").arg(offRead.name(), quietInk.name(), QString::number(offApart));
        QVERIFY2(
                readsAs(offRead, quietInk),
                qPrintable(qsl("the switched-off package's version reads nearest %1 where the row's version ink is %2 - the number went quiet with the package").arg(offRead.name(), quietInk.name())));

        // ...and in the Explore view a row stands for a package on the
        // repository, so what it says is the version waiting there
        QVERIFY2(writeRepositoryIndex(), "SETUP: the repository index could not be written");
        showView(NavigationView::Explore);
        const int exploreRow = rowOf(qsl("explore-only-one"));
        QVERIFY2(exploreRow >= 0, "SETUP: the Explore view does not list the repository's own packages");
        QVERIFY2(mpDialog->packageList->item(exploreRow)->data(PackageItemDelegate::cPackageVersionRole).toString() == qsl("1.0.0"),
                 qPrintable(qsl("an Explore row carries \"%1\" rather than the version the repository lists, 1.0.0")
                                    .arg(mpDialog->packageList->item(exploreRow)->data(PackageItemDelegate::cPackageVersionRole).toString())));

        // ...and in Updates it is the pair the details column says
        QVERIFY2(writeRepositoryIndex(qsl("9.9.9")), "SETUP: the repository index listing an update could not be written");
        QVERIFY2(mpDialog->readPackageRepositoryFile(), "SETUP: the repository index listing an update could not be read back");
        showView(NavigationView::Updates);
        const int updateRow = rowOf(mFirstPackage);
        QVERIFY2(updateRow >= 0, "SETUP: the package listed at a higher version did not reach the Updates view");
        QVERIFY2(mpDialog->packageList->item(updateRow)->data(PackageItemDelegate::cPackageVersionRole).toString() == qsl("1.2.3 → 9.9.9"),
                 qPrintable(qsl("an Updates row carries \"%1\" rather than the installed number, the arrow and the waiting one")
                                    .arg(mpDialog->packageList->item(updateRow)->data(PackageItemDelegate::cPackageVersionRole).toString())));

        QVERIFY(writeRepositoryIndex());
        QVERIFY(mpDialog->readPackageRepositoryFile());
        showView(NavigationView::Installed);
        mpDialog->packageList->setCurrentRow(0);
        settle();
    }

    // The two columns are parted by the design's grip handle, which the reader
    // drags - and where they left it is where the next window opens
    void test_theTwoColumnsAreSplitByAHandleThatIsRemembered()
    {
        auto* pSplitter = mpDialog->findChild<uiDesign::GripSplitter*>(qsl("packagesSplitter"));
        QVERIFY2(pSplitter, "there is no uiDesign::GripSplitter named 'packagesSplitter' - the two columns are still a row that cannot be resized");
        QCOMPARE(pSplitter->count(), 2);
        QVERIFY2(pSplitter->widget(0) == mpDialog->leftPanel, "the list column is not the splitter's first pane");
        QVERIFY2(pSplitter->widget(1) == mpDialog->rightPanel, "the details column is not the splitter's second pane");
        QVERIFY2(!pSplitter->isCollapsible(0) && !pSplitter->isCollapsible(1), "a column can be dragged away to nothing, leaving the window with one of its two halves");
        QVERIFY2(qobject_cast<uiDesign::GripSplitterHandle*>(pSplitter->handle(1)), "the seam between the columns is a platform-drawn QSplitterHandle rather than the design's grip handle");
        QVERIFY2(!mpDialog->leftPanel->styleSheet().contains(qsl("border-right")), "the list column still draws the hairline seam that the handle is now, so the two are drawn one beside the other");

        // Above the list column's own floor, which the splitter will not let a
        // drag go under
        const int wanted = mpDialog->leftPanel->minimumWidth() + 40;
        const QList<int> before = pSplitter->sizes();
        pSplitter->setSizes({wanted, pSplitter->width() - wanted - uiDesign::GripSplitter::scmHandleThickness});
        settle();
        qInfo().noquote() << qsl("  the seam was moved to %1; the list column measures %2").arg(QString::number(wanted), QString::number(mpDialog->leftPanel->width()));
        QVERIFY2(std::abs(mpDialog->leftPanel->width() - wanted) <= 2,
                 qPrintable(qsl("the seam was set at %1 and the list column measures %2 - the columns are not the splitter's to size")
                                    .arg(QString::number(wanted), QString::number(mpDialog->leftPanel->width()))));
        pSplitter->setSizes(before);
        settle();

        // ...and the same split through a window's whole life: dragged, closed,
        // opened again. Windows of this case's own, so that the fixture's one
        // stays open for the cases after this.
        QSettings& settings = *mudlet::getQSettings();
        settings.remove(qsl("packageManagerSplitterState"));

        QPointer<dlgPackageManager> pFirst = new dlgPackageManager(nullptr, mpHost);
        pFirst->show();
        QVERIFY(QTest::qWaitForWindowExposed(pFirst));
        settle();
        auto* pFirstSplit = pFirst->findChild<uiDesign::GripSplitter*>(qsl("packagesSplitter"));
        QVERIFY(pFirstSplit);
        const int dragged = pFirst->leftPanel->minimumWidth() + 60;
        pFirstSplit->setSizes({dragged, pFirstSplit->width() - dragged - uiDesign::GripSplitter::scmHandleThickness});
        settle();
        const int chosen = pFirst->leftPanel->width();
        pFirst->close();
        settle();
        QVERIFY2(!settings.value(qsl("packageManagerSplitterState")).toByteArray().isEmpty(),
                 "closing the window wrote nothing under 'packageManagerSplitterState', so the split the reader chose is gone with it");

        QPointer<dlgPackageManager> pSecond = new dlgPackageManager(nullptr, mpHost);
        pSecond->show();
        QVERIFY(QTest::qWaitForWindowExposed(pSecond));
        settle();
        const int restored = pSecond->leftPanel->width();
        qInfo().noquote() << qsl("  the seam was left at %1 and the next window opened at %2").arg(QString::number(chosen), QString::number(restored));
        QVERIFY2(std::abs(restored - chosen) <= 4,
                 qPrintable(qsl("the seam was left at %1 and the next window opened at %2 - the split is not put back").arg(QString::number(chosen), QString::number(restored))));
        pSecond->close();
        settle();
    }

    // "Install 1" is a count of something nobody asked to count. One chosen row
    // is the bare word; more than one says how many, as a sentence.
    void test_theActionButtonsSayWhatTheyDo()
    {
        showView(NavigationView::Installed);
        mpDialog->packageList->setCurrentRow(0);
        settle();
        QCOMPARE(mpDialog->packageList->selectedItems().size(), 1);
        QVERIFY2(mpDialog->pushButton_remove->text() == qsl("Remove"),
                 qPrintable(qsl("with one package chosen Remove reads \"%1\" rather than the bare word").arg(mpDialog->pushButton_remove->text())));

        mpDialog->packageList->selectAll();
        settle();
        const int chosen = mpDialog->packageList->selectedItems().size();
        QVERIFY2(chosen >= 2, "SETUP: the Installed view lists fewer than two packages, so there is no plural to read");
        qInfo().noquote() << qsl("  with %1 packages chosen Remove reads \"%2\"").arg(QString::number(chosen), mpDialog->pushButton_remove->text());
        QVERIFY2(mpDialog->pushButton_remove->text() == qsl("Remove %1 packages").arg(chosen),
                 qPrintable(qsl("with %1 packages chosen Remove reads \"%2\" rather than \"Remove %1 packages\"").arg(QString::number(chosen), mpDialog->pushButton_remove->text())));

        showView(NavigationView::Explore);
        mpDialog->packageList->setCurrentRow(0);
        settle();
        QVERIFY2(mpDialog->pushButton_installRepo->text() == qsl("Install"),
                 qPrintable(qsl("with one package chosen Install reads \"%1\" rather than the bare word").arg(mpDialog->pushButton_installRepo->text())));
        mpDialog->packageList->selectAll();
        settle();
        const int many = mpDialog->packageList->selectedItems().size();
        QVERIFY2(many >= 2, "SETUP: the Explore view lists fewer than two packages, so there is no plural to read");
        qInfo().noquote() << qsl("  with %1 packages chosen Install reads \"%2\"").arg(QString::number(many), mpDialog->pushButton_installRepo->text());
        QVERIFY2(mpDialog->pushButton_installRepo->text() == qsl("Install %1 packages").arg(many),
                 qPrintable(qsl("with %1 packages chosen Install reads \"%2\" rather than \"Install %1 packages\"").arg(QString::number(many), mpDialog->pushButton_installRepo->text())));
        QVERIFY2(!mpDialog->pushButton_installRepo->toolTip().isEmpty(), "Install lost the tooltip saying what it fetches from");

        showView(NavigationView::Installed);
        mpDialog->packageList->setCurrentRow(0);
        settle();
    }

    // Nothing else restyles this window: it is a QDialog that no other window
    // owns, so the appearance signal is the whole of what moves it. A pass that
    // stopped running leaves the sheets of the theme that has been left.
    void test_anAppearanceChangeRebuildsBothSheets()
    {
        QMap<QString, QString> sheets;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
            qInfo().noquote() << qsl("  %1: pane %2, page %3").arg(appearance.first, tokens.pane.name(), tokens.page.name());
            QVERIFY2(mpDialog->leftPanel->styleSheet().contains(tokens.pane.name()),
                     qPrintable(qsl("on the %1 appearance the list column's sheet does not name the pane tone %2 that appearance mixes").arg(appearance.first, tokens.pane.name())));
            QVERIFY2(mpDialog->rightPanel->styleSheet().contains(tokens.page.name()),
                     qPrintable(qsl("on the %1 appearance the details column's sheet does not name the page tone %2 that appearance mixes").arg(appearance.first, tokens.page.name())));
            QVERIFY2(!mpDialog->pushButton_installFile->icon().isNull(), qPrintable(qsl("on the %1 appearance Install from a file lost its glyph").arg(appearance.first)));
            sheets.insert(appearance.first, mpDialog->leftPanel->styleSheet());
        }
        QVERIFY2(sheets.value(qsl("dark")) != sheets.value(qsl("light")), "both appearances leave the list column with the same sheet, so the pass is not being re-run at all");
    }
};

#include "PackageManagerShellTest.moc"
MUDLET_GROUPED_TEST_MAIN(PackageManagerShellTest)
