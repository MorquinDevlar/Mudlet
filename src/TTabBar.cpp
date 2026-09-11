/***************************************************************************
 *   Copyright (C) 2018, 2020-2021 by Stephen Lyons                        *
 *                                               - slysven@virginmedia.com *
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

/***************************************************************************
 *   The profile strip: the design's chips, drawn by a style of its own     *
 *   because a stylesheet rule would take the tab away from it and with     *
 *   the tab the per-tab font emphasis and the connection indicator.        *
 ***************************************************************************/

#include "TTabBar.h"

#include "uiDesign.h"
#include "utils.h"

#include <QApplication>
#include <QEvent>
#include <QStyleOption>
#include <QStyleOptionTab>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QVariant>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QScreen>
#include <QDateTime>

// Constants for improved drag detection
static const int VERTICAL_MOVEMENT_RATIO_THRESHOLD = 60; // Percentage of movement that must be vertical
static const int TAB_REORDER_DELAY_MS = 150;             // Delay before allowing tab detachment

// A wash is composited rather than blitted, so what a word on one is actually
// read against is the mix rather than the wash's own colour
static QColor composited(const QColor& ink, const QColor& surface)
{
    const qreal weight = ink.alphaF();
    return QColor::fromRgbF(ink.redF() * weight + surface.redF() * (1.0 - weight), ink.greenF() * weight + surface.greenF() * (1.0 - weight), ink.blueF() * weight + surface.blueF() * (1.0 - weight));
}

// What a chip the reader chose is filled with on a light page: the accent taken
// towards black until the field's own white - what Qt writes on every well in
// the window - reads on it at the floor every word of the design clears. The
// walk ends at black, which white clears many times over, so it always lands on
// a colour rather than falling back on the page's words.
static QColor chosenFillOnLightPage(const uiDesign::ThemeTokens& tokens)
{
    return uiDesign::readableOn(tokens.field, tokens.accent, tokens.text, uiDesign::scmTextMinimumRatio);
}

// Which tab an option describes. The pixmap Qt drags a reordered tab around as
// is painted from an option whose rect has been moved to the origin, so tabAt()
// on that rect names the wrong tab or none; tabIndex carries the answer and has
// since Qt 6.8.
int TStyle::tabIndexOf(const QStyleOptionTab* tabOption) const
{
    if (!tabOption) {
        return -1;
    }
    if (tabOption->tabIndex >= 0) {
        return tabOption->tabIndex;
    }
    return mpTabBar ? mpTabBar->tabAt(tabOption->rect.center()) : -1;
}

TStyle::ChipLayout TStyle::layoutChip(const QStyleOptionTab* tabOption) const
{
    ChipLayout layout;
    if (!tabOption) {
        return layout;
    }

    const QRect bounds = tabOption->rect;
    // The gap between two chips comes off the trailing side, which is what the
    // sheet's margin-right does for the strips drawn by a rule
    const QRect chip = bounds.adjusted(0, 0, -uiDesign::scmTabGap, 0);
    const int horizontalInset = uiDesign::scmInputBorderWidth + uiDesign::scmTabPaddingHorizontal;
    const int verticalInset = uiDesign::scmInputBorderWidth + uiDesign::scmTabPaddingVertical;
    const QRect inner = chip.adjusted(horizontalInset, verticalInset, -horizontalInset, -verticalInset);

    int left = inner.left();
    int right = inner.right();

    QRect indicator;
    if (tabConnectionIndicator(tabIndexOf(tabOption)) != TabConnectionIndicator::None) {
        indicator = QRect(left, chip.top() + (chip.height() - sIndicatorDiameter) / 2, sIndicatorDiameter, sIndicatorDiameter);
        left += sIndicatorReservedWidth;
    }

    QRect close;
    if (!tabOption->rightButtonSize.isEmpty()) {
        const QSize wanted = tabOption->rightButtonSize;
        close = QRect(inner.right() - wanted.width() + 1, chip.top() + (chip.height() - wanted.height()) / 2, wanted.width(), wanted.height());
        right = close.left() - 1 - sCloseButtonGap;
    }

    const QRect text(left, inner.top(), qMax(0, right - left + 1), inner.height());

    layout.chip = QStyle::visualRect(tabOption->direction, bounds, chip);
    layout.indicator = indicator.isNull() ? indicator : QStyle::visualRect(tabOption->direction, bounds, indicator);
    layout.text = QStyle::visualRect(tabOption->direction, bounds, text);
    layout.close = close.isNull() ? close : QStyle::visualRect(tabOption->direction, bounds, close);
    return layout;
}

// The two appearances take two treatments of the chip the reader chose, and the
// reason is the page under it. On a light page the platform's own chosen tab is
// a filled one written on in white, and this chip follows it: the wash was too
// pale against the grey page to say which profile is on show, so the accent is
// darkened until that white reads on it and the word, the cross and a ring are
// written in it. On a fill there is nothing left for a bar or an outline to say,
// both being the accent on the accent. On a dark page a solid fill would be the
// loudest thing in the window, so the chip keeps the wash, the bar the settings
// sidebar draws down its chosen row and the outline, and its word is walked
// against the wash as that is really composited.
TStyle::ChipFace TStyle::chipFace(const QStyleOptionTab* tabOption, const uiDesign::ThemeTokens& tokens) const
{
    ChipFace face;
    face.fill = QColor(Qt::transparent);
    face.word = tokens.mutedText;
    face.surface = tokens.page;

    if (!(tabOption->state & State_Enabled)) {
        face.word = tokens.disabledText;
        return face;
    }

    if (tabOption->state & State_Selected) {
        if (!tokens.darkPage) {
            face.fill = chosenFillOnLightPage(tokens);
            face.surface = face.fill;
            face.word = tokens.field;
            return face;
        }
        face.fill = tokens.accentWash;
        face.surface = composited(tokens.accentWash, tokens.page);
        // accentText is walked only to the floor every word of the design
        // clears, and on this strip's grey page the accent at that floor fades
        // into its own wash - so the word naming the profile on show is walked
        // on again against the wash as it is really composited
        face.word = uiDesign::readableOn(face.surface, tokens.accentText, tokens.text, sChosenWordMinimumRatio);
        face.bar = true;
        face.outline = true;
        return face;
    }

    if (tabOption->state & State_MouseOver) {
        face.fill = tokens.hoverWash;
        face.surface = composited(tokens.hoverWash, tokens.page);
        face.word = tokens.accentText;
    }
    return face;
}

void TStyle::drawControl(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    const auto* tabOption = qstyleoption_cast<const QStyleOptionTab*>(option);
    if (tabOption && (element == CE_TabBarTab || element == CE_TabBarTabShape || element == CE_TabBarTabLabel)) {
        paintChip(painter, tabOption, widget, element != CE_TabBarTabLabel, element != CE_TabBarTabShape);
        return;
    }

    // Everything this style does not draw goes to the application style
    // (DarkTheme) rather than to our proxy base, which resolves to a separate
    // Fusion instance that doesn't have dark palette colors on Windows 10
    qApp->style()->drawControl(element, option, painter, widget);
}

void TStyle::paintChip(QPainter* painter, const QStyleOptionTab* tabOption, const QWidget* widget, const bool shape, const bool label) const
{
    const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
    const ChipLayout layout = layoutChip(tabOption);
    const ChipFace face = chipFace(tabOption, tokens);
    const bool chosen = tabOption->state & State_Selected;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    if (shape) {
        if (face.fill.alpha() > 0) {
            QPainterPath path;
            path.addRoundedRect(QRectF(layout.chip), uiDesign::scmRadiusChip, uiDesign::scmRadiusChip);
            painter->fillPath(path, face.fill);
        }
        if (face.bar) {
            uiDesign::paintAccentBar(painter, layout.chip, tokens.accent, uiDesign::scmRadiusChip);
        }
        // A border elsewhere in the design says where the keyboard is; this bar
        // takes none, so on it a border can only mean chosen. Last, so the bar's
        // outer edge is this outline rather than the other way about, and half a
        // pixel in on every side, so a one-pixel line lands on whole pixels
        // rather than across two. Its width was always in the metrics, so
        // nothing moved to make room.
        if (face.outline) {
            QPainterPath outline;
            outline.addRoundedRect(QRectF(layout.chip).adjusted(0.5, 0.5, -0.5, -0.5), uiDesign::scmRadiusChip, uiDesign::scmRadiusChip);
            painter->strokePath(outline, QPen(tokens.accent, uiDesign::scmInputBorderWidth));
        }
    }

    if (label) {
        const QString tabName = mpTabBar ? mpTabBar->tabData(tabIndexOf(tabOption)).toString() : QString();
        QFont font = widget ? widget->font() : painter->font();
        // Bold on a tab that is not the one on show says it has new output; on
        // the chosen one it says it is the one on show, the way the settings
        // sidebar draws its chosen row, and the wash tells the two apart
        font.setBold(chosen || mBoldTabsSet.contains(tabName));
        font.setItalic(mItalicTabsSet.contains(tabName));
        font.setUnderline(mUnderlineTabsSet.contains(tabName));
        painter->setFont(font);

        painter->setPen(face.word);
        const int mnemonic = qApp->style()->styleHint(SH_UnderlineShortcut, tabOption, widget) ? Qt::TextShowMnemonic : Qt::TextHideMnemonic;
        painter->drawText(layout.text, Qt::AlignCenter | mnemonic, tabOption->text);

        const TabConnectionIndicator state = tabConnectionIndicator(tabIndexOf(tabOption));
        if (state != TabConnectionIndicator::None && !layout.indicator.isNull()) {
            paintConnectionIndicator(painter, layout.indicator, state, tokens, face.surface, face.word);
        }
    }

    painter->restore();
}

void TStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    if (element == PE_IndicatorTabClose && option) {
        // Only the option, never the widget: with a profile stylesheet on the
        // bar this call arrives through QStyleSheetStyle, which hands its base
        // the tab bar in place of the button that asked
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const bool lit = option->state & (State_Raised | State_Sunken);
        // The cross on the chip the reader chose stands on that chip's fill
        // wherever it has one, so there it is written in the white the word
        // beside it is, lit or not: a filled chip has no wash to light up, and
        // the chrome tone would be swallowed by the fill
        const bool onAFill = (option->state & State_Selected) && !tokens.darkPage;
        const QColor ink = !(option->state & State_Enabled) ? tokens.disabledText : (onAFill ? tokens.field : (lit ? tokens.accentText : tokens.mutedText));
        const qreal ratio = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        const QPixmap cross = uiDesign::themedGlyphPixmap(qsl(":/icons/editor-clear.svg"), ink, uiDesign::scmTabCloseBoxSize, uiDesign::scmTabCloseGlyphSize, ratio);
        if (!cross.isNull()) {
            const QSize drawn = cross.deviceIndependentSize().toSize();
            painter->drawPixmap(QPoint(option->rect.left() + (option->rect.width() - drawn.width()) / 2, option->rect.top() + (option->rect.height() - drawn.height()) / 2), cross);
        }
        return;
    }

    if (element == PE_FrameTabBarBase) {
        // The band a platform fills the whole strip with behind the tabs is
        // neither the page the chips lie on nor anything the design asked for
        return;
    }

    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

QRect TStyle::subElementRect(SubElement element, const QStyleOption* option, const QWidget* widget) const
{
    const auto* tabOption = qstyleoption_cast<const QStyleOptionTab*>(option);
    if (tabOption) {
        switch (element) {
        case SE_TabBarTabText:
            return layoutChip(tabOption).text;
        case SE_TabBarTabRightButton:
            return layoutChip(tabOption).close;
        case SE_TabBarTabLeftButton:
            // Nothing is ever put on the leading edge: the cross trails the word
            return QRect();
        default:
            break;
        }
    }

    return QProxyStyle::subElementRect(element, option, widget);
}

// The tab is drawn here rather than by any other style, so its measurements are
// this style's too - what QTabBar adds round a tab's text is the chip's padding
// and its border, and the gap that tells two chips apart. The rest still goes to
// the application style (DarkTheme) rather than to our proxy base, which
// resolves to a separate instance of the platform's native style and would
// answer for a look nothing here draws.
QSize TStyle::sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget) const
{
    if (type == CT_TabBarTab) {
        // QTabBar has already added the text, the frame and the buttons
        return contentsSize;
    }
    return qApp->style()->sizeFromContents(type, option, contentsSize, widget);
}

int TStyle::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const
{
    switch (metric) {
    case PM_TabBarTabHSpace:
        return 2 * (uiDesign::scmTabPaddingHorizontal + uiDesign::scmInputBorderWidth) + uiDesign::scmTabGap;
    case PM_TabBarTabVSpace:
        return 2 * (uiDesign::scmTabPaddingVertical + uiDesign::scmInputBorderWidth);
    case PM_TabCloseIndicatorWidth:
    case PM_TabCloseIndicatorHeight:
        return uiDesign::scmTabCloseBoxSize;
    // Chips lie side by side on the page: none of them overlaps its neighbour,
    // none of them steps sideways when it is chosen, and there is no base
    case PM_TabBarTabOverlap:
    case PM_TabBarTabShiftHorizontal:
    case PM_TabBarTabShiftVertical:
    case PM_TabBarBaseHeight:
    case PM_TabBarBaseOverlap:
        return 0;
    default:
        return qApp->style()->pixelMetric(metric, option, widget);
    }
}

int TStyle::styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const
{
    switch (hint) {
    case SH_TabBar_CloseButtonPosition:
        // The cross trails the word on every platform, as it does on the
        // notepad's strip - macOS alone would otherwise put it in front
        return QTabBar::RightSide;
    case SH_TabBar_Alignment:
        // The chips share the width of the bar between them, so this only
        // decides where a strip too narrow to fill one starts - and that is the
        // leading edge, as every other row in the design begins there
        return Qt::AlignLeft;
    default:
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
}

// Read against what the chip is actually filled with rather than against the
// page: on an accent fill a dot walked only off the page disappears into it, and
// the ring the chrome tone draws goes with it - so the ring is written in the
// same ink as the word beside it.
void TStyle::paintConnectionIndicator(QPainter* painter, const QRect& box, TabConnectionIndicator state, const uiDesign::ThemeTokens& tokens, const QColor& surface, const QColor& ringInk) const
{
    const auto stateInk = [&tokens, &surface](const qreal hue) {
        return uiDesign::readableOn(surface, uiDesign::stateColor(hue, tokens.darkPage), tokens.text, uiDesign::scmQuietMinimumRatio);
    };

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);

    switch (state) {
    case TabConnectionIndicator::Connected:
        painter->setBrush(stateInk(uiDesign::scmStateHue_ok));
        painter->drawEllipse(box);
        break;
    case TabConnectionIndicator::Connecting:
        painter->setBrush(stateInk(uiDesign::scmStateHue_warning));
        painter->drawEllipse(box);
        break;
    case TabConnectionIndicator::Error: {
        painter->setBrush(stateInk(uiDesign::scmStateHue_error));
        QPolygon triangle;
        triangle << QPoint(box.center().x(), box.top()) << QPoint(box.left(), box.bottom()) << QPoint(box.right(), box.bottom());
        painter->drawPolygon(triangle);
        break;
    }
    case TabConnectionIndicator::Disconnected:
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(ringInk, sIndicatorRingWidth));
        painter->drawEllipse(box);
        break;
    case TabConnectionIndicator::None:
        break;
    }

    painter->restore();
}

bool TStyle::setTabConnectionIndicator(const QString& tabName, TabConnectionIndicator state)
{
    if (tabName.isEmpty()) {
        return false;
    }
    // Removing a stale indicator is always safe (the tab may already be gone,
    // e.g. from removeTab); for inserts, refuse unknown names so we don't
    // accumulate phantom entries.
    if (state == TabConnectionIndicator::None) {
        return mConnectionIndicators.remove(tabName) > 0;
    }
    bool textIsInATab = false;
    for (int i = 0, total = mpTabBar->count(); i < total; ++i) {
        if (mpTabBar->tabData(i).toString() == tabName) {
            textIsInATab = true;
            break;
        }
    }
    if (!textIsInATab) {
        return false;
    }
    const auto it = mConnectionIndicators.constFind(tabName);
    if (it != mConnectionIndicators.cend() && it.value() == state) {
        return false;
    }
    mConnectionIndicators.insert(tabName, state);
    return true;
}

bool TStyle::setTabConnectionIndicator(int index, TabConnectionIndicator state)
{
    if (!mpTabBar || index < 0 || index >= mpTabBar->count()) {
        return false;
    }
    return setTabConnectionIndicator(mpTabBar->tabData(index).toString(), state);
}

TabConnectionIndicator TStyle::tabConnectionIndicator(const QString& tabName) const
{
    if (tabName.isEmpty()) {
        return TabConnectionIndicator::None;
    }
    return mConnectionIndicators.value(tabName, TabConnectionIndicator::None);
}

TabConnectionIndicator TStyle::tabConnectionIndicator(int index) const
{
    if (!mpTabBar || index < 0 || index >= mpTabBar->count()) {
        return TabConnectionIndicator::None;
    }
    return tabConnectionIndicator(mpTabBar->tabData(index).toString());
}

void TStyle::setNamedTabState(const QString& tabName, const bool state, QSet<QString>& effect)
{
    bool textIsInATab = false;
    for (int i = 0, total = mpTabBar->count(); i < total; ++i) {
        if (mpTabBar->tabData(i).toString() == tabName) {
            textIsInATab = true;
            break;
        }
    }

    if (!textIsInATab) {
        return;
    }

    if (state) {
        effect.insert(tabName);
    } else {
        effect.remove(tabName);
    }
}

void TStyle::setIndexedTabState(const int index, const bool state, QSet<QString>& effect)
{
    if (index < 0 || index >= mpTabBar->count()) {
        return;
    }

    if (state) {
        effect.insert(mpTabBar->tabData(index).toString());
    } else {
        effect.remove(mpTabBar->tabData(index).toString());
    }
}

bool TStyle::namedTabState(const QString& tabName, const QSet<QString>& effect) const
{
    bool textIsInATab = false;
    for (int i = 0, total = mpTabBar->count(); i < total; ++i) {
        if (mpTabBar->tabData(i).toString() == tabName) {
            textIsInATab = true;
            break;
        }
    }

    if (!textIsInATab) {
        return false;
    }

    return effect.contains(tabName);
}

bool TStyle::indexedTabState(const int index, const QSet<QString>& effect) const
{
    if (index < 0 || index >= mpTabBar->count()) {
        return false;
    }

    return effect.contains(mpTabBar->tabData(index).toString());
}

QSize TTabBar::tabSizeHint(int index) const
{
    QSize s = QTabBar::tabSizeHint(index);

    // Every tab is measured bold, whether or not it is drawn that way: the tab
    // on show is bold as well as one carrying new output, so a strip measured
    // only for the tabs that are drawn bold would step sideways every time the
    // reader chose another profile. The same reason uiDesign::sidebarRowWidth()
    // measures every sidebar row bold. Italic and underline stay per tab, since
    // nothing but that profile's own state ever puts them on.
    //
    // Note that this method must use (because it is associated with sizing the
    // text to show) the (possibly Qt modified to include an accelarator) actual
    // tabText and not the profile name that we have stored in the tabData:
    const QFontMetrics fm(font());
    const int w = fm.horizontalAdvance(tabText(index));

    QFont f = font();
    f.setBold(true);
    f.setItalic(mStyle.tabItalic(index));
    f.setUnderline(mStyle.tabUnderline(index));
    const QFontMetrics bfm(f);

    const int bw = bfm.horizontalAdvance(tabText(index));

    s.setWidth(s.width() - w + bw);

    // Reserve room for the connection indicator we paint ourselves
    // (see TabConnectionIndicator declaration for why).
    if (mStyle.tabConnectionIndicator(index) != TabConnectionIndicator::None) {
        s.setWidth(s.width() + TStyle::sIndicatorReservedWidth);
    }

    return s;
}

bool TTabBar::adoptCloseButton(const int index)
{
    auto* pClose = tabButton(index, QTabBar::RightSide);
    if (!pClose || pClose->testAttribute(Qt::WA_SetStyle)) {
        return false;
    }
    pClose->setStyle(&mStyle);
    pClose->resize(pClose->sizeHint());
    return true;
}

// Qt builds the cross as a child of the bar and sizes it from the application
// style before anything here can reach it - QTabBar::insertTab() lays the tabs
// out with that size and only then calls this. Handing the button our style
// gives it the design's box, and the refresh is what makes the bar measure the
// tabs again against it, since it reads the widgets' current size().
void TTabBar::tabInserted(int index)
{
    if (adoptCloseButton(index)) {
        refreshAfterApplicationStyleChange();
    }
    QTabBar::tabInserted(index);
}

// QApplication::setStyle() delivers StyleChange only to widgets without
// WA_SetStyle, and installing our TStyle sets that attribute - so the bar
// keeps tab sizes computed against the previous application style unless
// we hand it the event it was skipped for.
void TTabBar::refreshAfterApplicationStyleChange()
{
    // Also the moment a cross that was never handed our style gets it: a caller
    // that turned closable tabs on after adding its tabs got its buttons from
    // QTabBar::setTabsClosable(), which never calls tabInserted()
    for (int i = 0, total = count(); i < total; ++i) {
        adoptCloseButton(i);
    }
    QEvent event(QEvent::StyleChange);
    QApplication::sendEvent(this, &event);
    updateGeometry();
}

QString TTabBar::tabName(const int index) const
{
    QString tabName{tabData(index).toString()};
    return tabName;
}

int TTabBar::tabIndex(const QString& tabName) const
{
    int index = -1;
    if (tabName.isEmpty()) {
        return index;
    }
    const int total = count();
    while (++index < total) {
        if (!tabData(index).toString().compare(tabName)) {
            return index;
        }
    }
    return -1;
}

void TTabBar::removeTab(int index)
{
    if (index >= 0 && index < count()) {
        setTabBold(index, false);
        setTabItalic(index, false);
        setTabUnderline(index, false);
        setTabConnectionIndicator(index, TabConnectionIndicator::None);
        QTabBar::removeTab(index);
    }
}

void TTabBar::removeTab(const QString& tabName)
{
    const int index = tabIndex(tabName);
    if (index > -1) {
        setTabBold(index, false);
        setTabItalic(index, false);
        setTabUnderline(index, false);
        setTabConnectionIndicator(index, TabConnectionIndicator::None);
        QTabBar::removeTab(index);
    }
}

QStringList TTabBar::tabNames() const
{
    QStringList results;
    for (int i = 0, total = count(); i < total; ++i) {
        results << tabData(i).toString();
    }

    return results;
}

void TTabBar::applyPrefixToDisplayedText(const QString& tabName, const QString& prefix)
{
    const int index = tabIndex(tabName);
    if (index > -1) {
        QTabBar::setTabText(index, qsl("%1%2").arg(prefix, tabData(index).toString()));
    }
}

void TTabBar::applyPrefixToDisplayedText(int index, const QString& prefix)
{
    if (index > -1) {
        QTabBar::setTabText(index, qsl("%1%2").arg(prefix, tabData(index).toString()));
    }
}

void TTabBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        mDragStartPos = event->pos();
        mDragIndex = tabAt(event->pos());
        mDragStartTime = QDateTime::currentMSecsSinceEpoch();
        mPendingDetach = false;
    }
    QTabBar::mousePressEvent(event);
}

void TTabBar::mouseMoveEvent(QMouseEvent* event)
{
    // Check if we should start a drag operation
    if (!(event->buttons() & Qt::LeftButton) || mDragIndex == -1) {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    // Calculate movement vectors
    const QPoint movement = event->pos() - mDragStartPos;
    const int totalDistance = movement.manhattanLength();

    // Only proceed if we've moved enough to start considering detachment
    if (totalDistance >= QApplication::startDragDistance()) {
        // Calculate directional components
        const int horizontalDistance = qAbs(movement.x());
        const int verticalDistance = qAbs(movement.y());

        // Ensure we have enough time for Qt's tab reordering to be attempted first
        const qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        const qint64 timeSincePress = currentTime - mDragStartTime;

        // Only consider detachment after the reorder delay has passed
        if (timeSincePress >= TAB_REORDER_DELAY_MS) {
            // Improved directional detection: require predominantly vertical movement
            // and sufficient distance from the tab bar
            bool isVerticalMovement = false;

            if (verticalDistance > 0) {
                const int verticalPercentage = (verticalDistance * 100) / (horizontalDistance + verticalDistance);
                isVerticalMovement = verticalPercentage >= VERTICAL_MOVEMENT_RATIO_THRESHOLD;
            }

            // Check if we're significantly outside the tab bar area
            const QPoint globalPos = mapToGlobal(event->pos());
            const QRect tabBarGlobalRect = QRect(mapToGlobal(rect().topLeft()), rect().size());

            // Calculate distance from tab bar with enhanced threshold
            if (!tabBarGlobalRect.contains(globalPos) && isVerticalMovement) {
                const QPoint distanceFromBar = globalPos - tabBarGlobalRect.center();
                const int distanceFromBarManhattan = distanceFromBar.manhattanLength();

                // Use the improved threshold and ensure it's primarily vertical movement
                if (distanceFromBarManhattan > DETACH_DISTANCE_THRESHOLD) {
                    emit tabDetachRequested(mDragIndex, globalPos);
                    mDragIndex = -1; // Reset drag state
                    return;
                }
            }
        }
    }

    // Always call the parent implementation to allow normal tab reordering
    QTabBar::mouseMoveEvent(event);
}

void TTabBar::dragEnterEvent(QDragEnterEvent* event)
{
    const QMimeData* mimeData = event->mimeData();

    if (mimeData->hasFormat("application/x-mudlet-tab")) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TTabBar::dragMoveEvent(QDragMoveEvent* event)
{
    const QMimeData* mimeData = event->mimeData();

    if (mimeData->hasFormat("application/x-mudlet-tab")) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TTabBar::dropEvent(QDropEvent* event)
{
    const QMimeData* mimeData = event->mimeData();

    if (mimeData->hasFormat("application/x-mudlet-tab")) {
        const QString tabName = QString::fromUtf8(mimeData->data("application/x-mudlet-tab"));
        const int dropIndex = tabAt(event->position().toPoint());
        emit tabReattachRequested(tabName, dropIndex);
        event->acceptProposedAction();
    }
}

void TTabBar::onDetachedTabReattach(const QString& tabName)
{
    // This slot can be connected to detached windows for reattachment
    const int insertIndex = count(); // Insert at end by default
    emit tabReattachRequested(tabName, insertIndex);
}
