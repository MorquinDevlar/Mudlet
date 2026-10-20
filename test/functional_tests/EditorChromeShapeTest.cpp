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
 * The shapes the editor's chrome is cut to - splitter seams, the code pane's
 * heading, colour wells - read off a shot of the window rather than off the
 * rules that draw them.
 *
 * Run with: ctest -R EditorChromeShapeTest -V
 */

#include <QImage>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitterHandle>
#include <QToolBar>
#include <QToolButton>
#include <QtTest/QtTest>
#include <chrono>

#include "GripSplitter.h"
#include "Host.h"
#include "HostDialogs.h"
#include "ProfileSessionTestHelper.h"
#include "dlgTriggerEditor.h"
#include "dlgTriggersMainArea.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorChromeShapeTest : public QObject
{
    Q_OBJECT

private:
    TestProfile::Session mSession;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorChromeShape-Test-Profile");

    // Mirrors the actions toolbar's padding in applyEditorShellStyle(), which the
    // grip sits inside
    static constexpr int scmToolBarPadding = 6;

    QImage windowShot() const { return mpEditor->grab().toImage(); }

    QSplitterHandle* paneSeam() const { return mpEditor->splitter_main->handle(1); }

    QSplitterHandle* codeHeading() const { return mpEditor->splitter_right->handle(1); }

    QList<QColor> acrossTheHandle(const QImage& shot, const QWidget* pHandle, const int y) const
    {
        QList<QColor> row;
        for (int x = 0; x < pHandle->width(); ++x) {
            row << shot.pixelColor(pHandle->mapTo(mpEditor, QPoint(x, y)));
        }
        return row;
    }

    static QString describe(const QList<QColor>& row)
    {
        QStringList names;
        for (const QColor& colour : row) {
            names << colour.name();
        }
        return names.join(QLatin1Char(' '));
    }

    static QPair<int, int> runOf(const QList<QColor>& row, const QColor& wanted)
    {
        int start = -1;
        int length = 0;
        for (int x = 0; x < row.size(); ++x) {
            if (row.at(x).rgb() != wanted.rgb()) {
                continue;
            }
            if (start < 0) {
                start = x;
            }
            ++length;
        }
        return {start, length};
    }

    void hover(QWidget* pWidget, const bool hovered) const
    {
        const QPointF local(pWidget->rect().center());
        if (hovered) {
            QEnterEvent enter(local, local, pWidget->mapToGlobal(pWidget->rect().center()));
            QCoreApplication::sendEvent(pWidget, &enter);
        } else {
            QEvent leave(QEvent::Leave);
            QCoreApplication::sendEvent(pWidget, &leave);
        }
        QTest::qWait(20ms);
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
        mpEditor->resize(1100, 800);
        mpEditor->slot_showTriggers();
        // The heading over the code pane is only laid out once the form above it
        // holds an item
        mpEditor->addTrigger(false);
        QTest::qWait(100ms);

        QVERIFY2(paneSeam() != nullptr, "The main splitter has no handle between its two panes");
        QVERIFY2(codeHeading() != nullptr, "The right hand splitter has no handle over the code pane");
    }

    void cleanupTestCase()
    {
        mpEditor = nullptr;
        mpHost = nullptr;
        mSession.stop();
    }

    void test_aPlainHandleIsOneHairlineBetweenTwoTones()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QSplitterHandle* pHandle = paneSeam();
        const QList<QColor> row = acrossTheHandle(windowShot(), pHandle, pHandle->height() / 2);
        const QPair<int, int> seam = runOf(row, tokens.separator);

        qInfo().noquote() << qsl("  across the %1px handle: %2; the seam is %3, the panel %4 and the page %5")
                                     .arg(QString::number(pHandle->width()), describe(row), tokens.separator.name(), tokens.pane.name(), tokens.page.name());

        QCOMPARE(pHandle->width(), uiDesign::GripSplitter::scmHandleThickness);
        QVERIFY2(seam.second == 1, qPrintable(qsl("the seam is %1px of %2 rather than one: %3").arg(QString::number(seam.second), tokens.separator.name(), describe(row))));

        QStringList wrong;
        for (int x = 0; x < row.size(); ++x) {
            if (x == seam.first) {
                continue;
            }
            const QColor wanted = x < seam.first ? tokens.pane : tokens.page;
            if (row.at(x).rgb() != wanted.rgb()) {
                wrong << qsl("x=%1 is %2 where the pane beside it is %3").arg(QString::number(x), row.at(x).name(), wanted.name());
            }
        }
        QVERIFY2(wrong.isEmpty(), qPrintable(qsl("the handle is not the two panes' own tones either side of the seam: %1").arg(wrong.join(qsl(", ")))));
    }

    void test_hoveringAPlainHandleWidensTheSeamToTheAccent()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QSplitterHandle* pHandle = paneSeam();
        hover(pHandle, true);
        const QImage shot = windowShot();
        // Three heights rather than one: a three-pixel grip pill drawn across the
        // middle of the handle would pass a single reading taken there
        const QList<QPair<QString, QList<QColor>>> rows{{qsl("near its top"), acrossTheHandle(shot, pHandle, 4)},
                                                        {qsl("across its middle"), acrossTheHandle(shot, pHandle, pHandle->height() / 2)},
                                                        {qsl("near its foot"), acrossTheHandle(shot, pHandle, pHandle->height() - 5)}};
        hover(pHandle, false);

        const int middle = (pHandle->width() - 1) / 2;
        QStringList measured;
        QStringList wrong;
        for (const auto& row : rows) {
            const QPair<int, int> lit = runOf(row.second, tokens.accent);
            measured << qsl("%1 %2").arg(row.first, describe(row.second));
            if (lit.second != 3) {
                wrong << qsl("%1 the seam is %2px of the accent rather than three").arg(row.first, QString::number(lit.second));
            } else if (lit.first != middle - 1) {
                wrong << qsl("%1 the seam starts at x=%2 rather than one either side of the middle at x=%3").arg(row.first, QString::number(lit.first), QString::number(middle));
            }
        }
        qInfo().noquote() << qsl("  hovered: %1; the accent is %2").arg(measured.join(qsl("; ")), tokens.accent.name());
        QVERIFY2(wrong.isEmpty(), qPrintable(qsl("the hovered handle is not one lit seam from end to end: %1").arg(wrong.join(qsl(", ")))));

        const QList<QColor> atRest = acrossTheHandle(windowShot(), pHandle, pHandle->height() / 2);
        QVERIFY2(runOf(atRest, tokens.accent).second == 0, qPrintable(qsl("the seam stayed lit after the pointer left it: %1").arg(describe(atRest))));
    }

    void test_theCodePanesHeadingIsCutToThePanelsCorner()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QSplitterHandle* pHandle = codeHeading();
        const QImage shot = windowShot();
        const QList<QColor> topRow = acrossTheHandle(shot, pHandle, 0);
        const QList<QColor> bottomRow = acrossTheHandle(shot, pHandle, pHandle->height() - 1);

        const QColor topCorner = topRow.constFirst();
        const QColor topMiddle = topRow.at(topRow.size() / 2);
        const QColor bottomCorner = bottomRow.constFirst();
        qInfo().noquote() << qsl("  the heading is %1px tall; its top corner is %2, the middle of its top edge %3 and its bottom corner %4; the strip is %5 and the page %6")
                                     .arg(QString::number(pHandle->height()), topCorner.name(), topMiddle.name(), bottomCorner.name(), tokens.separator.name(), tokens.page.name());

        QVERIFY2(topMiddle.rgb() == tokens.separator.rgb(), qPrintable(qsl("the heading strip does not reach the top of the handle: the middle of that edge is %1").arg(topMiddle.name())));
        QVERIFY2(topCorner.rgb() == tokens.page.rgb(), qPrintable(qsl("the heading's top corner is not cut away to the page: it is %1").arg(topCorner.name())));
        QVERIFY2(bottomCorner.rgb() == tokens.separator.rgb(), qPrintable(qsl("the heading's bottom corner is cut away as well, at %1 - only its top meets the page").arg(bottomCorner.name())));
    }

    void test_theHeadingCarriesNoGrip()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QSplitterHandle* pHandle = codeHeading();
        const QColor middle = windowShot().pixelColor(pHandle->mapTo(mpEditor, pHandle->rect().center()));
        qInfo().noquote()
                << qsl("  the middle of the %1x%2 strip is %3; the strip is %4").arg(QString::number(pHandle->width()), QString::number(pHandle->height()), middle.name(), tokens.separator.name());
        QVERIFY2(middle.rgb() == tokens.separator.rgb(),
                 qPrintable(qsl("something is drawn across the middle of the heading: it is %1 rather than the strip's own %2").arg(middle.name(), tokens.separator.name())));
    }

    void test_hoveringTheHeadingLightsItsTopEdge()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QSplitterHandle* pHandle = codeHeading();
        // The case above may leave the handle hovered, and entering a handle that
        // is already hovered lights nothing new
        hover(pHandle, false);

        const int x = pHandle->width() / 2;
        const auto toneAt = [&](const QImage& shot, const int y) {
            return shot.pixelColor(pHandle->mapTo(mpEditor, QPoint(x, y)));
        };

        hover(pHandle, true);
        const QImage lit = windowShot();
        QList<QColor> band;
        for (int y = 0; y < 4; ++y) {
            band << toneAt(lit, y);
        }
        hover(pHandle, false);
        const QColor atRest = toneAt(windowShot(), 0);

        qInfo().noquote() << qsl("  hovered, the first four rows are %1; at rest the top row is %2. The accent is %3 and the strip %4")
                                     .arg(describe(band), atRest.name(), tokens.accent.name(), tokens.separator.name());

        QStringList wrong;
        for (int y = 0; y < 3; ++y) {
            if (band.at(y).rgb() != tokens.accent.rgb()) {
                wrong << qsl("row %1 is %2").arg(QString::number(y), band.at(y).name());
            }
        }
        QVERIFY2(wrong.isEmpty(), qPrintable(qsl("the hovered heading is not three pixels of the accent along its top edge: %1").arg(wrong.join(qsl(", ")))));
        QVERIFY2(band.at(3).rgb() == tokens.separator.rgb(),
                 qPrintable(qsl("the lit band is deeper than three pixels: the fourth row is %1 rather than the strip's own %2").arg(band.at(3).name(), tokens.separator.name())));
        QVERIFY2(atRest.rgb() == tokens.separator.rgb(), qPrintable(qsl("the top edge stayed lit after the pointer left it: it is %1").arg(atRest.name())));
    }

    // Five of the seven forms are fixed fields whose heading resizes nothing -
    // see EditorFormShellTest
    void test_aHeadingThatDragsNothingStaysDark()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QSplitterHandle* pHandle = codeHeading();
        hover(pHandle, false);

        const int x = pHandle->width() / 2;
        const auto topRow = [&](const QImage& shot) {
            return shot.pixelColor(pHandle->mapTo(mpEditor, QPoint(x, 0)));
        };

        QVERIFY2(mpEditor->splitter_right->setHandleResizes(1, false), "the code pane's heading is not a handle this can be asked of");
        hover(pHandle, true);
        const QColor whileInert = topRow(windowShot());
        hover(pHandle, false);

        // Control: a band that never appears would otherwise pass this case
        QVERIFY(mpEditor->splitter_right->setHandleResizes(1, true));
        hover(pHandle, true);
        const QColor whileDragging = topRow(windowShot());
        hover(pHandle, false);

        qInfo().noquote() << qsl("  hovered, the top row reads %1 while the heading drags nothing and %2 while it drags; the strip is %3 and the accent %4")
                                     .arg(whileInert.name(), whileDragging.name(), tokens.separator.name(), tokens.accent.name());

        QVERIFY2(whileInert.rgb() == tokens.separator.rgb(),
                 qPrintable(qsl("a heading that resizes nothing lit its top edge anyway: it is %1 rather than the strip's own %2").arg(whileInert.name(), tokens.separator.name())));
        QVERIFY2(whileDragging.rgb() == tokens.accent.rgb(),
                 qPrintable(qsl("the heading did not light when it does drag, so the case above proves nothing: the top row is %1").arg(whileDragging.name())));
    }

    // Styling the bar drops the platform's handle; the fallback draws two dots a
    // shade off the bar
    void test_theToolbarsGripIsTheSixDotsInTheQuietTone()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QToolBar* pToolBar = mpEditor->toolBar;
        QVERIFY2(pToolBar != nullptr && pToolBar->isMovable(), "The actions toolbar is missing or cannot be moved, so it carries no grip");
        const int extent = pToolBar->style()->pixelMetric(QStyle::PM_ToolBarHandleExtent, nullptr, pToolBar);
        const int strip = extent + scmToolBarPadding;

        const QImage shot = windowShot();
        int inked = 0;
        qreal strongest = 0.0;
        QColor strongestColour;
        QRect dots;
        for (int x = 0; x < strip; ++x) {
            for (int y = 0; y < pToolBar->height() - 1; ++y) {
                const QColor measured = shot.pixelColor(pToolBar->mapTo(mpEditor, QPoint(x, y)));
                // A fraction rather than a match, as two-pixel dots are
                // antialiased; qAbs, as on a dark page the ink is lighter than
                // the bar
                const qreal towardsTheInk = static_cast<qreal>(qAbs(measured.green() - tokens.page.green())) / std::max(1, qAbs(tokens.mutedText.green() - tokens.page.green()));
                if (towardsTheInk < 0.1) {
                    continue;
                }
                ++inked;
                dots = dots.isNull() ? QRect(x, y, 1, 1) : dots.united(QRect(x, y, 1, 1));
                if (towardsTheInk > strongest) {
                    strongest = towardsTheInk;
                    strongestColour = measured;
                }
            }
        }
        qInfo().noquote() << qsl("  the handle is %1px wide; the grip is a %2x%3 block of %4 painted pixels at %5,%6, its strongest %7 - %8 of the way from the bar at %9 to the quiet tone at %10")
                                     .arg(QString::number(extent),
                                          QString::number(dots.width()),
                                          QString::number(dots.height()),
                                          QString::number(inked),
                                          QString::number(dots.x()),
                                          QString::number(dots.y()),
                                          strongestColour.isValid() ? strongestColour.name() : qsl("nothing at all"),
                                          QString::number(strongest, 'f', 2),
                                          tokens.page.name(),
                                          tokens.mutedText.name());

        QVERIFY2(extent >= 8, qPrintable(qsl("the handle is %1px wide, which is not room for a grip").arg(QString::number(extent))));
        // A floor rather than a measurement: six dots of two pixels across
        QVERIFY2(inked >= 6 * 4, qPrintable(qsl("the handle holds %1 painted pixels, so the six dots are not being drawn on it").arg(QString::number(inked))));
        QVERIFY2(strongest >= 0.7,
                 qPrintable(qsl("the grip's strongest pixel is %1, only %2 of the way to the quiet tone - it is a wash rather than the ink")
                                    .arg(strongestColour.name(), QString::number(strongest, 'f', 2))));
        QVERIFY2(dots.height() > dots.width(), qPrintable(qsl("the grip is %1x%2, which is not two columns of three dots").arg(QString::number(dots.width()), QString::number(dots.height()))));
    }

    void test_aColourWellIsCutToTheInputCorner()
    {
        // The well is on the options strip, which the editor opens closed
        mpEditor->setTriggerOptionsShown(true);
        QTest::qWait(50ms);

        QPushButton* pWell = mpEditor->mpTriggersMainArea->pushButtonFgColor;
        QVERIFY2(pWell != nullptr, "The trigger form has no foreground colour button");
        const QColor filled(0, 128, 0);
        pWell->setStyleSheet(dlgTriggerEditor::generateButtonStyleSheet(filled));
        QTest::qWait(50ms);
        QVERIFY2(pWell->isVisible() && pWell->width() > 8 && pWell->height() > 8, "The colour well is not on show, so there is nothing to measure");

        const QImage shot = windowShot();
        const auto at = [&](const QPoint& point) {
            return shot.pixelColor(pWell->mapTo(mpEditor, point));
        };
        const QColor fill = at(pWell->rect().center());
        const QColor corner = at(pWell->rect().topLeft());
        const QColor behind = at(QPoint(-2, pWell->height() / 2));
        qInfo().noquote() << qsl("  the well is filled %1, its corner is %2 and what it lies on is %3").arg(fill.name(), corner.name(), behind.name());

        QVERIFY2(fill.rgb() == filled.rgb(), qPrintable(qsl("the well is not filled with the colour it stands for: it is %1 where the colour is %2").arg(fill.name(), filled.name())));
        QVERIFY2(corner.rgb() != fill.rgb(), qPrintable(qsl("the well's corner is as solid as the rest of it, at %1 - it is a rectangle rather than a well").arg(corner.name())));
        QVERIFY2(corner.rgb() == behind.rgb(),
                 qPrintable(qsl("the well's corner is not cut away to what it lies on: the corner is %1 and the surface beside it %2").arg(corner.name(), behind.name())));
    }

    void test_theSoundFileClearButtonIsATintedGlyph()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QToolButton* pClear = mpEditor->mpTriggersMainArea->toolButton_clearSoundFile;
        QVERIFY2(pClear != nullptr, "The trigger form has no button to empty the sound file field");
        // The button is only shown while the trigger has a sound file
        mpEditor->showTriggerSoundFile(qsl("/tmp/EditorChromeShapeTest.wav"));
        QTest::qWait(50ms);
        QVERIFY2(!pClear->icon().isNull(), "The button carries no picture at all");

        // A fraction rather than a match, as a 1.5px stroke is antialiased;
        // qAbs, as on a dark page the words are lighter than the card
        const auto strongestInk = [&]() {
            const QImage shot = windowShot();
            qreal strongest = 0.0;
            for (int x = 0; x < pClear->width(); ++x) {
                for (int y = 0; y < pClear->height(); ++y) {
                    const QColor measured = shot.pixelColor(pClear->mapTo(mpEditor, QPoint(x, y)));
                    strongest = std::max(strongest, static_cast<qreal>(qAbs(measured.green() - tokens.card.green())) / std::max(1, qAbs(tokens.text.green() - tokens.card.green())));
                }
            }
            return strongest;
        };

        pClear->setEnabled(true);
        QTest::qWait(20ms);
        const qreal lively = strongestInk();
        pClear->setEnabled(false);
        QTest::qWait(20ms);
        const qreal quiet = strongestInk();
        qInfo().noquote() << qsl("  the glyph is drawn at %1px; its strongest pixel is %2 of the way from the card to the words switched on and %3 switched off")
                                     .arg(QString::number(pClear->iconSize().width()), QString::number(lively, 'f', 2), QString::number(quiet, 'f', 2));

        QCOMPARE(pClear->iconSize(), QSize(13, 13));
        QVERIFY2(lively > 0.5, qPrintable(qsl("the button's picture reaches only %1 of the way to the words, so it is not being drawn at all").arg(QString::number(lively, 'f', 2))));
        QVERIFY2(quiet < lively,
                 qPrintable(qsl("the button is drawn the same switched off as switched on - %1 against %2 - so it has no quiet tone")
                                    .arg(QString::number(quiet, 'f', 2), QString::number(lively, 'f', 2))));
    }
};

#include "EditorChromeShapeTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorChromeShapeTest)
