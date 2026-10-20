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
 * Guards an unpainted form falling back on QPalette::Window where Window and Base
 * agree (macOS light), and all three columns painted one flat tone.
 *
 * Run with: ctest -R EditorSurfaceToneTest -V
 */

#include <QLineEdit>
#include <QPalette>
#include <QPixmap>
#include <QScopeGuard>
#include <QSplitterHandle>
#include <QTreeWidget>
#include <QtTest/QtTest>
#include <chrono>
#include <cstdlib>

#include "Host.h"
#include "HostDialogs.h"
#include "PixelTestHelper.h"
#include "ProfileSessionTestHelper.h"
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
    TestProfile::Session mSession;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorSurfaceTone-Test-Profile");

    // Off the whole window: grab() renders only the widget and its children, so a
    // transparent widget comes back as an unpainted pixmap
    QColor pixelOfWindowUnder(QWidget* pWidget, const QPoint& pointInWidget) const
    {
        const QPixmap shot = mpEditor->grab();
        return shot.toImage().pixelColor(pWidget->mapTo(mpEditor, pointInWidget));
    }

    static QString describe(const QColor& measured, const uiDesign::ThemeTokens& tokens)
    {
        return qsl("%1 - the page is %2, the pane %3, the card %4, the field %5, the separator %6")
                .arg(measured.name(), tokens.page.name(), tokens.pane.name(), tokens.card.name(), tokens.field.name(), tokens.separator.name());
    }

    // Under the last pattern row: the one place in the edit column that is
    // nothing but its surface
    QColor editColumnTone() const
    {
        auto* pPatternList = mpEditor->findChild<QWidget*>(qsl("editorPatternList"));
        if (!pPatternList || pPatternList->height() < 20 || pPatternList->width() < 20) {
            return QColor();
        }
        return pixelOfWindowUnder(pPatternList, QPoint(pPatternList->width() - 3, pPatternList->height() - 3));
    }

    // Inside the sidebar's bottom padding, clear of the pill a chosen row is
    // drawn as
    QColor sidebarTone() const
    {
        QWidget* pSidebar = mpEditor->findChild<QWidget*>(qsl("editorSidebarPane"));
        if (!pSidebar || pSidebar->height() < 40 || pSidebar->width() < 12) {
            return QColor();
        }
        return pixelOfWindowUnder(pSidebar, QPoint(4, pSidebar->height() - 5));
    }

    // Window and Base both white: themeTokens() then steps the page down under
    // the card, so a surface nobody paints, which falls back on Window, stops
    // matching the page tone by accident
    static QPalette theWhiteOnWhitePalette()
    {
        QPalette palette(QApplication::palette());
        palette.setColor(QPalette::Window, QColor(Qt::white));
        palette.setColor(QPalette::Base, QColor(Qt::white));
        palette.setColor(QPalette::WindowText, QColor(Qt::black));
        return palette;
    }

    // Setting the application palette sends the PaletteChange that
    // dlgTriggerEditor::changeEvent() restyles on, as an appearance change does
    void restyleWith(const QPalette& palette) const
    {
        QApplication::setPalette(palette);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        QCoreApplication::processEvents();
        mpEditor->grab();
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
        if (TestProfile::Session::configCannotBeRedirected()) {
            QSKIP(TestProfile::Session::configCannotBeRedirectedReason());
        }

        QVERIFY2(mSession.start(mProfileName), qPrintable(mSession.why()));
        mpHost = mSession.host();

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = HostDialogs::of(mpHost).mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(1000, 800);

        // An empty view shows a placeholder rather than the form
        mpEditor->slot_showTriggers();
        mpEditor->addTrigger(false);
        QTest::qWait(100ms);
    }

    void cleanupTestCase()
    {
        mpEditor = nullptr;
        mpHost = nullptr;
        mSession.stop();
    }

    void test_theFormBehindThePatternsIsThePageTone()
    {
        auto* pPatternList = mpEditor->findChild<QWidget*>(qsl("editorPatternList"));
        QVERIFY2(pPatternList, "the widget the pattern rows are held in has gone, or been renamed");
        QVERIFY2(pPatternList->height() > 20 && pPatternList->width() > 20, "the pattern list is too small to read a pixel out of");

        const QPalette savedPalette = QApplication::palette();
        const auto putBack = qScopeGuard([this, savedPalette] {
            restyleWith(savedPalette);
        });
        restyleWith(theWhiteOnWhitePalette());

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QVERIFY2(tokens.page.rgb() != QApplication::palette().color(QPalette::Window).rgb(),
                 qPrintable(qsl("the page is %1 and QPalette::Window %2 - they agree, so a surface nothing painted would read as the page and this case would measure nothing")
                                    .arg(tokens.page.name(), QApplication::palette().color(QPalette::Window).name())));

        const QColor measured = pixelOfWindowUnder(pPatternList, QPoint(pPatternList->width() - 3, pPatternList->height() - 3));
        qInfo().noquote() << qsl("  the form behind the patterns is painted %1").arg(describe(measured, tokens));

        QVERIFY2(TestPixels::distance(measured, tokens.page) < TestPixels::distance(measured, tokens.field),
                 qPrintable(qsl("the form behind the patterns is painted %1 - it is nearer the field colour than the page, which is the sunken box the edit column used to read as")
                                    .arg(describe(measured, tokens))));
        QCOMPARE(measured.rgb(), tokens.page.rgb());
    }

    void test_theSidebarAndTheEditColumnAreBothThePage()
    {
        const QPalette savedPalette = QApplication::palette();
        const auto putBack = qScopeGuard([this, savedPalette] {
            restyleWith(savedPalette);
        });
        restyleWith(theWhiteOnWhitePalette());

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QColor windowColour = QApplication::palette().color(QPalette::Window);
        QVERIFY2(tokens.page.rgb() != windowColour.rgb(),
                 qPrintable(qsl("the page is %1 and QPalette::Window %2 - they agree, so a column nothing painted would read as the page").arg(tokens.page.name(), windowColour.name())));

        const QColor sidebar = sidebarTone();
        const QColor form = editColumnTone();
        QVERIFY2(sidebar.isValid() && form.isValid(), "the sidebar or the edit column is too small to read a pixel out of");

        // A control the editor paints nothing on. Its palette is set rather than
        // inherited: a stylesheet's background-color lands on the polished shell's
        // QPalette::Window, so an inheriting child would fill with the page tone.
        QWidget* pShell = mpEditor->findChild<QWidget*>(qsl("editorShell"));
        QVERIFY2(pShell, "the editor shell has gone, or been renamed");
        QWidget unpaintedProbe(pShell);
        unpaintedProbe.setPalette(QApplication::palette());
        unpaintedProbe.setAutoFillBackground(true);
        unpaintedProbe.setGeometry(pShell->width() / 2 - 20, pShell->height() / 2 - 20, 40, 40);
        unpaintedProbe.show();
        QCoreApplication::processEvents();
        const QColor unpainted = pixelOfWindowUnder(&unpaintedProbe, QPoint(unpaintedProbe.width() / 2, unpaintedProbe.height() / 2));

        qInfo().noquote() << qsl("measured: sidebar %1, edit column %2, panel %3, an unpainted widget %4; tokens: page %5, pane %6, separator %7, card %8")
                                     .arg(sidebar.name(), form.name(), itemPanelTone().name(), unpainted.name(), tokens.page.name(), tokens.pane.name(), tokens.separator.name(), tokens.card.name());

        QVERIFY2(sidebar.rgb() == form.rgb(), qPrintable(qsl("the sidebar is painted %1 and the column an item is edited in %2").arg(sidebar.name(), form.name())));
        QVERIFY2(sidebar.rgb() == tokens.page.rgb(), qPrintable(qsl("the sidebar is painted %1").arg(describe(sidebar, tokens))));
        QVERIFY2(form.rgb() == tokens.page.rgb(), qPrintable(qsl("the column an item is edited in is painted %1").arg(describe(form, tokens))));
        QVERIFY2(
                unpainted.rgb() != tokens.page.rgb(),
                qPrintable(qsl("a widget nothing paints reads %1, which is the page tone - so the two readings above would hold for a column nobody painted either").arg(describe(unpainted, tokens))));
    }

    void test_theItemPanelIsAPaneLiftedOffThePage()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QColor panel = itemPanelTone();
        const QColor form = editColumnTone();
        QVERIFY2(panel.isValid() && form.isValid(), "the item panel or the edit column is too small to read a pixel out of");

        QVERIFY2(panel.rgb() != form.rgb(), qPrintable(qsl("the panel of items and the column an item is edited in are both painted %1, so the window is one flat expanse").arg(panel.name())));
        QVERIFY2(panel.rgb() == tokens.pane.rgb(), qPrintable(qsl("the panel of items is painted %1").arg(describe(panel, tokens))));
        QVERIFY2(panel.lightness() > tokens.page.lightness(), qPrintable(qsl("the panel of items is not lifted off the page: it is painted %1").arg(describe(panel, tokens))));
        QVERIFY2(TestPixels::distance(panel, tokens.page) < TestPixels::distance(panel, tokens.card),
                 qPrintable(qsl("the panel of items is nearer a card than the page it lies beside: it is painted %1").arg(describe(panel, tokens))));
    }

    void test_theSplitterHandlesAreSeparatorsDarkerThanThePage()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();

        QSplitterHandle* pPanelHandle = mpEditor->splitter_main->handle(1);
        QVERIFY2(pPanelHandle && pPanelHandle->width() > 4 && pPanelHandle->height() > 80, "the handle between the panel and the edit column is missing or too small to read a pixel out of");
        // Clear of the middle of its length, where a splitter may draw a grip
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

    void test_theNameFieldKeepsTheFieldTone()
    {
        QLineEdit* pName = mpEditor->mpTriggersMainArea->lineEdit_trigger_name;
        QVERIFY2(pName->height() > 8 && pName->width() > 40, "the trigger name field is too small to read a pixel out of");

        const QPalette savedPalette = QApplication::palette();
        const auto putBack = qScopeGuard([this, savedPalette] {
            restyleWith(savedPalette);
        });
        restyleWith(theWhiteOnWhitePalette());

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QVERIFY2(tokens.field.rgb() != tokens.page.rgb(),
                 qPrintable(qsl("the field surface and the page are both %1 on this palette, so a field painted as neither would still pass").arg(tokens.field.name())));

        // Near the trailing edge: the name is drawn from the leading edge, so the
        // middle of the field is a letter once the name is long enough
        const QColor measured = pixelOfWindowUnder(pName, QPoint(pName->width() - 6, pName->height() / 2));
        qInfo().noquote() << qsl("  the trigger name field is painted %1").arg(describe(measured, tokens));

        QVERIFY2(measured.rgb() == tokens.field.rgb(), qPrintable(qsl("the trigger name field is painted %1 - it has lost the sunken colour a field is filled with").arg(describe(measured, tokens))));
    }

    // Scoped by id so the rule paints the shell alone and lets everything under
    // it show through, rather than painting over what draws it
    void test_theShellRuleNamesNothingButTheShell()
    {
        QWidget* pShell = mpEditor->findChild<QWidget*>(qsl("editorShell"));
        QVERIFY2(pShell, "the editor shell has gone, or been renamed");
        const QString sheet = pShell->styleSheet();

        QVERIFY2(sheet.contains(qsl("#editorShell {")), qPrintable(qsl("the shell is not painted at all, its stylesheet is \"%1\"").arg(sheet)));
        QVERIFY2(sheet.count(QLatin1Char('{')) == 1, qPrintable(qsl("the shell's stylesheet carries more than the one rule that paints it: \"%1\"").arg(sheet)));
        QVERIFY2(sheet.contains(uiDesign::themeTokens().page.name()), qPrintable(qsl("the shell is painted something other than the page tone: \"%1\"").arg(sheet)));
    }

    // Every muted tone is mixed from the softened text colour, so the whole scale
    // loses contrast with it - measured here rather than assumed
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

            QVERIFY2(tokens.text.rgb() != paletteText.rgb(),
                     qPrintable(qsl("on the %1 theme the words are the palette's own %2, which is the glare the softening is for").arg(theme, paletteText.name())));
            const int towardsThePage = std::abs(tokens.text.lightness() - paletteText.lightness());
            const int roomToThePage = std::abs(tokens.page.lightness() - paletteText.lightness());
            QVERIFY2(towardsThePage > 0 && towardsThePage < roomToThePage / 2,
                     qPrintable(qsl("on the %1 theme the words are %2, %3 levels off the palette's %4 with only %5 between that and the page - that is a wash, not a softening")
                                        .arg(theme, tokens.text.name(), QString::number(towardsThePage), paletteText.name(), QString::number(roomToThePage))));

            for (const auto& reading : {QPair<QString, qreal>{qsl("the page"), onPage}, {qsl("a card"), onCard}, {qsl("a field"), onField}}) {
                QVERIFY2(reading.second >= 4.5,
                         qPrintable(qsl("on the %1 theme the words read %2:1 against %3, under the 4.5:1 body text has to clear").arg(theme, QString::number(reading.second, 'f', 2), reading.first)));
            }
            QVERIFY2(mutedOnPage >= 3.0,
                     qPrintable(qsl("on the %1 theme the quiet tone reads %2:1 on the page, under the 3:1 a large or secondary line has to clear").arg(theme, QString::number(mutedOnPage, 'f', 2))));

            // A third of the way from page to text came to about 2.3:1 on a light
            // theme, leaving the disabled prompt-line pattern row all but invisible
            for (const auto& reading : {QPair<QString, qreal>{qsl("the page"), disabledOnPage}, {qsl("a field"), disabledOnField}}) {
                QVERIFY2(reading.second >= 3.0,
                         qPrintable(qsl("on the %1 theme an unavailable word is %2, reading %3:1 against %4 - under the 3:1 a secondary line has to clear, and a word nobody can read is not a word "
                                        "switched off")
                                            .arg(theme, tokens.disabledText.name(), QString::number(reading.second, 'f', 2), reading.first)));
            }
            QVERIFY2(disabledOnPage < mutedOnPage,
                     qPrintable(qsl("on the %1 theme an unavailable word reads %2:1 on the page against the quiet tone's %3:1, so the two say the same thing")
                                        .arg(theme, QString::number(disabledOnPage, 'f', 2), QString::number(mutedOnPage, 'f', 2))));

            if (!tokens.darkPage) {
                // Lifting a light theme's white Base towards a grey page only muddies it
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

    // The tones are mixed from the palette, so both themes are put to the tokens
    // rather than trusting whichever one the run is under. Put back afterwards,
    // as the window restyles off the application's palette.
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
            // A fraction of the room under the page rather than a fixed number of
            // levels, so a light page gets a groove rather than a black rule
            QVERIFY2(seamDrop < tokens.page.lightness() / 2,
                     qPrintable(qsl("on the %1 theme the seam is %2 levels under a page of %3, which reads as a rule drawn across the window").arg(theme).arg(seamDrop).arg(tokens.page.lightness())));
        }

        QApplication::setPalette(savedPalette);
        QTest::qWait(50ms);
    }
};

#include "EditorSurfaceToneTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorSurfaceToneTest)
