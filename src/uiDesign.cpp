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
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleOptionGroupBox>
#include <QStyleOptionViewItem>
#include <QSvgRenderer>
#include <QTabBar>
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
// The saturation and the two lightnesses every state colour is mixed at...
constexpr qreal scmStateSaturation = 0.55;
// ...bar the red. A green or an amber at that saturation still reads as
// itself, while a red there reads as salmon: the eye asks more of a red before
// it will call it one, so the error hue carries a saturation of its own.
constexpr qreal scmErrorStateSaturation = 0.85;
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
// How big the arrows drawn in the room a control leaves at its right edge are.
// The room itself is scmInputDropDownWidth, in the header, because a window
// laying out a split button of its own has to hold its words clear of it.
constexpr int scmInputArrowSize = 8;
constexpr int scmInputStepperArrowSize = 7;
// What a split button's menu half is washed in while it is held down: twice the
// accent wash the body of the button takes when it is pressed, since the half is
// drawn over a body that is already carrying that wash
constexpr qreal scmSplitHalfPressedWash = 2 * scmSoftWashStrength;
// How far the name beside the accent bar is held off the item's left edge in
// all. The bar is a border rather than a gap, so the padding written into the
// rules is what it leaves of that gutter.
constexpr int scmSidebarItemGutter = 10;
constexpr int scmSidebarItemPadding = scmSidebarItemGutter - scmAccentBarWidth;
// The ring drawn round the pill while the list holds the keyboard. It is taken
// out of the item rather than added to it, a pixel off either side, so the
// padding gives both back and the name stays where it was.
constexpr int scmSidebarFocusRingWidth = 1;
// How far a choice indicator's outline is taken from the surface it sits on
// towards the words beside it. The same step either way: the light theme was
// given less of it while the mark was only a card's, and at that weight the
// outline came to 2.9:1 against a white card - under the 3:1 the boundary of a
// control is held to, and less again on the page the editor's forms sit on.
constexpr qreal scmChoiceOutlineOnDark = 0.55;
constexpr qreal scmChoiceOutlineOnLight = 0.55;
// The corner a check box's box is drawn to. It is a glyph rather than one of
// the boxes the radius scale is about, which is why it is not on that scale; a
// radio button takes half its own size instead and comes out a circle.
constexpr int scmChoiceBoxRadius = 3;
// ...and what is left between that mark and the words after it, which a style
// would otherwise measure off the indicator it was going to draw itself
constexpr int scmChoiceLabelSpacing = 6;
// A control the user cannot reach is a piece of the page rather than a place to
// put something, so its surface is let back down towards the page - and its
// hairline is mixed off the page rather than off what it is drawn on
constexpr qreal scmDisabledFieldTowardsPage = 0.6;
constexpr qreal scmDisabledBorderWeight = 0.10;
// How far a button's face is lifted off the card it sits on: a button is
// pressed rather than typed into, so it stands a little proud of the surface
// that a field is sunk into. A dark page takes the larger step, having less
// room above the card than a light one has below it.
constexpr qreal scmButtonFaceLiftOnDark = 0.08;
constexpr qreal scmButtonFaceLiftOnLight = 0.04;
// A button is read across rather than typed into, so the words are given more
// room either side than a field leaves them
constexpr int scmButtonPaddingHorizontal = 12;
// ...and what a disclosure button leaves above and below the glyph and the word
// it carries. More than a field leaves its text: the glyph is drawn at the
// height of the line beside it, and a field's two pixels leave it touching the
// frame.
constexpr int scmDisclosurePaddingVertical = 5;
// How far the chevron of a button carrying a menu is held off its right edge
constexpr int scmButtonMenuIndicatorInset = 6;
// What a menu leaves round the rows inside its frame, so the first and the last
// of them stay inside the arc the frame is cut to
constexpr int scmMenuSurfacePadding = 4;
// What a row of a menu leaves round its own word. The air above and below it is
// scmMenuItemPaddingVertical, in the header because a tab takes the same. The
// room on the leading edge is what the mark or the picture beside that word
// stands in - the mark's own size and the gap every other choice leaves after
// one - and the trailing room is what a shortcut is written in.
constexpr int scmMenuItemPaddingLeading = scmChoiceIndicatorSize + scmChoiceLabelSpacing;
constexpr int scmMenuItemPaddingTrailing = scmButtonPaddingHorizontal;
// ...and where in that leading room the mark or the picture stands: centred in
// it. A sub-control is placed from the row's padding box, which is the very
// edge the row's highlight is drawn from, so a mark left where it lands sits on
// the arc of that highlight rather than inside it.
constexpr int scmMenuItemMarkInset = (scmMenuItemPaddingLeading - scmChoiceIndicatorSize) / 2;

// What the three steps either side of the body are measured at - see TypeStep.
// A caption is the same shade smaller that a chip's word is, since a chip is
// one; a title is a step up a reader sees without reading, and the display size
// is the one thing on a page allowed to be larger than a title.
constexpr qreal scmTypeRatio_caption = 0.85;
constexpr qreal scmTypeRatio_title = 1.15;
constexpr qreal scmTypeRatio_display = 1.45;

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
    // Here rather than left to the shell's own pass: a row standing for a
    // search result is made the first time a search finds one, which is long
    // after the sheet naming its accent on focus was set
    keepClickFocusOffControls(pButton);
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

namespace {
QString rgbaText(const QColor& color, const qreal alpha)
{
    return qsl("rgba(%1, %2, %3, %4)").arg(QString::number(color.red()), QString::number(color.green()), QString::number(color.blue()), QString::number(alpha, 'f', 3));
}
} // namespace

QString rgba(const QColor& color, const qreal alpha)
{
    return rgbaText(color, alpha);
}

QString rgba(const QColor& colourWithAlpha)
{
    return rgbaText(colourWithAlpha, colourWithAlpha.alphaF());
}

void paintAccentBar(QPainter* pPainter, const QRect& row, const QColor& accent, const int cornerRadius)
{
    pPainter->save();
    // A rectangular clip lands on whole pixels, so the bar's trailing edge is a
    // straight line down the row; the arc at each end is the path's, drawn
    // antialiased so that it lies on the pill's own
    pPainter->setClipRect(QRect(row.left(), row.top(), scmAccentBarWidth, row.height()), Qt::IntersectClip);
    pPainter->setRenderHint(QPainter::Antialiasing);
    QPainterPath pill;
    pill.addRoundedRect(row, cornerRadius, cornerRadius);
    pPainter->fillPath(pill, accent);
    pPainter->restore();
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
    // Mixed once as colours and written out of them, so the sheet a rule carries
    // and the fill a painter reaches for cannot come apart
    tokens.hoverWash = tokens.text;
    tokens.hoverWash.setAlphaF(scmHoverWashStrength);
    tokens.accentWash = tokens.accent;
    tokens.accentWash.setAlphaF(scmSoftWashStrength);
    tokens.hoverSoft = rgba(tokens.hoverWash);
    tokens.accentSoft = rgba(tokens.accentWash);
    return tokens;
}

QColor stateColor(const qreal hue, const bool darkPage)
{
    const qreal saturation = qFuzzyCompare(hue, scmStateHue_error) ? scmErrorStateSaturation : scmStateSaturation;
    return QColor::fromHslF(hue, saturation, darkPage ? scmStateLightnessOnDark : scmStateLightnessOnLight);
}

// The one red anything broken is drawn or written in: the note and the dot over
// the code pane, the mark on a broken item's row, the picture on the editor's
// error notice, the note refusing a duplicate event name, a shortcut that
// clashes and a certificate that cannot be trusted in the settings, and the
// edge round a field the connection dialog will not take. One function rather
// than the same expression in nine places, so none of them can drift.
//
// Walked off the strip the note over the code pane sits on. That is the darkest
// surface any of them is drawn on, which under a light theme is the hardest to
// read on and under a dark one the easiest - so the three places whose own
// surface is lighter than the strip (the events row's note on the page, the
// shortcut warning on a card, the certificate labels on a warning wash) walk it
// on from here against that surface. Each of those is a no-op wherever this
// value already clears the floor, which is everywhere under the light theme.
QColor errorInk(const ThemeTokens& tokens)
{
    return readableOn(tokens.separator, stateColor(scmStateHue_error, tokens.darkPage), tokens.text, scmTextMinimumRatio);
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
    const QString itemPadding = QString::number(scmSidebarItemPadding);
    const QString focusedItemPadding = QString::number(scmSidebarItemPadding - 2 * scmSidebarFocusRingWidth);
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
           + qsl("::item:hover { background-color: %1; }").arg(tokens.hoverSoft)
           + list
           // No font-weight here: a chosen row is drawn bold, but a sheet's
           // font on an item never reaches the painter, which lays the name out
           // in the option's own font. SidebarItemDelegate sets the weight, and
           // sidebarRowWidth() below measures every row in it.
           + qsl("::item:selected { color: %1; background: %2; }").arg(tokens.accentText.name(), pillFill(barStop(metrics.expandedWidth, metrics.padding)))
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

int sidebarRowWidth(const QListWidget* pList, const QString& name)
{
    if (!pList) {
        return 0;
    }
    QStyleOptionViewItem option;
    option.initFrom(pList);
    // The chosen row is drawn bold, so it is the bold name that has to fit
    QFont nameFont = pList->font();
    nameFont.setBold(true);
    option.font = nameFont;
    option.fontMetrics = QFontMetrics(nameFont);
    option.text = name;
    option.features |= QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration;
    // The list's own icon size rather than the design language's: the editor
    // offers a preference that moves it, and a bigger glyph takes its extra out
    // of the row rather than out of the name
    option.decorationSize = pList->iconSize();
    option.decorationPosition = QStyleOptionViewItem::Left;
    option.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
    // A picture of the right size and nothing else: it is the space a glyph is
    // given that is being measured, never the glyph
    QPixmap blank(pList->iconSize());
    blank.fill(Qt::transparent);
    option.icon = QIcon(blank);
    // Neither the option nor the call names the list, so a stylesheet style
    // wrapping it hands the question straight to the style underneath - which
    // is the one whose margins the rows are actually drawn with
    option.widget = nullptr;
    const int fromTheStyle = qApp->style()->sizeFromContents(QStyle::CT_ItemViewItem, &option, QSize(), nullptr).width();
    // ...and what the sheet above adds to every row: the accent bar, drawn as a
    // transparent left border on an unchosen one, and the padding that stands
    // the glyph off it
    return fromTheStyle + scmAccentBarWidth + scmSidebarItemPadding;
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

// What is left of a glyph once the empty margin around it is taken off. Lucide
// draws inside a 24 unit box and leaves room on every side, which a glyph 20px
// or more across can afford; an 8 or 9px control arrow cannot, and spends half
// its height on the margin rather than on the mark.
static QPixmap croppedToItsInk(const QPixmap& glyph)
{
    const QImage image = glyph.toImage().convertToFormat(QImage::Format_ARGB32);
    int left = image.width();
    int right = -1;
    int top = image.height();
    int bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) == 0) {
                continue;
            }
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
    }
    if (right < left || bottom < top) {
        return glyph;
    }
    return glyph.copy(QRect(QPoint(left, top), QPoint(right, bottom)));
}

static QString themedArrowFile(const QColor& color, const bool pointingUp, const int size)
{
    // "chevron" rather than the "arrow" these files were first written under:
    // a cache written by an earlier build holds the stretched shape below
    const QString filePath = glyphCacheFile(qsl("chevron-%1-%2-%3.png").arg(pointingUp ? qsl("up") : qsl("down"), color.name().mid(1), QString::number(size)));
    if (filePath.isEmpty() || QFileInfo::exists(filePath)) {
        return filePath;
    }

    // The same chevron the editor's search history is offered under, so a
    // control that drops something down says it the way the rest of the window
    // does. It replaces a full-colour bitmap of a solid triangle.
    QPixmap source = croppedToItsInk(glyphPixmap(qsl(":/icons/editor-chevron-down.svg")));
    if (source.isNull()) {
        return QString();
    }
    if (pointingUp) {
        source = source.transformed(QTransform().rotate(180));
    }
    source = tintedGlyph(source, color);

    // Written twice on a screen that doubles its pixels, the way the grip is:
    // a stylesheet loads the path it is given through QPixmap, which looks for
    // the @2x file beside it first
    const qreal ratio = qApp ? qApp->devicePixelRatio() : 1.0;
    QList<qreal> ratios{1.0};
    if (ratio > 1.0) {
        ratios << ratio;
    }
    for (const qreal drawnAt : std::as_const(ratios)) {
        const int drawnSize = qRound(size * drawnAt);
        // Fitted into the square at its own proportions: a chevron is twice
        // as wide as it is tall, and stretched to fill the square it became a
        // heavy V that read larger than every control it sat in
        const QPixmap arrow = source.scaled(drawnSize, drawnSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QString wanted =
                drawnAt > 1.0 ? glyphCacheFile(qsl("chevron-%1-%2-%3@%4x.png").arg(pointingUp ? qsl("up") : qsl("down"), color.name().mid(1), QString::number(size), QString::number(qRound(drawnAt))))
                              : filePath;
        if (!arrow.save(wanted, "PNG")) {
            return QString();
        }
    }
    return filePath;
}

QPixmap themedGlyphPixmap(const QString& glyphFile, const QColor& color, const int boxSize, const int glyphSize, const qreal devicePixelRatio)
{
    const QString key = qsl("uiDesign-themedGlyph-%1-%2-%3-%4-%5")
                                .arg(QFileInfo(glyphFile).baseName(), color.name(QColor::HexArgb), QString::number(boxSize), QString::number(glyphSize), QString::number(devicePixelRatio));
    QPixmap glyph;
    if (QPixmapCache::find(key, &glyph)) {
        return glyph;
    }

    QPixmap source = croppedToItsInk(glyphPixmap(glyphFile));
    if (source.isNull()) {
        return QPixmap();
    }
    source = tintedGlyph(source, color);

    // Fitted into its square at its own proportions: a dash is a good deal
    // wider than it is tall, and stretched to fill the square it would read
    // as a rule across the box
    const int drawnSize = qRound(glyphSize * devicePixelRatio);
    const QPixmap mark = source.scaled(drawnSize, drawnSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    glyph = QPixmap(qRound(boxSize * devicePixelRatio), qRound(boxSize * devicePixelRatio));
    glyph.fill(Qt::transparent);
    QPainter painter(&glyph);
    painter.drawPixmap(QPoint((glyph.width() - mark.width()) / 2, (glyph.height() - mark.height()) / 2), mark);
    painter.end();
    glyph.setDevicePixelRatio(devicePixelRatio);

    QPixmapCache::insert(key, glyph);
    return glyph;
}

// The mark a control's indicator shows, left in the cache for a stylesheet to
// point at the way the chevrons above are - each file drawn by the call above,
// so the mark a rule points at and the one a painter reaches for are the one
// picture rather than two that were meant to match.
//
// The margin is what makes the size hold. A sub-control scales the picture it
// is given *down* to its contents and never up, so a mark drawn on its own
// fills whatever box it lands in - and lands there at whichever of the 1x and
// 2x files the platform picked, which is not a thing a rule can say. A mark
// carrying its own margin comes out at the fraction of the box it was drawn at
// either way.
static QString themedGlyphFile(const QString& glyphFile, const QColor& color, const int boxSize, const int glyphSize)
{
    const QString stem = QFileInfo(glyphFile).baseName();
    const auto cacheName = [&stem, &color, boxSize, glyphSize](const int drawnAt) {
        const QString suffix = drawnAt > 1 ? qsl("@%1x").arg(QString::number(drawnAt)) : QString();
        return qsl("%1-%2-%3-%4%5.png").arg(stem, color.name().mid(1), QString::number(boxSize), QString::number(glyphSize), suffix);
    };
    const QString filePath = glyphCacheFile(cacheName(1));
    if (filePath.isEmpty() || QFileInfo::exists(filePath)) {
        return filePath;
    }

    const qreal ratio = qApp ? qApp->devicePixelRatio() : 1.0;
    QList<qreal> ratios{1.0};
    if (ratio > 1.0) {
        ratios << ratio;
    }
    for (const qreal drawnAt : std::as_const(ratios)) {
        const QPixmap glyph = themedGlyphPixmap(glyphFile, color, boxSize, glyphSize, drawnAt);
        if (glyph.isNull()) {
            return QString();
        }
        const QString wanted = drawnAt > 1.0 ? glyphCacheFile(cacheName(qRound(drawnAt))) : filePath;
        if (!glyph.save(wanted, "PNG")) {
            return QString();
        }
    }
    return filePath;
}

// The one mark that is a shape rather than a glyph: the dot inside a chosen
// radio button. Lucide has no circle small enough to survive being drawn at
// six pixels, and a filled disc is the whole of the drawing anyway. Centred in
// a square of the indicator's own size for the same reason the glyphs above
// are.
static QString dotGlyphFile(const QColor& color, const int boxSize, const int diameter)
{
    const auto cacheName = [&color, boxSize, diameter](const int drawnAt) {
        const QString suffix = drawnAt > 1 ? qsl("@%1x").arg(QString::number(drawnAt)) : QString();
        return qsl("dot-%1-%2-%3%4.png").arg(color.name().mid(1), QString::number(boxSize), QString::number(diameter), suffix);
    };
    const QString filePath = glyphCacheFile(cacheName(1));
    if (filePath.isEmpty() || QFileInfo::exists(filePath)) {
        return filePath;
    }

    const qreal ratio = qApp ? qApp->devicePixelRatio() : 1.0;
    QList<qreal> ratios{1.0};
    if (ratio > 1.0) {
        ratios << ratio;
    }
    for (const qreal drawnAt : std::as_const(ratios)) {
        QPixmap dot(qRound(boxSize * drawnAt), qRound(boxSize * drawnAt));
        dot.fill(Qt::transparent);
        QPainter painter(&dot);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(drawnAt, drawnAt);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        const qreal inset = (boxSize - diameter) / 2.0;
        painter.drawEllipse(QRectF(inset, inset, diameter, diameter));
        painter.end();
        const QString wanted = drawnAt > 1.0 ? glyphCacheFile(cacheName(qRound(drawnAt))) : filePath;
        if (!dot.save(wanted, "PNG")) {
            return QString();
        }
    }
    return filePath;
}

// Every selector a shared sheet writes names a control and nothing about where
// it is; a caller with one container to claim under puts them all beneath it
static QString scopedTo(const QString& selectorPrefix, const QStringList& selectors)
{
    if (selectorPrefix.isEmpty()) {
        return selectors.join(qsl(", "));
    }
    QStringList scopedSelectors;
    scopedSelectors.reserve(selectors.size());
    for (const QString& selector : selectors) {
        scopedSelectors << selectorPrefix + QLatin1Char(' ') + selector;
    }
    return scopedSelectors.join(qsl(", "));
}

QColor disabledFieldColour(const ThemeTokens& tokens)
{
    return blend(tokens.field, tokens.page, scmDisabledFieldTowardsPage);
}

QColor disabledBorderColour(const ThemeTokens& tokens)
{
    return blend(tokens.page, tokens.text, scmDisabledBorderWeight);
}

QString inputStyleSheet(const ThemeTokens& tokens, const QString& selectorPrefix)
{
    const QColor disabledField = disabledFieldColour(tokens);
    const QColor disabledBorder = disabledBorderColour(tokens);

    const auto scoped = [&selectorPrefix](const QStringList& selectors) {
        return scopedTo(selectorPrefix, selectors);
    };

    // Drawing a control's frame from a stylesheet takes the arrows inside it
    // away with it, and the macOS style then leaves a combo box looking like a
    // line edit and a spin box with no steppers at all. So the two controls that
    // carry arrows are only claimed once there is a picture to put the arrows
    // back with; without one they keep the frame the platform draws them with.
    // The combo box and the steppers draw at different sizes, and a picture a
    // stylesheet scales for itself is a picture drawn at neither: each gets a
    // file cut to the size its rule asks for
    const QString downArrow = themedArrowFile(tokens.mutedText, false, scmInputArrowSize);
    const QString upArrow = themedArrowFile(tokens.mutedText, true, scmInputArrowSize);
    const QString stepperDownArrow = themedArrowFile(tokens.mutedText, false, scmInputStepperArrowSize);
    const QString stepperUpArrow = themedArrowFile(tokens.mutedText, true, scmInputStepperArrowSize);
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
            // The list a combo box drops down is the field opened up: the same
            // surface, the same hairline and the same corner as the box it came
            // out of, since a combo box on a card would otherwise open a list in
            // the card's own grey, lifted off nothing. The frame the platform
            // draws round that list is named away first: it is a QFrame of its
            // own, and its bevel is the one part of a field no rule above
            // reaches.
            //
            // That frame is also a window, and a window is filled before what is
            // in it is drawn: a corner rounded on the list alone would leave the
            // window's own square one showing behind it in whatever colour it
            // took. So the surface belongs to the list and the frame around it
            // is asked for none - which only opens the corner once the window is
            // see-through and its brushes are named as nothing, neither of which
            // a sheet can say. letPopupsTakeTheFieldsCorner() says both. The
            // list's 2px padding keeps its square viewport inside the arc.
            + scoped({qsl("QComboBox QFrame")}) + qsl(" { border: none; margin: 0px; padding: 0px; background: transparent; }") + scoped({qsl("QComboBox QAbstractItemView")})
            + qsl(" { background-color: %1; color: %2; border: %6px solid %3; border-radius: %7px; padding: 2px;"
                  " selection-background-color: %4; selection-color: %5; outline: none; }")
                      .arg(tokens.field.name(), tokens.text.name(), tokens.border.name(), tokens.accent.name(), tokens.accentText.name(), QString::number(scmInputBorderWidth))
                      .arg(QString::number(scmRadiusInput))
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
                 + scoped({qsl("QAbstractSpinBox::up-arrow")}) + qsl(" { image: url(\"%1\"); width: %2px; height: %2px; }").arg(stepperUpArrow, QString::number(scmInputStepperArrowSize))
                 + scoped({qsl("QAbstractSpinBox::down-arrow")}) + qsl(" { image: url(\"%1\"); width: %2px; height: %2px; }").arg(stepperDownArrow, QString::number(scmInputStepperArrowSize));

        // A stepper and a drop-down are the two things on a field that are
        // pressed rather than typed into, so each says it is being used: the
        // chevron takes the accent under the pointer and holds it while the
        // button is down, and the stepper's own square lights the hover wash
        // and then the accent's. A tint the cache could not write leaves the
        // rule out rather than pointing it at nothing.
        const QString accentArrow = themedArrowFile(tokens.accent, false, scmInputArrowSize);
        const QString accentStepperDownArrow = themedArrowFile(tokens.accent, false, scmInputStepperArrowSize);
        const QString accentStepperUpArrow = themedArrowFile(tokens.accent, true, scmInputStepperArrowSize);
        if (!accentArrow.isEmpty()) {
            rules += scoped({qsl("QComboBox::down-arrow:hover"), qsl("QComboBox::down-arrow:pressed")})
                     + qsl(" { image: url(\"%1\"); width: %2px; height: %2px; }").arg(accentArrow, QString::number(scmInputArrowSize));
        }
        if (!accentStepperUpArrow.isEmpty() && !accentStepperDownArrow.isEmpty()) {
            rules += scoped({qsl("QAbstractSpinBox::up-arrow:hover"), qsl("QAbstractSpinBox::up-arrow:pressed")})
                     + qsl(" { image: url(\"%1\"); width: %2px; height: %2px; }").arg(accentStepperUpArrow, QString::number(scmInputStepperArrowSize))
                     + scoped({qsl("QAbstractSpinBox::down-arrow:hover"), qsl("QAbstractSpinBox::down-arrow:pressed")})
                     + qsl(" { image: url(\"%1\"); width: %2px; height: %2px; }").arg(accentStepperDownArrow, QString::number(scmInputStepperArrowSize));
        }
        rules += scoped({qsl("QAbstractSpinBox::up-button:hover"), qsl("QAbstractSpinBox::down-button:hover")}) + qsl(" { background-color: %1; }").arg(tokens.hoverSoft)
                 + scoped({qsl("QAbstractSpinBox::up-button:pressed"), qsl("QAbstractSpinBox::down-button:pressed")}) + qsl(" { background-color: %1; }").arg(tokens.accentSoft);
    }
    return rules;
}

namespace {
// The inks and the pictures one mark is drawn from, mixed once and handed to
// each of the three controls that carry one
struct ChoiceMarkInks
{
    QColor outline;
    QColor disabledBorder;
    QColor disabledFill;
    // Empty where the cache could not be written, which leaves the rule that
    // would have pointed at the picture out altogether rather than pointing it
    // at nothing - the way inputStyleSheet() only claims a combo box once its
    // arrows exist
    QString tick;
    QString dash;
    QString quietTick;
    QString quietDash;
    QString dot;
    QString quietDot;
};
} // namespace

static ChoiceMarkInks choiceMarkInks(const ThemeTokens& tokens)
{
    ChoiceMarkInks inks;
    // Mixed over the surface the box sits on, while the box itself is filled
    // like every other control the user sets something with
    inks.outline = blend(tokens.card, tokens.text, tokens.darkPage ? scmChoiceOutlineOnDark : scmChoiceOutlineOnLight);
    inks.disabledBorder = disabledBorderColour(tokens);
    inks.disabledFill = disabledFieldColour(tokens);
    // Read against the fill it is drawn on rather than against the accent
    // framing the box: the accent is the hairline, and the mark inside it is
    // the nearest colour to that accent a reader can still make out on a field
    const QColor markInk = readableOn(tokens.field, tokens.accent, tokens.text, scmQuietMinimumRatio);
    inks.tick = themedGlyphFile(qsl(":/icons/control-check.svg"), markInk, scmChoiceIndicatorSize, scmChoiceGlyphSize);
    inks.dash = themedGlyphFile(qsl(":/icons/control-minus.svg"), markInk, scmChoiceIndicatorSize, scmChoiceGlyphSize);
    inks.quietTick = themedGlyphFile(qsl(":/icons/control-check.svg"), tokens.disabledText, scmChoiceIndicatorSize, scmChoiceGlyphSize);
    inks.quietDash = themedGlyphFile(qsl(":/icons/control-minus.svg"), tokens.disabledText, scmChoiceIndicatorSize, scmChoiceGlyphSize);
    inks.dot = dotGlyphFile(markInk, scmChoiceIndicatorSize, scmChoiceDotDiameter);
    inks.quietDot = dotGlyphFile(tokens.disabledText, scmChoiceIndicatorSize, scmChoiceDotDiameter);
    return inks;
}

// One indicator, drawn out in full: the box, what says it is being used, and
// the picture inside it once the choice is made. The radius is the whole of
// what says whether it is a box or a circle. "set" is what a made choice is
// marked with and "part" what a box that is neither on nor off shows, which a
// radio button has no state for and hands in empty.
static QString choiceMarkRules(
        const QString& selector, const int radius, const QString& set, const QString& part, const QString& quietSet, const QString& quietPart, const ChoiceMarkInks& inks, const ThemeTokens& tokens)
{
    QString rules = qsl("%1 { width: %2px; height: %2px; border: %3px solid %4; border-radius: %5px; background-color: %6; }"
                        // A control being reached for rather than a surface, so
                        // the hairline goes to the colour the choice will be
                        // made in before it is made
                        "%1:hover, %1:focus { border: %3px solid %7; }"
                        "%1:pressed { background-color: %8; }")
                            .arg(selector, QString::number(scmChoiceIndicatorSize), QString::number(scmInputBorderWidth), inks.outline.name(), QString::number(radius), tokens.field.name())
                            .arg(tokens.accent.name(), tokens.accentSoft);
    if (!set.isEmpty()) {
        rules += qsl("%1:checked { border: %2px solid %3; image: url(\"%4\"); }").arg(selector, QString::number(scmInputBorderWidth), tokens.accent.name(), set);
    }
    // The state a platform paints as a filled grey square, which reads as some
    // third kind of control rather than as a box holding both answers at once
    if (!part.isEmpty()) {
        rules += qsl("%1:indeterminate { border: %2px solid %3; image: url(\"%4\"); }").arg(selector, QString::number(scmInputBorderWidth), tokens.accent.name(), part);
    }
    // Last, and so ahead of the two states above at the same specificity: a
    // choice the user cannot change is still shown, in the tone every other
    // unavailable thing is written in rather than in the accent
    rules += qsl("%1:disabled { border: %2px solid %3; background-color: %4; }").arg(selector, QString::number(scmInputBorderWidth), inks.disabledBorder.name(), inks.disabledFill.name());
    if (!quietSet.isEmpty()) {
        rules += qsl("%1:disabled:checked { image: url(\"%2\"); }").arg(selector, quietSet);
    }
    if (!quietPart.isEmpty()) {
        rules += qsl("%1:disabled:indeterminate { image: url(\"%2\"); }").arg(selector, quietPart);
    }
    return rules;
}

QString choiceStyleSheet(const ThemeTokens& tokens, const QString& selectorPrefix)
{
    const ChoiceMarkInks inks = choiceMarkInks(tokens);
    // Half its own size, which Qt clamps to a circle: what says a choice is one
    // of a set rather than a switch of its own is the shape of the box it is
    // made in
    const int dotRadius = (scmChoiceIndicatorSize + 1) / 2;
    return choiceMarkRules(scopedTo(selectorPrefix, {qsl("QCheckBox::indicator")}), scmChoiceBoxRadius, inks.tick, inks.dash, inks.quietTick, inks.quietDash, inks, tokens)
           + choiceMarkRules(scopedTo(selectorPrefix, {qsl("QRadioButton::indicator")}), dotRadius, inks.dot, QString(), inks.quietDot, QString(), inks, tokens)
           // What a style leaves between an indicator and the words after it is
           // measured off the indicator the style itself would have drawn, so a
           // mark of another size leaves the two touching on one platform and
           // well apart on the next. Named here, and the same in both.
           //
           // The control's own frame is named as well, as nothing: a rule that
           // leaves the frame to the platform leaves the layout rectangle to it
           // too, and the macOS style trims eleven pixels off a check box's for
           // the bezel it would have drawn - so a layout placed the words of the
           // next control over the last letters of this one.
           + scopedTo(selectorPrefix, {qsl("QCheckBox"), qsl("QRadioButton")})
           + qsl(" { spacing: %1px; border: none; }").arg(QString::number(scmChoiceLabelSpacing))
           // The platform draws a focus ring round the whole control as well,
           // which beside the accent the indicator has just taken reads as two
           // rings round one thing
           + scopedTo(selectorPrefix, {qsl("QCheckBox:focus"), qsl("QRadioButton:focus")}) + qsl(" { outline: none; }");
}

// Lifted off the card rather than sunk into it: a button is pressed, where a
// field is typed into. Every recipe drawing a button's face reads it here, so
// that a button on a form and a button opening a strip of controls are the same
// surface rather than two that happen to agree.
static QColor buttonFaceColour(const ThemeTokens& tokens)
{
    return blend(tokens.card, tokens.text, tokens.darkPage ? scmButtonFaceLiftOnDark : scmButtonFaceLiftOnLight);
}

QString buttonStyleSheet(const ThemeTokens& tokens, const QString& selectorPrefix)
{
    const QColor face = buttonFaceColour(tokens);
    const auto scoped = [&selectorPrefix](const QStringList& selectors) {
        return scopedTo(selectorPrefix, selectors);
    };

    // A tool button whose trailing half opens a menu cannot be made a push
    // button without giving that split up, so it joins the rule instead - but
    // only one that says it is standing on a form as a button. A window's other
    // tool buttons are its own business: the editor's forms carry several that
    // paint themselves.
    const QString menuButton = qsl("QToolButton[%1=\"true\"]").arg(QString::fromLatin1(scmProp_menuButton));

    QString rules =
            scoped({qsl("QPushButton"), menuButton})
            + qsl(" { background-color: %1; color: %2; border: %3px solid %4; border-radius: %5px; padding: %6px %7px; min-height: %8px; }")
                      .arg(face.name(), tokens.text.name(), QString::number(scmInputBorderWidth), tokens.border.name(), QString::number(scmRadiusInput))
                      .arg(QString::number(scmInputPaddingVertical), QString::number(scmButtonPaddingHorizontal), QString::number(scmInputContentHeight))
            + scoped({qsl("QPushButton:hover"), menuButton + qsl(":hover")})
            + qsl(" { border: %2px solid %1; background-color: %3; }").arg(tokens.accent.name(), QString::number(scmInputBorderWidth), tokens.hoverSoft)
            + scoped({qsl("QPushButton:pressed"), menuButton + qsl(":pressed")}) + qsl(" { background-color: %1; }").arg(tokens.accentSoft)
            + scoped({qsl("QPushButton:focus"), menuButton + qsl(":focus")}) + qsl(" { border: %2px solid %1; }").arg(tokens.accent.name(), QString::number(scmInputBorderWidth))
            + scoped({qsl("QPushButton:disabled"), menuButton + qsl(":disabled")})
            + qsl(" { color: %1; border: %3px solid %2; background-color: transparent; }").arg(tokens.disabledText.name(), disabledBorderColour(tokens).name(), QString::number(scmInputBorderWidth));

    // A button carrying a menu drops something down, so it says so with the
    // chevron every other control that does drops it under. Styling the button
    // at all takes the platform's own indicator away with it, so the rule is
    // only claimed once there is a picture to put one back with - and the room
    // the words are held clear by is what the rule's own width reserves, since
    // no selector can ask whether a button has a menu.
    const QString menuChevron = themedArrowFile(tokens.mutedText, false, scmInputArrowSize);
    if (!menuChevron.isEmpty()) {
        rules += scoped({qsl("QPushButton::menu-indicator")})
                 + qsl(" { image: url(\"%1\"); subcontrol-origin: padding; subcontrol-position: center right; right: %2px; width: %3px; height: %3px; }")
                           .arg(menuChevron, QString::number(scmButtonMenuIndicatorInset), QString::number(scmInputArrowSize));
        // ...and the split kind, whose trailing half is a control of its own
        // rather than an indicator on the face, drawn by the recipe below.
        // Nothing measures the room for the words for us here, since the label
        // is laid out in the whole of the contents rectangle - so the padding
        // leaves the half its width.
        rules += scoped({menuButton}) + qsl(" { padding-right: %1px; }").arg(QString::number(scmButtonPaddingHorizontal + scmInputDropDownWidth))
                 + splitButtonMenuHalfStyleSheet(scoped({menuButton}), scmRadiusInput, tokens);

        const QString accentChevron = themedArrowFile(tokens.accent, false, scmInputArrowSize);
        if (!accentChevron.isEmpty()) {
            rules += scoped({qsl("QPushButton::menu-indicator:hover"), qsl("QPushButton::menu-indicator:pressed")})
                     + qsl(" { image: url(\"%1\"); subcontrol-origin: padding; subcontrol-position: center right; right: %2px; width: %3px; height: %3px; }")
                               .arg(accentChevron, QString::number(scmButtonMenuIndicatorInset), QString::number(scmInputArrowSize));
        }
    }
    return rules;
}

// Two click areas have to read as two before the pointer arrives, and a sheet on
// the button has already taken the platform's own separator between them away.
// So the half is given a segment's worth of drawing: the seam on its leading
// edge and nothing else at rest, a wash of its own once the button is pointed
// at, and the design's chevron rather than the filled triangle a style leaves
// there.
//
// The wash is a step ahead of whatever the body takes, because the body's own
// hover fills the whole button, the half included: where the body lights to
// hoverSoft the half lights to the accent's wash - the same wash a row of the
// menu it opens takes under the pointer - and where the body is pressed to that
// wash the half takes twice it. A half washed in what the body is already
// washed in is a half that never reads as its own target.
//
// Which of the two halves the pointer is over is not what the wash says, and
// cannot be: QToolButton tracks its hovered sub-control through
// QStyle::hitTestComplexControl, which QStyleSheetStyle does not answer for
// CC_ToolButton, so the button reports SC_ToolButton for a point anywhere on it
// and the :hover here matches whenever the button as a whole is pointed at. The
// press it hands the menu is right either way - QToolButton::mousePressEvent
// asks subControlRect(), which the sheet does answer.
QString splitButtonMenuHalfStyleSheet(const QString& buttonSelector, const int cornerRadius, const ThemeTokens& tokens)
{
    const QString chevron = themedArrowFile(tokens.mutedText, false, scmInputArrowSize);
    if (chevron.isEmpty()) {
        return QString();
    }

    QString rules = buttonSelector
                    + qsl("::menu-button { subcontrol-origin: padding; subcontrol-position: center right; width: %1px;"
                          " border: none; border-left: %2px solid %3; background: transparent;"
                          " border-top-right-radius: %4px; border-bottom-right-radius: %4px; }")
                              .arg(QString::number(scmInputDropDownWidth), QString::number(scmInputBorderWidth), tokens.border.name(), QString::number(cornerRadius))
                    + buttonSelector + qsl("::menu-button:hover { background-color: %1; }").arg(tokens.accentSoft) + buttonSelector
                    + qsl("::menu-button:pressed { background-color: %1; }").arg(rgba(tokens.accent, scmSplitHalfPressedWash)) + buttonSelector
                    + qsl("::menu-arrow { image: url(\"%1\"); width: %2px; height: %2px; }").arg(chevron, QString::number(scmInputArrowSize));

    const QString accentChevron = themedArrowFile(tokens.accent, false, scmInputArrowSize);
    if (!accentChevron.isEmpty()) {
        rules += buttonSelector + qsl("::menu-arrow:hover, ") + buttonSelector
                 + qsl("::menu-arrow:pressed { image: url(\"%1\"); width: %2px; height: %2px; }").arg(accentChevron, QString::number(scmInputArrowSize));
    }
    return rules;
}

QString toolBarStyleSheet(const QString& toolBarSelector, const ToolBarSeam seam, const ThemeTokens& tokens)
{
    // The bar can be dragged to another edge of the window or floated, and what
    // says so is the grip at its leading end. Styling the bar at all takes the
    // platform's own handle with it, which leaves a pair of faint dots barely
    // on the page - so the same six a pattern row is dragged by are inked to
    // the palette and pointed at here.
    const QString gripAcross = gripGlyphFile(tokens.mutedText, false);
    const QString gripAlong = gripGlyphFile(tokens.mutedText, true);
    const QString seamEdge = seam == ToolBarSeam::Bottom ? qsl("bottom") : qsl("top");

    return toolBarSelector + qsl(" { background-color: %1; border: none; border-%2: 1px solid %3; spacing: 2px; padding: 4px 6px; }").arg(tokens.page.name(), seamEdge, tokens.border.name())
           + toolBarSelector
           + qsl("::separator { background-color: %1; width: 1px; margin: 5px 6px; }").arg(tokens.border.name())
           // A background rather than an image: a sub-control's image is
           // stretched to fill it, which turns six small dots into a wash
           // across the whole handle
           + toolBarSelector
           + qsl("::handle { background-image: url(%1); background-repeat: no-repeat; background-position: center; width: %2px; margin: 6px 2px; }")
                     .arg(gripAcross, QString::number(scmToolBarGripExtent))
           + toolBarSelector
           + qsl("::handle:vertical { background-image: url(%1); height: %2px; margin: 2px 6px; }").arg(gripAlong, QString::number(scmToolBarGripExtent))
           // The transparent border keeps the label from stepping sideways when
           // a hovered button gains one
           + toolBarSelector
           + qsl(" QToolButton { color: %1; border: 1px solid transparent; border-radius: %2px; padding: %3px %4px; }")
                     .arg(tokens.mutedText.name(), QString::number(scmToolBarButtonRadius), QString::number(scmToolBarButtonPaddingVertical), QString::number(scmToolBarButtonPaddingHorizontal))
           // The accent rather than the words' full tone: the glyph beside the
           // word is inked accentText for QIcon::Active, so the two halves of a
           // hovered button light up as one
           + toolBarSelector + qsl(" QToolButton:hover { color: %1; background-color: %2; }").arg(tokens.accentText.name(), tokens.hoverSoft) + toolBarSelector
           + qsl(" QToolButton:pressed { background-color: %1; }").arg(tokens.accentSoft)
           // A checkable action that is on is held down in every way but the
           // pointer, so it is drawn the way a held button is - and its glyph is
           // already inked accentText for QIcon::On, so the word beside it has
           // to light with it
           + toolBarSelector + qsl(" QToolButton:checked { color: %1; background-color: %2; }").arg(tokens.accentText.name(), tokens.accentSoft) + toolBarSelector
           + qsl(" QToolButton:disabled { color: %1; }").arg(tokens.disabledText.name());
}

QString disclosureButtonStyleSheet(const QString& buttonSelector, const ThemeTokens& tokens)
{
    return buttonSelector
           + qsl(" { color: %1; border: %2px solid %3; border-radius: %4px; padding: %5px %6px; background-color: %7; }")
                     .arg(tokens.mutedText.name(), QString::number(scmInputBorderWidth), tokens.border.name(), QString::number(scmRadiusInput), QString::number(scmDisclosurePaddingVertical))
                     .arg(QString::number(scmButtonPaddingHorizontal), buttonFaceColour(tokens).name())
           + buttonSelector + qsl(":hover { color: %1; background-color: %2; }").arg(tokens.accentText.name(), tokens.hoverSoft) + buttonSelector
           + qsl(":checked { color: %1; border: %2px solid %3; background-color: %4; }").arg(tokens.accentText.name(), QString::number(scmInputBorderWidth), tokens.accent.name(), tokens.accentSoft)
           + buttonSelector + qsl(":focus { border-color: %1; }").arg(tokens.accent.name());
}

// What a control drops down when it is not a list of values: the menu under a
// toolbar button, the options behind a search field, the menu the pointer opens
// over a code pane. Drawn as the list a combo box drops down is - both are a
// list opened out of the window it belongs to, so both take the hairline, the
// corner and the accent under the chosen row - on the card tone rather than the
// field's, since a menu is a panel lifted off the page where a combo box's list
// is the field it came out of, opened up.
//
// Nothing here names a font. A menu is read in the font of whatever it hangs
// from, and a size written into these rules would take that away.
QString menuStyleSheet(const ThemeTokens& tokens, const QString& selectorPrefix)
{
    const auto scoped = [&selectorPrefix](const QStringList& selectors) {
        return scopedTo(selectorPrefix, selectors);
    };

    QString rules =
            scoped({qsl("QMenu")})
            + qsl(" { background-color: %1; color: %2; border: %3px solid %4; border-radius: %5px; padding: %6px; }")
                      .arg(tokens.card.name(), tokens.text.name(), QString::number(scmInputBorderWidth), tokens.border.name(), QString::number(scmRadiusInput))
                      .arg(QString::number(scmMenuSurfacePadding))
            // The row is a word in a box the way a chip is, so it takes the
            // chip's corner rather than the field's - it is a good deal shorter
            // than the menu it sits in, and the menu's own corner repeated
            // inside itself reads as a second frame
            + scoped({qsl("QMenu::item")})
            + qsl(" { background: transparent; border-radius: %1px; padding: %2px %3px %2px %4px; }")
                      .arg(QString::number(scmRadiusChip), QString::number(scmMenuItemPaddingVertical), QString::number(scmMenuItemPaddingTrailing), QString::number(scmMenuItemPaddingLeading))
            // What the pointer is on, or what the keyboard has walked to - one
            // state in a menu, and drawn the way a combo box's list draws its
            // chosen row
            + scoped({qsl("QMenu::item:selected")}) + qsl(" { background-color: %1; color: %2; }").arg(tokens.accentSoft, tokens.accentText.name()) + scoped({qsl("QMenu::item:disabled")})
            + qsl(" { color: %1; }").arg(tokens.disabledText.name())
            // A seam between two runs of rows, inset by the room a row leaves
            // at its leading end rather than run across the whole width: what
            // it parts is the rows either side of it, not the frame
            + scoped({qsl("QMenu::separator")})
            + qsl(" { height: 1px; background-color: %1; margin: %2px %3px; }").arg(tokens.separator.name(), QString::number(scmMenuSurfacePadding), QString::number(scmMenuItemPaddingLeading));

    // A row that is switched on or off carries the one mark every other choice
    // is made with, written out by the builder the check boxes are drawn from.
    // The states a menu has no way to enter - a hover, a focus, a press, a third
    // answer - are simply never matched: what the pointer is on is the row, and
    // the rule above is where that is said. Radio-style rows would come out a
    // box rather than a circle, which none of these menus offers.
    const ChoiceMarkInks inks = choiceMarkInks(tokens);
    rules += choiceMarkRules(scoped({qsl("QMenu::indicator")}), scmChoiceBoxRadius, inks.tick, QString(), inks.quietTick, QString(), inks, tokens)
             // Where that mark stands, and the picture on a row carrying one
             // instead: both are placed from the row's leading edge, which is
             // the edge its highlight is drawn from, so both are moved into the
             // room the row leaves them. A placement rather than a second way of
             // drawing either of them.
             + scoped({qsl("QMenu::indicator"), qsl("QMenu::icon")}) + qsl(" { left: %1px; }").arg(QString::number(scmMenuItemMarkInset));
    return rules;
}

// A row of chips on the page rather than the folder tabs a platform cuts. The
// three states are the ones every other list of choices in this design carries
// - quiet, washed under the pointer, filled in the accent while chosen - so a
// tab is read as the same kind of thing as a sidebar's row and a menu's, and
// the pane under it is left to whatever the window draws there.
QString tabBarStyleSheet(const QString& tabWidgetSelector, const ThemeTokens& tokens)
{
    // The bar belongs to the tab widget rather than sitting under it, so it is
    // reached by both selectors: the sub-control for what the widget lays out,
    // and the descendant for the QTabBar itself.
    const QString bar = tabWidgetSelector + qsl(" QTabBar");

    QString rules = tabWidgetSelector
                    + qsl("::pane { border: none; background: transparent; margin: %1px; }").arg(QString::number(scmTabPaneInset))
                    // Where the row of chips starts, which is where the pane under it
                    // does
                    + tabWidgetSelector + qsl("::tab-bar { left: %1px; }").arg(QString::number(scmTabStripInset)) + bar
                    + qsl(" { background: transparent; border: none; }")
                    // The transparent border keeps the word from stepping sideways when
                    // a chosen tab gains one, the way a toolbar's buttons do
                    + bar
                    + qsl("::tab { background: transparent; color: %1; border: %2px solid transparent; border-radius: %3px;"
                          " padding: %4px %5px; margin-right: %6px; }")
                              .arg(tokens.mutedText.name(), QString::number(scmInputBorderWidth), QString::number(scmRadiusChip), QString::number(scmTabPaddingVertical))
                              .arg(QString::number(scmTabPaddingHorizontal), QString::number(scmTabGap))
                    + bar + qsl("::tab:hover { background-color: %1; color: %2; }").arg(tokens.hoverSoft, tokens.accentText.name()) + bar
                    + qsl("::tab:selected { background-color: %1; color: %2; }").arg(tokens.accentSoft, tokens.accentText.name())
                    // Where the keyboard is, said the way every other control in this
                    // design says it
                    + bar + qsl("::tab:focus { border-color: %1; }").arg(tokens.accent.name()) + bar + qsl("::tab:disabled { color: %1; }").arg(tokens.disabledText.name());

    // The cross that closes a tab. Drawn from the one x in the resources,
    // tinted into the cache for the rule to point at - and left out where the
    // cache could not be written, the way every other rule aimed at a picture
    // is, so the tab keeps the platform's own cross rather than losing it.
    const QString quietCross = themedGlyphFile(qsl(":/icons/editor-clear.svg"), tokens.mutedText, scmTabCloseBoxSize, scmTabCloseGlyphSize);
    const QString litCross = themedGlyphFile(qsl(":/icons/editor-clear.svg"), tokens.accentText, scmTabCloseBoxSize, scmTabCloseGlyphSize);
    if (!quietCross.isEmpty()) {
        rules += bar + qsl("::close-button { image: url(\"%1\"); subcontrol-position: right; width: %2px; height: %2px; }").arg(quietCross, QString::number(scmTabCloseBoxSize));
        if (!litCross.isEmpty()) {
            rules += bar + qsl("::close-button:hover { image: url(\"%1\"); width: %2px; height: %2px; }").arg(litCross, QString::number(scmTabCloseBoxSize));
        }
    }
    return rules;
}

namespace {
// The box a tab's cross is drawn in is the button widget's own size, and
// nothing a rule says reaches it. Qt's close button asks the style for
// PM_TabCloseIndicatorWidth in its constructor and resizes itself to the
// answer; QStyleSheetStyle has no case for that metric, so the ::close-button
// rule's width and height are never read and the style underneath answers -
// twenty pixels under Fusion, which is what the dark appearance is drawn on,
// fourteen under the macOS style. The picture the rule points at is drawn at
// scmTabCloseBoxSize with a mark scmTabCloseGlyphSize across, and it is painted
// into whatever rectangle the button ended up with, so on a twenty pixel button
// an eight pixel mark comes out at eleven - visibly heavier than the same cross
// on the profile strip.
//
// The moment to say otherwise is QEvent::ChildPolished. The button's sizeHint()
// calls ensurePolished() before it asks the style anything, and that sends the
// event to the bar - before the hint is answered and before the constructor's
// resize() runs. A resize is bounded by the widget's minimum and maximum size,
// so a box fixed here is what the constructor's resize comes out at, and
// QTabBar::setTabButton() then lays the tabs out from a size that is already
// the recipe's. The bar's own two scroll buttons are QToolButtons and are left
// with whatever the platform gave them, the way the sheet leaves their arrows.
//
// Where that box stands is the same story a second time. A ::tab rule with a box
// makes QStyleSheetStyle answer 0 for PM_TabBarTabHSpace, and its
// SE_TabBarTabRightButton hands QCommonStyle the tab's raw rectangle - only
// SE_TabBarTabText is given the rule's contents rect - so the cross is placed at
// tab->rect.right() - width, which is scmTabGap past the drawn chip and a
// further scmTabPaddingHorizontal + scmInputBorderWidth outside the box the word
// is laid in. The rule's subcontrol-position decides the side and nothing else.
// QTabBarPrivate::layoutTab() applies that rectangle with move(), so a
// QEvent::Move on the button is the one moment after every layout of the bar,
// and the cross is put back inside the padding there. Moving it sends a second
// Move, which is why the wanted position is compared before it is asked for.
class TabCloseBoxKeeper : public QObject
{
public:
    // Named rather than found by type: without Q_OBJECT - which it has no
    // signals to want - a qobject_cast for this class matches every QObject
    static constexpr char scmName[] = "uiDesignTabCloseBoxKeeper";

    explicit TabCloseBoxKeeper(QObject* pParent)
    : QObject(pParent)
    {
        setObjectName(QLatin1StringView(scmName));
    }

    // Both halves of what a cross is owed, for a button the filter met on
    // ChildPolished and for one that was already on the bar when it was styled
    void keep(QAbstractButton* pButton)
    {
        pButton->setFixedSize(scmTabCloseBoxSize, scmTabCloseBoxSize);
        pButton->installEventFilter(this);
    }

    void placeInsideThePadding(QWidget* pButton)
    {
        auto* pTabBar = qobject_cast<QTabBar*>(parent());
        if (!pTabBar || !pButton || pButton->parentWidget() != pTabBar) {
            return;
        }
        for (int i = 0; i < pTabBar->count(); ++i) {
            for (const QTabBar::ButtonPosition side : {QTabBar::LeftSide, QTabBar::RightSide}) {
                if (pTabBar->tabButton(i, side) != pButton) {
                    continue;
                }
                // The gap that tells two chips apart comes off the trailing
                // side, so a chip is the tab's rectangle less that gap. The
                // height is the chip's own, and Qt has already centred the
                // button on it.
                const QRect tab = pTabBar->tabRect(i);
                const int padding = scmInputBorderWidth + scmTabPaddingHorizontal;
                QRect wanted = pButton->geometry();
                wanted.moveLeft(side == QTabBar::RightSide ? tab.right() - scmTabGap - padding - pButton->width() + 1 : tab.left() + padding);
                wanted = QStyle::visualRect(pTabBar->layoutDirection(), tab, wanted);
                if (pButton->pos() != wanted.topLeft()) {
                    pButton->move(wanted.topLeft());
                }
                return;
            }
        }
    }

protected:
    bool eventFilter(QObject* pWatched, QEvent* pEvent) override
    {
        if (pEvent->type() == QEvent::ChildPolished) {
            QObject* pChild = static_cast<QChildEvent*>(pEvent)->child();
            auto* pButton = qobject_cast<QAbstractButton*>(pChild);
            if (pButton && !qobject_cast<QToolButton*>(pChild)) {
                keep(pButton);
            }
        } else if (pEvent->type() == QEvent::Move) {
            placeInsideThePadding(qobject_cast<QWidget*>(pWatched));
        }
        return QObject::eventFilter(pWatched, pEvent);
    }
};
} // namespace

void prepareTabStrip(QTabBar* pTabBar)
{
    if (!pTabBar) {
        return;
    }

    // In document mode the macOS style fills the whole bar with a band of its
    // own behind the tabs - neither the page the chips lie on nor anything a
    // rule asked for, and not something background: transparent takes away. It
    // is drawn as the bar's base, so the bar is asked not to draw one.
    pTabBar->setDrawBase(false);

    QObject* pFound = pTabBar->findChild<QObject*>(QLatin1StringView(TabCloseBoxKeeper::scmName), Qt::FindDirectChildrenOnly);
    auto* pKeeper = pFound ? static_cast<TabCloseBoxKeeper*>(pFound) : new TabCloseBoxKeeper(pTabBar);
    if (!pFound) {
        pTabBar->installEventFilter(pKeeper);
    }

    // A bar styled after it was filled has crosses the platform already sized
    // and already placed, which the filter above was not there to catch. The
    // style change is what makes the bar lay its tabs out again, since it
    // measures them from the widgets' current size().
    bool anyCut = false;
    for (int i = 0; i < pTabBar->count(); ++i) {
        for (const QTabBar::ButtonPosition side : {QTabBar::LeftSide, QTabBar::RightSide}) {
            auto* pCross = qobject_cast<QAbstractButton*>(pTabBar->tabButton(i, side));
            if (!pCross || qobject_cast<QToolButton*>(pCross)) {
                continue;
            }
            anyCut = anyCut || pCross->size() != QSize(scmTabCloseBoxSize, scmTabCloseBoxSize);
            pKeeper->keep(pCross);
            pKeeper->placeInsideThePadding(pCross);
        }
    }
    if (anyCut) {
        QEvent styleChanged(QEvent::StyleChange);
        QApplication::sendEvent(pTabBar, &styleChanged);
    }
}

void keepClickFocusOffControls(QWidget* pRoot)
{
    if (!pRoot) {
        return;
    }
    const auto leaveItToTheKeyboard = [](QWidget* pControl) {
        // A control asking for no focus at all is left saying so: the point
        // here is which way focus arrives, not whether it can
        if (pControl->focusPolicy() == Qt::NoFocus) {
            return;
        }
        const auto* pCard = qobject_cast<QGroupBox*>(pControl);
        if (!qobject_cast<QCheckBox*>(pControl) && !qobject_cast<QRadioButton*>(pControl) && !qobject_cast<QPushButton*>(pControl) && !(pCard && pCard->isCheckable())) {
            return;
        }
        pControl->setFocusPolicy(Qt::TabFocus);
    };
    leaveItToTheKeyboard(pRoot);
    for (QWidget* pControl : pRoot->findChildren<QWidget*>()) {
        leaveItToTheKeyboard(pControl);
    }
}

namespace {
// What the frame round a dropped-down list is painted with, said as nothing.
// The sheet cannot say it: a stylesheet gives a QFrame subclass no styled
// background, so its rule for the container reaches only that widget's palette
// - and Qt::WA_WindowPropagation, which the container carries so a popup is
// drawn in the font and the colours of the box it belongs to, hands it the
// field's brushes anyway. Only these two roles are named, so the ink the list's
// words are written in still follows the appearance.
void namePopupSurfaceAsNothing(QWidget* pPopup)
{
    QPalette unpainted = pPopup->palette();
    unpainted.setBrush(QPalette::Window, Qt::transparent);
    unpainted.setBrush(QPalette::Base, Qt::transparent);
    pPopup->setPalette(unpainted);
}

// ...and said again on the way in. An appearance change swaps the application's
// style, which re-resolves every widget's palette - after the shell has
// restyled, so the shell's own pass cannot be the last word. A popup is shown
// before it is painted, which is the last moment that can be.
class PopupSurfaceKeeper : public QObject
{
public:
    // Named rather than found by type: without Q_OBJECT - which it has no
    // signals to want - a qobject_cast for this class matches every QObject
    static constexpr char scmName[] = "uiDesignPopupSurfaceKeeper";

    explicit PopupSurfaceKeeper(QObject* pParent)
    : QObject(pParent)
    {
        setObjectName(QLatin1StringView(scmName));
    }

protected:
    bool eventFilter(QObject* pWatched, QEvent* pEvent) override
    {
        if (pEvent->type() == QEvent::Show) {
            if (auto* pPopup = qobject_cast<QWidget*>(pWatched)) {
                namePopupSurfaceAsNothing(pPopup);
            }
        }
        return QObject::eventFilter(pWatched, pEvent);
    }
};
} // namespace

void letPopupsTakeTheFieldsCorner(QWidget* pRoot)
{
    if (!pRoot) {
        return;
    }
    const auto openTheCorner = [](QComboBox* pComboBox) {
        // view() makes the container on the first call, which is what lets this
        // run before the box has ever been dropped down
        QAbstractItemView* pList = pComboBox->view();
        if (!pList) {
            return;
        }
        // The window is the container the list sits in, not the list: the
        // attribute and the brushes both have to be on whatever the platform
        // makes a surface for
        QWidget* pPopup = pList->window();
        if (!pPopup || pPopup == pComboBox->window()) {
            return;
        }
        pPopup->setAttribute(Qt::WA_TranslucentBackground);
        namePopupSurfaceAsNothing(pPopup);
        if (!pPopup->findChild<QObject*>(QLatin1StringView(PopupSurfaceKeeper::scmName), Qt::FindDirectChildrenOnly)) {
            pPopup->installEventFilter(new PopupSurfaceKeeper(pPopup));
        }
    };
    // A menu is its own popup window rather than a list living in one, so the
    // three things are said on the menu itself. It is the same corner and for
    // the same reason: menuStyleSheet() rounds a frame that would otherwise be
    // drawn over the square window Qt fills first.
    const auto openTheMenusCorner = [](QMenu* pMenu) {
        pMenu->setAttribute(Qt::WA_TranslucentBackground);
        namePopupSurfaceAsNothing(pMenu);
        if (!pMenu->findChild<QObject*>(QLatin1StringView(PopupSurfaceKeeper::scmName), Qt::FindDirectChildrenOnly)) {
            pMenu->installEventFilter(new PopupSurfaceKeeper(pMenu));
        }
    };
    if (auto* pComboBox = qobject_cast<QComboBox*>(pRoot)) {
        openTheCorner(pComboBox);
    }
    for (QComboBox* pComboBox : pRoot->findChildren<QComboBox*>()) {
        openTheCorner(pComboBox);
    }
    if (auto* pMenu = qobject_cast<QMenu*>(pRoot)) {
        openTheMenusCorner(pMenu);
    }
    for (QMenu* pMenu : pRoot->findChildren<QMenu*>()) {
        openTheMenusCorner(pMenu);
    }
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
    // The same mark the check boxes under the card carry, so that turning a
    // whole card on reads as the same act as turning one option on
    const ChoiceMarkInks inks = choiceMarkInks(tokens);
    return choiceMarkRules(qsl("QGroupBox[%1=\"true\"]::indicator").arg(QLatin1StringView(cardProperty)), scmChoiceBoxRadius, inks.tick, inks.dash, inks.quietTick, inks.quietDash, inks, tokens);
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

int typeSize(const TypeStep step)
{
    const QFont applicationFont = QApplication::font();
    // A font set in pixels answers -1 for its point size, so the size it is
    // actually rendered at is asked of QFontInfo instead
    qreal base = applicationFont.pointSizeF();
    if (base <= 0.0) {
        base = QFontInfo(applicationFont).pointSizeF();
    }

    qreal ratio = 1.0;
    switch (step) {
    case TypeStep::Caption:
        ratio = scmTypeRatio_caption;
        break;
    case TypeStep::Body:
        break;
    case TypeStep::Title:
        ratio = scmTypeRatio_title;
        break;
    case TypeStep::Display:
        ratio = scmTypeRatio_display;
        break;
    }
    return std::max(1, qRound(base * ratio));
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
