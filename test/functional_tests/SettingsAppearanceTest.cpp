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
 * Appearance is the one setting that repaints the dialog it is changed from.
 * The shell draws its own surfaces - the pages, the sidebar, the cards - from
 * a stylesheet built out of a palette, so a theme change that does not reach
 * that stylesheet leaves the whole of the dialog in the previous theme while
 * the text on it turns over to the new one.
 *
 * The same stylesheet draws every field a setting is typed into, from the
 * recipe the editor window is drawn from - and it is scoped to the pages, so
 * the cases below also hold it off the search field, the sidebar and the
 * indicators the card rules own.
 *
 * Run with: ctest -R SettingsAppearanceTest -V
 */

#include <algorithm>
#include <cmath>
#include <memory>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QGroupBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPixmap>
#include <QCheckBox>
#include <QRadioButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "TelnetServerStub.h"
#include "dlgProfilePreferences.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

class SettingsAppearanceTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    QTemporaryDir mCacheDir;
    QByteArray mSavedXdgCache;
    TelnetServerStub* mpServer = nullptr;
    Host* mpHost = nullptr;
    dlgProfilePreferences* mpPreferences = nullptr;
    const QString mProfileName = qsl("SettingsAppearance-Test");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

    void deleteProfileDirectory(const QString& profileName)
    {
        QDir dir(mudlet::getMudletPath(enums::profileHomePath, profileName));
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }

    void writeFreshEditorThemesFile()
    {
        const QString file = mudlet::getMudletPath(enums::editorWidgetThemeJsonFile);
        QVERIFY(QDir().mkpath(QFileInfo(file).absolutePath()));
        QFile themes(file);
        QVERIFY(themes.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(themes.write("[]") == 2);
    }

    static QWidget* shellOf(const dlgProfilePreferences* pDialog) { return pDialog->findChild<QWidget*>(qsl("settingsShell")); }

    QWidget* shell() const { return shellOf(mpPreferences); }

    // mudlet::showOptionsDialog() assigns the profile's Lua stylesheet to the
    // dialog on every show, so a dialog built by hand here is not the one the
    // application puts on screen until it has one too.
    void openPreferences()
    {
        mpPreferences = new dlgProfilePreferences(mudlet::self(), mpHost);
        mpPreferences->setStyleSheet(mpHost->mProfileStyleSheet);
        mpPreferences->resize(1060, 760);
        mpPreferences->show();
        QVERIFY(QTest::qWaitForWindowExposed(mpPreferences));
        QVERIFY2(shell(), "the settings shell was never built");
    }

    void setAppearance(const enums::Appearance state)
    {
        mpPreferences->comboBox_appearance->setCurrentIndex(state);
        QCoreApplication::processEvents();
        // A dialog on screen paints itself between one change and the next, and
        // painting is what settles a widget's palette against the application's.
        // Under the offscreen platform nothing paints unless it is asked to, so
        // without this a case would measure a dialog no user could be looking at.
        mpPreferences->grab();
    }

    static QColor pixelOf(QWidget* pWidget, const QPoint& point)
    {
        const QPixmap shot = pWidget->grab();
        return shot.toImage().pixelColor(point);
    }

    // The sidebar keeps a 16px margin under its last item, so the bottom left
    // of the shell is one of its own surfaces rather than anything on a page -
    // and the shell and the sidebar are painted the same colour, so this reads
    // the page colour whichever of the two the pixel lands on.
    static QColor paintedSurfaceOf(const dlgProfilePreferences* pDialog)
    {
        QWidget* pShell = shellOf(pDialog);
        return pixelOf(pShell, QPoint(3, pShell->height() - 3));
    }

    QColor paintedSurface() const { return paintedSurfaceOf(mpPreferences); }

    // Which side of the light/dark line the application has moved to. The shell
    // has to be on the same one, whatever it was painted in a moment ago.
    static bool applicationIsLight() { return QApplication::palette().color(QPalette::Base).lightness() >= 128; }

    static qreal relativeLuminance(const QColor& colour)
    {
        const auto channel = [](const qreal value) {
            return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel(colour.redF()) + 0.7152 * channel(colour.greenF()) + 0.0722 * channel(colour.blueF());
    }

    static qreal contrastRatio(const QColor& one, const QColor& other)
    {
        const qreal first = relativeLuminance(one);
        const qreal second = relativeLuminance(other);
        return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
    }

    // Which of two colours a painted pixel is: both surfaces are flat fills, so
    // the nearer one is the one that was painted
    static int distanceBetween(const QColor& one, const QColor& other) { return std::abs(one.red() - other.red()) + std::abs(one.green() - other.green()) + std::abs(one.blue() - other.blue()); }


    // The selector half of every rule in a stylesheet, one selector per entry -
    // "a, b { ... }" counts as two
    static QStringList selectorsIn(const QString& styleSheet)
    {
        QStringList selectors;
        const QStringList rules = styleSheet.split(QLatin1Char('}'), Qt::SkipEmptyParts);
        for (const QString& rule : rules) {
            const QString selectorList = rule.section(QLatin1Char('{'), 0, 0);
            const QStringList parts = selectorList.split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString& part : parts) {
                if (const QString selector = part.simplified(); !selector.isEmpty()) {
                    selectors.append(selector);
                }
            }
        }
        return selectors;
    }

    // What one property is set to in a small hand-built sheet - no selectors,
    // no nesting, so the declarations are what lies between the semicolons
    static QString declarationValue(const QString& styleSheet, const QString& property)
    {
        for (const QString& declaration : styleSheet.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            if (declaration.section(QLatin1Char(':'), 0, 0).trimmed() == property) {
                return declaration.section(QLatin1Char(':'), 1).trimmed();
            }
        }
        return QString();
    }

    // An "rgba(r, g, b, a)" wash as it comes out over what it is drawn on,
    // which is the colour a word on it is actually read against
    static QColor washOver(const QColor& surface, const QString& value)
    {
        static const QRegularExpression channels(qsl("^rgba?\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*(?:,\\s*([0-9.]+)\\s*)?\\)$"));
        const QRegularExpressionMatch match = channels.match(value);
        if (!match.hasMatch()) {
            return QColor();
        }
        const qreal alpha = match.captured(4).isEmpty() ? 1.0 : match.captured(4).toDouble();
        const auto mix = [alpha](const int over, const int under) {
            return qRound(under + (over - under) * alpha);
        };
        return QColor(mix(match.captured(1).toInt(), surface.red()), mix(match.captured(2).toInt(), surface.green()), mix(match.captured(3).toInt(), surface.blue()));
    }

    static QString describe(const QColor& surface)
    {
        return qsl("the shell is painted %1 (lightness %2) while the application palette is %3 (Base %4)")
                .arg(surface.name(), QString::number(surface.lightness()), applicationIsLight() ? qsl("light") : qsl("dark"), QApplication::palette().color(QPalette::Base).name());
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
        QVERIFY(mCacheDir.isValid());
        mSavedXdgCache = qgetenv("XDG_CACHE_HOME");
        qputenv("XDG_CACHE_HOME", mCacheDir.path().toUtf8());

        mpServer = new TelnetServerStub(qApp);
        mpServer->start(mLocalhost, 0);
        mPort = QString::number(mpServer->serverPort());
        mudlet::start();
        mudlet::self()->setupConfig();
        QCOMPARE(mudlet::getMudletPath(enums::mainPath), qsl("%1/mudlet").arg(mConfigDir.path()));
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>(qsl("MudletInstanceCoordinator")));
        mudlet::self()->init();
        mudlet::self()->setStorePasswordsSecurely(false);
        deleteProfileDirectory(mProfileName);
        writeFreshEditorThemesFile();

        mpHost = TestProfile::create(mProfileName, mLocalhost, mPort);
        QVERIFY2(mpHost, "No active host after profile creation");
    }

    void cleanupTestCase()
    {
        mpHost = nullptr;
        delete mpServer;
        mpServer = nullptr;
        if (mudlet::self()) {
            deleteProfileDirectory(mProfileName);
            delete mudlet::self();
        }
        mSavedXdg.isNull() ? qunsetenv("XDG_CONFIG_HOME") : qputenv("XDG_CONFIG_HOME", mSavedXdg);
        mSavedXdgCache.isNull() ? qunsetenv("XDG_CACHE_HOME") : qputenv("XDG_CACHE_HOME", mSavedXdgCache);
    }

    void init() { openPreferences(); }

    void cleanup()
    {
        delete mpPreferences;
        mpPreferences = nullptr;
        mudlet::self()->setAppearance(enums::Appearance::systemSetting);
    }

    // The dialog opens in one theme and is asked for the other while it is
    // still on screen. Nothing here reads a colour this test chose: the shell
    // is only required to end up on the same side of the light/dark line as
    // the application palette it is supposed to be drawn from.
    void test_theShellFollowsAThemeChangeMadeWhileItIsOpen()
    {
        setAppearance(enums::Appearance::dark);
        QVERIFY2(!applicationIsLight(), "the application did not go dark, so the flip below is not the one this case is about");
        QVERIFY2(paintedSurface().lightness() < 128, qPrintable(describe(paintedSurface())));

        setAppearance(enums::Appearance::light);
        QVERIFY2(applicationIsLight(), "the application did not go light, so the flip below is not the one this case is about");
        QVERIFY2(paintedSurface().lightness() >= 128, qPrintable(describe(paintedSurface())));
    }

    // ...and the same the other way round, since a fix that reads the theme
    // once could be right in one direction and wrong in the other
    void test_theShellFollowsAThemeChangeBackToDark()
    {
        setAppearance(enums::Appearance::light);
        QVERIFY2(applicationIsLight(), "the application did not go light, so the flip below is not the one this case is about");
        QVERIFY2(paintedSurface().lightness() >= 128, qPrintable(describe(paintedSurface())));

        setAppearance(enums::Appearance::dark);
        QVERIFY2(!applicationIsLight(), "the application did not go dark, so the flip below is not the one this case is about");
        QVERIFY2(paintedSurface().lightness() < 128, qPrintable(describe(paintedSurface())));
    }

    // ...and a dialog told of the change by something other than its own combo
    // box. A second profile's settings dialog hears it through
    // mudlet::signal_appearanceChanged, which is emitted after the mode has
    // already turned over - so a dialog that decides by reading that mode
    // before and after its own call finds it unmoved and keeps the previous
    // theme's shell under the new theme's text. The count either side says the
    // dialog the change was made in still restyles once and not twice: the slot
    // is re-entered through that same signal, and the second pass would be
    // invisible from the outside.
    void test_aSecondDialogFollowsAThemeChangeMadeInTheFirst()
    {
        // Away from the appearance the case moves to first, so that the move is
        // a real one whichever appearance the machine running it is in
        setAppearance(enums::Appearance::light);

        // Alongside the one init() opened, and given what
        // mudlet::showOptionsDialog() gives a dialog it puts on screen
        auto pSecond = std::make_unique<dlgProfilePreferences>(mudlet::self(), mpHost);
        pSecond->setStyleSheet(mpHost->mProfileStyleSheet);
        pSecond->resize(1060, 760);
        pSecond->show();
        QVERIFY(QTest::qWaitForWindowExposed(pSecond.get()));
        QVERIFY2(shellOf(pSecond.get()), "the second dialog's settings shell was never built");
        pSecond->grab();

        const QString lightPage = uiDesign::themeTokens().page.name();
        int firstStyled = mpPreferences->mShellStyleApplications;
        int secondStyled = pSecond->mShellStyleApplications;

        // Through the *first* dialog's control: the second one has nothing but
        // the application's signal to go on
        setAppearance(enums::Appearance::dark);
        pSecond->grab();
        const QString darkPage = uiDesign::themeTokens().page.name();
        QVERIFY2(!applicationIsLight(), "the application did not go dark, so the flip below is not the one this case is about");
        QVERIFY2(darkPage != lightPage, "the two appearances lay the page in the same colour, so the sheets below cannot be told apart");
        QVERIFY2(paintedSurfaceOf(pSecond.get()).lightness() < 128,
                 qPrintable(qsl("the second dialog is painted %1 (lightness %2) while the application went dark")
                                    .arg(paintedSurfaceOf(pSecond.get()).name(), QString::number(paintedSurfaceOf(pSecond.get()).lightness()))));
        QVERIFY2(shellOf(pSecond.get())->styleSheet().contains(darkPage),
                 qPrintable(qsl("the second dialog's shell is still mixed from the light page's %1 rather than the dark page's %2").arg(lightPage, darkPage)));
        QVERIFY2(pSecond->mShellStyleApplications - secondStyled == 1,
                 qPrintable(qsl("the second dialog restyled %1 times for one appearance change").arg(pSecond->mShellStyleApplications - secondStyled)));
        QVERIFY2(mpPreferences->mShellStyleApplications - firstStyled == 1,
                 qPrintable(qsl("the dialog the change was made in restyled %1 times for it").arg(mpPreferences->mShellStyleApplications - firstStyled)));

        firstStyled = mpPreferences->mShellStyleApplications;
        secondStyled = pSecond->mShellStyleApplications;

        // ...and back, since a fix that records the theme once could be right
        // in one direction and wrong in the other
        setAppearance(enums::Appearance::light);
        pSecond->grab();
        QVERIFY2(applicationIsLight(), "the application did not go light, so the flip below is not the one this case is about");
        QVERIFY2(paintedSurfaceOf(pSecond.get()).lightness() >= 128,
                 qPrintable(qsl("the second dialog is painted %1 (lightness %2) while the application went light")
                                    .arg(paintedSurfaceOf(pSecond.get()).name(), QString::number(paintedSurfaceOf(pSecond.get()).lightness()))));
        QVERIFY2(shellOf(pSecond.get())->styleSheet().contains(lightPage),
                 qPrintable(qsl("the second dialog's shell is still mixed from the dark page's %1 rather than the light page's %2").arg(darkPage, lightPage)));
        QVERIFY2(pSecond->mShellStyleApplications - secondStyled == 1,
                 qPrintable(qsl("the second dialog restyled %1 times for one appearance change").arg(pSecond->mShellStyleApplications - secondStyled)));
        QVERIFY2(mpPreferences->mShellStyleApplications - firstStyled == 1,
                 qPrintable(qsl("the dialog the change was made in restyled %1 times for it").arg(mpPreferences->mShellStyleApplications - firstStyled)));
    }

    // A QLabel bakes the colour of an anchor into its document the moment its
    // text is set, from the application palette of that moment - so the loop
    // that used to write QPalette::Link to these labels afterwards did nothing
    // at all, and every link on this dialog was painted in the palette's own
    // blue. Nothing else can see that: the readability audit reads inks off
    // palettes, and the palette said the right thing while the document said
    // the wrong one.
    void test_everyLinkCarriesTheAccentInBothAppearances()
    {
        auto* pLink = mpPreferences->findChild<QLabel*>(qsl("settingsHeroLink"));
        QVERIFY2(pLink, "the security status card has no 'settingsHeroLink'");
        QVERIFY2(pLink->text().contains(qsl("<a ")), "the hero link carries no anchor, so nothing below is measured");

        setAppearance(enums::Appearance::dark);
        const QString onDark = uiDesign::themeTokens().accentText.name();
        const QString linkOnDark = pLink->text();

        setAppearance(enums::Appearance::light);
        const QString onLight = uiDesign::themeTokens().accentText.name();
        const QString linkOnLight = pLink->text();

        qInfo().noquote() << qsl("  the hero link reads \"%1\" on dark and \"%2\" on light").arg(linkOnDark, linkOnLight);
        QVERIFY2(onDark != onLight, "the two appearances answer the same accent ink, so the check below cannot tell them apart");
        QVERIFY2(linkOnDark.contains(qsl("color: %1").arg(onDark)), qPrintable(qsl("the hero link is not inked %1 on the dark appearance: \"%2\"").arg(onDark, linkOnDark)));
        QVERIFY2(linkOnLight.contains(qsl("color: %1").arg(onLight)),
                 qPrintable(qsl("the hero link is not inked %1 on the light appearance - an appearance change did not re-ink it: \"%2\"").arg(onLight, linkOnLight)));
    }

    // A card is filled by the shell stylesheet and the text on it is not, so when
    // the two stop agreeing about the theme the result is a dark card under dark
    // text - 1.2:1 before this was fixed, against the 4.5:1 text should keep
    void test_aCardsTextStaysReadableAfterAThemeChange()
    {
        setAppearance(enums::Appearance::dark);
        setAppearance(enums::Appearance::light);

        auto* pCard = mpPreferences->findChild<QGroupBox*>(qsl("card_theme"));
        QVERIFY2(pCard, "the Appearance card this case reads its colours off is not there any more");
        auto* pLabel = mpPreferences->label_appearance;
        const QColor fill = pixelOf(pCard, QPoint(pCard->width() / 2, pCard->height() - 4));
        const QColor ink = pLabel->palette().color(pLabel->foregroundRole());
        const qreal ratio = contrastRatio(fill, ink);
        QVERIFY2(ratio >= 4.5, qPrintable(qsl("a card is painted %1 under %2 text, a contrast of %3:1").arg(fill.name(), ink.name(), QString::number(ratio, 'f', 2))));
    }

    // A certificate the connection complained about is called out by washing
    // the control it is about in the warning hue. The words on that wash were
    // written out - "red" on a fixed pale yellow, which is 3.9:1 and under the
    // floor - and neither the wash nor the words moved with the theme, since
    // they were chosen off inDarkMode() rather than off the page.
    void test_theCertificateWarningReadsOnItsWashInBothAppearances()
    {
        // What the SSL error path does: it gives each control a sheet of its
        // own, and restyleCertificateWarnings() then rewrites only the controls
        // already carrying one
        const QList<QWidget*> warned{mpPreferences->checkBox_self_signed, mpPreferences->ssl_issuer_label};
        for (QWidget* pControl : warned) {
            pControl->setStyleSheet(qsl("font-weight: bold;"));
        }

        // Both appearances, and each measured after a real move rather than
        // after asking for the one the machine was already in
        setAppearance(enums::Appearance::dark);
        setAppearance(enums::Appearance::light);
        QStringList washes;
        for (const enums::Appearance appearance : {enums::Appearance::light, enums::Appearance::dark}) {
            setAppearance(appearance);
            // The controls stand on the SSL cards, so what the wash lies over
            // is the card tone
            const QColor card = uiDesign::themeTokens().card;
            for (QWidget* pControl : warned) {
                const QString sheet = pControl->styleSheet();
                const QColor ink(declarationValue(sheet, qsl("color")));
                const QColor wash = washOver(card, declarationValue(sheet, qsl("background")));
                QVERIFY2(ink.isValid() && wash.isValid(),
                         qPrintable(qsl("%1 was not restyled for the %2 appearance - its sheet reads \"%3\"")
                                            .arg(pControl->objectName(), appearance == enums::Appearance::dark ? qsl("dark") : qsl("light"), sheet)));
                washes.append(wash.name());
                const qreal ratio = contrastRatio(ink, wash);
                QVERIFY2(ratio >= uiDesign::scmTextMinimumRatio,
                         qPrintable(qsl("%1 is written %2 on a warning wash that comes out %3, a contrast of %4:1 against the %5 floor")
                                            .arg(pControl->objectName(), ink.name(), wash.name(), QString::number(ratio, 'f', 2), QString::number(uiDesign::scmTextMinimumRatio, 'f', 1))));
            }
        }
        qInfo().noquote() << qsl("  the warning wash comes out %1").arg(washes.join(qsl(", ")));
        QVERIFY2(washes.at(0) != washes.at(2), "the wash is the same colour in both appearances, so it is not being mixed against the page");
    }

    // The shell's surfaces are its own, and a profile's Lua stylesheet is
    // applied to the whole dialog - so the one must not be able to repaint the
    // other, before a theme change or after one.
    void test_aProfileStyleSheetDoesNotTakeOverTheShellsSurfaces()
    {
        setAppearance(enums::Appearance::dark);
        mpPreferences->setStyleSheet(qsl("QWidget { background-color: rgb(255, 0, 0); }"));
        QCoreApplication::processEvents();
        const QColor beforeTheFlip = paintedSurface();
        QVERIFY2(!(beforeTheFlip.red() > 200 && beforeTheFlip.green() < 60), qPrintable(qsl("the profile stylesheet painted the shell %1 before any theme change").arg(beforeTheFlip.name())));

        setAppearance(enums::Appearance::light);
        QVERIFY2(applicationIsLight(), "the application did not go light, so this is not the mid-life flip");
        const QColor surface = paintedSurface();
        QVERIFY2(!(surface.red() > 200 && surface.green() < 60), qPrintable(qsl("the profile stylesheet painted the shell %1 after the theme change").arg(surface.name())));
        QVERIFY2(surface.lightness() >= 128, qPrintable(describe(surface)));
    }

    // A setting is typed into the same control the editor window is filled in
    // through: the field surface, sunk into the card, at the height the shared
    // recipe gives every field. Left to the platform it is a flat box drawn a
    // third shorter, which is what this dialog looked like beside the editor.
    void test_aPagesFieldsAreDrawnFromTheSharedInputRecipe()
    {
        setAppearance(enums::Appearance::dark);
        auto* pField = mpPreferences->lineEdit_logFileFolder;
        QVERIFY2(pField, "the log folder field this case reads is not there any more");
        QVERIFY2(pField->height() >= uiDesign::scmInputHeight,
                 qPrintable(qsl("a field on a page is %1px tall, against the %2px the shared recipe asks for").arg(QString::number(pField->height()), QString::number(uiDesign::scmInputHeight))));

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        // Inside the border and inside the padding before the text, where
        // nothing but the control's own surface is drawn: the words in this
        // field are a path under a temporary directory, so a point under them
        // lands on a descender or not from one run to the next
        const QColor fill = pixelOf(pField, QPoint(uiDesign::scmInputBorderWidth + 2, pField->height() / 2));
        QVERIFY2(distanceBetween(fill, tokens.field) < distanceBetween(fill, tokens.card),
                 qPrintable(qsl("a field is painted %1, nearer the card's %2 than the field surface's %3").arg(fill.name(), tokens.card.name(), tokens.field.name())));
    }

    // The page title stands over the page and the wordmark over the sidebar
    // because the sheet says so, not because a rule reads as though it does.
    // Both used to name a percentage - 145% and 125% - and Qt's stylesheet
    // parser reads pt and px for font-size and nothing else, so both were
    // dropped and every word in the dialog was the same size. Read off the
    // resolved font, which is the only thing that says a rule applied.
    void test_theShellsHeadingsAreSetAtTheStepsTheScaleNames()
    {
        auto* pTitle = mpPreferences->findChild<QLabel*>(qsl("settingsPageTitle"));
        QVERIFY2(pTitle, "the dialog has no 'settingsPageTitle'");
        QCOMPARE(pTitle->font().pointSize(), uiDesign::typeSize(uiDesign::TypeStep::Display));

        auto* pWordmark = mpPreferences->findChild<QLabel*>(qsl("settingsWordmark"));
        QVERIFY2(pWordmark, "the dialog has no 'settingsWordmark'");
        QCOMPARE(pWordmark->font().pointSize(), uiDesign::typeSize(uiDesign::TypeStep::Title));

        // A word on a card carries no size of its own, so it is the body step -
        // which is what the two above have to be larger than for either of them
        // to be a heading at all
        auto* pCard = mpPreferences->findChild<QGroupBox*>(qsl("card_theme"));
        QVERIFY2(pCard, "the Appearance card this case measures the body size against is not there any more");
        QCOMPARE(pCard->font().pointSize(), uiDesign::typeSize(uiDesign::TypeStep::Body));
        QVERIFY2(pTitle->font().pointSize() > pWordmark->font().pointSize() && pWordmark->font().pointSize() > pCard->font().pointSize(),
                 qPrintable(qsl("the page title, the wordmark and a card's words are %1/%2/%3pt - the headings are not standing over the page")
                                    .arg(QString::number(pTitle->font().pointSize()), QString::number(pWordmark->font().pointSize()), QString::number(pCard->font().pointSize()))));
    }

    // ...and the card under it is untouched by the rules that draw the fields
    // on it
    void test_aCardIsStillPaintedAsACard()
    {
        setAppearance(enums::Appearance::dark);
        auto* pCard = mpPreferences->findChild<QGroupBox*>(qsl("card_theme"));
        QVERIFY2(pCard, "the Appearance card this case reads its colours off is not there any more");
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QColor fill = pixelOf(pCard, QPoint(pCard->width() / 2, pCard->height() - 4));
        // The colour itself rather than the nearer of two: the card sheet fills
        // a card with the card tone outright, and a nearest-of-two reading would
        // pass a card left in whatever the platform paints a group box
        QVERIFY2(fill.rgb() == tokens.card.rgb(), qPrintable(qsl("a card is painted %1 rather than the card tone's %2").arg(fill.name(), tokens.card.name())));
    }

    // The fields are claimed under the stack of pages and nowhere else. Named
    // on the shell instead, the same rules would take the search box over the
    // pages and the editors the sidebar's list opens.
    void test_theInputRulesReachNothingOutsideThePages()
    {
        auto* pStack = mpPreferences->findChild<QStackedWidget*>(qsl("settingsStack"));
        auto* pSearchField = mpPreferences->findChild<QLineEdit*>(qsl("settingsSearchField"));
        QVERIFY2(pStack && pSearchField, "the stack and the search field are the two things this case is about");
        QVERIFY2(!pStack->isAncestorOf(pSearchField), "the search field is inside the stack, so the rules scoped to the pages now draw it too");

        const QStringList inputTypes{qsl("QLineEdit"), qsl("QPlainTextEdit"), qsl("QTextEdit"), qsl("QComboBox"), qsl("QAbstractSpinBox")};
        int scopedRules = 0;
        for (const QString& selector : selectorsIn(shell()->styleSheet())) {
            const bool namesAField = std::any_of(inputTypes.cbegin(), inputTypes.cend(), [&selector](const QString& type) {
                return selector.contains(type);
            });
            if (!namesAField) {
                continue;
            }
            QVERIFY2(selector.startsWith(qsl("#settingsStack ")), qPrintable(qsl("\"%1\" draws a field from outside the stack of pages").arg(selector)));
            ++scopedRules;
        }
        QVERIFY2(scopedRules > 0, "the shell stylesheet draws no fields at all, so nothing here was checked");

        // The search field keeps a rule of its own, at the corner a control
        // that heads a panel is drawn with
        QVERIFY2(shell()->styleSheet().contains(qsl("#settingsSearchField { border: 1px solid")), "the search field lost the rule that draws it");
        QVERIFY2(shell()->styleSheet().contains(qsl("border-radius: %1px; padding-left").arg(QString::number(uiDesign::scmRadiusProminentInput))),
                 "the search field is no longer drawn with the prominent input's corner");
    }

    // The shared recipe draws fields; everything else on a card is drawn by the
    // card's own rules, the check indicators among them. Read off what the rules
    // paint rather than off the text of the rules: a selector list says nothing
    // about which controls a sheet actually reaches, and a rule that quietly
    // grew a subcontrol nobody named would pass a reading of the words.
    void test_theInputRulesNameNothingButFields()
    {
        QWidget stack;
        stack.setObjectName(qsl("settingsStack"));
        auto* pLayout = new QVBoxLayout(&stack);
        auto* pCheck = new QCheckBox(qsl("a choice"), &stack);
        auto* pRadio = new QRadioButton(qsl("one of several"), &stack);
        auto* pBar = new QScrollBar(Qt::Vertical, &stack);
        auto* pField = new QLineEdit(qsl("typed in"), &stack);
        pLayout->addWidget(pCheck);
        pLayout->addWidget(pRadio);
        pLayout->addWidget(pBar);
        pLayout->addWidget(pField);
        // Held at a size the rules cannot move: the field's rule carries a
        // min-height, and a control that grew by a pixel would come back as a
        // picture of a different size whether or not anything repainted it
        pCheck->setFixedSize(180, 24);
        pRadio->setFixedSize(180, 24);
        pBar->setFixedSize(16, 90);
        pField->setFixedSize(180, 30);
        stack.resize(260, 220);
        stack.show();
        QVERIFY2(QTest::qWaitForWindowExposed(&stack), "the throwaway stack was never put on screen, so there is nothing here to grab");

        const QImage bareCheck = pCheck->grab().toImage();
        const QImage bareRadio = pRadio->grab().toImage();
        const QImage bareBar = pBar->grab().toImage();
        const QImage bareField = pField->grab().toImage();

        stack.setStyleSheet(uiDesign::inputStyleSheet(uiDesign::themeTokens(), qsl("#settingsStack")));
        QCoreApplication::processEvents();
        QTest::qWait(50);

        QVERIFY2(pField->grab().toImage() != bareField, "the input rules left the field exactly as the platform drew it, so this case is reading a sheet that reaches nothing");
        QVERIFY2(pCheck->grab().toImage() == bareCheck, "the input rules redrew a check box, which is not a field and is drawn by the card's own rules");
        QVERIFY2(pRadio->grab().toImage() == bareRadio, "the input rules redrew a radio button, which is not a field and is drawn by the card's own rules");
        QVERIFY2(pBar->grab().toImage() == bareBar, "the input rules redrew a scroll bar, which is chrome and is drawn by the shared scroll bar recipe");
    }

    // The list a combo box drops down is a window of its own, parented to the
    // box - so the rule that draws it as the field opened up has to be found
    // across that boundary, from a sheet scoped to the stack several widgets up
    void test_aComboBoxPopupIsTheFieldOpenedUp()
    {
        setAppearance(enums::Appearance::dark);
        auto* pCombo = mpPreferences->comboBox_appearance;
        // Shown rather than only asked for: a list is polished against the
        // stylesheets over it when it is dropped down, and an unshown one still
        // holds the palette it was made with
        pCombo->showPopup();
        QCoreApplication::processEvents();
        QAbstractItemView* pList = pCombo->view();
        QVERIFY2(pList, "the appearance combo box has no list to drop down");

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        // The colour itself, not the nearest of two: left unstyled the list is
        // painted the platform's base colour, a few levels off the field, and
        // the nearest-of-two reading would let a rule that never reached it
        // pass for one that did
        const QColor fill = pixelOf(pList->viewport(), QPoint(pList->viewport()->width() / 2, 4));
        pCombo->hidePopup();
        QVERIFY2(fill.rgb() == tokens.field.rgb(),
                 qPrintable(qsl("the popup list is painted %1 rather than the field surface's %2 - the rule scoped to the stack did not reach it").arg(fill.name(), tokens.field.name())));
    }

    // The one menu this dialog owns - the profiles a map can be copied to -
    // hangs off the dialog rather than off the shell, so the shell's own sheet
    // never reaches it and it carries the design's menu itself
    void test_theMapProfilesMenuIsDrawnFromTheDesign()
    {
        setAppearance(enums::Appearance::dark);
        QMenu* pMenu = mpPreferences->pushButton_chooseProfiles->menu();
        QVERIFY2(pMenu, "the choose-profiles button carries no menu, so there is nothing here to draw");

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QString sheet = pMenu->styleSheet();
        QVERIFY2(sheet.contains(qsl("QMenu")) && sheet.contains(tokens.card.name()),
                 qPrintable(qsl("the map profiles menu is not drawn on the design's card surface (%1): its sheet is \"%2\"").arg(tokens.card.name(), sheet.left(120))));
        QVERIFY2(pMenu->testAttribute(Qt::WA_TranslucentBackground), "the map profiles menu's window was never asked to be see-through, so its corner is opaque");
    }

    // ...and it is opened up round the same corner. The list lives in a window
    // of its own, which is filled before anything in it is drawn, so a radius on
    // the list alone leaves that window's square corners showing through in the
    // fill. Read off a grab that keeps its alpha: nothing at all where the
    // corner is cut away, the frame where the frame belongs, and the field
    // between the two.
    void test_aComboBoxPopupIsOpenAtTheCorner()
    {
        setAppearance(enums::Appearance::dark);
        auto* pCombo = mpPreferences->comboBox_appearance;
        pCombo->showPopup();
        QCoreApplication::processEvents();
        QAbstractItemView* pList = pCombo->view();
        QVERIFY2(pList, "the appearance combo box has no list to drop down");
        QWidget* pPopup = pList->window();
        QVERIFY2(pPopup && pPopup != mpPreferences, "the list dropped down inside the dialog rather than in a window of its own");
        QVERIFY2(pPopup->testAttribute(Qt::WA_TranslucentBackground), "the popup's window was never asked to be see-through, so it is opaque behind whatever corner the list is given");

        const QImage shot = pPopup->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        const qreal ratio = shot.devicePixelRatio();
        // The row the list's own frame is drawn on, and a point on the first row
        // of what it holds - both named off the geometry rather than guessed,
        // since the window is taller than the list it holds
        const QPoint onTheFrame(shot.width() / 2, qRound(pList->geometry().top() * ratio));
        QWidget* pViewport = pList->viewport();
        const QPoint inTheList = pViewport->mapTo(pPopup, QPoint(pViewport->width() / 2, 4)) * ratio;
        pCombo->hidePopup();

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QColor corner = QColor::fromRgba(shot.pixel(0, 0));
        const QColor frame = QColor::fromRgba(shot.pixel(onTheFrame));
        const QColor list = QColor::fromRgba(shot.pixel(inTheList));
        QVERIFY2(corner.alpha() == 0, qPrintable(qsl("the popup's corner is painted %1 rather than cut away, so the list's radius has a square window behind it").arg(corner.name(QColor::HexArgb))));
        QVERIFY2(frame.alpha() == 255 && frame.rgb() == tokens.border.rgb(),
                 qPrintable(qsl("the top of the popup's frame is %1 rather than the hairline's %2 - the frame the corner is cut out of is not there")
                                    .arg(frame.name(QColor::HexArgb), tokens.border.name())));
        QVERIFY2(list.alpha() == 255 && list.rgb() == tokens.field.rgb(),
                 qPrintable(qsl("the popup's first row is %1 rather than the field surface's %2 - the window was opened up and took the list with it")
                                    .arg(list.name(QColor::HexArgb), tokens.field.name())));

        // ...and it is still open once something has handed the window its
        // colours back. An appearance change swaps the application's style,
        // which re-resolves every widget's palette - after the shell has
        // restyled, so the shell's pass cannot be the last word and the popup
        // has to say it again on the way in. The platform the cases run on does
        // not re-resolve, so the wipe is made here rather than waited for.
        pPopup->setPalette(QPalette());
        pCombo->showPopup();
        QCoreApplication::processEvents();
        const QColor cornerAgain = QColor::fromRgba(pPopup->grab().toImage().convertToFormat(QImage::Format_ARGB32).pixel(0, 0));
        pCombo->hidePopup();
        QVERIFY2(cornerAgain.alpha() == 0,
                 qPrintable(qsl("a palette handed back to the popup closed its corner again, painting it %1 - nothing says the frame is nothing at the one moment left, which is the popup being shown")
                                    .arg(cornerAgain.name(QColor::HexArgb))));
    }
};

#include "SettingsAppearanceTest.moc"
MUDLET_GROUPED_TEST_MAIN(SettingsAppearanceTest)
