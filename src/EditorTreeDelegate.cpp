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
// A profile keeps its rows' pictures as one QIcon each, so a tree of nothing but
// folders would otherwise have the cache grow a row at a time
constexpr int scmDecorationCacheLimit = 256;
} // namespace

EditorTreeDelegate::EditorTreeDelegate(TTreeWidget* pTree, const TreeType treeType, Host* pHost)
: QStyledItemDelegate(pTree)
, mpTree(pTree)
, mTreeType(treeType)
, mpHost(pHost)
{
    restyle();
}

void EditorTreeDelegate::restyle()
{
    const ThemeTokens tokens = themeTokens();
    // The green that "on" is read as everywhere else in the editor
    mRunningDot = stateColor(scmStateHue_ok, tokens.darkPage);
    mQuietDot = tokens.mutedText;
    for (auto& cached : mDotGlyphs) {
        cached = QPixmap();
    }
    clearDecorations();
}

void EditorTreeDelegate::clearDecorations() const
{
    for (auto& cached : mDotDecorations) {
        cached = Decoration();
    }
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
    // that says so. A trigger, an alias and a script are asked outright;
    // a timer, a button and a key have no such question to answer, so the
    // answer is read back off the description the add sites wrote.
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
        distinctive = pT->isFolder() || pT->isFilterChain() || pT->checkIfNew() || !pT->state();
        break;
    }
    case TreeType::Alias: {
        TAlias* pT = mpHost->getAliasUnit()->getAlias(id);
        if (!pT) {
            return state;
        }
        wantedOn = pT->shouldBeActive();
        running = pT->isActive() && pT->ancestorsActive();
        distinctive = pT->isFolder() || pT->checkIfNew() || !pT->state();
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
        distinctive = pT->isFolder() || pT->checkIfNew() || !pT->state();
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

qreal EditorTreeDelegate::glyphRatio() const
{
    return mpTree ? mpTree->devicePixelRatioF() : 1.0;
}

QPixmap EditorTreeDelegate::dotGlyph(const DotState state) const
{
    const qreal ratio = glyphRatio();
    if (!qFuzzyCompare(ratio + 1.0, mDotGlyphRatio + 1.0)) {
        mDotGlyphRatio = ratio;
        for (auto& cached : mDotGlyphs) {
            cached = QPixmap();
        }
        // Which is also what the ratio is kept out of the decoration key by
        clearDecorations();
    }

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

EditorTreeDelegate::Decoration EditorTreeDelegate::decorationFor(const ItemState& state, const QPixmap& keptGlyph, const QSize& keptSize) const
{
    // Before either cache is looked in: a change of screen is noticed here, and
    // what it throws away is exactly what a lookup would otherwise hand back
    const QPixmap glyph = dotGlyph(state.dot);

    if (keptSize.isEmpty()) {
        Decoration& dotOnly = mDotDecorations[static_cast<int>(state.dot)];
        if (dotOnly.size.isEmpty()) {
            dotOnly.icon = QIcon(glyph);
            dotOnly.size = QSize(scmDotDiameter, scmDotDiameter);
        }
        return dotOnly;
    }

    const DecorationKey key{static_cast<int>(state.dot), keptGlyph.cacheKey()};
    if (const auto cached = mDecorations.constFind(key); cached != mDecorations.constEnd()) {
        return cached.value();
    }
    if (mDecorations.size() >= scmDecorationCacheLimit) {
        mDecorations.clear();
    }

    // The dot leads, and the picture the row has earned the right to keep
    // follows it - both centred on whichever of the two is the taller, so that
    // rows with and without a picture stay the same height
    const qreal ratio = glyph.devicePixelRatio();
    const int slotWidth = scmDotDiameter + scmDotGap + keptSize.width();
    const int slotHeight = std::max(keptSize.height(), scmDotDiameter);
    QPixmap composed(qRound(slotWidth * ratio), qRound(slotHeight * ratio));
    composed.setDevicePixelRatio(ratio);
    composed.fill(Qt::transparent);
    QPainter painter(&composed);
    painter.drawPixmap(QPointF(0.0, (slotHeight - scmDotDiameter) / 2.0), glyph);
    painter.drawPixmap(QPointF(scmDotDiameter + scmDotGap, (slotHeight - keptSize.height()) / 2.0), keptGlyph);
    painter.end();

    Decoration decoration;
    decoration.icon = QIcon(composed);
    decoration.size = QSize(slotWidth, slotHeight);
    mDecorations.insert(key, decoration);
    return decoration;
}

void EditorTreeDelegate::initStyleOption(QStyleOptionViewItem* pOption, const QModelIndex& index) const
{
    QStyledItemDelegate::initStyleOption(pOption, index);
    const ItemState state = stateOf(index);
    if (!state.known) {
        return;
    }

    if (pOption->decorationSize != mDecorationBaseSize) {
        mDecorationBaseSize = pOption->decorationSize;
        clearDecorations();
    }

    const QIcon rowIcon = pOption->icon;
    const QSize keptSize = (state.keepIcon && !rowIcon.isNull()) ? rowIcon.actualSize(pOption->decorationSize) : QSize();
    // Asked for before the lookup rather than inside the miss: this is what the
    // composed decoration is keyed on, and QIcon hands back one shared pixmap
    // for every row drawn from the same file
    const QPixmap keptGlyph = keptSize.isEmpty() ? QPixmap() : rowIcon.pixmap(keptSize, glyphRatio(), QIcon::Normal, QIcon::Off);
    const Decoration decoration = decorationFor(state, keptGlyph, keptSize);

    // The decoration is the dot: the view lays room out for it and the style
    // draws it, so nothing here has to work out where a row's text begins
    pOption->decorationSize = decoration.size;
    pOption->icon = decoration.icon;
    pOption->features |= QStyleOptionViewItem::HasDecoration;

    // The tree's stylesheet names no colour for an unselected row, which leaves
    // one for this to quieten
    if (state.dot != DotState::Running && !(pOption->state & QStyle::State_Selected)) {
        pOption->palette.setColor(QPalette::Text, mQuietDot);
        pOption->palette.setColor(QPalette::WindowText, mQuietDot);
    }
}

QRect EditorTreeDelegate::dotHitRect(const QModelIndex& index) const
{
    if (!mpTree || !index.isValid() || index.column() != 0 || !stateOf(index).known) {
        return {};
    }
    const QRect rowRect = mpTree->visualRect(index);
    if (rowRect.isEmpty()) {
        return {};
    }

    // Asked of the style rather than worked out here: the trees carry a
    // stylesheet, so it is QStyleSheetStyle that decides where a row's picture
    // lands, and its padding is nothing this could guess at
    QStyleOptionViewItem option = mpTree->viewItemOption();
    option.rect = rowRect;
    initStyleOption(&option, index);
    const QRect decorationRect = mpTree->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, mpTree);
    if (decorationRect.isEmpty()) {
        return {};
    }

    // The decoration is as wide as the dot, or as wide as the dot and the
    // picture beside it - either way the dot is the leading square of it, and
    // the row's remaining leading edge is left to start a drag from
    const QRect dotRect(decorationRect.left(), decorationRect.top() + (decorationRect.height() - scmDotDiameter) / 2, scmDotDiameter, scmDotDiameter);
    return dotRect.adjusted(-scmDotHitSlack, -scmDotHitSlack, scmDotHitSlack, scmDotHitSlack);
}

} // namespace uiDesign
