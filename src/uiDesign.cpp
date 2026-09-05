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

#include "uiDesign.h"

#include "TKeySequenceEdit.h"
#include "utils.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmapCache>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleOptionGroupBox>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolButton>
#include <QTransform>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

namespace uiDesign {

namespace {
// The saturation and the two lightnesses every state colour is mixed at
constexpr qreal scmStateSaturation = 0.55;
constexpr qreal scmStateLightnessOnDark = 0.58;
constexpr qreal scmStateLightnessOnLight = 0.36;
// The hue and saturation of a highlighter pen; only the lightness comes off the
// page, the way a state colour is mixed
constexpr qreal scmMarkerHue = 0.13;
constexpr qreal scmMarkerSaturation = 0.9;
constexpr qreal scmMarkerLightnessOnDark = 0.34;
constexpr qreal scmMarkerLightnessOnLight = 0.72;
// How far a card is lifted off the page it lies on. A dark page takes a fraction
// of the light one: the same step in absolute lightness reads as a much larger
// one where there is less light to begin with.
constexpr qreal scmCardLiftOnDark = 0.06;
constexpr qreal scmCardLiftOnLight = 0.55;
// ...and what a card has to gain over its page to read as lifted at all, before
// the page is the one that has to move. macOS answers white to Window and Base
// alike on its light appearance, and a card lightened off a white page lands
// back on it.
constexpr int scmMinimumCardLift = 6;
constexpr qreal scmPageDropUnderCard = 0.05;
// How far a pane is lifted off the page, said as a fraction of the card's lift
// rather than of the room above the page: it is the smallest step the depth
// model has, and measuring it against the card is what keeps it a small one on
// either theme without a second pair of numbers to keep in step.
constexpr qreal scmPaneLiftTowardsCard = 0.2;
// ...and how far a separator is taken below the page, towards black. A dark page
// has room under it for a groove; a light one is near enough to white that the
// same drop would draw a grey rule across the window rather than a seam between
// two panes.
constexpr qreal scmSeparatorDropOnDark = 0.36;
constexpr qreal scmSeparatorDropOnLight = 0.10;
// How far the words are pulled back towards the page they are written on. A
// palette answers pure white to WindowText on a dark theme and pure black on a
// light one, and neither is a colour a page of text is set in: the white glares
// and the black is heavier than anything else in the window. A sixteenth of the
// way back leaves a dark theme at the off-white the design was drawn in, and a
// tenth is all a light one can give up before the smaller type starts to fade.
// Every muted tone is mixed from this, so the whole text scale moves with it.
constexpr qreal scmTextSoftenOnDark = 0.16;
constexpr qreal scmTextSoftenOnLight = 0.10;
// ...and how far a field is lifted back towards the page. QPalette::Base is
// near-black on a dark theme, which reads as a hole cut in the window rather
// than as a surface sunk into it. A light theme's Base is white, and lifting
// white towards a grey page only muddies it - so the lift is the dark theme's
// alone.
constexpr qreal scmFieldLiftOnDark = 0.30;
// What an unavailable word is written in: a third of the way from the page to
// the words on it, so it reads as a word switched off rather than as one merely
// quiet. That fraction alone is not enough on a light theme - it lands about
// 2.3:1 on the page, which is a word the reader has to hunt for - so it is a
// starting point rather than the answer, and the mixture is walked on towards
// the text until it clears the floor an inactive word is held to on every
// surface such a word is ever set on.
constexpr qreal scmDisabledTextWeight = 0.32;
// ...and where a quieter weight of the body text starts, before the same walk
// takes it to the floor body text itself keeps
constexpr qreal scmMutedTextWeight = 0.70;
// How far the accent is taken towards the end of the lightness scale its page is
// not at, before the same walk carries it clear of a wash of itself
constexpr qreal scmAccentTextLiftOnDark = 0.45;
constexpr qreal scmAccentTextDropOnLight = 0.2;
// How far each of those walks moves the tone on every pass
constexpr qreal scmToneWeightStep = 0.02;
// How many stops readableOn() tries between the colour it was asked for and the
// end of the scale it is walking towards
constexpr int scmReadabilitySteps = 12;
constexpr int scmInputPaddingHorizontal = 6;
// The room the arrows are given at a control's right edge, and how big the
// arrows drawn in it are
constexpr int scmInputDropDownWidth = 18;
constexpr int scmInputArrowSize = 9;
constexpr int scmInputStepperWidth = 16;
constexpr int scmInputStepperArrowSize = 8;
// How far the name beside the accent bar is held off the item's left edge in
// all. The bar is a border rather than a gap, so the padding written into the
// rules is what it leaves of that gutter.
constexpr int scmSidebarItemGutter = 10;
// The ring drawn round the pill while the list holds the keyboard. It is taken
// out of the item rather than added to it, a pixel off either side, so the
// padding gives both back and the name stays where it was.
constexpr int scmSidebarFocusRingWidth = 1;
// A styled check indicator has no size of its own to fall back on. How far right
// of the frame edge this leaves a checkable card's title is not a constant to go
// with it: styles disagree on the room after an indicator, which is between the
// box and the words rather than before both.
constexpr int scmCardIndicatorSize = 13;
// How far that indicator's outline is taken from the card towards the words on
// it. A dark card needs more of the step than a light one, which has the page's
// own contrast to fall back on.
constexpr qreal scmCardIndicatorOutlineOnDark = 0.55;
constexpr qreal scmCardIndicatorOutlineOnLight = 0.45;

// One channel of a colour, straightened out of the curve a display applies to it
qreal linearised(const qreal channel)
{
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

qreal relativeLuminance(const QColor& color)
{
    return 0.2126 * linearised(color.redF()) + 0.7152 * linearised(color.greenF()) + 0.0722 * linearised(color.blueF());
}
} // namespace

bool alignInLayoutTree(QLayout* pLayout, const QWidget* pWidget, const Qt::Alignment alignment)
{
    for (int i = 0, total = pLayout->count(); i < total; ++i) {
        QLayoutItem* pItem = pLayout->itemAt(i);
        if (pItem->widget() == pWidget) {
            pItem->setAlignment(alignment);
            return true;
        }
        if (QLayout* pChildLayout = pItem->layout(); pChildLayout && alignInLayoutTree(pChildLayout, pWidget, alignment)) {
            return true;
        }
    }
    return false;
}

bool removeFromLayoutTree(QLayout* pLayout, QWidget* pWidget)
{
    for (int i = 0, total = pLayout->count(); i < total; ++i) {
        QLayoutItem* pItem = pLayout->itemAt(i);
        if (pItem->widget() == pWidget) {
            delete pLayout->takeAt(i);
            pLayout->invalidate();
            return true;
        }
        if (QLayout* pChildLayout = pItem->layout(); pChildLayout && removeFromLayoutTree(pChildLayout, pWidget)) {
            return true;
        }
    }
    return false;
}

void detachFromLayout(QWidget* pWidget)
{
    QWidget* pParent = pWidget->parentWidget();
    if (QLayout* pLayout = pParent ? pParent->layout() : nullptr; pLayout) {
        removeFromLayoutTree(pLayout, pWidget);
    }
}

// A layout tells the layouts above it that it has changed by *posting* a layout
// request, and nothing between a change and the measurement that judges it runs
// an event loop to deliver one. Both halves below are needed: invalidate() drops
// what a layout worked out about its items, while what it caches about a
// *widget* - the size hints, in the layout item it made for it - goes only with
// updateGeometry() on that widget.
void invalidateLayoutsUpTo(QWidget* pWidget, const QWidget* pTop)
{
    for (QWidget* pAncestor = pWidget; pAncestor; pAncestor = pAncestor->parentWidget()) {
        if (QLayout* pLayout = pAncestor->layout(); pLayout) {
            pLayout->invalidate();
        }
        pAncestor->updateGeometry();
        if (pAncestor == pTop) {
            return;
        }
    }
}

void markAsShellSurface(QWidget* pWidget)
{
    pWidget->setProperty("settingsSurface", true);
}

void insertGridRowAtTop(QGridLayout* pGrid, QWidget* pWidget)
{
    const int rows = pGrid->rowCount();
    const int columns = std::max(1, pGrid->columnCount());
    QList<std::pair<int, int>> rowProperties;
    rowProperties.reserve(rows);
    for (int row = 0; row < rows; ++row) {
        rowProperties.append({pGrid->rowStretch(row), pGrid->rowMinimumHeight(row)});
    }

    QList<std::tuple<QLayoutItem*, int, int, int, int>> items;
    items.reserve(pGrid->count());
    while (pGrid->count()) {
        int row = 0;
        int column = 0;
        int rowSpan = 1;
        int columnSpan = 1;
        pGrid->getItemPosition(0, &row, &column, &rowSpan, &columnSpan);
        items.append({pGrid->takeAt(0), row, column, rowSpan, columnSpan});
    }

    pGrid->addWidget(pWidget, 0, 0, 1, columns);
    for (const auto& [pItem, row, column, rowSpan, columnSpan] : items) {
        pGrid->addItem(pItem, row + 1, column, rowSpan, columnSpan, pItem->alignment());
    }
    pGrid->setRowStretch(0, 0);
    pGrid->setRowMinimumHeight(0, 0);
    for (int row = 0; row < rows; ++row) {
        pGrid->setRowStretch(row + 1, rowProperties.at(row).first);
        pGrid->setRowMinimumHeight(row + 1, rowProperties.at(row).second);
    }
}

void buildControlSentenceRow(QBoxLayout* pRow, const QString& translatedSentence, QWidget* pControl)
{
    // A screen reader announces a field by its own name and not by the labels
    // beside it, so the control is given the whole sentence it sits in
    pControl->setAccessibleName(QString(translatedSentence).remove(qsl("%1")).simplified());
    buildControlSentenceRow(pRow, translatedSentence, QList<QWidget*>{pControl});
}

void buildControlSentenceRow(QBoxLayout* pRow, const QString& translatedSentence, const QList<QWidget*>& controls)
{
    // Null while the row is a bare layout waiting to be added to another one,
    // and Qt reparents everything in it at that point
    QWidget* pParent = pRow->parentWidget();

    // The gaps between the words and the controls are the row's own spacing, so
    // each run of words is trimmed of what would otherwise double it
    const auto addWords = [pRow, pParent](const QString& words) {
        const QString trimmed = words.trimmed();
        if (!trimmed.isEmpty()) {
            pRow->addWidget(new QLabel(trimmed, pParent));
        }
    };

    QList<bool> placed(controls.size(), false);
    static const QRegularExpression placeholderPattern(qsl("%(\\d+)"));
    qsizetype wordsFrom = 0;
    QRegularExpressionMatchIterator placeholders = placeholderPattern.globalMatch(translatedSentence);
    while (placeholders.hasNext()) {
        const QRegularExpressionMatch placeholder = placeholders.next();
        const int index = placeholder.captured(1).toInt() - 1;
        if (index < 0 || index >= controls.size() || placed.at(index)) {
            continue;
        }
        addWords(translatedSentence.mid(wordsFrom, placeholder.capturedStart() - wordsFrom));
        pRow->addWidget(controls.at(index));
        placed[index] = true;
        wordsFrom = placeholder.capturedEnd();
    }
    addWords(translatedSentence.mid(wordsFrom));

    // A translation that lost a placeholder is a mistake in the .ts file, and
    // the row it produces still has to hold every control it was given
    for (qsizetype index = 0; index < controls.size(); ++index) {
        if (!placed.at(index)) {
            pRow->addWidget(controls.at(index));
        }
    }
}

void makeChevronRow(QAbstractButton* pButton)
{
    pButton->setProperty("settingsChevronRow", true);
    pButton->setCursor(Qt::PointingHandCursor);
    pButton->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
}

void collectFocusableInLayoutOrder(const QLayout* pLayout, QList<QWidget*>& chain)
{
    for (int i = 0, total = pLayout->count(); i < total; ++i) {
        QLayoutItem* pItem = pLayout->itemAt(i);
        if (QWidget* pWidget = pItem->widget(); pWidget) {
            if ((pWidget->focusPolicy() & Qt::TabFocus) == Qt::TabFocus) {
                chain.append(pWidget);
            }
            if (const QLayout* pChildLayout = pWidget->layout(); pChildLayout) {
                collectFocusableInLayoutOrder(pChildLayout, chain);
            }
        } else if (const QLayout* pChildLayout = pItem->layout(); pChildLayout) {
            collectFocusableInLayoutOrder(pChildLayout, chain);
        }
    }
}

QString spotlightStyleSheet(const QColor& accent, const qreal strength)
{
    // Drawn round a card, so it takes a card's corner
    return qsl("#settingsSpotlight { border: 2px solid rgba(%1, %2, %3, %4); border-radius: %6px; background-color: rgba(%1, %2, %3, %5); }")
            .arg(QString::number(accent.red()), QString::number(accent.green()), QString::number(accent.blue()), QString::number(strength, 'f', 3), QString::number(strength * 0.08, 'f', 3))
            .arg(QString::number(scmRadiusPanel));
}

QString foldForSearch(const QString& text)
{
    QString plain;
    plain.reserve(text.size());
    bool inTag = false;
    for (const QChar character : text) {
        if (character == QLatin1Char('<')) {
            inTag = true;
        } else if (character == QLatin1Char('>')) {
            inTag = false;
        } else if (!inTag && character != QLatin1Char('&')) {
            plain.append(character);
        }
    }

    const QString decomposed = plain.normalized(QString::NormalizationForm_KD);
    QString folded;
    folded.reserve(decomposed.size());
    for (const QChar character : decomposed) {
        if (character.category() != QChar::Mark_NonSpacing) {
            folded.append(character);
        }
    }
    return folded.simplified().toCaseFolded();
}

QString visibleTextOf(const QWidget* pWidget)
{
    if (const auto* pLabel = qobject_cast<const QLabel*>(pWidget); pLabel) {
        return pLabel->text();
    }
    if (const auto* pGroupBox = qobject_cast<const QGroupBox*>(pWidget); pGroupBox) {
        return pGroupBox->title();
    }
    if (const auto* pButton = qobject_cast<const QAbstractButton*>(pWidget); pButton) {
        return pButton->text();
    }
    return QString();
}

void collectSearchText(const QWidget* pWidget, QStringList& parts)
{
    parts << pWidget->property(scmProp_searchKeywords).toString() << pWidget->toolTip();
    const auto* pComboBox = qobject_cast<const QComboBox*>(pWidget);
    if (!pComboBox) {
        parts << visibleTextOf(pWidget);
        return;
    }
    // ...but not what a font picker lists: those are the fonts installed on this
    // machine, and they make any card holding one a result for "color" or "mono"
    if (qobject_cast<const QFontComboBox*>(pWidget)) {
        return;
    }
    for (int i = 0, total = pComboBox->count(); i < total; ++i) {
        parts << pComboBox->itemText(i);
    }
}

QString highlightTextOf(const QWidget* pWidget)
{
    const QString text = visibleTextOf(pWidget);
    if (text.isEmpty()) {
        return QString();
    }
    const QString keywords = pWidget->property(scmProp_searchKeywords).toString();
    return keywords.isEmpty() ? text : qsl("%1 %2").arg(text, keywords);
}

bool wordEnoughToSearch(const QStringList& needles)
{
    for (const QString& needle : needles) {
        if (needle.size() >= 2) {
            return true;
        }
        switch (needle.at(0).script()) {
        case QChar::Script_Han:
        case QChar::Script_Hiragana:
        case QChar::Script_Katakana:
        case QChar::Script_Hangul:
            return true;
        default:
            break;
        }
    }
    return false;
}

void repolish(QWidget* pWidget)
{
    pWidget->style()->unpolish(pWidget);
    pWidget->style()->polish(pWidget);
    pWidget->update();
}

void setSearchMatch(QWidget* pWidget, const QVariant& matched)
{
    pWidget->setProperty("searchMatch", matched);
    repolish(pWidget);
}

QColor blend(const QColor& from, const QColor& to, const qreal amount)
{
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * amount, from.greenF() + (to.greenF() - from.greenF()) * amount, from.blueF() + (to.blueF() - from.blueF()) * amount);
}

QString rgba(const QColor& color, const qreal alpha)
{
    return qsl("rgba(%1, %2, %3, %4)").arg(QString::number(color.red()), QString::number(color.green()), QString::number(color.blue()), QString::number(alpha, 'f', 3));
}

qreal contrastRatio(const QColor& first, const QColor& second)
{
    const qreal firstLuminance = relativeLuminance(first);
    const qreal secondLuminance = relativeLuminance(second);
    return (std::max(firstLuminance, secondLuminance) + 0.05) / (std::min(firstLuminance, secondLuminance) + 0.05);
}

QColor readableOn(const QColor& background, const QColor& wanted, const QColor& fallback, const qreal minimumRatio)
{
    if (!wanted.isValid() || !background.isValid()) {
        return wanted.isValid() ? wanted : fallback;
    }
    if (contrastRatio(background, wanted) >= minimumRatio) {
        return wanted;
    }
    // Away from the surface it lies on rather than towards the text colour: it
    // is the hue that says which part of a pattern this is, so the hue is the
    // half worth keeping and the lightness the half to spend
    const QColor limit = background.lightness() < 128 ? QColor(Qt::white) : QColor(Qt::black); // theme-fixed: the ends of the lightness axis walked along below, not colours of a theme
    for (int step = 1; step <= scmReadabilitySteps; ++step) {
        const QColor moved = blend(wanted, limit, static_cast<qreal>(step) / scmReadabilitySteps);
        if (contrastRatio(background, moved) >= minimumRatio) {
            return moved;
        }
    }
    return fallback;
}

ThemeTokens themeTokens()
{
    // The application's palette rather than any one widget's: a stylesheet
    // freezes the palette of what it is set on, so a profile's Lua stylesheet
    // leaves a window holding the theme it was shown in - and even without one,
    // the palette change is an event still undelivered while a window is
    // restyling itself. qApp's palette is swapped synchronously by
    // mudlet::setAppearance(), so it is already the new one.
    const QPalette themePalette = QApplication::palette();
    ThemeTokens tokens;
    tokens.accent = themePalette.color(QPalette::Highlight);
    // The window's own colour, not the colour of an input field: a page mixed
    // off Base is a page darker than the fields lying on it, which is the one
    // way round three surfaces cannot be read as depth.
    tokens.page = themePalette.color(QPalette::Window);
    tokens.darkPage = tokens.page.lightness() < 128;
    tokens.card = blend(tokens.page, QColor(Qt::white), tokens.darkPage ? scmCardLiftOnDark : scmCardLiftOnLight);
    // Where a palette leaves no room above the page - a white Window under a
    // white Base - the card keeps the window's colour and the page steps down
    // instead, so that the pair still reads in the order it means
    if (tokens.card.lightness() - tokens.page.lightness() < scmMinimumCardLift) {
        tokens.card = tokens.page;
        tokens.page = blend(tokens.page, QColor(Qt::black), scmPageDropUnderCard);
    }
    // Mixed after the page rather than taken straight off the palette, and
    // after the step above has settled which page it is: the words are pulled
    // back towards whatever they end up written on
    tokens.text = blend(themePalette.color(QPalette::WindowText), tokens.page, tokens.darkPage ? scmTextSoftenOnDark : scmTextSoftenOnLight);
    tokens.field = tokens.darkPage ? blend(themePalette.color(QPalette::Base), tokens.page, scmFieldLiftOnDark) : themePalette.color(QPalette::Base);
    // A fifth of the way from the page towards a card: told apart from the page
    // beside it, and nowhere near reading as a panel laid on top of it
    tokens.pane = blend(tokens.page, tokens.card, scmPaneLiftTowardsCard);
    tokens.separator = blend(tokens.page, QColor(Qt::black), tokens.darkPage ? scmSeparatorDropOnDark : scmSeparatorDropOnLight);
    tokens.border = blend(tokens.page, tokens.text, tokens.darkPage ? 0.22 : 0.18);
    tokens.mutedText = blend(tokens.page, tokens.text, scmMutedTextWeight);
    // ...and walked on towards the words until it can be read on all three
    // surfaces. A muted tone is quieter than the body text, not a different
    // class of thing: it is what a card's description, a chip's word and the
    // status bar are written in, so it carries the same floor. On a card - the
    // most lifted of the three - the weight it starts at lands a hair under it.
    for (qreal weight = scmMutedTextWeight; weight < 1.0
                                            && (contrastRatio(tokens.mutedText, tokens.page) < scmTextMinimumRatio || contrastRatio(tokens.mutedText, tokens.pane) < scmTextMinimumRatio
                                                || contrastRatio(tokens.mutedText, tokens.card) < scmTextMinimumRatio);) {
        weight = std::min(1.0, weight + scmToneWeightStep);
        tokens.mutedText = blend(tokens.page, tokens.text, weight);
    }
    tokens.disabledText = blend(tokens.page, tokens.text, scmDisabledTextWeight);
    // Every surface an unavailable word can be set on: the page - a toolbar
    // button, a label on a form - a card, a pane, and a field it is typed into.
    // On a light theme those are a grey page and a white well, and the card is
    // the one furthest from the page on a dark one.
    for (qreal weight = scmDisabledTextWeight;
         weight < 1.0
         && (contrastRatio(tokens.disabledText, tokens.page) < scmQuietMinimumRatio || contrastRatio(tokens.disabledText, tokens.pane) < scmQuietMinimumRatio
             || contrastRatio(tokens.disabledText, tokens.card) < scmQuietMinimumRatio || contrastRatio(tokens.disabledText, tokens.field) < scmQuietMinimumRatio);) {
        weight = std::min(1.0, weight + scmToneWeightStep);
        tokens.disabledText = blend(tokens.page, tokens.text, weight);
    }
    // A saturated highlight colour rarely holds its own against either page, so
    // it is taken towards the end of the lightness scale the page is not at -
    // and then walked on until it can be read on a wash of that same accent,
    // which is what it is drawn on wherever it appears: a chosen row in a tree
    // or a sidebar, a chip that is switched on. A colour that clears the
    // deepest of those washes clears the lighter ones, and a wash can lie on
    // any of the three surfaces, so all three are asked.
    {
        const QColor limit = tokens.darkPage ? QColor(Qt::white) : QColor(Qt::black); // theme-fixed: the ends of the lightness axis walked along below, not colours of a theme
        const QColor started = blend(tokens.accent, limit, tokens.darkPage ? scmAccentTextLiftOnDark : scmAccentTextDropOnLight);
        const QList<QColor> washes{
                blend(tokens.page, tokens.accent, scmAccentWashStrength), blend(tokens.pane, tokens.accent, scmAccentWashStrength), blend(tokens.card, tokens.accent, scmAccentWashStrength)};
        const auto readsOnEveryWash = [&washes](const QColor& ink) {
            return std::all_of(washes.cbegin(), washes.cend(), [&ink](const QColor& wash) {
                return contrastRatio(ink, wash) >= scmTextMinimumRatio;
            });
        };
        tokens.accentText = started;
        for (int step = 1; step <= scmReadabilitySteps && !readsOnEveryWash(tokens.accentText); ++step) {
            tokens.accentText = blend(started, limit, static_cast<qreal>(step) / scmReadabilitySteps);
        }
    }
    tokens.marker = QColor::fromHslF(scmMarkerHue, scmMarkerSaturation, tokens.darkPage ? scmMarkerLightnessOnDark : scmMarkerLightnessOnLight);
    tokens.hoverSoft = rgba(tokens.text, 0.07);
    tokens.accentSoft = rgba(tokens.accent, 0.14);
    return tokens;
}

QColor stateColor(const qreal hue, const bool darkPage)
{
    return QColor::fromHslF(hue, scmStateSaturation, darkPage ? scmStateLightnessOnDark : scmStateLightnessOnLight);
}

QString scrollBarStyleSheet(const QString& selectorPrefix, const ThemeTokens& tokens, const QColor& surface)
{
    const QColor groove = surface.isValid() ? surface : tokens.page;
    return qsl("%1 QScrollBar:vertical { background-color: %2; width: 12px; margin: 0px; border: none; }"
               "%1 QScrollBar:horizontal { background-color: %2; height: 12px; margin: 0px; border: none; }"
               "%1 QScrollBar::handle:vertical { background-color: %3; border-radius: 5px; min-height: 32px; margin: 1px; }"
               "%1 QScrollBar::handle:horizontal { background-color: %3; border-radius: 5px; min-width: 32px; margin: 1px; }"
               "%1 QScrollBar::handle:hover { background-color: %4; }"
               "%1 QScrollBar::add-line, %1 QScrollBar::sub-line { width: 0px; height: 0px; }"
               "%1 QScrollBar::add-page, %1 QScrollBar::sub-page { background-color: %2; }")
            .arg(selectorPrefix, groove.name(), blend(groove, tokens.text, 0.22).name(), blend(groove, tokens.text, 0.40).name());
}

QString sidebarStyleSheet(const QString& listName, const QString& separatorName, const QColor& itemColor, const SidebarMetrics& metrics, const ThemeTokens& tokens)
{
    // A border-left accent bar is drawn as an arc where the pill's corner
    // radius is, pinching the bar to nothing at both ends. A gradient is
    // clipped by the radius instead, so the bar keeps its width and the pill
    // its rounded corners - at the cost of the bar being a fraction of the
    // item's width, and a different fraction at each of the two widths.
    const auto barStop = [](const int width, const int padding) {
        return static_cast<qreal>(scmAccentBarWidth) / std::max(1, width - 2 * padding);
    };
    // Two stops a ten-thousandth apart rather than one: a gradient is
    // interpolated between its stops, and the bar has to end rather than fade
    const auto pillFill = [&tokens](const qreal stop) {
        return qsl("qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %1, stop:%2 %1, stop:%3 %4, stop:1 %4)")
                .arg(tokens.accent.name(), QString::number(stop, 'f', 5), QString::number(stop + 0.0001, 'f', 5), tokens.accentSoft);
    };

    const QString list = QLatin1Char('#') + listName;
    const QString separator = QLatin1Char('#') + separatorName;
    const QString accentBar = QString::number(scmAccentBarWidth);
    const QString focusRing = QString::number(scmSidebarFocusRingWidth);
    const QString itemPadding = QString::number(scmSidebarItemGutter - scmAccentBarWidth);
    const QString focusedItemPadding = QString::number(scmSidebarItemGutter - scmAccentBarWidth - 2 * scmSidebarFocusRingWidth);
    const QString railProperty = QLatin1StringView(scmProp_rail);
    const QString focusedProperty = QLatin1StringView(scmProp_focused);
    // Or the platform style draws its own selection as a square box inside the
    // rounded pill the rules below draw
    return list
           + qsl(" { background: transparent; border: none; outline: none; show-decoration-selected: 1;"
                 " selection-background-color: transparent; selection-color: %1; }")
                     .arg(tokens.accentText.name())
           // The transparent left border keeps a chosen row's name from
           // stepping sideways under its accent bar; outline:none drops a
           // focus rectangle drawn square inside a round pill
           + list + qsl("::item { border-radius: 8px; border-left: %1px solid transparent; padding-left: %2px; color: %3; outline: none; }").arg(accentBar, itemPadding, itemColor.name()) + list
           + qsl("::item:hover { background-color: %1; }").arg(tokens.hoverSoft) + list
           + qsl("::item:selected { color: %1; font-weight: bold; background: %2; }").arg(tokens.accentText.name(), pillFill(barStop(metrics.expandedWidth, metrics.padding)))
           // Keyboard focus is otherwise indistinguishable from the selection;
           // an event filter on the list puts scmProp_focused on
           + list
           + qsl("[%1=\"true\"]::item:selected { border: %2px solid %3; border-left: %4px solid %3; padding-left: %5px; }")
                     .arg(focusedProperty, focusRing, tokens.accent.name(), accentBar, focusedItemPadding)
           // On a rail the row is only as wide as its icon, so the padding
           // goes and the bar is a different fraction
           + list + qsl("[%1=\"true\"]::item { padding-left: 0px; }").arg(railProperty) + list
           + qsl("[%1=\"true\"]::item:selected { background: %2; }").arg(railProperty, pillFill(barStop(metrics.railWidth, metrics.railPadding))) + separator
           + qsl(" { border: none; background-color: %1; margin: 8px %2px; }").arg(tokens.border.name(), QString::number(metrics.separatorInset)) + separator
           + qsl("[%1=\"true\"] { margin: 8px 2px; }").arg(railProperty);
}

bool setSidebarCollapsed(QWidget* pPane, QListWidget* pList, const QString& separatorName, const bool collapsed, const SidebarMetrics& metrics)
{
    const int wanted = collapsed ? metrics.railWidth : metrics.expandedWidth;
    if (collapsed == pList->property(scmProp_rail).toBool() && pPane->width() == wanted) {
        return false;
    }
    const int padding = collapsed ? metrics.railPadding : metrics.padding;
    pPane->setFixedWidth(wanted);
    pPane->layout()->setContentsMargins(padding, metrics.verticalPadding, padding, metrics.verticalPadding);
    // What the shared delegate leaves the names out by, and the stylesheet
    // draws the narrower selection pill from
    pList->setProperty(scmProp_rail, collapsed);
    repolish(pList);
    for (auto* pSeparator : pList->findChildren<QFrame*>(separatorName)) {
        pSeparator->setProperty(scmProp_rail, collapsed);
        repolish(pSeparator);
    }
    pList->doItemsLayout();
    return true;
}

// A stylesheet can only take a picture from a file or a resource, and neither
// can be recoloured on the way in - so the arrow a combo box and a spin box are
// drawn with is cut from the one glyph in the resources, tinted for the theme in
// force, and left in the cache directory for the sheet to point at. The colour
// is part of the file's name: a theme change writes a new file rather than
// changing one a stylesheet has already read and cached by path.
// Where a glyph a stylesheet has to point at is written. One directory for all
// of them, made on the way past.
static QString glyphCacheFile(const QString& fileName)
{
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheRoot.isEmpty()) {
        return QString();
    }
    QDir cacheDir(cacheRoot + qsl("/ui-glyphs"));
    if (!cacheDir.exists() && !cacheDir.mkpath(qsl("."))) {
        return QString();
    }
    return cacheDir.absoluteFilePath(fileName);
}

QString gripGlyphFile(const QColor& color, const bool alongTheBar)
{
    const int columns = alongTheBar ? scmGripDotsAlong : scmGripDotsAcross;
    const int rows = alongTheBar ? scmGripDotsAcross : scmGripDotsAlong;
    const QString filePath = glyphCacheFile(qsl("grip-%1-%2x%3.png").arg(color.name().mid(1), QString::number(columns), QString::number(rows)));
    if (filePath.isEmpty() || QFileInfo::exists(filePath)) {
        return filePath;
    }

    const qreal width = (columns - 1) * scmGripDotPitch + scmGripDotDiameter;
    const qreal height = (rows - 1) * scmGripDotPitch + scmGripDotDiameter;
    // Written twice on a screen that doubles its pixels: a stylesheet loads the
    // path it is given through QPixmap, which looks for the @2x file beside it
    // first, so the plain one is what the rule names and the larger one is what
    // a retina screen actually draws
    const qreal ratio = qApp ? qApp->devicePixelRatio() : 1.0;
    QList<qreal> ratios{1.0};
    if (ratio > 1.0) {
        ratios << ratio;
    }
    for (const qreal drawnAt : std::as_const(ratios)) {
        QPixmap grip(qRound(width * drawnAt), qRound(height * drawnAt));
        grip.fill(Qt::transparent);
        QPainter painter(&grip);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(drawnAt, drawnAt);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        for (int column = 0; column < columns; ++column) {
            for (int row = 0; row < rows; ++row) {
                painter.drawEllipse(QRectF(column * scmGripDotPitch, row * scmGripDotPitch, scmGripDotDiameter, scmGripDotDiameter));
            }
        }
        painter.end();
        const QString wanted =
                drawnAt > 1.0 ? glyphCacheFile(qsl("grip-%1-%2x%3@%4x.png").arg(color.name().mid(1), QString::number(columns), QString::number(rows), QString::number(qRound(drawnAt)))) : filePath;
        if (!grip.save(wanted, "PNG")) {
            return QString();
        }
    }
    return filePath;
}

static QString themedArrowFile(const QColor& color, const bool pointingUp)
{
    const QString filePath = glyphCacheFile(qsl("arrow-%1-%2.png").arg(pointingUp ? qsl("up") : qsl("down"), color.name().mid(1)));
    if (filePath.isEmpty() || QFileInfo::exists(filePath)) {
        return filePath;
    }

    QPixmap glyph(qsl(":/icons/arrow-down_grey-16x.png"));
    if (glyph.isNull()) {
        return QString();
    }
    if (pointingUp) {
        glyph = glyph.transformed(QTransform().rotate(180));
    }
    if (!tintedGlyph(glyph, color).save(filePath, "PNG")) {
        return QString();
    }
    return filePath;
}

QString inputStyleSheet(const ThemeTokens& tokens, const QString& selectorPrefix)
{
    // A field the user cannot reach is a piece of the page rather than a place
    // to put something, so its surface is let back down towards the page
    const QColor disabledField = blend(tokens.field, tokens.page, 0.6);
    const QColor disabledBorder = blend(tokens.page, tokens.text, 0.10);

    // Every selector below names a control and nothing about where it is; a
    // caller with one container to claim under puts them all beneath it here
    const auto scoped = [&selectorPrefix](const QStringList& selectors) {
        if (selectorPrefix.isEmpty()) {
            return selectors.join(qsl(", "));
        }
        QStringList scopedSelectors;
        scopedSelectors.reserve(selectors.size());
        for (const QString& selector : selectors) {
            scopedSelectors << selectorPrefix + QLatin1Char(' ') + selector;
        }
        return scopedSelectors.join(qsl(", "));
    };

    // Drawing a control's frame from a stylesheet takes the arrows inside it
    // away with it, and the macOS style then leaves a combo box looking like a
    // line edit and a spin box with no steppers at all. So the two controls that
    // carry arrows are only claimed once there is a picture to put the arrows
    // back with; without one they keep the frame the platform draws them with.
    const QString downArrow = themedArrowFile(tokens.mutedText, false);
    const QString upArrow = themedArrowFile(tokens.mutedText, true);
    const bool arrowsAvailable = !downArrow.isEmpty() && !upArrow.isEmpty();

    QStringList fieldTypes{qsl("QLineEdit"), qsl("QPlainTextEdit"), qsl("QTextEdit")};
    if (arrowsAvailable) {
        fieldTypes << qsl("QComboBox") << qsl("QAbstractSpinBox");
    }
    QStringList focusedTypes;
    QStringList disabledTypes;
    for (const QString& fieldType : std::as_const(fieldTypes)) {
        focusedTypes << fieldType + qsl(":focus");
        disabledTypes << fieldType + qsl(":disabled");
    }

    QString rules =
            scoped(fieldTypes)
            + qsl(" { background-color: %1; color: %2; border: %10px solid %3; border-radius: %4px;"
                  " padding: %5px %6px; min-height: %7px;"
                  " selection-background-color: %8; selection-color: %9; }")
                      .arg(tokens.field.name(), tokens.text.name(), tokens.border.name(), QString::number(scmRadiusInput))
                      .arg(QString::number(scmInputPaddingVertical), QString::number(scmInputPaddingHorizontal), QString::number(scmInputContentHeight))
                      .arg(tokens.accent.name(), tokens.accentText.name(), QString::number(scmInputBorderWidth))
            // A 1px accent frame rather than a ring drawn outside the control:
            // where the platform draws one of its own, two rings round the same
            // box read as a fault
            + scoped(focusedTypes) + qsl(" { border: %2px solid %1; }").arg(tokens.accent.name(), QString::number(scmInputBorderWidth)) + scoped(disabledTypes)
            + qsl(" { color: %1; background-color: %2; border: %4px solid %3; }").arg(tokens.disabledText.name(), disabledField.name(), disabledBorder.name(), QString::number(scmInputBorderWidth))
            // The list a combo box drops down is a surface lifted off the page,
            // not a taller copy of the field it came out of
            + scoped({qsl("QComboBox QAbstractItemView")})
            + qsl(" { background-color: %1; color: %2; border: 1px solid %3;"
                  " selection-background-color: %4; selection-color: %5; outline: none; }")
                      .arg(tokens.card.name(), tokens.text.name(), tokens.border.name(), tokens.accent.name(), tokens.accentText.name())
            // Both of those hold a QLineEdit of their own, which the rules above
            // would otherwise draw as a second field inside the first
            + scoped({qsl("QComboBox QLineEdit"), qsl("QAbstractSpinBox QLineEdit")}) + qsl(" { background: transparent; border: none; border-radius: 0px; padding: 0px; min-height: 0px; }");

    if (arrowsAvailable) {
        rules += scoped({qsl("QComboBox::drop-down")})
                 + qsl(" { subcontrol-origin: padding; subcontrol-position: center right; width: %1px; border: none; background: transparent; }").arg(QString::number(scmInputDropDownWidth))
                 + scoped({qsl("QComboBox::down-arrow")}) + qsl(" { image: url(\"%1\"); width: %2px; height: %2px; }").arg(downArrow, QString::number(scmInputArrowSize))
                 + scoped({qsl("QAbstractSpinBox::up-button")})
                 + qsl(" { subcontrol-origin: border; subcontrol-position: top right; width: %1px; border: none; background: transparent; }").arg(QString::number(scmInputStepperWidth))
                 + scoped({qsl("QAbstractSpinBox::down-button")})
                 + qsl(" { subcontrol-origin: border; subcontrol-position: bottom right; width: %1px; border: none; background: transparent; }").arg(QString::number(scmInputStepperWidth))
                 + scoped({qsl("QAbstractSpinBox::up-arrow")}) + qsl(" { image: url(\"%1\"); width: %2px; height: %2px; }").arg(upArrow, QString::number(scmInputStepperArrowSize))
                 + scoped({qsl("QAbstractSpinBox::down-arrow")}) + qsl(" { image: url(\"%1\"); width: %2px; height: %2px; }").arg(downArrow, QString::number(scmInputStepperArrowSize));
    }
    return rules;
}

QPixmap glyphPixmap(const QString& file)
{
    const QString key = qsl("uiDesign-glyph-%1-%2").arg(file, QString::number(scmGlyphStrokeWidth));
    QPixmap glyph;
    if (QPixmapCache::find(key, &glyph)) {
        return glyph;
    }

    if (!file.endsWith(QLatin1StringView(".svg"), Qt::CaseInsensitive)) {
        glyph = QPixmap(file);
        QPixmapCache::insert(key, glyph);
        return glyph;
    }

    QFile source(file);
    if (!source.open(QIODevice::ReadOnly)) {
        return glyph;
    }
    QByteArray svg = source.readAll();
    // Lucide writes the weight into every file as stroke-width="2"; swapping it
    // here is what makes the weight a token instead of a property of the asset
    svg.replace(QByteArrayLiteral(R"(stroke-width="2")"), QByteArrayLiteral(R"(stroke-width=")") + QByteArray::number(scmGlyphStrokeWidth) + '"');

    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) {
        return glyph;
    }
    glyph = QPixmap(scmGlyphRasterSize, scmGlyphRasterSize);
    glyph.fill(Qt::transparent);
    QPainter painter(&glyph);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);
    painter.end();

    QPixmapCache::insert(key, glyph);
    return glyph;
}

QPixmap tintedGlyph(const QPixmap& source, const QColor& color)
{
    QPixmap glyph = source;
    QPainter painter(&glyph);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(glyph.rect(), color);
    painter.end();
    return glyph;
}

QIcon tintedIcon(const QString& glyphOff, const QString& glyphOn, const ThemeTokens& tokens)
{
    const QPixmap sourceOff = glyphPixmap(glyphOff);
    const QPixmap sourceOn = glyphOn == glyphOff ? sourceOff : glyphPixmap(glyphOn);

    QIcon icon;
    icon.addPixmap(tintedGlyph(sourceOff, tokens.mutedText), QIcon::Normal, QIcon::Off);
    const QPixmap accentOff = tintedGlyph(sourceOff, tokens.accentText);
    icon.addPixmap(accentOff, QIcon::Active, QIcon::Off);
    icon.addPixmap(accentOff, QIcon::Selected, QIcon::Off);
    const QPixmap accentOn = glyphOn == glyphOff ? accentOff : tintedGlyph(sourceOn, tokens.accentText);
    icon.addPixmap(accentOn, QIcon::Normal, QIcon::On);
    icon.addPixmap(accentOn, QIcon::Active, QIcon::On);
    icon.addPixmap(accentOn, QIcon::Selected, QIcon::On);
    const QPixmap disabledOff = tintedGlyph(sourceOff, tokens.disabledText);
    icon.addPixmap(disabledOff, QIcon::Disabled, QIcon::Off);
    icon.addPixmap(glyphOn == glyphOff ? disabledOff : tintedGlyph(sourceOn, tokens.disabledText), QIcon::Disabled, QIcon::On);
    return icon;
}

QIcon tintedIcon(const QString& glyph, const ThemeTokens& tokens)
{
    return tintedIcon(glyph, glyph, tokens);
}

void restyleActionGlyphs(const QList<ActionGlyph>& glyphs, const ThemeTokens& tokens)
{
    for (const ActionGlyph& glyph : glyphs) {
        if (!glyph.pAction) {
            continue;
        }
        glyph.pAction->setIcon(glyph.glyphOn.isEmpty() ? tintedIcon(glyph.glyphOff, tokens) : tintedIcon(glyph.glyphOff, glyph.glyphOn, tokens));
    }
}

QString cardStyleSheet(const CardMetrics& metrics, const ThemeTokens& tokens)
{
    const QString cardProperty = QLatin1StringView(metrics.cardProperty);
    const QString padding = QString::number(metrics.padding);
    // The title is the card's first line, inside the frame: the top padding is
    // what leaves room for it, and the same padding sets it in from the left
    // edge as the controls under it
    QString rules =
            qsl("QGroupBox[%1=\"true\"] { background-color: %2; border: 1px solid %3; border-radius: %4px;"
                " padding: %5px %6px %6px %6px; font-weight: bold; }"
                "QGroupBox[%1=\"true\"]::title { subcontrol-origin: padding; subcontrol-position: top left;"
                " left: %6px; top: %6px; padding: 0px; }"
                // ...but only the title is bold, not everything the card holds:
                "QGroupBox[%1=\"true\"] > * { font-weight: normal; }")
                    .arg(cardProperty, tokens.card.name(), tokens.border.name(), QString::number(scmRadiusPanel), QString::number(metrics.padding + metrics.titleHeight + scmCardTitleGap), padding);
    if (metrics.plainProperty) {
        // A card carrying a single option needs no heading, nor room inside for one
        rules += qsl("QGroupBox[%1=\"true\"] { padding-top: %2px; }").arg(QLatin1StringView(metrics.plainProperty), padding);
    }
    if (metrics.flattenNestedGroupBoxes) {
        // A group box the .ui file nests inside what is now a card would draw a
        // second frame; a heading alone divides them
        rules += qsl("QGroupBox[%1=\"true\"] QGroupBox { border: none; background: transparent; margin-top: 20px; padding: 0px 0px 0px 8px; font-weight: bold; }"
                     "QGroupBox[%1=\"true\"] QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 0px; padding: 0px; }")
                         .arg(cardProperty);
    }
    return rules;
}

QString cardIndicatorStyleSheet(const char* cardProperty, const ThemeTokens& tokens)
{
    // Mixed over the card the box sits on, while the box itself is filled like
    // every other control the user sets something with
    const QColor indicatorOutline = blend(tokens.card, tokens.text, tokens.darkPage ? scmCardIndicatorOutlineOnDark : scmCardIndicatorOutlineOnLight);
    return qsl("QGroupBox[%1=\"true\"]::indicator { width: %2px; height: %2px; border: 1px solid %3; border-radius: 3px; background-color: %4; }"
               "QGroupBox[%1=\"true\"]::indicator:hover { border: 1px solid %5; }"
               // A fixed green check mark, so the fill under it is the field
               // colour every other control is set from rather than an accent
               // that could be orange
               "QGroupBox[%1=\"true\"]::indicator:checked { border: 1px solid %5; image: url(:/icons/dialog-ok-apply_small.png); }")
            .arg(QLatin1StringView(cardProperty), QString::number(scmCardIndicatorSize), indicatorOutline.name(), tokens.field.name(), tokens.accent.name());
}

// Where the style puts a card's title, on a throwaway box laid out under the
// rules the real cards are drawn with
static QRect measuredCardTitleRect(QWidget* pParent, const QString& indicatorRules, const char* cardProperty, const bool checkable)
{
    QGroupBox box(pParent);
    box.setProperty(cardProperty, true);
    box.setCheckable(checkable);
    // Never shown or read, but a box with no title has no label to place
    box.setTitle(qsl("Aa"));
    // Its own rather than the shell's, which is the string being built. The
    // weight goes with the indicator rules because a card's title is set bold,
    // and how tall a line of it comes to is one of the answers asked for here.
    box.setStyleSheet(indicatorRules + qsl("QGroupBox[%1=\"true\"] { font-weight: bold; }").arg(QString::fromLatin1(cardProperty)));
    QStyleOptionGroupBox option;
    option.initFrom(&box);
    option.subControls = QStyle::SC_GroupBoxFrame | QStyle::SC_GroupBoxLabel;
    if (checkable) {
        option.subControls |= QStyle::SC_GroupBoxCheckBox;
        option.state |= QStyle::State_On;
    }
    option.text = box.title();
    option.textAlignment = Qt::AlignLeft;
    option.lineWidth = 0;
    option.midLineWidth = 0;
    return box.style()->subControlRect(QStyle::CC_GroupBox, &option, QStyle::SC_GroupBoxLabel, &box);
}

int measuredCardTitleHeight(QWidget* pParent, const QString& indicatorRules, const char* cardProperty)
{
    // The taller of the two: a checkable card's title line is as tall as its
    // check indicator where the type is smaller than the box
    return qMax(measuredCardTitleRect(pParent, indicatorRules, cardProperty, true).height(), measuredCardTitleRect(pParent, indicatorRules, cardProperty, false).height());
}

QString inlineGlyph(const QPixmap& glyph)
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    glyph.save(&buffer, "PNG");
    return qsl(R"(<img src="data:image/png;base64,%1" width="18" height="18">)").arg(QString::fromLatin1(png.toBase64()));
}

QString withLinkColour(const QString& richText, const QColor& colour)
{
    // Every anchor this is asked about is written "<a href=", so a plain
    // replace reaches all of them without parsing the document
    return QString(richText).replace(qsl("<a "), qsl("<a style=\"color: %1\" ").arg(colour.name()));
}

QFont fixedPitchFont(const QFont& base, const qreal scale)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (base.pointSizeF() > 0.0) {
        font.setPointSizeF(base.pointSizeF() * scale);
    } else {
        font.setPixelSize(std::max(1, qRound(base.pixelSize() * scale)));
    }
    return font;
}

QVariant controlValue(const QObject* pControl)
{
    if (const auto* pGroupBox = qobject_cast<const QGroupBox*>(pControl)) {
        return pGroupBox->isCheckable() ? QVariant(pGroupBox->isChecked()) : QVariant();
    }
    if (const auto* pCheckBox = qobject_cast<const QCheckBox*>(pControl)) {
        // The check state rather than isChecked(), for the tri-state boxes
        return QVariant::fromValue(pCheckBox->checkState());
    }
    if (const auto* pButton = qobject_cast<const QAbstractButton*>(pControl)) {
        if (qobject_cast<const QPushButton*>(pControl) || qobject_cast<const QToolButton*>(pControl)) {
            return {};
        }
        return pButton->isChecked();
    }
    if (const auto* pFontComboBox = qobject_cast<const QFontComboBox*>(pControl)) {
        return pFontComboBox->currentFont();
    }
    if (const auto* pComboBox = qobject_cast<const QComboBox*>(pControl)) {
        return pComboBox->currentIndex();
    }
    if (const auto* pSpinBox = qobject_cast<const QSpinBox*>(pControl)) {
        return pSpinBox->value();
    }
    if (const auto* pDoubleSpinBox = qobject_cast<const QDoubleSpinBox*>(pControl)) {
        return pDoubleSpinBox->value();
    }
    if (const auto* pDateTimeEdit = qobject_cast<const QDateTimeEdit*>(pControl)) {
        return pDateTimeEdit->dateTime();
    }
    if (const auto* pLineEdit = qobject_cast<const QLineEdit*>(pControl)) {
        return pLineEdit->text();
    }
    return {};
}

bool beingTypedInto(const QObject* pControl)
{
    const auto* pLineEdit = qobject_cast<const QLineEdit*>(pControl);
    return pLineEdit && pLineEdit->hasFocus() && pLineEdit->isModified();
}

SettingsSnapshot::SettingsSnapshot(const QWidget& owner, const QMap<QString, QKeySequence>& shortcuts)
: mOwner(owner)
, mCurrentShortcuts(shortcuts)
{
}

bool SettingsSnapshot::carriesValue(const QObject* pControl)
{
    return controlValue(pControl).isValid();
}

void SettingsSnapshot::take()
{
    const QHash<const QObject*, QVariant> previous = mValues;
    mValues.clear();
    for (const auto* pWidget : mOwner.findChildren<QWidget*>()) {
        const QVariant value = controlValue(pWidget);
        if (!value.isValid()) {
            continue;
        }
        // The apply this snapshot follows left a half-typed field alone, so what
        // it was last populated with has to stand until that edit finishes
        if (const auto it = previous.constFind(pWidget); beingTypedInto(pWidget) && it != previous.constEnd()) {
            mValues.insert(pWidget, *it);
            continue;
        }
        mValues.insert(pWidget, value);
    }
    mShortcuts = mCurrentShortcuts;
}

void SettingsSnapshot::take(const QObject* pControl)
{
    mValues.insert(pControl, controlValue(pControl));
}

bool SettingsSnapshot::dirty(const QObject* pControl) const
{
    // The debounce is shared, so the apply about to read this was very likely
    // started by another control's edit
    if (beingTypedInto(pControl)) {
        return false;
    }
    const auto it = mValues.constFind(pControl);
    if (it == mValues.constEnd()) {
        // A control that came into being after the last snapshot:
        return true;
    }
    return *it != controlValue(pControl);
}

// For a setting spread over several controls - the borders, the Discord privacy
// flags - one of them changing means the write happens. What is written is
// still composed control by control: an undirty control contributes the value
// the Host holds now rather than what it shows, which a script may have moved
// on from (#10165). Members that are separate settings take their own guard.
bool SettingsSnapshot::anyDirty(const QList<const QObject*>& controls) const
{
    for (const auto* pControl : controls) {
        if (dirty(pControl)) {
            return true;
        }
    }
    return false;
}

bool SettingsSnapshot::shortcutsDirty() const
{
    return mCurrentShortcuts != mShortcuts;
}

bool SettingsSnapshot::shortcutDirty(const QString& key) const
{
    return mCurrentShortcuts.value(key) != mShortcuts.value(key);
}

bool SettingsSnapshot::pendingEdits(const QTimer* pApplyTimer, const QLineEdit* pSearchField) const
{
    // Whatever the settings say, what the controls hold is the user's until the
    // apply has run - and the refresh at the end of it re-reads them anyway
    if (pApplyTimer && pApplyTimer->isActive()) {
        return true;
    }
    for (const auto* pWidget : mOwner.findChildren<QWidget*>()) {
        if (pWidget == pSearchField || !carriesValue(pWidget)) {
            continue;
        }
        // dirty() answers false for a field being typed into, which is exactly
        // the edit that must not be written over here - so it is asked separately
        if (beingTypedInto(pWidget) || dirty(pWidget)) {
            return true;
        }
    }
    if (shortcutsDirty()) {
        return true;
    }
    // A shortcut editor holds a capture until editingFinished, so one showing
    // anything other than what it last committed is an edit in progress
    for (auto it = mEditors.cbegin(), end = mEditors.cend(); it != end; ++it) {
        if (it.value() && it.value()->keySequence() != mCurrentShortcuts.value(it.key())) {
            return true;
        }
    }
    return false;
}

TKeySequenceEdit* SettingsSnapshot::editorFor(const QString& key) const
{
    return mEditors.value(key).data();
}

void SettingsSnapshot::addEditor(const QString& key, TKeySequenceEdit* pEditor)
{
    mEditors.insert(key, pEditor);
}

} // namespace uiDesign
