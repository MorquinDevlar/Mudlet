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
 * What each of the editor's three columns is painted, measured off the window.
 *
 * The sidebar and the column an item is edited in are the page. The panel of
 * items between them is a pane: a fifth of a card's lift off the page, which is
 * enough to be told apart from the columns either side of it and far short of
 * reading as a panel laid on top of them. The handle that resizes two of them is
 * the seam between them, and is darker than either.
 *
 * Two bugs are guarded here. The form was reading as a different surface from
 * the panel because nothing was painting it at all - everything from the frame
 * an item is edited in down to the seven forms inside it is transparent and fell
 * back on QPalette::Window, which agrees with the page tone only until a palette
 * answers the same colour to Window and to Base (macOS in light appearance among
 * them), where themeTokens() steps the page down under the card. And afterwards
 * every one of the three columns was painted that one tone, leaving the window a
 * single flat expanse with no seam between the panels.
 *
 * Run with: ctest -R EditorSurfaceToneTest -V
 */

#include <QLineEdit>
#include <QPalette>
#include <QPixmap>
#include <QSplitterHandle>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest/QtTest>
#include <chrono>
#include <cstdlib>

#include "Host.h"
#include "MudletInstanceCoordinator.h"
#include "PortableModeTestHelper.h"
#include "ProfileTestHelper.h"
#include "TelnetServerStub.h"
#include "dlgTriggerEditor.h"
#include "dlgTriggersMainArea.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorSurfaceToneTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir mConfigDir;
    QByteArray mSavedXdg;
    TelnetServerStub* mpServer = nullptr;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorSurfaceTone-Test-Profile");
    QString mPort;
    const QString mLocalhost = qsl("localhost");

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

    // Off a shot of the whole window rather than of the widget alone. What is
    // being measured here is a surface a widget shows through rather than one it
    // paints, and grab() renders only the widget it is called on and its
    // children - a transparent one comes back as an unpainted pixmap.
    QColor pixelOfWindowUnder(QWidget* pWidget, const QPoint& pointInWidget) const
    {
        const QPixmap shot = mpEditor->grab();
        return shot.toImage().pixelColor(pWidget->mapTo(mpEditor, pointInWidget));
    }

    // Both surfaces are flat fills, so the nearer of the two is the one that
    // was painted
    static int distanceBetween(const QColor& one, const QColor& other) { return std::abs(one.red() - other.red()) + std::abs(one.green() - other.green()) + std::abs(one.blue() - other.blue()); }

    static QString describe(const QColor& measured, const uiDesign::ThemeTokens& tokens)
    {
        return qsl("%1 - the page is %2, the pane %3, the card %4, the field %5, the separator %6")
                .arg(measured.name(), tokens.page.name(), tokens.pane.name(), tokens.card.name(), tokens.field.name(), tokens.separator.name());
    }

    // The empty room under the last pattern row, which is what a form with one
    // pattern in it is mostly made of - and the one place in the column an item
    // is edited in that is nothing but its surface
    QColor editColumnTone() const
    {
        auto* pPatternList = mpEditor->findChild<QWidget*>(qsl("editorPatternList"));
        if (!pPatternList || pPatternList->height() < 20 || pPatternList->width() < 20) {
            return QColor();
        }
        return pixelOfWindowUnder(pPatternList, QPoint(pPatternList->width() - 3, pPatternList->height() - 3));
    }

    // ...and the room under the sidebar's last row, inside its own bottom
    // padding, clear of the pill a chosen row is drawn as
    QColor sidebarTone() const
    {
        QWidget* pSidebar = mpEditor->findChild<QWidget*>(qsl("editorSidebarPane"));
        if (!pSidebar || pSidebar->height() < 40 || pSidebar->width() < 12) {
            return QColor();
        }
        return pixelOfWindowUnder(pSidebar, QPoint(4, pSidebar->height() - 5));
    }

    QColor itemPanelTone() const
    {
        QWidget* pTreeViewport = mpEditor->treeWidget_triggers->viewport();
        if (pTreeViewport->height() < 20 || pTreeViewport->width() < 20) {
            return QColor();
        }
        return pixelOfWindowUnder(pTreeViewport, QPoint(pTreeViewport->width() - 3, pTreeViewport->height() - 3));
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
        mPort = QString::number(mpServer->serverPort());
        mudlet::start();
        mudlet::self()->setupConfig();
        QCOMPARE(mudlet::getMudletPath(enums::mainPath), qsl("%1/mudlet").arg(mConfigDir.path()));
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>(qsl("MudletInstanceCoordinator")));
        mudlet::self()->init();
        mudlet::self()->setStorePasswordsSecurely(false);
        deleteProfileDirectory(mProfileName);

        startProfile(mProfileName, mLocalhost, mPort);

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = mpHost->mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(1000, 800);

        // A trigger to edit, so the form is the one a user looks at
        mpEditor->slot_showTriggers();
        mpEditor->addTrigger(false);
        QTest::qWait(100ms);
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

    // The empty room under the last pattern row, which is what a form with one
    // pattern in it is mostly made of
    void test_theFormBehindThePatternsIsThePageTone()
    {
        auto* pPatternList = mpEditor->findChild<QWidget*>(qsl("editorPatternList"));
        QVERIFY2(pPatternList, "the widget the pattern rows are held in has gone, or been renamed");
        QVERIFY2(pPatternList->height() > 20 && pPatternList->width() > 20, "the pattern list is too small to read a pixel out of");

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QColor measured = pixelOfWindowUnder(pPatternList, QPoint(pPatternList->width() - 3, pPatternList->height() - 3));

        QVERIFY2(distanceBetween(measured, tokens.page) < distanceBetween(measured, tokens.field),
                 qPrintable(qsl("the form behind the patterns is painted %1 - it is nearer the field colour than the page, which is the sunken box the edit column used to read as")
                                    .arg(describe(measured, tokens))));
        QCOMPARE(measured.rgb(), tokens.page.rgb());
    }

    // The two columns drawn on the page: the one down the left the sections are
    // picked in, and the one an item is edited in. Both measured off the window
    // rather than taken from the tokens, so a column nothing paints - which is
    // how the form came to differ from the panel - is caught here.
    void test_theSidebarAndTheEditColumnAreBothThePage()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QColor sidebar = sidebarTone();
        const QColor form = editColumnTone();
        QVERIFY2(sidebar.isValid() && form.isValid(), "the sidebar or the edit column is too small to read a pixel out of");

        qInfo().noquote() << qsl("measured: sidebar %1, edit column %2, panel %3; tokens: page %4, pane %5, separator %6, card %7")
                                     .arg(sidebar.name(), form.name(), itemPanelTone().name(), tokens.page.name(), tokens.pane.name(), tokens.separator.name(), tokens.card.name());

        QVERIFY2(sidebar.rgb() == form.rgb(), qPrintable(qsl("the sidebar is painted %1 and the column an item is edited in %2").arg(sidebar.name(), form.name())));
        QVERIFY2(sidebar.rgb() == tokens.page.rgb(), qPrintable(qsl("the sidebar is painted %1").arg(describe(sidebar, tokens))));
    }

    // ...and the panel between them, which is a surface of its own: lifted off
    // the page so that it reads as a pane rather than as more of the window, and
    // nowhere near a card, which would read as a panel laid on top of it
    void test_theItemPanelIsAPaneLiftedOffThePage()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QColor panel = itemPanelTone();
        const QColor form = editColumnTone();
        QVERIFY2(panel.isValid() && form.isValid(), "the item panel or the edit column is too small to read a pixel out of");

        QVERIFY2(panel.rgb() != form.rgb(), qPrintable(qsl("the panel of items and the column an item is edited in are both painted %1, so the window is one flat expanse").arg(panel.name())));
        QVERIFY2(panel.rgb() == tokens.pane.rgb(), qPrintable(qsl("the panel of items is painted %1").arg(describe(panel, tokens))));
        QVERIFY2(panel.lightness() > tokens.page.lightness(), qPrintable(qsl("the panel of items is not lifted off the page: it is painted %1").arg(describe(panel, tokens))));
        QVERIFY2(distanceBetween(panel, tokens.page) < distanceBetween(panel, tokens.card),
                 qPrintable(qsl("the panel of items is nearer a card than the page it lies beside: it is painted %1").arg(describe(panel, tokens))));
    }

    // The seam between two panes is the handle that resizes them, which is where
    // the reader already looks for the join. Both of the window's splitters: the
    // one parting the panel from the edit column, and the one down that column
    // whose handle carries the code pane's heading.
    void test_theSplitterHandlesAreSeparatorsDarkerThanThePage()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();

        QSplitterHandle* pPanelHandle = mpEditor->splitter_main->handle(1);
        QVERIFY2(pPanelHandle && pPanelHandle->width() > 4 && pPanelHandle->height() > 80, "the handle between the panel and the edit column is missing or too small to read a pixel out of");
        // Clear of the grip, which is drawn across the middle of the handle
        const QColor panelSeam = pixelOfWindowUnder(pPanelHandle, QPoint(pPanelHandle->width() / 2, 6));

        QSplitterHandle* pCodeHandle = mpEditor->splitter_right->handle(1);
        QVERIFY2(pCodeHandle && pCodeHandle->height() > 8 && pCodeHandle->width() > 80, "the handle carrying the code pane's heading is missing or too small to read a pixel out of");
        // Above the heading and the compile chip, both of which are drawn down
        // the middle of the strip
        const QColor codeSeam = pixelOfWindowUnder(pCodeHandle, QPoint(pCodeHandle->width() / 4, 1));

        QVERIFY2(panelSeam.rgb() == tokens.separator.rgb(), qPrintable(qsl("the handle between the panel and the edit column is painted %1").arg(describe(panelSeam, tokens))));
        QVERIFY2(codeSeam.rgb() == tokens.separator.rgb(), qPrintable(qsl("the handle carrying the code pane's heading is painted %1").arg(describe(codeSeam, tokens))));
        QVERIFY2(panelSeam.lightness() < tokens.page.lightness(), qPrintable(qsl("the seam between two panes is not darker than the page: it is painted %1").arg(describe(panelSeam, tokens))));
    }

    // The one thing on the form that is still a field is the one typed into
    void test_theNameFieldKeepsTheFieldTone()
    {
        QLineEdit* pName = mpEditor->mpTriggersMainArea->lineEdit_trigger_name;
        QVERIFY2(pName->height() > 8 && pName->width() > 40, "the trigger name field is too small to read a pixel out of");

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        // Inside the field's own border and clear of whatever is typed in it,
        // which is drawn from the leading edge - the middle of the field is a
        // letter of the item's name as soon as the name is long enough for it
        const QColor measured = pixelOfWindowUnder(pName, QPoint(pName->width() - 6, pName->height() / 2));

        QVERIFY2(distanceBetween(measured, tokens.field) < distanceBetween(measured, tokens.page),
                 qPrintable(qsl("the trigger name field is painted %1 - it has lost the sunken colour a field is filled with").arg(describe(measured, tokens))));
    }

    // The page tone is painted on the shell, which is Mudlet's own scaffolding
    // and nothing a profile's Lua stylesheet is offered. Named outright, so that
    // it draws that one widget and lets everything under it show through rather
    // than painting over what draws it.
    void test_theShellRuleNamesNothingButTheShell()
    {
        QWidget* pShell = mpEditor->findChild<QWidget*>(qsl("editorShell"));
        QVERIFY2(pShell, "the editor shell has gone, or been renamed");
        const QString sheet = pShell->styleSheet();

        QVERIFY2(sheet.contains(qsl("#editorShell {")), qPrintable(qsl("the shell is not painted at all, its stylesheet is \"%1\"").arg(sheet)));
        QVERIFY2(sheet.count(QLatin1Char('{')) == 1, qPrintable(qsl("the shell's stylesheet carries more than the one rule that paints it: \"%1\"").arg(sheet)));
        QVERIFY2(sheet.contains(uiDesign::themeTokens().page.name()), qPrintable(qsl("the shell is painted something other than the page tone: \"%1\"").arg(sheet)));
    }

    // The words and the wells they are typed into. A palette answers pure white
    // to WindowText on a dark theme and near-black to Base, which is white type
    // in a hole cut through the window - so the text is pulled a little way back
    // towards the page it is written on and a dark field is lifted a little way
    // up towards it. Every muted tone is mixed over the page from that same
    // text colour, so the whole scale softens with it, and what the softening
    // left of the contrast is measured here rather than assumed.
    void test_theWordsAreSoftenedAndADarkFieldIsLifted()
    {
        const QPalette savedPalette = QApplication::palette();

        QPalette darkPalette(savedPalette);
        darkPalette.setColor(QPalette::Window, QColor(0x35, 0x35, 0x35));
        darkPalette.setColor(QPalette::WindowText, QColor(Qt::white));
        darkPalette.setColor(QPalette::Base, QColor(0x19, 0x19, 0x19));

        QPalette lightPalette(savedPalette);
        lightPalette.setColor(QPalette::Window, QColor(0xec, 0xec, 0xec));
        lightPalette.setColor(QPalette::WindowText, QColor(Qt::black));
        lightPalette.setColor(QPalette::Base, QColor(Qt::white));

        for (const QPalette& palette : {darkPalette, lightPalette}) {
            QApplication::setPalette(palette);
            const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
            const QString theme = tokens.darkPage ? qsl("dark") : qsl("light");
            const QColor paletteText = palette.color(QPalette::WindowText);
            const QColor paletteField = palette.color(QPalette::Base);

            const qreal onPage = uiDesign::contrastRatio(tokens.text, tokens.page);
            const qreal onCard = uiDesign::contrastRatio(tokens.text, tokens.card);
            const qreal onField = uiDesign::contrastRatio(tokens.text, tokens.field);
            const qreal mutedOnPage = uiDesign::contrastRatio(tokens.mutedText, tokens.page);
            const qreal disabledOnPage = uiDesign::contrastRatio(tokens.disabledText, tokens.page);
            const qreal disabledOnField = uiDesign::contrastRatio(tokens.disabledText, tokens.field);
            qInfo().noquote()
                    << qsl("%1: text %2 (palette %3), field %4 (palette %5), page %6, card %7, muted %8 - text reads %9:1 on the page, %10:1 on a card, %11:1 in a field; muted %12:1 on the page")
                               .arg(theme,
                                    tokens.text.name(),
                                    paletteText.name(),
                                    tokens.field.name(),
                                    paletteField.name(),
                                    tokens.page.name(),
                                    tokens.card.name(),
                                    tokens.mutedText.name(),
                                    QString::number(onPage, 'f', 2),
                                    QString::number(onCard, 'f', 2),
                                    QString::number(onField, 'f', 2),
                                    QString::number(mutedOnPage, 'f', 2));
            qInfo().noquote() << qsl("%1: an unavailable word is %2, reading %3:1 on the page and %4:1 in a field")
                                         .arg(theme, tokens.disabledText.name(), QString::number(disabledOnPage, 'f', 2), QString::number(disabledOnField, 'f', 2));

            // Softened, and softened the one way: towards the page rather than
            // away from it, and never past it
            QVERIFY2(tokens.text.rgb() != paletteText.rgb(),
                     qPrintable(qsl("on the %1 theme the words are the palette's own %2, which is the glare the softening is for").arg(theme, paletteText.name())));
            const int towardsThePage = std::abs(tokens.text.lightness() - paletteText.lightness());
            const int roomToThePage = std::abs(tokens.page.lightness() - paletteText.lightness());
            QVERIFY2(towardsThePage > 0 && towardsThePage < roomToThePage / 2,
                     qPrintable(qsl("on the %1 theme the words are %2, %3 levels off the palette's %4 with only %5 between that and the page - that is a wash, not a softening")
                                        .arg(theme, tokens.text.name(), QString::number(towardsThePage), paletteText.name(), QString::number(roomToThePage))));

            // What a reader has to be able to make out: the words on each of
            // the three surfaces they are set on, and the quieter tone that
            // labels and captions are written in
            for (const auto& reading : {QPair<QString, qreal>{qsl("the page"), onPage}, {qsl("a card"), onCard}, {qsl("a field"), onField}}) {
                QVERIFY2(reading.second >= 4.5,
                         qPrintable(qsl("on the %1 theme the words read %2:1 against %3, under the 4.5:1 body text has to clear").arg(theme, QString::number(reading.second, 'f', 2), reading.first)));
            }
            QVERIFY2(mutedOnPage >= 3.0,
                     qPrintable(qsl("on the %1 theme the quiet tone reads %2:1 on the page, under the 3:1 a large or secondary line has to clear").arg(theme, QString::number(mutedOnPage, 'f', 2))));

            // ...and the tone an unavailable word is written in, which is
            // quieter again but still a word. A third of the way from the page
            // to the text lands at about 2.3:1 on a light theme - the pattern
            // row that says "match on the prompt line (disabled)" was all but
            // invisible - so it is floored against both of the surfaces such a
            // word is set on: the page it is a label on, and the field it is
            // typed into.
            for (const auto& reading : {QPair<QString, qreal>{qsl("the page"), disabledOnPage}, {qsl("a field"), disabledOnField}}) {
                QVERIFY2(reading.second >= 3.0,
                         qPrintable(qsl("on the %1 theme an unavailable word is %2, reading %3:1 against %4 - under the 3:1 a secondary line has to clear, and a word nobody can read is not a word "
                                        "switched off")
                                            .arg(theme, tokens.disabledText.name(), QString::number(reading.second, 'f', 2), reading.first)));
            }
            // ...and it is still quieter than the tone a live secondary line is
            // written in, or "switched off" has stopped meaning anything
            QVERIFY2(disabledOnPage < mutedOnPage,
                     qPrintable(qsl("on the %1 theme an unavailable word reads %2:1 on the page against the quiet tone's %3:1, so the two say the same thing")
                                        .arg(theme, QString::number(disabledOnPage, 'f', 2), QString::number(mutedOnPage, 'f', 2))));

            if (!tokens.darkPage) {
                // A light theme's Base is white, and lifting white towards a
                // grey page only muddies it
                QCOMPARE(tokens.field.rgb(), paletteField.rgb());
                continue;
            }
            QVERIFY2(tokens.field.lightness() > paletteField.lightness() && tokens.field.lightness() < tokens.page.lightness(),
                     qPrintable(qsl("on the dark theme the field is %1, which is not between the palette's %2 and the page's %3 - a well is sunk into the page, not cut through it")
                                        .arg(tokens.field.name(), paletteField.name(), tokens.page.name())));
        }

        QApplication::setPalette(savedPalette);
        QTest::qWait(50ms);
    }

    // The three tones are mixed from the palette, so what holds on the theme the
    // test happens to be running under says nothing about the other one. Both
    // are put to the tokens directly here: a dark page with room under it for a
    // groove, and a light page near enough to white that the same drop would
    // draw a grey rule across the window instead.
    //
    // Last of the cases, and the palette is put back afterwards, because the
    // window restyles itself off the one the application is holding.
    void test_thePaneAndTheSeamHoldOnBothThemes()
    {
        const QPalette savedPalette = QApplication::palette();

        QPalette darkPalette(savedPalette);
        darkPalette.setColor(QPalette::Window, QColor(0x2c, 0x2c, 0x2e));
        darkPalette.setColor(QPalette::WindowText, QColor(Qt::white));
        darkPalette.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));

        QPalette lightPalette(savedPalette);
        lightPalette.setColor(QPalette::Window, QColor(0xec, 0xec, 0xec));
        lightPalette.setColor(QPalette::WindowText, QColor(Qt::black));
        lightPalette.setColor(QPalette::Base, QColor(Qt::white));

        for (const QPalette& palette : {darkPalette, lightPalette}) {
            QApplication::setPalette(palette);
            const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
            const QString theme = tokens.darkPage ? qsl("dark") : qsl("light");
            qInfo().noquote() << qsl("%1: page %2, pane %3, separator %4, card %5, border %6")
                                         .arg(theme, tokens.page.name(), tokens.pane.name(), tokens.separator.name(), tokens.card.name(), tokens.border.name());

            const int paneLift = tokens.pane.lightness() - tokens.page.lightness();
            const int cardLift = tokens.card.lightness() - tokens.page.lightness();
            const int seamDrop = tokens.page.lightness() - tokens.separator.lightness();

            QVERIFY2(paneLift > 0, qPrintable(qsl("on the %1 theme the pane is not lifted off the page at all: %2 against %3").arg(theme, tokens.pane.name(), tokens.page.name())));
            QVERIFY2(paneLift * 3 < cardLift,
                     qPrintable(qsl("on the %1 theme the pane is %2 levels off the page and a card is %3 - the pane reads as a panel laid on top").arg(theme).arg(paneLift).arg(cardLift)));
            QVERIFY2(seamDrop >= 4, qPrintable(qsl("on the %1 theme the seam is only %2 levels under the page, which is no groove at all").arg(theme).arg(seamDrop)));
            // A groove, not a black rule: the drop is a fraction of the room
            // under the page rather than a number of levels, so a light page
            // gets a seam it can carry
            QVERIFY2(seamDrop < tokens.page.lightness() / 2,
                     qPrintable(qsl("on the %1 theme the seam is %2 levels under a page of %3, which reads as a rule drawn across the window").arg(theme).arg(seamDrop).arg(tokens.page.lightness())));
        }

        QApplication::setPalette(savedPalette);
        QTest::qWait(50ms);
    }
};

#include "EditorSurfaceToneTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorSurfaceToneTest)
