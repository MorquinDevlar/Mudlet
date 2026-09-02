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
 * The About dialog was four tab pages of concatenated HTML with the version and
 * the copyright painted onto the splash picture in a serif face. It is now a
 * shell of real widgets over the same .ui file, and five things about that can
 * go wrong without anything else noticing.
 *
 * The version used to be part of the artwork, so it could not be selected,
 * copied, translated or read by a screen reader, and the picture could not be
 * scaled without the type going soft. It is a label now, and the picture is
 * whatever getSplashScreen() answered and nothing more - which is checked by
 * reading the band of pixels the old text was painted across.
 *
 * The build information is what a bug report needs, and the Copy button is the
 * whole of how it gets there.
 *
 * The third-party page is one row per component with the licence folded away.
 * How many rows there are depends on what this build actually bundles, so the
 * rows are held against the data rather than against a number typed here.
 *
 * The supporter pennants are drawn rather than lettered onto two raster frames.
 * One banner per name in each of the two lists, with the blades on the first
 * list alone.
 *
 * Steam builds may not point at Patreon at all, which used to be one branch in
 * one string and is now a button that is not built.
 *
 * Run with: ctest -R AboutDialogShellTest -V
 */

#include <algorithm>

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QGroupBox>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QToolButton>
#include <QtTest/QtTest>

#include "AboutSupporterBanner.h"
#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "ctelnet.h"
#include "dlgAboutDialog.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class AboutDialogShellTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgAboutDialog* mpDialog = nullptr;
    QString mPort;
    const QString mLocalhost = qsl("localhost");
    const QString mProfileName = qsl("AboutDialogShell-Test-Profile");

    // How far the two lists of supporters run, so that the banners can be
    // counted rather than assumed. Kept here rather than read off the dialog:
    // reading them off what is on screen would pass a page with nothing on it.
    static constexpr int scmSwordsSupporters = 6;
    static constexpr int scmPlaqueSupporters = 2;

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

    // The way mudlet::slot_showAboutDialog() opens it
    dlgAboutDialog* openDialog()
    {
        auto* pDialog = new dlgAboutDialog(mudlet::self());
        pDialog->show();
        static_cast<void>(QTest::qWaitForWindowExposed(pDialog));
        QCoreApplication::processEvents();
        return pDialog;
    }

    static QList<QToolButton*> navButtons(const dlgAboutDialog* pDialog)
    {
        QList<QToolButton*> buttons;
        for (const QString& key : QStringList{qsl("mudlet"), qsl("supporters"), qsl("license"), qsl("thirdparty")}) {
            buttons << pDialog->findChild<QToolButton*>(qsl("aboutNavButton_%1").arg(key));
        }
        return buttons;
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

        mpServer = new TelnetServerStub(qApp);
        mpServer->start(mLocalhost, 0);
        QVERIFY2(mpServer->isListening(), qPrintable(qsl("TelnetServerStub failed to start: %1").arg(mpServer->errorString())));
        mPort = QString::number(mpServer->serverPort());
        mudlet::start();
        mudlet::self()->setupConfig();
        QCOMPARE(mudlet::getMudletPath(enums::mainPath), qsl("%1/mudlet").arg(mConfigDir.path()));
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>(qsl("MudletInstanceCoordinator")));
        mudlet::self()->init();
        // The captions below are read as words rather than as pictures
        mudlet::self()->setInterfaceLanguage(qsl("en_US"));
        mudlet::self()->setStorePasswordsSecurely(false);

        deleteProfileDirectory(mProfileName);
        auto host = TestProfile::create(mProfileName, mLocalhost, mPort);
        QVERIFY2(host, "No active host after profile creation");
        QSignalSpy connected(&(host->mTelnet), &cTelnet::signal_connected);
        QVERIFY2(connected.wait(2000), "Could not connect with the host.");

        mpDialog = openDialog();
        QVERIFY2(mpDialog, "the About dialog could not be opened");
    }

    void cleanupTestCase()
    {
        delete mpDialog;
        mpDialog = nullptr;
        delete mpServer;
        mpServer = nullptr;
        if (mudlet::self()) {
            deleteProfileDirectory(mProfileName);
            delete mudlet::self();
        }
        mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME") : qputenv("XDG_CONFIG_HOME", mSavedXdg);
    }

    // The shell itself: the arch keeps a column of its own, and the four tabs
    // are four pages driven by a row of four buttons
    void test_theShellIsAColumnOfArtAndFourPages()
    {
        QVERIFY2(mpDialog->findChild<QWidget*>(qsl("aboutShell")), "the dialog has no 'aboutShell' - it is still the .ui file's own layout");
        QVERIFY2(mpDialog->findChild<QWidget*>(qsl("aboutArtColumn")), "the artwork has no column of its own");

        auto* pStack = mpDialog->findChild<QStackedWidget*>(qsl("aboutStack"));
        QVERIFY2(pStack, "the dialog has no 'aboutStack' of pages");
        QCOMPARE(pStack->count(), 4);

        const QStringList keys{qsl("mudlet"), qsl("supporters"), qsl("license"), qsl("thirdparty")};
        const QList<QToolButton*> buttons = navButtons(mpDialog);
        for (int index = 0; index < keys.size(); ++index) {
            QToolButton* pButton = buttons.at(index);
            QVERIFY2(pButton, qPrintable(qsl("there is no 'aboutNavButton_%1'").arg(keys.at(index))));
            QVERIFY2(pButton->isCheckable(), qPrintable(qsl("the %1 button does not say whether its page is the one on show").arg(keys.at(index))));
            QVERIFY2(!pButton->icon().isNull(), qPrintable(qsl("the %1 button carries no glyph").arg(keys.at(index))));
        }

        // Every button in turn, so that a stack wired to one page cannot pass
        for (int index = 0; index < keys.size(); ++index) {
            buttons.at(index)->click();
            QCoreApplication::processEvents();
            const QWidget* pShown = pStack->currentWidget();
            const QString wanted = qsl("aboutPage_%1").arg(keys.at(index));
            QVERIFY2(pShown && pShown->objectName() == wanted,
                     qPrintable(qsl("clicking the %1 button showed '%2' rather than '%3'").arg(keys.at(index), pShown ? pShown->objectName() : qsl("nothing"), wanted)));
            QVERIFY2(buttons.at(index)->isChecked(), qPrintable(qsl("the %1 button is not lit while its own page is showing").arg(keys.at(index))));
        }

        // ...and the way in the footer's licence link takes
        buttons.at(0)->click();
        QCoreApplication::processEvents();
        QVERIFY(QMetaObject::invokeMethod(mpDialog, "showPage", Q_ARG(QString, qsl("license"))));
        QCoreApplication::processEvents();
        QCOMPARE(pStack->currentWidget()->objectName(), qsl("aboutPage_license"));
        QVERIFY2(buttons.at(2)->isChecked(), "showPage(\"license\") moved the stack without lighting the License button");

        // The .ui file's own browser is reused rather than replaced, so the GPL
        // and its translator note travel with it
        QVERIFY2(mpDialog->findChild<QGroupBox*>(qsl("aboutLicenseNotice")), "the License page has no notice card over the licence");
        auto* pLicence = mpDialog->findChild<QTextBrowser*>(qsl("textBrowser_license"));
        QVERIFY2(pLicence, "the dialog has no 'textBrowser_license' at all");
        QVERIFY2(pLicence->isVisible(), "the .ui file's licence browser is not on the License page");
        QVERIFY2(pLicence->toPlainText().contains(qsl("GNU GENERAL PUBLIC LICENSE")), "the licence browser does not hold the GPL");
        buttons.at(0)->click();
        QCoreApplication::processEvents();

        // Nothing on the Mudlet page may be wider than the column it is in. A
        // chip row that cannot wrap makes a maker's card as wide as the sum of
        // their contacts, which pushes the card beside it - and the third link
        // column - off the page at both sizes.
        auto* pMudletPage = mpDialog->findChild<QScrollArea*>(qsl("aboutPage_mudlet"));
        QVERIFY2(pMudletPage, "there is no 'aboutPage_mudlet'");
        for (const QSize& size : QList<QSize>{QSize(1080, 680), QSize(900, 560)}) {
            // Resized outright: the constructor clamps its opening size to the
            // screen, which offscreen is smaller than either of these
            mpDialog->resize(size);
            QCoreApplication::processEvents();
            QTest::qWait(50ms);
            QCoreApplication::processEvents();
            const int overflow = pMudletPage->horizontalScrollBar()->maximum();
            qInfo().noquote() << qsl("  at %1x%2 the Mudlet page scrolls %3px sideways").arg(QString::number(mpDialog->width()), QString::number(mpDialog->height()), QString::number(overflow));
            QVERIFY2(overflow == 0,
                     qPrintable(qsl("the Mudlet page scrolls %1px sideways at %2x%3 - something on it is wider than the column it is in")
                                        .arg(QString::number(overflow), QString::number(mpDialog->width()), QString::number(mpDialog->height()))));
        }
    }

    // Nothing is painted onto the arch any more: the version is a label, and the
    // band of the picture the serif version used to be drawn across is the
    // picture's own pixels
    void test_theArtworkCarriesNoPaintedText()
    {
        auto* pArt = mpDialog->findChild<QLabel*>(qsl("mudletTitleLabel"));
        QVERIFY2(pArt, "the dialog has no 'mudletTitleLabel'");
        const QPixmap shown = pArt->pixmap();
        QVERIFY2(!shown.isNull(), "the artwork label carries no picture at all");

        auto* pVersion = mpDialog->findChild<QLabel*>(qsl("aboutVersion"));
        QVERIFY2(pVersion, "the version is not a label - it is still painted onto the picture");
        // APP_VERSION is a compile definition of the application rather than of
        // this test, so the version is held against the one string that carries
        // it: scmVersion is "Mudlet " and then exactly what this label shows
        QVERIFY2(!pVersion->text().isEmpty(), "the version label is empty");
        QCOMPARE(mudlet::self()->scmVersion, qsl("Mudlet %1").arg(pVersion->text()));
        QVERIFY2(pVersion->text().endsWith(mudlet::self()->mAppBuild), qPrintable(qsl("the version label \"%1\" does not end with this build's own suffix").arg(pVersion->text())));

        // The dialog's own copy rather than a second call to getSplashScreen():
        // on 1 April that answers one of twenty-four Easter eggs at random, so
        // asking again would compare two different pictures 23 times in 24
        const QImage splash = mpDialog->splashImage();
        QVERIFY2(!splash.isNull(), "the dialog kept no splash image, so there is no picture to compare against");
        const QSize logical = shown.deviceIndependentSize().toSize();
        const int expectedHeight = qRound(static_cast<qreal>(splash.height()) * logical.width() / splash.width());
        QVERIFY2(qAbs(logical.height() - expectedHeight) <= 1,
                 qPrintable(qsl("the artwork is %1x%2 where the splash scaled to that width is %1x%3 - something else is in the label")
                                    .arg(QString::number(logical.width()), QString::number(logical.height()), QString::number(expectedHeight))));

        // The old code drew the version at y=270 and the copyright at y=340 of a
        // 360-tall picture, both centred. Read that whole lower band against the
        // plainly scaled splash: type in a colour of its own is thousands of
        // pixels, where the rounded corners and the hairline are neither in this
        // band nor this many.
        const QImage drawn = shown.toImage().convertToFormat(QImage::Format_ARGB32);
        const QImage plain = splash.scaledToWidth(drawn.width(), Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
        const int shortest = std::min(drawn.height(), plain.height());
        const int from = shortest * 70 / 100;
        const int to = shortest * 98 / 100;
        const int left = drawn.width() * 8 / 100;
        const int right = drawn.width() * 92 / 100;
        int differing = 0;
        int read = 0;
        for (int y = from; y < to; ++y) {
            for (int x = left; x < right; ++x) {
                ++read;
                const QColor was = plain.pixelColor(x, y);
                const QColor now = drawn.pixelColor(x, y);
                if (qAbs(was.red() - now.red()) > 8 || qAbs(was.green() - now.green()) > 8 || qAbs(was.blue() - now.blue()) > 8) {
                    ++differing;
                }
            }
        }
        qInfo().noquote() << qsl("  the artwork is %1x%2 and %3 of %4 pixels in the band the version used to be painted across differ from the plain splash")
                                     .arg(QString::number(logical.width()), QString::number(logical.height()), QString::number(differing), QString::number(read));
        QVERIFY2(read > 0, "the band the old text was painted across is empty, so nothing was compared");
        QVERIFY2(differing * 100 < read,
                 qPrintable(qsl("%1 of %2 pixels of the artwork's lower band are not the splash's own - something is still painted onto the picture")
                                    .arg(QString::number(differing), QString::number(read))));
    }

    // What a bug report needs, and the one gesture that gets it there
    void test_copyWritesTheBuildInformationToTheClipboard()
    {
        QApplication::clipboard()->clear();
        auto* pButton = mpDialog->findChild<QPushButton*>(qsl("aboutCopyBuildInfo"));
        QVERIFY2(pButton, "the art column has no 'aboutCopyBuildInfo' button");
        const QString resting = pButton->text();
        QVERIFY2(mpDialog->findChild<QPushButton*>(qsl("aboutCopyButton")), "the build information card has no 'aboutCopyButton'");

        pButton->click();
        QCoreApplication::processEvents();

        const QString onTheClipboard = QApplication::clipboard()->text();
        const QStringList lines = onTheClipboard.split(QChar::LineFeed, Qt::SkipEmptyParts);
        const QList<QPair<QString, QString>> rows = dlgAboutDialog::buildInfoRows();
        qInfo().noquote()
                << qsl("  the clipboard holds %1 line(s) against %2 build-information row(s); the first is \"%3\"").arg(QString::number(lines.size()), QString::number(rows.size()), lines.value(0));

        QVERIFY2(!onTheClipboard.isEmpty(), "Copy build information put nothing on the clipboard");
        QCOMPARE(lines.size(), rows.size());
        QCOMPARE(lines.first(), qsl("%1: %2").arg(rows.first().first, rows.first().second));
        QVERIFY2(lines.first().contains(mudlet::self()->scmVersion), qPrintable(qsl("the first line is \"%1\", which does not name the version").arg(lines.first())));
        QVERIFY2(pButton->text() != resting, qPrintable(qsl("the button still reads \"%1\" straight after copying, so nothing says it worked").arg(pButton->text())));

        // ...and it goes back to being a button once the moment has passed
        QTest::qWait(1900ms);
        QCOMPARE(pButton->text(), resting);
    }

    // One row per component this build actually bundles, each with its licence
    // folded away until it is asked for
    void test_everyThirdPartyRowOpensToItsLicence()
    {
        // The page has to be the one on show, or every body below reads as
        // hidden whether its row is open or not
        QToolButton* pNav = mpDialog->findChild<QToolButton*>(qsl("aboutNavButton_thirdparty"));
        QVERIFY2(pNav, "there is no 'aboutNavButton_thirdparty'");
        pNav->click();
        QCoreApplication::processEvents();

        const QList<QToolButton*> toggles = mpDialog->findChildren<QToolButton*>(qsl("aboutThirdPartyToggle"));
        const QList<QLabel*> bodies = mpDialog->findChildren<QLabel*>(qsl("aboutThirdPartyBody"));
        const QVector<aboutThirdParty> components = dlgAboutDialog::thirdPartyComponents();
        qInfo().noquote() << qsl("  the Third party page shows %1 row(s) against %2 component(s) in this build").arg(QString::number(toggles.size()), QString::number(components.size()));

        QVERIFY2(!components.isEmpty(), "this build bundles no third-party components at all, so the page below proves nothing");
        QCOMPARE(toggles.size(), components.size());
        QCOMPARE(bodies.size(), components.size());

        for (QLabel* pBody : bodies) {
            QVERIFY2(!pBody->isVisible(), "a third-party licence is showing before its row has been opened");
            QVERIFY2(!pBody->text().isEmpty(), "a third-party row carries no licence text at all");
        }
        // The text browsers these labels replaced carried
        // Qt::TextBrowserInteraction, so a licence could be selected and copied
        QVERIFY2(bodies.first()->textInteractionFlags().testFlag(Qt::TextSelectableByMouse), "a third-party licence cannot be selected with the mouse - the text browser it replaced could be");

        toggles.first()->setChecked(true);
        QCoreApplication::processEvents();
        QVERIFY2(bodies.first()->isVisible(), "opening the first third-party row did not show its licence");
        int stillHidden = 0;
        for (int index = 1; index < bodies.size(); ++index) {
            stillHidden += bodies.at(index)->isVisible() ? 0 : 1;
        }
        QCOMPARE(stillHidden, bodies.size() - 1);

        toggles.first()->setChecked(false);
        QCoreApplication::processEvents();
        QVERIFY2(!bodies.first()->isVisible(), "closing a third-party row left its licence on show");

        mpDialog->findChild<QToolButton*>(qsl("aboutNavButton_mudlet"))->click();
        QCoreApplication::processEvents();
    }

    // The pennants are widgets now: one per name, blades on the higher tier only
    void test_everySupporterHasABannerOfTheRightTier()
    {
        QToolButton* pNav = mpDialog->findChild<QToolButton*>(qsl("aboutNavButton_supporters"));
        QVERIFY2(pNav, "there is no 'aboutNavButton_supporters'");
        pNav->click();
        QCoreApplication::processEvents();

        const QList<uiDesign::AboutSupporterBanner*> banners = mpDialog->findChildren<uiDesign::AboutSupporterBanner*>();
        int withSwords = 0;
        QStringList names;
        for (const uiDesign::AboutSupporterBanner* pBanner : banners) {
            withSwords += pBanner->swords() ? 1 : 0;
            names << pBanner->name();
            QVERIFY2(!pBanner->name().isEmpty(), "a supporter banner carries no name");
            QCOMPARE(pBanner->accessibleName(), pBanner->name());
            // A centred widget is given its size hint rather than stretched, so
            // a pennant that hints at nothing is drawn as nothing
            QVERIFY2(pBanner->width() > 100 && pBanner->height() > 20,
                     qPrintable(qsl("the banner for \"%1\" is %2x%3").arg(pBanner->name(), QString::number(pBanner->width()), QString::number(pBanner->height()))));
        }
        qInfo().noquote() << qsl("  %1 banner(s), %2 of them with swords: %3").arg(QString::number(banners.size()), QString::number(withSwords), names.join(qsl(", ")));

        QCOMPARE(banners.size(), scmSwordsSupporters + scmPlaqueSupporters);
        QCOMPARE(withSwords, scmSwordsSupporters);

        mpDialog->findChild<QToolButton*>(qsl("aboutNavButton_mudlet"))->click();
        QCoreApplication::processEvents();
    }

    // A Steam build does not point anywhere money can change hands
    void test_steamModeOffersNoPatreon()
    {
        const bool wasSteam = mudlet::smSteamMode;
        mudlet::smSteamMode = true;
        auto* pSteamDialog = openDialog();
        const bool hasButton = pSteamDialog->findChild<QPushButton*>(qsl("aboutPatreonButton")) != nullptr;
        auto* pIntro = pSteamDialog->findChild<QLabel*>(qsl("aboutSupportersIntro"));
        const QString introText = pIntro ? pIntro->text() : QString();
        delete pSteamDialog;
        mudlet::smSteamMode = wasSteam;

        QVERIFY2(pIntro, "the Supporters page has no 'aboutSupportersIntro'");
        QVERIFY2(!hasButton, "a Steam build still offers the Patreon button");
        QVERIFY2(!introText.contains(qsl("<a")), qPrintable(qsl("a Steam build's supporters intro still carries a link: \"%1\"").arg(introText.simplified())));

        // ...and the ordinary build does offer both
        QVERIFY2(mpDialog->findChild<QPushButton*>(qsl("aboutPatreonButton")), "a non-Steam build has no Patreon button, so the check above says nothing");
    }

    // A QLabel bakes the colour of a link into its document the moment its text
    // is set, from the application palette of that moment - so a palette written
    // to the label afterwards leaves every anchor at Qt's own blue, which is
    // 2.4:1 on a dark page. The readability audit cannot see it: it reads inks
    // off palettes, and the palette here says the right thing while the document
    // says the wrong one.
    void test_everyLinkCarriesTheAccentInBothAppearances()
    {
        auto* pFooter = mpDialog->findChild<QLabel*>(qsl("aboutFooter"));
        QVERIFY2(pFooter, "the art column has no 'aboutFooter'");
        QVERIFY2(pFooter->text().contains(qsl("<a ")), "the footer carries no link, so nothing below is measured");

        mudlet::self()->setAppearance(enums::Appearance::dark);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        const QString onDark = uiDesign::themeTokens().accentText.name();
        const QString footerOnDark = pFooter->text();

        mudlet::self()->setAppearance(enums::Appearance::light);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        const QString onLight = uiDesign::themeTokens().accentText.name();
        const QString footerOnLight = pFooter->text();
        const QString licenceHtml = mpDialog->findChild<QTextBrowser*>(qsl("textBrowser_license"))->document()->toHtml();

        mudlet::self()->setAppearance(enums::Appearance::systemSetting);
        QCoreApplication::processEvents();

        qInfo().noquote() << qsl("  the footer reads \"%1\" on dark and \"%2\" on light").arg(footerOnDark, footerOnLight);
        QVERIFY2(onDark != onLight, "the two appearances answer the same accent ink, so the check below cannot tell them apart");
        QVERIFY2(footerOnDark.contains(qsl("color: %1").arg(onDark)), qPrintable(qsl("the footer's link is not inked %1 on the dark appearance: \"%2\"").arg(onDark, footerOnDark)));
        QVERIFY2(footerOnLight.contains(qsl("color: %1").arg(onLight)),
                 qPrintable(qsl("the footer's link is not inked %1 on the light appearance - an appearance change did not re-ink it: \"%2\"").arg(onLight, footerOnLight)));
        // ...and the licence browser, whose head declares a rule for anchors
        // that a QTextDocument does not ink them with
        QVERIFY2(licenceHtml.contains(onLight, Qt::CaseInsensitive), qPrintable(qsl("the licence text's links do not carry the accent %1").arg(onLight)));
    }
};

#include "AboutDialogShellTest.moc"
MUDLET_GROUPED_TEST_MAIN(AboutDialogShellTest)
