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
 * The profile strip is the design's row of chips, and every part of that claim
 * is about pixels: a word in a box, filled with the walked accent and written on
 * in white while it is the one on show on a light page and washed in the accent
 * on a dark one, a lighter wash under the pointer, the cross trailing the word
 * at the size the recipe names, and a connection indicator inked from the state
 * colours. None of it comes from a stylesheet - TStyle paints it - so nothing
 * but a grab says whether it happened.
 *
 * The chips are also measured here rather than merely looked at: the tab's
 * height is the word's plus the recipe's padding, the chips lie side by side and
 * share the whole width of the bar between them, and the cross, the word and the
 * dot share one centre line. That last is the light-mode complaint the
 * platform's own tabs earned - a close cross visibly higher than the word
 * beside it.
 *
 * Both appearances, because an ink that reads in one and vanishes in the other
 * is exactly the defect the tokens exist to prevent.
 *
 * Run with: ctest -R ProfileTabBarStyleTest -V
 */

#include <QAbstractButton>
#include <QApplication>
#include <QFontMetrics>
#include <QHash>
#include <QHoverEvent>
#include <QImage>
#include <QPalette>
#include <QResizeEvent>
#include <QStyleOptionTab>
#include <QtTest/QtTest>

#include "DarkTheme.h"
#include "TTabBar.h"
#include "uiDesign.h"

#include "GroupedTest.h"

// initStyleOption() is QTabBar's to call, and what the style is handed is the
// only place the text's rectangle can be read from
class ExposedTabBar : public TTabBar
{
public:
    using QTabBar::initStyleOption;
    using TTabBar::TTabBar;
};

class ProfileTabBarStyleTest : public QObject
{
    Q_OBJECT

private:
    ExposedTabBar* mpTabBar = nullptr;
    QPalette mSavedPalette;

    // How far a sampled pixel may sit from the colour the tokens mix, per
    // channel: a wash is composited rather than blitted, so the two agree to a
    // level or two rather than exactly
    static constexpr int scmInkTolerance = 2;
    // Wider than three short names need, so that a chip drawn only as wide as
    // its word rather than as the share of the bar it was given would be caught
    static constexpr int scmBarWidth = 600;
    // What the word naming the profile on show has to reach against the wash it
    // is written on: short of the 7:1 the ink is mixed for, because the glyph's
    // own edges are antialiased and only its stems are at full ink
    static constexpr qreal scmChosenWordFloor = 6.5;
    // ...and the same allowance where the chip is filled rather than washed,
    // said as a fraction of what the field's white itself reads at on that
    // fill: a rasteriser that leaves no pixel at full coverage still has to
    // bring the word most of the way there
    static constexpr qreal scmOnFillWordReach = 0.9;

    // What a chip the reader chose is filled with on a light page: the accent
    // taken towards black until the field's own white reads on it at the floor
    // every word of the design clears. Mixed here rather than asked of TStyle,
    // so the two are two readings of the same rule rather than one reading of
    // itself.
    static QColor chosenFillOnLightPage(const uiDesign::ThemeTokens& tokens) { return uiDesign::readableOn(tokens.field, tokens.accent, tokens.text, uiDesign::scmTextMinimumRatio); }

    // Qt composites a wash rather than blitting it, so what the reader sees is
    // the mix rather than the colour the tokens answer with
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

    static QString saidAs(const QColor& colour) { return colour.name(QColor::HexArgb); }

    static QString saidAs(const QRect& box) { return qsl("%1,%2 %3x%4").arg(QString::number(box.left()), QString::number(box.top()), QString::number(box.width()), QString::number(box.height())); }

    // A widget that was never shown keeps its resize pending until something
    // renders it, and QTabBar lays its tabs out on that event - so without this
    // the chips would be measured for a top-level's default width rather than
    // for the one the bar was given, and the two differ now that the chips share
    // that width between them
    static void layOutAtItsOwnSize(QTabBar* pTabBar)
    {
        QResizeEvent resized(pTabBar->size(), QSize());
        QApplication::sendEvent(pTabBar, &resized);
        QCoreApplication::processEvents();
    }

    QImage grabTheBar() const
    {
        QCoreApplication::processEvents();
        return mpTabBar->grab().toImage().convertToFormat(QImage::Format_ARGB32);
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

    // What the chips are lying on: the colour most of the strip is, which is the
    // bar's own surface everywhere no chip is washed
    static QColor commonestColour(const QImage& image)
    {
        QHash<QRgb, int> counts;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                ++counts[image.pixel(x, y)];
            }
        }
        QRgb commonest = 0;
        int most = -1;
        for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
            if (it.value() > most) {
                most = it.value();
                commonest = it.key();
            }
        }
        return QColor(commonest);
    }

    // The chip a tab is drawn as: the tab's rectangle less the gap that tells
    // two chips apart, which TStyle takes off the trailing side
    QRect chipOf(const int index) const { return mpTabBar->tabRect(index).adjusted(0, 0, -uiDesign::scmTabGap, 0); }

    // Inside the chip's leading padding, past the accent bar a chosen chip
    // carries, where nothing is drawn but the wash
    QPoint washSpot(const int index) const
    {
        const QRect chip = chipOf(index);
        return QPoint(chip.left() + uiDesign::scmAccentBarWidth + 2, chip.center().y());
    }

    // On the accent bar down a chosen chip's leading edge, clear of the arc at
    // either end of it
    QPoint barSpot(const int index) const
    {
        const QRect chip = chipOf(index);
        return QPoint(chip.left() + 1, chip.center().y());
    }

    // Where TStyle stands the connection indicator: the design's 8px dot at the
    // start of the chip's contents, centred on the chip
    QRect indicatorOf(const int index) const
    {
        const QRect chip = chipOf(index);
        return QRect(chip.left() + uiDesign::scmInputBorderWidth + uiDesign::scmTabPaddingHorizontal,
                     chip.top() + (chip.height() - TStyle::sIndicatorDiameter) / 2,
                     TStyle::sIndicatorDiameter,
                     TStyle::sIndicatorDiameter);
    }

    QAbstractButton* crossOf(const int index) const { return qobject_cast<QAbstractButton*>(mpTabBar->tabButton(index, QTabBar::RightSide)); }

    // The pointer over a tab, said the way the platform says it. QTest::mouseMove
    // does not always reach a window nothing is compositing, so the hover event
    // itself is sent when it did not arrive.
    void hoverOver(const int index)
    {
        const QPoint over = mpTabBar->tabRect(index).center();
        QTest::mouseMove(mpTabBar, over);
        QCoreApplication::processEvents();
        QHoverEvent hover(QEvent::HoverMove, QPointF(over), QPointF(over), QPointF(-1, -1));
        QApplication::sendEvent(mpTabBar, &hover);
        QCoreApplication::processEvents();
    }

    // The two claims that are about colour rather than about geometry, so that
    // the appearance case below can make them a second time against a dark
    // palette rather than restating them. How the chip the reader chose is
    // drawn is the one thing the two appearances answer differently.
    void theChosenChipIsWashed()
    {
        const QImage strip = grabTheBar();
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QColor surface = commonestColour(strip);

        const QColor resting = sampled(strip, washSpot(2));
        QVERIFY2(readsAs(resting, surface, 0),
                 qPrintable(qsl("a tab that is not the one on show is drawn on something other than the strip's own surface: %1 rather than %2").arg(saidAs(resting), saidAs(surface))));
        const QColor restingEdge = sampled(strip, barSpot(2));
        QVERIFY2(readsAs(restingEdge, surface, 0), qPrintable(qsl("a tab that is not on show carries a bar on its leading edge: %1").arg(saidAs(restingEdge))));
        const QRect restingChip = chipOf(2);
        const QColor restingTop = sampled(strip, QPoint(restingChip.center().x(), restingChip.top()));
        QVERIFY2(readsAs(restingTop, surface, 0), qPrintable(qsl("a tab that is not on show is outlined: %1 on its top edge rather than the strip's own %2").arg(saidAs(restingTop), saidAs(surface))));

        const QRect chosenChip = chipOf(0);
        if (!tokens.darkPage) {
            // On a light page the chip is filled with the accent taken far
            // enough towards black to be written on in the field's white, as
            // the platform's own chosen tab is - so that one colour is read
            // wherever the wash, the bar and the outline each used to be, and
            // a bar or an outline in the accent would have nothing left to say
            const QColor fill = chosenFillOnLightPage(tokens);
            struct Spot
            {
                const char* where;
                QPoint at;
            };
            const Spot spots[]{{"inside its leading padding", washSpot(0)}, {"where a bar would stand", barSpot(0)}, {"on its top edge", QPoint(chosenChip.center().x(), chosenChip.top())}};
            for (const Spot& spot : spots) {
                const QColor read = sampled(strip, spot.at);
                QVERIFY2(readsAs(read, fill),
                         qPrintable(qsl("the tab on show is not filled with %1, the accent %2 walked until the field's white reads on it: %3 it is %4")
                                            .arg(saidAs(fill), saidAs(tokens.accent), QString::fromUtf8(spot.where), saidAs(read))));
            }
            return;
        }

        const QColor chosen = sampled(strip, washSpot(0));
        const QColor wanted = composited(tokens.accentWash, surface);
        QVERIFY2(readsAs(chosen, wanted), qPrintable(qsl("the tab on show is not washed in the accent: %1 where the accent wash over %2 is %3").arg(saidAs(chosen), saidAs(surface), saidAs(wanted))));

        // ...and carries the sidebar's bar down its leading edge, since on a
        // dark page the wash alone did not say which profile was on show
        const QColor bar = sampled(strip, barSpot(0));
        QVERIFY2(readsAs(bar, tokens.accent), qPrintable(qsl("the tab on show has no accent bar on its leading edge: %1 where the accent is %2").arg(saidAs(bar), saidAs(tokens.accent))));

        // ...and is outlined all the way round in the same accent
        const QColor edge = sampled(strip, QPoint(chosenChip.center().x(), chosenChip.top()));
        QVERIFY2(readsAs(edge, tokens.accent), qPrintable(qsl("the tab on show has no accent outline: %1 on its top edge where the accent is %2").arg(saidAs(edge), saidAs(tokens.accent))));
    }

    // On a dark page the word naming the profile on show is walked past the
    // floor every other word of the design is held to: the accent ink at that
    // floor was mixed against the deepest wash, and on this strip's page it
    // fades into its own. On a light page the fill is what was walked instead,
    // far enough for the word to be the field's own white. Either way what is
    // read is the strongest pixel the word marks, since only the glyph's stems
    // are at full ink.
    void theChosenWordIsStrong()
    {
        const QImage strip = grabTheBar();
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        // What the word is actually read against: the walked fill where the
        // chip is filled, and the wash as it composites over the page where it
        // is washed
        const QColor behind = tokens.darkPage ? composited(tokens.accentWash, commonestColour(strip)) : chosenFillOnLightPage(tokens);

        QStyleOptionTab option;
        mpTabBar->initStyleOption(&option, 0);
        const QRect word = mpTabBar->style()->subElementRect(QStyle::SE_TabBarTabText, &option, mpTabBar);
        const qreal ratio = strip.devicePixelRatio();
        const QRect scanned = QRect(qRound(word.left() * ratio), qRound(word.top() * ratio), qRound(word.width() * ratio), qRound(word.height() * ratio)) & strip.rect();
        QVERIFY2(!scanned.isEmpty(), "the chosen chip leaves its word no room at all, so there is nothing to read");

        qreal strongest = 0.0;
        QColor strongestInk;
        for (int y = scanned.top(); y <= scanned.bottom(); ++y) {
            for (int x = scanned.left(); x <= scanned.right(); ++x) {
                const QColor pixel(strip.pixel(x, y));
                const qreal reads = uiDesign::contrastRatio(pixel, behind);
                if (reads > strongest) {
                    strongest = reads;
                    strongestInk = pixel;
                }
            }
        }

        if (!tokens.darkPage) {
            const qreal inkReads = uiDesign::contrastRatio(tokens.field, behind);
            const bool atFullInk = readsAs(strongestInk, tokens.field);
            if (!atFullInk) {
                qDebug("no pixel of the word is at the field's own white, so it is read by how far it reaches instead");
            }
            QVERIFY2(atFullInk || strongest >= scmOnFillWordReach * inkReads,
                     qPrintable(qsl("the word on the tab on show is not written in the field's white %1 on the fill %2: its strongest pixel is %3, reaching %4:1, where that white reads at %5:1")
                                        .arg(saidAs(tokens.field), saidAs(behind), saidAs(strongestInk), QString::number(strongest, 'f', 2), QString::number(inkReads, 'f', 2))));
            return;
        }

        const QColor walked = uiDesign::readableOn(behind, tokens.accentText, tokens.text, TStyle::sChosenWordMinimumRatio);
        QVERIFY2(strongest >= scmChosenWordFloor,
                 qPrintable(qsl("the word on the tab on show reaches only %1:1 against its wash %2 - its strongest pixel is %3, where the ink at the design's floor %4 reads at %5:1 and the "
                                "walked ink %6 at %7:1")
                                    .arg(QString::number(strongest, 'f', 2),
                                         saidAs(behind),
                                         saidAs(strongestInk),
                                         saidAs(tokens.accentText),
                                         QString::number(uiDesign::contrastRatio(tokens.accentText, behind), 'f', 2),
                                         saidAs(walked),
                                         QString::number(uiDesign::contrastRatio(walked, behind), 'f', 2))));
    }

    void theIndicatorIsAStateColour()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();

        // A tab that is not the one on show lies on the strip's own page, so
        // that is what its dot is walked against
        mpTabBar->setTabConnectionIndicator(2, TabConnectionIndicator::Connected);
        QImage strip = grabTheBar();
        const QColor dot = sampled(strip, indicatorOf(2).center());
        const QColor wantedDot = uiDesign::readableOn(tokens.page, uiDesign::stateColor(uiDesign::scmStateHue_ok, tokens.darkPage), tokens.text, uiDesign::scmQuietMinimumRatio);
        QVERIFY2(readsAs(dot, wantedDot), qPrintable(qsl("a connected profile's dot is not the state colour for something working: %1 rather than %2").arg(saidAs(dot), saidAs(wantedDot))));

        mpTabBar->setTabConnectionIndicator(2, TabConnectionIndicator::Disconnected);
        strip = grabTheBar();
        const QRect box = indicatorOf(2);
        const QColor ring = sampled(strip, QPoint(box.left(), box.center().y()));
        QVERIFY2(readsAs(ring, tokens.mutedText), qPrintable(qsl("a disconnected profile's ring is not drawn in the chrome tone: %1 rather than %2").arg(saidAs(ring), saidAs(tokens.mutedText))));

        mpTabBar->setTabConnectionIndicator(2, TabConnectionIndicator::None);
        QCoreApplication::processEvents();

        if (tokens.darkPage) {
            return;
        }

        // ...while on a light page the chip the reader chose is filled, and what
        // stands on that fill is read against it: a dot at the page's reading is
        // swallowed by it, and so is a ring in the chrome tone
        const QColor fill = chosenFillOnLightPage(tokens);
        mpTabBar->setTabConnectionIndicator(0, TabConnectionIndicator::Connected);
        strip = grabTheBar();
        const QColor chosenDot = sampled(strip, indicatorOf(0).center());
        const QColor wantedChosenDot = uiDesign::readableOn(fill, uiDesign::stateColor(uiDesign::scmStateHue_ok, tokens.darkPage), tokens.text, uiDesign::scmQuietMinimumRatio);
        QVERIFY2(readsAs(chosenDot, wantedChosenDot),
                 qPrintable(qsl("the dot on the tab on show is not the state colour walked against the fill %1 it stands on: %2 rather than %3")
                                    .arg(saidAs(fill), saidAs(chosenDot), saidAs(wantedChosenDot))));

        mpTabBar->setTabConnectionIndicator(0, TabConnectionIndicator::Disconnected);
        strip = grabTheBar();
        const QRect chosenBox = indicatorOf(0);
        const QColor chosenRing = sampled(strip, QPoint(chosenBox.left(), chosenBox.center().y()));
        QVERIFY2(readsAs(chosenRing, tokens.field),
                 qPrintable(qsl("the ring on the tab on show is not drawn in the field's white its word is: %1 rather than %2").arg(saidAs(chosenRing), saidAs(tokens.field))));

        mpTabBar->setTabConnectionIndicator(0, TabConnectionIndicator::None);
        QCoreApplication::processEvents();
    }

private slots:
    void init()
    {
        mSavedPalette = QApplication::palette();
        mpTabBar = new ExposedTabBar(nullptr);
        // What mudlet.cpp sets, and in that order: the crosses have to exist
        // before the tabs do, or QTabBar makes them itself and never says so
        mpTabBar->setMovable(true);
        mpTabBar->setTabsClosable(true);
        // The bar is a window of its own here, where the main window's frame is
        // what fills the surface behind the chips - so it is asked to fill it
        mpTabBar->setAutoFillBackground(true);
        for (const QString& profileName : {qsl("Alpha"), qsl("Bravo"), qsl("Delta")}) {
            mpTabBar->setTabData(mpTabBar->addTab(profileName), profileName);
        }
        mpTabBar->resize(scmBarWidth, mpTabBar->sizeHint().height());
        layOutAtItsOwnSize(mpTabBar);
    }

    void cleanup()
    {
        delete mpTabBar;
        mpTabBar = nullptr;
        QApplication::setPalette(mSavedPalette);
    }

    // The cross is on the trailing edge on every platform, and it is the box the
    // recipe names rather than whatever the platform's own close indicator is
    void test_theCrossTrailsTheWordAndIsTheDesignsSize()
    {
        const QSize wanted(uiDesign::scmTabCloseBoxSize, uiDesign::scmTabCloseBoxSize);
        for (int i = 0; i < mpTabBar->count(); ++i) {
            QAbstractButton* pCross = crossOf(i);
            QVERIFY2(pCross, qPrintable(qsl("tab %1 carries no cross on its trailing edge").arg(QString::number(i))));
            QVERIFY2(!mpTabBar->tabButton(i, QTabBar::LeftSide), qPrintable(qsl("tab %1 carries a cross in front of its word, where the platform puts it").arg(QString::number(i))));
            QVERIFY2(pCross->size() == wanted,
                     qPrintable(qsl("the cross on tab %1 is %2x%3 rather than the %4x%4 box the design draws it in")
                                        .arg(QString::number(i), QString::number(pCross->size().width()), QString::number(pCross->size().height()), QString::number(uiDesign::scmTabCloseBoxSize))));
        }

        QVERIFY2(!mpTabBar->drawBase(), "the strip still draws the band a platform fills the whole bar with behind the tabs");
        QVERIFY2(mpTabBar->expanding(), "the chips are held to the width of their words rather than sharing the width of the window, as the profile strip always has");
        QVERIFY2(mpTabBar->testAttribute(Qt::WA_Hover), "the bar asks for no hover events, so no chip can ever be washed under the pointer");
    }

    // The light-mode complaint: the platform drew the cross visibly higher than
    // the word beside it
    void test_everythingIsCentredOnTheChip()
    {
        for (int i = 0; i < mpTabBar->count(); ++i) {
            const int chipCentre = mpTabBar->tabRect(i).center().y();

            QAbstractButton* pCross = crossOf(i);
            QVERIFY(pCross);
            const int crossCentre = pCross->geometry().center().y();
            QVERIFY2(std::abs(crossCentre - chipCentre) <= 1,
                     qPrintable(qsl("the cross on tab %1 sits %2px off the chip's centre line, where the word is").arg(QString::number(i), QString::number(std::abs(crossCentre - chipCentre)))));

            QStyleOptionTab option;
            mpTabBar->initStyleOption(&option, i);
            const QRect text = mpTabBar->style()->subElementRect(QStyle::SE_TabBarTabText, &option, mpTabBar);
            QVERIFY2(std::abs(text.center().y() - chipCentre) <= 1,
                     qPrintable(qsl("the word on tab %1 sits %2px off the chip's centre line").arg(QString::number(i), QString::number(std::abs(text.center().y() - chipCentre)))));
            QVERIFY2(text.right() < pCross->geometry().left(), qPrintable(qsl("the word on tab %1 runs under the cross rather than stopping in front of it").arg(QString::number(i))));
        }
    }

    void test_theChipIsTheRecipesSize()
    {
        const int wantedHeight = mpTabBar->fontMetrics().height() + 2 * (uiDesign::scmTabPaddingVertical + uiDesign::scmInputBorderWidth);
        QCOMPARE(mpTabBar->tabSizeHint(0).height(), wantedHeight);

        QCOMPARE(mpTabBar->tabRect(0).left(), 0);
        for (int i = 1; i < mpTabBar->count(); ++i) {
            QVERIFY2(mpTabBar->tabRect(i).left() == mpTabBar->tabRect(i - 1).right() + 1,
                     qPrintable(qsl("chip %1 starts at %2 rather than where the one before it ends, %3 - the gap between them is inside a chip's own rectangle")
                                        .arg(QString::number(i), QString::number(mpTabBar->tabRect(i).left()), QString::number(mpTabBar->tabRect(i - 1).right() + 1))));
        }

        // The chips share the bar between them rather than being held to the
        // width of their words, so the last one ends where the bar does
        const int lastEdge = mpTabBar->tabRect(mpTabBar->count() - 1).right();
        QVERIFY2(lastEdge == mpTabBar->width() - 1,
                 qPrintable(qsl("three chips end at %1 on a %2px bar, so they are not sharing its width between them").arg(QString::number(lastEdge), QString::number(mpTabBar->width()))));
    }

    // Bold already says something on this strip - a profile with new output -
    // and the chosen chip's word is bold as well, the way the settings sidebar
    // draws its chosen row; the wash is what tells the two readings apart. What
    // that costs is that every tab has to be measured bold, or the strip would
    // shift sideways each time the choice moved.
    void test_theChosenWordIsBoldAndTheStripDoesNotShiftForIt()
    {
        // The same word on both chips, so the only thing that can differ
        // between the two readings is which of them is the one on show
        ExposedTabBar bar(nullptr);
        bar.setAutoFillBackground(true);
        bar.setTabData(bar.addTab(qsl("Same")), qsl("Same-A"));
        bar.setTabData(bar.addTab(qsl("Same")), qsl("Same-B"));
        bar.resize(scmBarWidth, bar.sizeHint().height());
        layOutAtItsOwnSize(&bar);
        bar.setCurrentIndex(0);
        QCoreApplication::processEvents();

        const auto wordBox = [&bar](const int index) {
            QStyleOptionTab option;
            bar.initStyleOption(&option, index);
            return bar.style()->subElementRect(QStyle::SE_TabBarTabText, &option, &bar);
        };

        const QImage strip = bar.grab().toImage().convertToFormat(QImage::Format_ARGB32);
        const qreal ratio = strip.devicePixelRatio();
        const auto separation = [](const QColor& from, const QColor& to) {
            return std::abs(from.red() - to.red()) + std::abs(from.green() - to.green()) + std::abs(from.blue() - to.blue());
        };
        // How much of a word's box the word marks, read against the ink that
        // word is written in rather than against a fixed distance: a chosen
        // chip's word is in the accent's ink and a resting one's in the chrome
        // tone, and a fixed threshold would count the stronger ink as more word
        const auto inkCoverage = [&strip, ratio, &separation](const QRect& box, const QColor& ink) {
            const QRect scanned = QRect(qRound(box.left() * ratio), qRound(box.top() * ratio), qRound(box.width() * ratio), qRound(box.height() * ratio)) & strip.rect();
            QHash<QRgb, int> counts;
            for (int y = scanned.top(); y <= scanned.bottom(); ++y) {
                for (int x = scanned.left(); x <= scanned.right(); ++x) {
                    ++counts[strip.pixel(x, y)];
                }
            }
            QRgb commonest = 0;
            int most = -1;
            for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
                if (it.value() > most) {
                    most = it.value();
                    commonest = it.key();
                }
            }
            const QColor behind(commonest);
            const int reach = separation(behind, ink);
            int touched = 0;
            for (int y = scanned.top(); y <= scanned.bottom(); ++y) {
                for (int x = scanned.left(); x <= scanned.right(); ++x) {
                    if (2 * separation(QColor(strip.pixel(x, y)), behind) >= reach) {
                        ++touched;
                    }
                }
            }
            return touched;
        };

        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        // Whichever ink that chip's word is actually written in, or the reading
        // above counts the whole box as word
        const int chosenInk = inkCoverage(wordBox(0), tokens.darkPage ? tokens.accentText : tokens.field);
        const int restingInk = inkCoverage(wordBox(1), tokens.mutedText);
        QVERIFY2(restingInk > 0, "the resting chip's word marks nothing at all, so there is no reading to compare the chosen one against");
        QVERIFY2(100 * chosenInk >= 115 * restingInk,
                 qPrintable(qsl("the word on the chosen chip marks %1 pixels of its box against the %2 the same word marks on the resting chip beside it, so it is not drawn any bolder")
                                    .arg(QString::number(chosenInk), QString::number(restingInk))));

        // ...and the room every chip leaves its word is the bold word's, so the
        // strip stands still when the choice moves. A pixel of slack because Qt
        // measures a tab's text with QFontMetrics::size() and this with
        // horizontalAdvance().
        QFont bold = bar.font();
        bold.setBold(true);
        for (int i = 0; i < bar.count(); ++i) {
            const int boldAdvance = QFontMetrics(bold).horizontalAdvance(bar.tabText(i));
            QVERIFY2(boldAdvance > bar.fontMetrics().horizontalAdvance(bar.tabText(i)), "bold and regular measure this word the same here, so nothing below could be told apart");
            QVERIFY2(wordBox(i).width() + 1 >= boldAdvance,
                     qPrintable(qsl("chip %1 leaves its word %2px, less than the %3px that word takes in bold - so the chip a reader chooses is measured narrower than it is drawn")
                                        .arg(QString::number(i), QString::number(wordBox(i).width()), QString::number(boldAdvance))));
        }

        QList<QRect> before;
        for (int i = 0; i < bar.count(); ++i) {
            before << bar.tabRect(i);
        }
        bar.setCurrentIndex(1);
        QCoreApplication::processEvents();
        for (int i = 0; i < bar.count(); ++i) {
            QVERIFY2(bar.tabRect(i) == before.at(i),
                     qPrintable(qsl("chip %1 stood at %2 and moved to %3 when the choice moved to the chip beside it, so the whole strip steps sideways on every switch")
                                        .arg(QString::number(i), saidAs(before.at(i)), saidAs(bar.tabRect(i)))));
        }
    }

    void test_theChosenChipIsWashedInTheAccent() { theChosenChipIsWashed(); }

    void test_theChosenWordIsWalkedPastTheFloor() { theChosenWordIsStrong(); }

    void test_theIndicatorIsAStateColour() { theIndicatorIsAStateColour(); }

    // The cross is chrome, so it is mutedText - not the near-black x a platform
    // draws a closable tab with.
    //
    // Read as how far from the surface the ink gets rather than as a colour
    // match: the mark is a 1.5px stroke drawn at 8px, so antialiasing leaves no
    // pixel at full coverage and the darkest one is a blend rather than the ink
    // itself. What tells the two crosses apart is how far that blend goes - a
    // cross inked in the palette's own text colour overshoots the chrome tone,
    // which is by definition pulled back towards the surface.
    void test_theCrossIsDrawnInChromeInk()
    {
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        QAbstractButton* pCross = crossOf(2);
        QVERIFY(pCross);
        const QImage drawn = pCross->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        const QColor surface = commonestColour(drawn);

        const auto distance = [](const QColor& from, const QColor& to) {
            return std::abs(from.red() - to.red()) + std::abs(from.green() - to.green()) + std::abs(from.blue() - to.blue());
        };
        const int chromeReach = distance(surface, tokens.mutedText);
        QVERIFY2(chromeReach > 0, "the chrome tone is the surface itself here, so nothing could be told from a drawn mark");

        int furthest = 0;
        QColor furthestInk;
        QRect marked;
        const qreal ratio = drawn.devicePixelRatio();
        for (int y = 0; y < drawn.height(); ++y) {
            for (int x = 0; x < drawn.width(); ++x) {
                const QColor pixel(drawn.pixel(x, y));
                if (distance(surface, pixel) > furthest) {
                    furthest = distance(surface, pixel);
                    furthestInk = pixel;
                }
                if (distance(surface, pixel) > 3 * scmInkTolerance) {
                    marked |= QRect(x, y, 1, 1);
                }
            }
        }

        // The mark is the recipe's, not whatever x a platform draws a closable
        // tab with: it is scmTabCloseGlyphSize across inside the larger box, and
        // a platform's own fills nearly all of that box instead
        const QSize markSize(qRound(marked.width() / ratio), qRound(marked.height() / ratio));
        QVERIFY2(std::abs(markSize.width() - uiDesign::scmTabCloseGlyphSize) <= 2 && std::abs(markSize.height() - uiDesign::scmTabCloseGlyphSize) <= 2,
                 qPrintable(qsl("the mark inside the cross's box measures %1x%2 rather than the %3x%3 the recipe draws it at, so it is not this design's x")
                                    .arg(QString::number(markSize.width()), QString::number(markSize.height()), QString::number(uiDesign::scmTabCloseGlyphSize))));

        QVERIFY2(furthest * 2 >= chromeReach,
                 qPrintable(qsl("the cross barely marks its box: the furthest pixel from the surface %1 is %2, only %3 of the %4 the chrome tone %5 would reach")
                                    .arg(saidAs(surface), saidAs(furthestInk), QString::number(furthest), QString::number(chromeReach), saidAs(tokens.mutedText))));
        QVERIFY2(furthest <= chromeReach + 3 * scmInkTolerance,
                 qPrintable(qsl("the cross is inked past the chrome tone %1 - its furthest pixel %2 is %3 from the surface %4, where the chrome tone is %5, so it is drawn in "
                                "something stronger such as the palette's own %6")
                                    .arg(saidAs(tokens.mutedText),
                                         saidAs(furthestInk),
                                         QString::number(furthest),
                                         saidAs(surface),
                                         QString::number(chromeReach),
                                         saidAs(QApplication::palette().color(QPalette::WindowText)))));

        if (tokens.darkPage) {
            return;
        }

        // The cross on the tab on show is read the same way, against the fill
        // the chip carries on a light page rather than against the page: the
        // chrome tone it wears at rest would be swallowed by that fill.
        // Read off the strip rather than off the button's own grab, since the
        // button paints the page behind its mark and not the chip under it.
        QAbstractButton* pChosenCross = crossOf(0);
        QVERIFY(pChosenCross);
        const QImage strip = grabTheBar();
        const qreal stripRatio = strip.devicePixelRatio();
        const QRect box = pChosenCross->geometry();
        const QRect scanned = QRect(qRound(box.left() * stripRatio), qRound(box.top() * stripRatio), qRound(box.width() * stripRatio), qRound(box.height() * stripRatio)) & strip.rect();
        QVERIFY2(!scanned.isEmpty(), "the cross on the tab on show stands outside the strip, so there is nothing to read");

        const QColor fill = chosenFillOnLightPage(tokens);
        const QColor filled = commonestColour(strip.copy(scanned));
        QVERIFY2(readsAs(filled, fill), qPrintable(qsl("the cross on the tab on show is not standing on the fill: what is behind it is %1 rather than %2").arg(saidAs(filled), saidAs(fill))));

        const int inkReach = distance(fill, tokens.field);
        QVERIFY2(inkReach > 0, "the field's white is the fill itself here, so nothing could be told from a drawn mark");

        int chosenFurthest = 0;
        QColor chosenFurthestInk;
        for (int y = scanned.top(); y <= scanned.bottom(); ++y) {
            for (int x = scanned.left(); x <= scanned.right(); ++x) {
                const QColor pixel(strip.pixel(x, y));
                if (distance(fill, pixel) > chosenFurthest) {
                    chosenFurthest = distance(fill, pixel);
                    chosenFurthestInk = pixel;
                }
            }
        }

        QVERIFY2(chosenFurthest * 2 >= inkReach,
                 qPrintable(qsl("the cross on the tab on show barely marks the fill %1: its furthest pixel %2 gets %3 from it, only part of the %4 the field's white %5 would reach")
                                    .arg(saidAs(fill), saidAs(chosenFurthestInk), QString::number(chosenFurthest), QString::number(inkReach), saidAs(tokens.field))));
        // ...and further from the fill than the chrome tone it wears at rest
        // could get on it even at full coverage, which is what tells a cross
        // written for the fill from one still inked in the page's grey
        const int chromeOnTheFill = distance(fill, tokens.mutedText);
        QVERIFY2(chosenFurthest > chromeOnTheFill + scmInkTolerance,
                 qPrintable(qsl("the cross on the tab on show is still inked in the chrome tone %1 rather than in the field's white %2: its furthest pixel %3 gets %4 from the fill, where the "
                                "chrome tone itself gets %5")
                                    .arg(saidAs(tokens.mutedText), saidAs(tokens.field), saidAs(chosenFurthestInk), QString::number(chosenFurthest), QString::number(chromeOnTheFill))));
        QVERIFY2(chosenFurthest <= inkReach + 3 * scmInkTolerance,
                 qPrintable(qsl("the cross on the tab on show is inked past the field's white %1 on the fill %2: its furthest pixel %3 is %4 from the fill, where that white is %5")
                                    .arg(saidAs(tokens.field), saidAs(fill), saidAs(chosenFurthestInk), QString::number(chosenFurthest), QString::number(inkReach))));
    }

    void test_theHoveredChipIsWashed()
    {
        hoverOver(1);
        const QImage strip = grabTheBar();
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QColor surface = commonestColour(strip);

        const QColor hovered = sampled(strip, washSpot(1));
        const QColor wanted = composited(tokens.hoverWash, surface);
        QVERIFY2(readsAs(hovered, wanted), qPrintable(qsl("the chip under the pointer is not washed: %1 where the hover wash over %2 is %3").arg(saidAs(hovered), saidAs(surface), saidAs(wanted))));
    }

    // The same colour claims against the dark palette. An ink that reads in
    // one appearance and vanishes in the other is the defect the tokens exist
    // to prevent, and only a second pass catches it.
    void test_bothAppearances()
    {
        theChosenChipIsWashed();
        theChosenWordIsStrong();
        theIndicatorIsAStateColour();

        DarkTheme theme;
        QPalette dark = QApplication::palette();
        theme.polish(dark);
        QApplication::setPalette(dark);
        QCoreApplication::processEvents();
        QVERIFY2(uiDesign::themeTokens().darkPage, "the dark palette did not take, so the second pass would repeat the first");

        theChosenChipIsWashed();
        theChosenWordIsStrong();
        theIndicatorIsAStateColour();
    }
};

#include "ProfileTabBarStyleTest.moc"
MUDLET_GROUPED_TEST_MAIN(ProfileTabBarStyleTest)
