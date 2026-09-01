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

#include "EditorTreeDelegate.h"

#include "Host.h"
#include "TAction.h"
#include "TAlias.h"
#include "TKey.h"
#include "TScript.h"
#include "TTimer.h"
#include "TTrigger.h"
#include "uiDesign.h"

#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>

#include <algorithm>

namespace uiDesign {

namespace {
constexpr int scmDotDiameter = 9;
// Between the dot and whatever picture a row has earned the right to keep
constexpr int scmDotGap = 6;
constexpr qreal scmHollowDotPenWidth = 1.5;
// How far past the dot a click still counts as one on it
constexpr int scmDotHitSlack = 2;
// The square the chevron is drawn in, centred in the indentation step it stands
// in - the same square the dot is drawn in, so the two read as one row of marks
constexpr int scmChevronBox = 9;
// Half the height of the chevron's stroke inside that square
constexpr qreal scmChevronArm = 2.6;
constexpr qreal scmChevronPenWidth = 1.4;
// What each level is held in by if the view had none to report, which it always
// does have - a style with no metric for it is what this stands in for
constexpr int scmFallbackIndentStep = 20;
// A profile keeps its rows' pictures as one QIcon each, so a tree of nothing but
// folders would otherwise have the cache grow a row at a time
constexpr int scmDecorationCacheLimit = 256;
// A click carrying one of these is the selection being worked on rather than a
// row being switched on or off
constexpr Qt::KeyboardModifiers scmSelectionModifiers = Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier;

// How deep in the tree a row is. The view counts the same thing to work out how
// far to indent a row, and keeps the answer to itself.
int levelOf(const QModelIndex& index)
{
    int level = 0;
    for (QModelIndex ancestor = index.parent(); ancestor.isValid(); ancestor = ancestor.parent()) {
        ++level;
    }
    return level;
}
} // namespace

EditorTreeDelegate::EditorTreeDelegate(TTreeWidget* pTree, const TreeType treeType, Host* pHost)
: QStyledItemDelegate(pTree)
, mpTree(pTree)
, mTreeType(treeType)
, mpHost(pHost)
{
    if (mpTree) {
        // The room a row's depth holds it in, and the chevron standing in the
        // last step of that room, are drawn here from now on - so the view is
        // asked for none of its own. What it was using is what they are drawn
        // to, so nothing moves; what changes is that the row is one rectangle
        // rather than a column of branch cells with the row stuck to them, and
        // the pill a selected row is drawn as can be a single unbroken shape.
        mIndentStep = mpTree->indentation() > 0 ? mpTree->indentation() : scmFallbackIndentStep;
        mpTree->setIndentation(0);
        // The viewport is made by QAbstractScrollArea's constructor and never
        // replaced, so it is here to be watched from the moment the tree exists
        mpTree->viewport()->installEventFilter(this);
    }
    restyle();
}

void EditorTreeDelegate::restyle()
{
    const ThemeTokens tokens = themeTokens();
    // The green that "on" is read as everywhere else in the editor
    mRunningDot = stateColor(scmStateHue_ok, tokens.darkPage);
    mQuietDot = tokens.mutedText;
    // Chrome the reader reaches for rather than reads, so it is drawn in the
    // tone the rest of the editor's chrome is
    mChevronInk = tokens.mutedText;
    for (auto& cached : mDotGlyphs) {
        cached = QPixmap();
    }
    for (auto& cached : mChevronGlyphs) {
        cached = QPixmap();
    }
    clearDecorations();
}

void EditorTreeDelegate::setHeadingIconSize(const int size)
{
    if (size == mHeadingIconSize) {
        return;
    }
    mHeadingIconSize = size;
    if (mpTree) {
        // A heading row's height is measured from its decoration, so the rows
        // have to be asked for their size again
        mpTree->doItemsLayout();
    }
}

void EditorTreeDelegate::clearDecorations() const
{
    mPlainDecorations.clear();
    mDecorations.clear();
}

// The row's own picture is not asked what state it is in - the item the row
// stands for is, by the id the row carries. A row with no id is one of the tree's
// own headings, which is nothing to switch on or off.
EditorTreeDelegate::ItemState EditorTreeDelegate::stateOf(const QModelIndex& index) const
{
    ItemState state;
    if (mpHost.isNull() || !index.isValid()) {
        return state;
    }
    const QVariant idData = index.data(Qt::UserRole);
    if (!idData.isValid()) {
        return state;
    }
    const int id = idData.toInt();

    // An item the editor has made but nobody has saved yet keeps the picture
    // that says so, and the description the add sites wrote is what says which
    // those are. TTrigger, TAlias and TScript have a checkIfNew() of their own,
    // but it answers the wrong question here: mIsNew starts true and is only
    // ever cleared by a save made from this editor, so an item read back out of
    // the profile - which is every item, every time the editor is opened -
    // reports itself as new for the whole session. Reading the description
    // instead is also what the other three types already do.
    const bool newByDescription = !mNewItemDescription.isEmpty() && index.data(Qt::AccessibleDescriptionRole).toString() == mNewItemDescription;

    bool wantedOn = false;
    bool running = false;
    bool distinctive = false;
    switch (mTreeType) {
    case TreeType::Trigger: {
        TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(id);
        if (!pT) {
            return state;
        }
        wantedOn = pT->shouldBeActive();
        running = pT->isActive() && pT->ancestorsActive();
        // A filter chain is a trigger that other triggers are matched inside of,
        // and its picture is the only thing that says so
        distinctive = pT->isFolder() || pT->isFilterChain() || newByDescription || !pT->state();
        break;
    }
    case TreeType::Alias: {
        TAlias* pT = mpHost->getAliasUnit()->getAlias(id);
        if (!pT) {
            return state;
        }
        wantedOn = pT->shouldBeActive();
        running = pT->isActive() && pT->ancestorsActive();
        distinctive = pT->isFolder() || newByDescription || !pT->state();
        break;
    }
    case TreeType::Timer: {
        TTimer* pT = mpHost->getTimerUnit()->getTimer(id);
        if (!pT) {
            return state;
        }
        wantedOn = pT->shouldBeActive();
        // An offset timer is armed by the timer above it firing and disarms
        // itself once it has fired, so isActive() is false for most of the life
        // of a working one - and ancestorsActive() walks the same flag up a
        // chain that is offset timers all the way. TTimer::shouldAncestorsBeActive()
        // exists for exactly that, and is what TLuaInterpreter's isActive()
        // reads for an offset timer.
        running = pT->isOffsetTimer() ? (pT->shouldBeActive() && pT->shouldAncestorsBeActive()) : (pT->isActive() && pT->ancestorsActive());
        // An offset timer runs from another timer firing rather than from a
        // clock, which its own picture is what tells the reader
        distinctive = pT->isFolder() || pT->isOffsetTimer() || newByDescription || !pT->state();
        break;
    }
    case TreeType::Script: {
        TScript* pT = mpHost->getScriptUnit()->getScript(id);
        if (!pT) {
            return state;
        }
        wantedOn = pT->shouldBeActive();
        running = pT->isActive() && pT->ancestorsActive();
        distinctive = pT->isFolder() || newByDescription || !pT->state();
        break;
    }
    case TreeType::Action: {
        TAction* pT = mpHost->getActionUnit()->getAction(id);
        if (!pT) {
            return state;
        }
        wantedOn = pT->shouldBeActive();
        running = pT->isActive() && pT->ancestorsActive();
        distinctive = pT->isFolder() || newByDescription || !pT->state();
        break;
    }
    case TreeType::Key: {
        TKey* pT = mpHost->getKeyUnit()->getKey(id);
        if (!pT) {
            return state;
        }
        wantedOn = pT->shouldBeActive();
        running = pT->isActive() && pT->ancestorsActive();
        distinctive = pT->isFolder() || newByDescription || !pT->state();
        break;
    }
    default:
        return state;
    }

    state.known = true;
    state.dot = running ? DotState::Running : (wantedOn ? DotState::WantedOn : DotState::Off);
    state.keepIcon = distinctive;
    return state;
}

// A row at the top of a tree is given no chevron: the trees are asked for an
// undecorated root, so the view drew no arrow beside those either, and the one
// row at that depth is the tree's own heading rather than anything a profile
// holds.
EditorTreeDelegate::ChevronState EditorTreeDelegate::chevronOf(const QModelIndex& index, const int level) const
{
    if (!mpTree || !index.isValid() || index.column() != 0 || level < 1) {
        return ChevronState::None;
    }
    const QAbstractItemModel* pModel = index.model();
    if (!pModel || pModel->rowCount(index) < 1) {
        return ChevronState::None;
    }
    return mpTree->isExpanded(index) ? ChevronState::Open : ChevronState::Closed;
}

qreal EditorTreeDelegate::glyphRatio() const
{
    return mpTree ? mpTree->devicePixelRatioF() : 1.0;
}

void EditorTreeDelegate::syncGlyphRatio() const
{
    const qreal ratio = glyphRatio();
    if (qFuzzyCompare(ratio + 1.0, mDotGlyphRatio + 1.0)) {
        return;
    }
    mDotGlyphRatio = ratio;
    for (auto& cached : mDotGlyphs) {
        cached = QPixmap();
    }
    for (auto& cached : mChevronGlyphs) {
        cached = QPixmap();
    }
    // Which is also what the ratio is kept out of the decoration key by
    clearDecorations();
}

QPixmap EditorTreeDelegate::dotGlyph(const DotState state) const
{
    syncGlyphRatio();
    const qreal ratio = mDotGlyphRatio;

    QPixmap& glyph = mDotGlyphs[static_cast<int>(state)];
    if (!glyph.isNull()) {
        return glyph;
    }

    glyph = QPixmap(qRound(scmDotDiameter * ratio), qRound(scmDotDiameter * ratio));
    glyph.setDevicePixelRatio(ratio);
    glyph.fill(Qt::transparent);

    QPainter painter(&glyph);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // A stroked circle is drawn centred on its path, so a hollow dot is pulled in
    // by half a pen to end up the same size as a filled one
    const qreal inset = (state == DotState::Off) ? scmHollowDotPenWidth / 2.0 : 0.0;
    const QRectF circle(inset, inset, scmDotDiameter - 2.0 * inset, scmDotDiameter - 2.0 * inset);
    if (state == DotState::Off) {
        QPen pen(mQuietDot);
        pen.setWidthF(scmHollowDotPenWidth);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
    } else {
        painter.setPen(Qt::NoPen);
        // Switched on inside a switched-off group is still switched on, but it is
        // not running, so it is not drawn in the colour that says it is
        painter.setBrush(state == DotState::Running ? mRunningDot : mQuietDot);
    }
    painter.drawEllipse(circle);
    // Before the pixmap is handed out: a copy taken while a painter is still
    // active on the original is a copy of something being written to
    painter.end();
    return glyph;
}

QPixmap EditorTreeDelegate::chevronGlyph(const ChevronState state) const
{
    QPixmap& glyph = mChevronGlyphs[static_cast<int>(state)];
    if (!glyph.isNull()) {
        return glyph;
    }

    const qreal ratio = mDotGlyphRatio;
    glyph = QPixmap(qRound(scmChevronBox * ratio), qRound(scmChevronBox * ratio));
    glyph.setDevicePixelRatio(ratio);
    glyph.fill(Qt::transparent);

    QPainter painter(&glyph);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(mChevronInk);
    pen.setWidthF(scmChevronPenWidth);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const qreal centre = scmChevronBox / 2.0;
    // Pointing the way the row will open: along the list while it is folded,
    // down into it once it is not. The arm across the point is shortened, so
    // that the mark stays inside its square whichever way round it is drawn.
    const qreal reach = scmChevronArm / 1.6;
    QPolygonF stroke;
    if (state == ChevronState::Open) {
        stroke << QPointF(centre - scmChevronArm, centre - reach) << QPointF(centre, centre + reach) << QPointF(centre + scmChevronArm, centre - reach);
    } else {
        stroke << QPointF(centre - reach, centre - scmChevronArm) << QPointF(centre + reach, centre) << QPointF(centre - reach, centre + scmChevronArm);
    }
    painter.drawPolyline(stroke);
    painter.end();
    return glyph;
}

EditorTreeDelegate::Decoration EditorTreeDelegate::decorationFor(const ItemState& state, const int level, const ChevronState chevron, const QPixmap& keptGlyph, const QSize& keptSize) const
{
    syncGlyphRatio();
    const QPixmap dot = state.known ? dotGlyph(state.dot) : QPixmap();

    // Everything the leading edge is composed from, in one number: what the dot
    // is reading, which way the chevron points, and how far in the row is held
    const int shape = (state.known ? 1 + static_cast<int>(state.dot) : 0) | (static_cast<int>(chevron) << 2) | (level << 4);
    const bool plain = keptSize.isEmpty();
    QHash<DecorationKey, Decoration>& cache = plain ? mPlainDecorations : mDecorations;
    const DecorationKey key{shape, plain ? -1 : keptGlyph.cacheKey()};
    if (const auto cached = cache.constFind(key); cached != cache.constEnd()) {
        return cached.value();
    }
    if (cache.size() >= scmDecorationCacheLimit) {
        cache.clear();
    }

    // The room the row's depth holds it in comes first, with the chevron
    // standing in the last step of it - where the view used to draw its branch
    // arrow. Then the dot, then the picture the row has earned the right to keep
    // - all of them centred on whichever is the taller, so that rows with and
    // without a picture stay the same height.
    const int lead = level * mIndentStep;
    const int dotWidth = state.known ? scmDotDiameter : 0;
    const int gap = (dotWidth > 0 && !plain) ? scmDotGap : 0;
    const int slotWidth = lead + dotWidth + gap + keptSize.width();
    const int slotHeight = std::max({keptSize.height(), dotWidth, chevron == ChevronState::None ? 0 : scmChevronBox, 1});

    const qreal ratio = mDotGlyphRatio;
    QPixmap composed(qRound(slotWidth * ratio), qRound(slotHeight * ratio));
    composed.setDevicePixelRatio(ratio);
    composed.fill(Qt::transparent);
    QPainter painter(&composed);
    if (chevron != ChevronState::None) {
        painter.drawPixmap(QPointF(lead - mIndentStep + (mIndentStep - scmChevronBox) / 2.0, (slotHeight - scmChevronBox) / 2.0), chevronGlyph(chevron));
    }
    if (state.known) {
        painter.drawPixmap(QPointF(lead, (slotHeight - scmDotDiameter) / 2.0), dot);
    }
    if (!plain) {
        painter.drawPixmap(QPointF(lead + dotWidth + gap, (slotHeight - keptSize.height()) / 2.0), keptGlyph);
    }
    painter.end();

    Decoration decoration;
    decoration.icon = QIcon(composed);
    decoration.size = QSize(slotWidth, slotHeight);
    cache.insert(key, decoration);
    return decoration;
}

void EditorTreeDelegate::initStyleOption(QStyleOptionViewItem* pOption, const QModelIndex& index) const
{
    QStyledItemDelegate::initStyleOption(pOption, index);

    if (pOption->decorationSize != mDecorationBaseSize) {
        mDecorationBaseSize = pOption->decorationSize;
        clearDecorations();
    }

    const ItemState state = stateOf(index);
    const int level = levelOf(index);
    const ChevronState chevron = chevronOf(index, level);
    // Nothing to lead the row with: no dot, no room to hold it in and nothing
    // folded inside it, which is what a tree's own heading row is. It carries
    // the same glyph as the row beside it in the sidebar, so it is drawn at the
    // size the sidebar draws it rather than at the tree's own - which is a
    // preference, and a third again as large at its default.
    if (!state.known && level < 1 && chevron == ChevronState::None) {
        if (mHeadingIconSize > 0) {
            pOption->decorationSize = QSize(mHeadingIconSize, mHeadingIconSize);
        }
        return;
    }

    const QIcon rowIcon = pOption->icon;
    // A row the dot says nothing about keeps whatever picture it was given: it
    // is the picture that says what the row is
    const bool keepIcon = state.known ? state.keepIcon : true;
    const QSize keptSize = (keepIcon && !rowIcon.isNull()) ? rowIcon.actualSize(pOption->decorationSize) : QSize();
    // Asked for before the lookup rather than inside the miss: this is what the
    // composed decoration is keyed on, and QIcon hands back one shared pixmap
    // for every row drawn from the same file
    const QPixmap keptGlyph = keptSize.isEmpty() ? QPixmap() : rowIcon.pixmap(keptSize, glyphRatio(), QIcon::Normal, QIcon::Off);
    const Decoration decoration = decorationFor(state, level, chevron, keptGlyph, keptSize);

    // The decoration is the whole of the row's leading edge: the view lays room
    // out for it and the style draws it, so nothing here has to work out where a
    // row's text begins
    pOption->decorationSize = decoration.size;
    pOption->icon = decoration.icon;
    pOption->features |= QStyleOptionViewItem::HasDecoration;

    // The tree's stylesheet names no colour for an unselected row, which leaves
    // one for this to quieten
    if (state.known && state.dot != DotState::Running && !(pOption->state & QStyle::State_Selected)) {
        pOption->palette.setColor(QPalette::Text, mQuietDot);
        pOption->palette.setColor(QPalette::WindowText, mQuietDot);
    }
}

QRect EditorTreeDelegate::dotHitRect(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (!mpTree || !index.isValid() || index.column() != 0 || option.rect.isEmpty() || !stateOf(index).known) {
        return {};
    }

    // Asked of the style rather than worked out here: the trees carry a
    // stylesheet, so it is QStyleSheetStyle that decides where a row's picture
    // lands, and its padding is nothing this could guess at
    QStyleOptionViewItem dotOption(option);
    initStyleOption(&dotOption, index);
    const QRect decorationRect = mpTree->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &dotOption, mpTree);
    if (decorationRect.isEmpty()) {
        return {};
    }

    // The decoration leads with the room the row's depth holds it in, and the
    // dot is the square that follows it - then whatever picture the row kept.
    // The rest of the row's leading edge is left to start a drag from.
    const int lead = levelOf(index) * mIndentStep;
    const QRect dotRect(decorationRect.left() + lead, decorationRect.top() + (decorationRect.height() - scmDotDiameter) / 2, scmDotDiameter, scmDotDiameter);
    return dotRect.adjusted(-scmDotHitSlack, -scmDotHitSlack, scmDotHitSlack, scmDotHitSlack);
}

QRect EditorTreeDelegate::chevronHitRect(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const int level = levelOf(index);
    if (!mpTree || !index.isValid() || index.column() != 0 || option.rect.isEmpty() || chevronOf(index, level) == ChevronState::None) {
        return {};
    }

    QStyleOptionViewItem chevronOption(option);
    initStyleOption(&chevronOption, index);
    const QRect decorationRect = mpTree->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &chevronOption, mpTree);
    if (decorationRect.isEmpty()) {
        return {};
    }

    // The last step of the room the row is held in, over the whole height of the
    // row - which is the cell the view used to draw its branch arrow in, and
    // what a reader who has used a tree before will aim at
    return QRect(decorationRect.left() + (level - 1) * mIndentStep, option.rect.top(), mIndentStep, option.rect.height());
}

// The option the view would hand editorEvent() for a mouse event on this row,
// rebuilt: QAbstractItemView makes one out of initViewItemOption(), the row's
// rectangle and whether the row is the current one, and keeps all three to
// itself
QStyleOptionViewItem EditorTreeDelegate::rowOption(const QModelIndex& index) const
{
    QStyleOptionViewItem option = mpTree->viewItemOption();
    option.rect = mpTree->visualRect(index);
    if (index == mpTree->currentIndex()) {
        option.state |= QStyle::State_HasFocus;
    }
    return option;
}

QRect EditorTreeDelegate::dotHitRect(const QModelIndex& index) const
{
    if (!mpTree) {
        return {};
    }
    return dotHitRect(rowOption(index), index);
}

QRect EditorTreeDelegate::chevronHitRect(const QModelIndex& index) const
{
    if (!mpTree) {
        return {};
    }
    return chevronHitRect(rowOption(index), index);
}

bool EditorTreeDelegate::eventFilter(QObject* pWatched, QEvent* pEvent)
{
    // Anything else being watched is an editor widget that QStyledItemDelegate
    // put this filter on itself, and its own handling of those has to stand
    if (!mpTree || pWatched != mpTree->viewport()) {
        return QStyledItemDelegate::eventFilter(pWatched, pEvent);
    }

    switch (pEvent->type()) {
    case QEvent::MouseButtonPress:
        // Every press starts a new one of these, and the second press of a
        // double click is not a press but its own event type - which is what
        // lets the breadcrumb below outlive the first press of the pair
        mPressAnswered = false;
        mLastDotPressIndex = QPersistentModelIndex();
        break;
    case QEvent::MouseMove:
        // The view records what was pressed before it consults a delegate, and
        // consults one about a row rather than about the blank space past the
        // last one - so a move handed on here would drag-select out of the mark
        // that was pressed, or lift the row it is on out of the tree the moment
        // it left them both
        if (mPressAnswered && (static_cast<QMouseEvent*>(pEvent)->buttons() & Qt::LeftButton)) {
            return true;
        }
        break;
    case QEvent::MouseButtonRelease:
        // The press did the switching or the folding, so its release is eaten
        // wherever it lands: handed on, the view would read it as a click on
        // whichever row it ended over and reload that row, losing whatever was
        // typed into the editor
        if (mPressAnswered) {
            mPressAnswered = false;
            return true;
        }
        break;
    default:
        break;
    }

    return false;
}

bool EditorTreeDelegate::editorEvent(QEvent* pEvent, QAbstractItemModel* pModel, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    const QEvent::Type eventType = pEvent->type();
    if (eventType != QEvent::MouseButtonPress && eventType != QEvent::MouseButtonDblClick) {
        return QStyledItemDelegate::editorEvent(pEvent, pModel, option, index);
    }

    auto* pMouseEvent = static_cast<QMouseEvent*>(pEvent);
    if (pMouseEvent->button() != Qt::LeftButton || (pMouseEvent->modifiers() & scmSelectionModifiers)) {
        return QStyledItemDelegate::editorEvent(pEvent, pModel, option, index);
    }
    const QPoint pressedAt = pMouseEvent->position().toPoint();
    // The chevron is asked first, and the dot's couple of pixels of slack are
    // what the two would otherwise argue over: the view used to hand the whole
    // of that cell to the branch arrow before a delegate saw the press at all
    const bool onChevron = chevronHitRect(option, index).contains(pressedAt);
    const bool onDot = !onChevron && dotHitRect(option, index).contains(pressedAt);

    if (eventType == QEvent::MouseButtonDblClick) {
        // The pair's first press has already folded or switched the row, so the
        // second is eaten whether or not it landed on the same mark: the
        // platform allows a double click a few pixels of drift, which is most of
        // the way out of a target the size of the dot, and a drifted one handed
        // on reaches the view's activated() and switches the row straight back
        if (!onChevron && !onDot && !(mLastDotPressIndex.isValid() && mLastDotPressIndex == index)) {
            return QStyledItemDelegate::editorEvent(pEvent, pModel, option, index);
        }
        // Set for the release that closes the double click, which the filter
        // swallows for the same reason it swallows a single click's
        mPressAnswered = true;
        return true;
    }

    if (onChevron) {
        // Nothing else about the row is touched: the selection stays where it
        // was, which is what the view did while it owned this cell
        mpTree->setExpanded(index, !mpTree->isExpanded(index));
        mPressAnswered = true;
        return true;
    }

    if (!onDot) {
        return QStyledItemDelegate::editorEvent(pEvent, pModel, option, index);
    }

    // The toggle reads the tree's current item rather than being handed one, so
    // the row is chosen first and asked for after - one toggle, one path
    if (QTreeWidgetItem* pItem = mpTree->itemFromIndex(index)) {
        mpTree->setCurrentItem(pItem);
        emit toggleRequested();
    }
    mPressAnswered = true;
    mLastDotPressIndex = index;
    return true;
}

} // namespace uiDesign
