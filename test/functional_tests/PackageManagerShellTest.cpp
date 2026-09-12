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
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <memory>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
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

        mpDialog = new dlgPackageManager(nullptr, mpHost);
        QVERIFY2(mpDialog, "the package manager was not built at all");
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
