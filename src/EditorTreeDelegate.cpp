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

#include "EditorTreeRowMetrics.h"
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
constexpr int scmTypeLabelGap = 12;
constexpr int scmTypeLabelMinimumCharacters = 4;
} // namespace

EditorTreeDelegate::EditorTreeDelegate(TTreeWidget* pTree, const TreeType treeType, Host* pHost)
: QStyledItemDelegate(pTree)
, mpTree(pTree)
, mTreeType(treeType)
, mpHost(pHost)
{
    if (mpTree) {
        // The view indents nothing and the decoration draws depth and chevron: QTreeView paints its
        // indented branch cells itself in the style's selection colour, out of any stylesheet rule's
        // reach, so a nested row's selection could not be one pill
        mIndentStep = mpTree->indentation() > 0 ? mpTree->indentation() : scmTreeFallbackIndentStep;
        mpTree->setIndentation(0);
        // QAbstractScrollArea makes the viewport in its constructor and the trees never replace it
        mpTree->viewport()->installEventFilter(this);
    }
    restyle();
}

void EditorTreeDelegate::restyle()
{
    const ThemeTokens tokens = themeTokens();
    mRunningDot = stateColor(scmStateHue_ok, tokens.darkPage);
    mQuietDot = tokens.mutedText;
    mChevronInk = tokens.mutedText;
    mMarkInk = tokens.mutedText;
    mErrorMarkInk = errorInk(tokens);
    // Must match the colour the trees' stylesheet writes a chosen row's name in
    mSelectedMarkInk = tokens.accentText;
    mAccentBar = tokens.accent;
    for (auto& cached : mDotGlyphs) {
        cached = QPixmap();
    }
    for (auto& cached : mChevronGlyphs) {
        cached = QPixmap();
    }
    for (auto& cached : mMarkGlyphs) {
        cached = QPixmap();
    }
    clearDecorations();
}

void EditorTreeDelegate::setNewDescriptions(const QString& item, const QString& folder)
{
    mNewItemDescription = item;
    mNewFolderDescription = folder;
}

void EditorTreeDelegate::clearDecorations() const
{
    mDecorations.clear();
}

EditorTreeDelegate::ItemState EditorTreeDelegate::stateOf(const QModelIndex& index) const
{
    ItemState state;
    if (mpHost.isNull() || !index.isValid()) {
        return state;
    }
    const QVariant idData = index.data(Qt::UserRole);
    const TreeType treeType = mTreeType == TreeType::None ? static_cast<TreeType>(index.data(scmRole_rowTreeType).toInt()) : mTreeType;
    if (!idData.isValid() || treeType == TreeType::None) {
        if (mTreeType == TreeType::None) {
            state.dotSlot = true;
            state.mark = RowMark::Folder;
        }
        return state;
    }
    const int id = idData.toInt();

    // Not checkIfNew(): mIsNew is cleared only by a save from this editor, so every item loaded
    // from the profile reports itself as new for the whole session
    const QString description = index.data(Qt::AccessibleDescriptionRole).toString();
    const bool newByDescription = !description.isEmpty() && (description == mNewItemDescription || description == mNewFolderDescription);

    const auto markFor = [this, newByDescription](const bool compiles, const bool folder, const QString& packageName, const QString& name, const RowMark kind) {
        if (!compiles) {
            return RowMark::Error;
        }
        if (newByDescription) {
            return folder ? RowMark::NewFolder : RowMark::NewItem;
        }
        if (kind != RowMark::None) {
            return kind;
        }
        // The Lua ancestors API's test for a package's top folder; a module's master folder is not a package
        if (folder && !packageName.isEmpty() && packageName == name && !mpHost->mInstalledModules.contains(packageName)) {
            return RowMark::Package;
        }
        return folder ? RowMark::Folder : RowMark::None;
    };

    bool wantedOn = false;
    bool running = false;
    RowMark mark = RowMark::None;
    switch (treeType) {
    case TreeType::Trigger: {
        TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(id);
        if (!pT) {
            return state;
        }
        wantedOn = pT->shouldBeActive();
        running = pT->isActive() && pT->ancestorsActive();
        mark = markFor(pT->state(), pT->isFolder(), pT->mPackageName, pT->getName(), pT->isFilterChain() ? RowMark::Filter : RowMark::None);
        break;
    }
    case TreeType::Alias: {
        TAlias* pT = mpHost->getAliasUnit()->getAlias(id);
        if (!pT) {
            return state;
        }
        wantedOn = pT->shouldBeActive();
        running = pT->isActive() && pT->ancestorsActive();
        mark = markFor(pT->state(), pT->isFolder(), pT->mPackageName, pT->getName(), RowMark::None);
        break;
    }
    case TreeType::Timer: {
        TTimer* pT = mpHost->getTimerUnit()->getTimer(id);
        if (!pT) {
            return state;
        }
        wantedOn = pT->shouldBeActive();
        // An offset timer's isActive() is false between firings, and ancestorsActive() walks that same
        // flag; shouldAncestorsBeActive() is what TLuaInterpreter's isActive() reads for one
        running = pT->isOffsetTimer() ? (pT->shouldBeActive() && pT->shouldAncestorsBeActive()) : (pT->isActive() && pT->ancestorsActive());
        mark = markFor(pT->state(), pT->isFolder(), pT->mPackageName, pT->getName(), pT->isOffsetTimer() ? RowMark::OffsetTimer : RowMark::None);
        break;
    }
    case TreeType::Script: {
        TScript* pT = mpHost->getScriptUnit()->getScript(id);
        if (!pT) {
            return state;
        }
        wantedOn = pT->shouldBeActive();
        running = pT->isActive() && pT->ancestorsActive();
        mark = markFor(pT->state(), pT->isFolder(), pT->mPackageName, pT->getName(), RowMark::None);
        break;
    }
    case TreeType::Action: {
        TAction* pT = mpHost->getActionUnit()->getAction(id);
        if (!pT) {
            return state;
        }
        wantedOn = pT->shouldBeActive();
        running = pT->isActive() && pT->ancestorsActive();
        mark = markFor(pT->state(), pT->isFolder(), pT->mPackageName, pT->getName(), RowMark::None);
        break;
    }
    case TreeType::Key: {
        TKey* pT = mpHost->getKeyUnit()->getKey(id);
        if (!pT) {
            return state;
        }
        wantedOn = pT->shouldBeActive();
        running = pT->isActive() && pT->ancestorsActive();
        mark = markFor(pT->state(), pT->isFolder(), pT->mPackageName, pT->getName(), RowMark::None);
        break;
    }
    default:
        return state;
    }

    state.known = true;
    state.dot = running ? DotState::Running : (wantedOn ? DotState::WantedOn : DotState::Off);
    state.mark = mark;
    return state;
}

int EditorTreeDelegate::rowLevelOf(const QModelIndex& index) const
{
    return treeRowLevelOf(index) + (mTreeType == TreeType::None ? 1 : 0);
}

// None at the top level: that row is the tree's own heading, and the trees use an undecorated root
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
    for (auto& cached : mMarkGlyphs) {
        cached = QPixmap();
    }
    // Clearing on a ratio change is why the decoration key can leave the ratio out
    clearDecorations();
}

QPixmap EditorTreeDelegate::dotGlyph(const DotState state, const bool broken) const
{
    syncGlyphRatio();
    const qreal ratio = mDotGlyphRatio;

    QPixmap& glyph = mDotGlyphs[(broken ? scmDotStateCount : 0) + static_cast<int>(state)];
    if (!glyph.isNull()) {
        return glyph;
    }

    TreeDotForm form = TreeDotForm::Ring;
    if (state == DotState::Running) {
        form = TreeDotForm::Filled;
    } else if (state == DotState::WantedOn) {
        form = TreeDotForm::RingWithCentre;
    }
    const QColor ink = broken ? mErrorMarkInk : (state == DotState::Running ? mRunningDot : mQuietDot);
    glyph = treeRowDotGlyph(form, ink, ratio);
    return glyph;
}

QPixmap EditorTreeDelegate::chevronGlyph(const ChevronState state) const
{
    QPixmap& glyph = mChevronGlyphs[static_cast<int>(state)];
    if (!glyph.isNull()) {
        return glyph;
    }

    glyph = treeRowChevronGlyph(state == ChevronState::Open, mChevronInk, mDotGlyphRatio);
    return glyph;
}

QPixmap EditorTreeDelegate::markGlyph(const RowMark mark, const bool selected) const
{
    QPixmap& glyph = mMarkGlyphs[2 * static_cast<int>(mark) + (selected ? 1 : 0)];
    if (!glyph.isNull()) {
        return glyph;
    }

    QString file;
    switch (mark) {
    case RowMark::Folder:
        file = qsl(":/icons/editor-folder.svg");
        break;
    case RowMark::Filter:
        file = qsl(":/icons/editor-filter.svg");
        break;
    case RowMark::OffsetTimer:
        file = qsl(":/icons/editor-offset-timer.svg");
        break;
    case RowMark::Error:
        file = qsl(":/icons/editor-errors.svg");
        break;
    case RowMark::NewFolder:
        file = qsl(":/icons/editor-new-folder.svg");
        break;
    case RowMark::NewItem:
        file = qsl(":/icons/editor-new-item.svg");
        break;
    case RowMark::Package:
        file = qsl(":/icons/packages-package.svg");
        break;
    case RowMark::None:
        return glyph;
    }

    const QColor ink = mark == RowMark::Error ? mErrorMarkInk : (selected ? mSelectedMarkInk : mMarkInk);
    const qreal ratio = mDotGlyphRatio;
    glyph = tintedGlyph(uiDesign::glyphPixmap(file), ink).scaled(QSize(scmTreeMarkSize, scmTreeMarkSize) * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    glyph.setDevicePixelRatio(ratio);
    return glyph;
}

EditorTreeDelegate::Decoration EditorTreeDelegate::decorationFor(const ItemState& state, const int level, const ChevronState chevron, const bool selected) const
{
    syncGlyphRatio();

    const int key = (state.known ? 1 + static_cast<int>(state.dot) : 0) | (static_cast<int>(chevron) << 2) | (static_cast<int>(state.mark) << 4) | ((selected ? 1 : 0) << 7)
                    | ((state.dotSlot ? 1 : 0) << 8) | (level << 9);
    if (const auto cached = mDecorations.constFind(key); cached != mDecorations.constEnd()) {
        return cached.value();
    }
    if (mDecorations.size() >= scmTreeDecorationCacheLimit) {
        mDecorations.clear();
    }

    const int lead = level * mIndentStep;
    const int dotWidth = (state.known || state.dotSlot) ? scmTreeDotDiameter : 0;
    const int markWidth = state.mark == RowMark::None ? 0 : scmTreeMarkSize;
    const int gap = (dotWidth > 0 && markWidth > 0) ? scmTreeDotGap : 0;
    const int slotWidth = std::max(lead + dotWidth + gap + markWidth, 1);

    const qreal ratio = mDotGlyphRatio;
    QPixmap composed(qRound(slotWidth * ratio), qRound(scmTreeSlotHeight * ratio));
    composed.setDevicePixelRatio(ratio);
    composed.fill(Qt::transparent);
    QPainter painter(&composed);
    if (chevron != ChevronState::None) {
        painter.drawPixmap(QPointF(lead - mIndentStep + (mIndentStep - scmTreeChevronBox) / 2.0, (scmTreeSlotHeight - scmTreeChevronBox) / 2.0), chevronGlyph(chevron));
    }
    if (state.known) {
        painter.drawPixmap(QPointF(lead, (scmTreeSlotHeight - scmTreeDotDiameter) / 2.0), dotGlyph(state.dot, state.mark == RowMark::Error));
    }
    if (markWidth > 0) {
        painter.drawPixmap(QPointF(lead + dotWidth + gap, (scmTreeSlotHeight - scmTreeMarkSize) / 2.0), markGlyph(state.mark, selected));
    }
    painter.end();

    Decoration decoration;
    decoration.icon = QIcon(composed);
    decoration.size = QSize(slotWidth, scmTreeSlotHeight);
    mDecorations.insert(key, decoration);
    return decoration;
}

void EditorTreeDelegate::initStyleOption(QStyleOptionViewItem* pOption, const QModelIndex& index) const
{
    QStyledItemDelegate::initStyleOption(pOption, index);

    const ItemState state = stateOf(index);
    const int level = rowLevelOf(index);
    const ChevronState chevron = chevronOf(index, level);
    // A tree's heading keeps the glyph restyleEditorTreeHeadingIcons() put on it, at the mark size
    // so it is as tall as the rows it heads
    if (!state.known && level < 1 && chevron == ChevronState::None) {
        pOption->decorationSize = QSize(scmTreeMarkSize, scmTreeMarkSize);
        return;
    }

    const Decoration decoration = decorationFor(state, level, chevron, pOption->state & QStyle::State_Selected);

    pOption->decorationSize = decoration.size;
    pOption->icon = decoration.icon;
    pOption->features |= QStyleOptionViewItem::HasDecoration;

    // Effective only because the trees' stylesheet names no colour for an unselected row
    if (state.known && state.dot != DotState::Running && !(pOption->state & QStyle::State_Selected)) {
        pOption->palette.setColor(QPalette::Text, mQuietDot);
        pOption->palette.setColor(QPalette::WindowText, mQuietDot);
    }
}

void EditorTreeDelegate::paint(QPainter* pPainter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyledItemDelegate::paint(pPainter, option, index);

    if (index.column() != 0 || option.rect.isEmpty()) {
        return;
    }
    if (mTreeType == TreeType::None) {
        paintTypeLabel(pPainter, option, index);
    }
    if (!(option.state & QStyle::State_Selected)) {
        return;
    }

    // Painted rather than a border-left, which the pill's corner radius bends into a bracket
    paintAccentBar(pPainter, option.rect, mAccentBar);
}

void EditorTreeDelegate::paintTypeLabel(QPainter* pPainter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const QString label = index.siblingAtColumn(1).data(Qt::DisplayRole).toString();
    if (!mpTree || label.isEmpty()) {
        return;
    }
    QStyleOptionViewItem labelOption(option);
    initStyleOption(&labelOption, index);
    const QWidget* pWidget = labelOption.widget ? labelOption.widget : mpTree;
    QStyle* pStyle = pWidget->style();
    // QCommonStyle draws a row's words inside the text rectangle less this much on either side
    const int textMargin = pStyle->pixelMetric(QStyle::PM_FocusFrameHMargin, &labelOption, pWidget) + 1;
    const QRect textRect = pStyle->subElementRect(QStyle::SE_ItemViewItemText, &labelOption, pWidget).adjusted(textMargin, 0, -textMargin, 0);
    const QFontMetrics metrics(labelOption.font);
    const int room = textRect.width() - metrics.horizontalAdvance(labelOption.text) - scmTypeLabelGap;
    if (room < scmTypeLabelMinimumCharacters * metrics.horizontalAdvance(QLatin1Char('0'))) {
        return;
    }
    pPainter->save();
    pPainter->setClipRect(labelOption.rect);
    pPainter->setFont(labelOption.font);
    pPainter->setPen((labelOption.state & QStyle::State_Selected) ? mSelectedMarkInk : mMarkInk);
    pPainter->drawText(QRect(textRect.right() - room + 1, textRect.top(), room, textRect.height()), Qt::AlignRight | Qt::AlignVCenter, metrics.elidedText(label, Qt::ElideRight, room));
    pPainter->restore();
}

QRect EditorTreeDelegate::dotHitRect(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (!mpTree || !index.isValid() || index.column() != 0 || option.rect.isEmpty() || !stateOf(index).known) {
        return {};
    }

    // Asked of the style: QStyleSheetStyle places a row's picture, and its padding cannot be guessed here
    QStyleOptionViewItem dotOption(option);
    initStyleOption(&dotOption, index);
    const QRect decorationRect = mpTree->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &dotOption, mpTree);
    if (decorationRect.isEmpty()) {
        return {};
    }

    // The dot and a little slack only, so the rest of the leading edge can still start a drag
    const int lead = rowLevelOf(index) * mIndentStep;
    const QRect dotRect(decorationRect.left() + lead, decorationRect.top() + (decorationRect.height() - scmTreeDotDiameter) / 2, scmTreeDotDiameter, scmTreeDotDiameter);
    return dotRect.adjusted(-scmTreeMarkHitSlack, -scmTreeMarkHitSlack, scmTreeMarkHitSlack, scmTreeMarkHitSlack);
}

QRect EditorTreeDelegate::chevronHitRect(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const int level = rowLevelOf(index);
    if (!mpTree || !index.isValid() || index.column() != 0 || option.rect.isEmpty() || chevronOf(index, level) == ChevronState::None) {
        return {};
    }

    QStyleOptionViewItem chevronOption(option);
    initStyleOption(&chevronOption, index);
    const QRect decorationRect = mpTree->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &chevronOption, mpTree);
    if (decorationRect.isEmpty()) {
        return {};
    }

    // The whole indentation step, where the view's branch arrow stood and a reader will aim
    return QRect(decorationRect.left() + (level - 1) * mIndentStep, option.rect.top(), mIndentStep, option.rect.height());
}

// Rebuilds what QAbstractItemView hands editorEvent(), which it makes privately from
// initViewItemOption(), the row's rectangle and whether the row is current
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

EditorTreeDelegate::DotState EditorTreeDelegate::dotStateOf(const QModelIndex& index) const
{
    return stateOf(index).dot;
}

EditorTreeDelegate::RowMark EditorTreeDelegate::markOf(const QModelIndex& index) const
{
    return stateOf(index).mark;
}

bool EditorTreeDelegate::eventFilter(QObject* pWatched, QEvent* pEvent)
{
    // QStyledItemDelegate filters its own editor widgets through this, and that handling has to stand
    if (!mpTree || pWatched != mpTree->viewport()) {
        return QStyledItemDelegate::eventFilter(pWatched, pEvent);
    }

    switch (pEvent->type()) {
    case QEvent::MouseButtonPress:
        // Reset on every press in case a release never arrived; a double click's second press is
        // MouseButtonDblClick, so mLastDotPressIndex outlives the first press of the pair
        mPressAnswered = false;
        mLastDotPressIndex = QPersistentModelIndex();
        break;
    case QEvent::MouseMove:
        // The view recorded the press before asking the delegate, so a move handed on would
        // drag-select out of the mark or lift the row out of the tree
        if (mPressAnswered && (static_cast<QMouseEvent*>(pEvent)->buttons() & Qt::LeftButton)) {
            return true;
        }
        break;
    case QEvent::MouseButtonRelease:
        // Handed on, the view would read it as a click on whichever row it ended over and reload
        // that row, losing whatever was typed into the editor
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
    if (pMouseEvent->button() != Qt::LeftButton || (pMouseEvent->modifiers() & scmTreeSelectionModifiers)) {
        return QStyledItemDelegate::editorEvent(pEvent, pModel, option, index);
    }
    const QPoint pressedAt = pMouseEvent->position().toPoint();
    // Chevron first: the dot's slack overlaps the chevron's cell
    const bool onChevron = chevronHitRect(option, index).contains(pressedAt);
    const bool onDot = !onChevron && dotHitRect(option, index).contains(pressedAt);

    if (eventType == QEvent::MouseButtonDblClick) {
        // Eaten even when it drifted off the mark: a double click is allowed a few pixels of drift,
        // and handed on it reaches activated() and switches the row straight back
        if (!onChevron && !onDot && !(mLastDotPressIndex.isValid() && mLastDotPressIndex == index)) {
            return QStyledItemDelegate::editorEvent(pEvent, pModel, option, index);
        }
        mPressAnswered = true;
        return true;
    }

    if (onChevron) {
        mpTree->setExpanded(index, !mpTree->isExpanded(index));
        mPressAnswered = true;
        return true;
    }

    if (!onDot) {
        return QStyledItemDelegate::editorEvent(pEvent, pModel, option, index);
    }

    if (QTreeWidgetItem* pItem = mpTree->itemFromIndex(index)) {
        mpTree->setCurrentItem(pItem);
        emit toggleRequested();
    }
    mPressAnswered = true;
    mLastDotPressIndex = index;
    return true;
}

} // namespace uiDesign
