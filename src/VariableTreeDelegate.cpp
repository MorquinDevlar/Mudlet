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

#include "VariableTreeDelegate.h"

#include "ChipRow.h"
#include "EditorTreeRowMetrics.h"
#include "TTreeWidget.h"
#include "TVar.h"
#include "uiDesign.h"
#include "utils.h"

#include <QApplication>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QToolTip>
#include <QTreeWidgetItem>

#include <algorithm>

namespace uiDesign {

namespace {
// A chip's full corner would round a 9px square away altogether
constexpr qreal scmKeptSquareRadius = scmRadiusChip / 2.0;
// The drawing elides anyway; this stops a row carrying a megabyte string around
constexpr int scmPreviewCharacterLimit = 80;
constexpr int scmPreviewMinimumCharacters = 6;
} // namespace

VariableTreeDelegate::VariableTreeDelegate(TTreeWidget* pTree)
: QStyledItemDelegate(pTree)
, mpTree(pTree)
{
    if (mpTree) {
        // Indentation and chevron are drawn in the decoration, as EditorTreeDelegate
        // does, so the row is one rectangle the selection pill covers in one shape
        mIndentStep = mpTree->indentation() > 0 ? mpTree->indentation() : scmTreeFallbackIndentStep;
        mpTree->setIndentation(0);
        mpTree->viewport()->installEventFilter(this);
    }
    restyle();
}

void VariableTreeDelegate::restyle()
{
    const ThemeTokens tokens = themeTokens();
    mChromeInk = tokens.mutedText;
    mSelectedInk = tokens.accentText;
    mDisabledInk = tokens.disabledText;
    // A running dot's blue, so "kept" and "on" read as one kind of statement
    mKeptFill = stateColor(scmStateHue_ok, tokens.darkPage);
    mAccentBar = tokens.accent;
    clearGlyphs();
}

void VariableTreeDelegate::clearGlyphs() const
{
    for (auto& cached : mTypeGlyphs) {
        cached = QPixmap();
    }
    for (auto& cached : mHiddenGlyphs) {
        cached = QPixmap();
    }
    for (auto& cached : mKeptGlyphs) {
        cached = QPixmap();
    }
    for (auto& cached : mChevronGlyphs) {
        cached = QPixmap();
    }
    mDecorations.clear();
}

qreal VariableTreeDelegate::glyphRatio() const
{
    return mpTree ? mpTree->devicePixelRatioF() : 1.0;
}

void VariableTreeDelegate::syncGlyphRatio() const
{
    const qreal ratio = glyphRatio();
    if (qFuzzyCompare(ratio + 1.0, mGlyphRatio + 1.0)) {
        return;
    }
    mGlyphRatio = ratio;
    clearGlyphs();
}

void VariableTreeDelegate::syncPreviewFont() const
{
    if (!mpTree) {
        return;
    }
    const QFont wanted = mpTree->font();
    if (!mPreviewFont.family().isEmpty() && wanted == mMeasuredFrom) {
        return;
    }
    mMeasuredFrom = wanted;
    mPreviewFont = chipFont(mpTree);
    mPreviewMetrics = QFontMetrics(mPreviewFont);
}

QString VariableTreeDelegate::variablePreview(TVar* pVar)
{
    if (!pVar) {
        return {};
    }

    switch (pVar->getValueType()) {
    case LUA_TSTRING: {
        // A row is one line tall; a paragraph would otherwise show its second
        // line's indentation
        QString value = pVar->getValue().section(QChar('\n'), 0, 0).simplified();
        if (value.length() > scmPreviewCharacterLimit) {
            value = value.left(scmPreviewCharacterLimit) + QChar(0x2026);
        }
        return qsl("\"%1\"").arg(value);
    }
    case LUA_TNUMBER:
        [[fallthrough]];
    case LUA_TBOOLEAN:
        // Not translated: true and false are values, not descriptions of one
        return pVar->getValue();
    case LUA_TTABLE: {
        const QList<TVar*> members = pVar->getChildren(false);
        if (members.isEmpty()) {
            return qsl("{ }");
        }
        bool everyKeyAnIndex = true;
        for (const TVar* pMember : members) {
            if (pMember->getKeyType() != LUA_TNUMBER) {
                everyKeyAnIndex = false;
                break;
            }
        }
        if (everyKeyAnIndex) {
            //: How a Lua table whose every key is a number is summarised at the trailing edge of its row in the editor's Variables view. %1 is how many members it has.
            return tr("{ %1 items }").arg(members.size());
        }
        //: How a Lua table is summarised at the trailing edge of its row in the editor's Variables view. %1 is how many members it has.
        return tr("{ %1 keys }").arg(members.size());
    }
    case LUA_TFUNCTION:
        //: Shown at the trailing edge of a row in the editor's Variables view where the variable holds a Lua function rather than a value that can be shown.
        return tr("function");
    case LUA_TNIL:
        // Lua's own type names, not translated either
        return qsl("nil");
    case LUA_TLIGHTUSERDATA:
        [[fallthrough]];
    case LUA_TUSERDATA:
        return qsl("userdata");
    case LUA_TTHREAD:
        return qsl("thread");
    default:
        // LUA_TNONE: a variable just made in the editor, with no value yet
        return {};
    }
}

void VariableTreeDelegate::setVariableRowData(QTreeWidgetItem* pItem, TVar* pVar, const bool hidden)
{
    if (!pItem || !pVar) {
        return;
    }
    pItem->setData(0, Qt::UserRole, pVar->getValueType());
    pItem->setData(0, scmRole_variablePreview, variablePreview(pVar));
    pItem->setData(0, scmRole_variableKeyIsIndex, pVar->getKeyType() == LUA_TNUMBER);
    pItem->setData(0, scmRole_variableHidden, hidden);
}

VariableTreeDelegate::RowState VariableTreeDelegate::stateOf(const QModelIndex& index) const
{
    RowState state;
    if (!index.isValid()) {
        return state;
    }
    const QVariant typeData = index.data(Qt::UserRole);
    if (!typeData.isValid()) {
        // The tree's heading row, which stands for nothing Lua holds
        return state;
    }

    state.known = true;
    switch (typeData.toInt()) {
    case LUA_TTABLE:
        state.type = TypeMark::Table;
        break;
    case LUA_TSTRING:
        state.type = TypeMark::String;
        break;
    case LUA_TNUMBER:
        state.type = TypeMark::Number;
        break;
    case LUA_TBOOLEAN:
        state.type = TypeMark::Boolean;
        break;
    case LUA_TFUNCTION:
        state.type = TypeMark::Function;
        break;
    default:
        state.type = TypeMark::Other;
        break;
    }

    // Read off the flags, not asked of Lua: the places that build a row strip
    // ItemIsUserCheckable, and they are the ones that know why
    state.unavailable = !(index.flags() & Qt::ItemIsUserCheckable);
    const QVariant keptData = index.data(Qt::CheckStateRole);
    if (state.unavailable || !keptData.isValid()) {
        state.kept = KeptState::None;
        return state;
    }
    switch (static_cast<Qt::CheckState>(keptData.toInt())) {
    case Qt::Checked:
        state.kept = KeptState::All;
        break;
    case Qt::PartiallyChecked:
        state.kept = KeptState::Some;
        break;
    default:
        state.kept = KeptState::Off;
        break;
    }
    return state;
}

// No chevron at level 0: the root is undecorated and its one row is the heading
VariableTreeDelegate::ChevronState VariableTreeDelegate::chevronOf(const QModelIndex& index, const int level) const
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

QString VariableTreeDelegate::displayNameFor(const QModelIndex& index) const
{
    const QString name = index.data(Qt::DisplayRole).toString();
    if (!index.data(scmRole_variableKeyIsIndex).toBool()) {
        return name;
    }
    return qsl("[%1]").arg(name);
}

QString VariableTreeDelegate::typeGlyphFile(const QModelIndex& index) const
{
    switch (stateOf(index).type) {
    case TypeMark::Table:
        // A table is braces wherever it is drawn, the sidebar's Variables row included
        return qsl(":/icons/editor-variables.svg");
    case TypeMark::String:
        return qsl(":/icons/editor-type-string.svg");
    case TypeMark::Number:
        return qsl(":/icons/editor-type-number.svg");
    case TypeMark::Boolean:
        return qsl(":/icons/editor-type-boolean.svg");
    case TypeMark::Function:
        return qsl(":/icons/editor-type-function.svg");
    case TypeMark::Other:
        return qsl(":/icons/editor-type-other.svg");
    case TypeMark::None:
        return {};
    }
    return {};
}

QColor VariableTreeDelegate::rowInk(const QModelIndex& index, const bool selected) const
{
    return inkForSlot(inkSlotFor(stateOf(index), selected));
}

bool VariableTreeDelegate::carriesHiddenMark(const QModelIndex& index) const
{
    return index.isValid() && index.column() == 0 && index.data(scmRole_variableHidden).toBool();
}

// Selection wins: the accent is the one ink that holds on the selection wash
int VariableTreeDelegate::inkSlotFor(const RowState& state, const bool selected) const
{
    if (selected) {
        return 1;
    }
    return state.unavailable ? 2 : 0;
}

QColor VariableTreeDelegate::inkForSlot(const int slot) const
{
    switch (slot) {
    case 1:
        return mSelectedInk;
    case 2:
        return mDisabledInk;
    default:
        return mChromeInk;
    }
}

QPixmap VariableTreeDelegate::keptGlyph(const KeptState kept, const bool selected) const
{
    QPixmap& glyph = mKeptGlyphs[2 * static_cast<int>(kept) + (selected ? 1 : 0)];
    if (!glyph.isNull() || kept == KeptState::None) {
        return glyph;
    }

    const qreal ratio = mGlyphRatio;
    glyph = QPixmap(qRound(scmTreeDotDiameter * ratio), qRound(scmTreeDotDiameter * ratio));
    glyph.setDevicePixelRatio(ratio);
    glyph.fill(Qt::transparent);

    QPainter painter(&glyph);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // A stroke is centred on its path; half a pen in keeps hollow and filled the same size
    const qreal inset = (kept == KeptState::All) ? 0.0 : scmTreeHollowPenWidth / 2.0;
    const QRectF square(inset, inset, scmTreeDotDiameter - 2.0 * inset, scmTreeDotDiameter - 2.0 * inset);
    if (kept == KeptState::All) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(mKeptFill);
        painter.drawRoundedRect(square, scmKeptSquareRadius, scmKeptSquareRadius);
    } else {
        if (kept == KeptState::Some) {
            painter.save();
            painter.setClipRect(QRectF(square.left(), square.top(), square.width() / 2.0, square.height()));
            painter.setPen(Qt::NoPen);
            painter.setBrush(mKeptFill);
            painter.drawRoundedRect(square, scmKeptSquareRadius, scmKeptSquareRadius);
            painter.restore();
        }
        QPen pen(selected ? mSelectedInk : mChromeInk);
        pen.setWidthF(scmTreeHollowPenWidth);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(square, scmKeptSquareRadius, scmKeptSquareRadius);
    }
    // A copy taken while a painter is still active is a copy of something mid-write
    painter.end();
    return glyph;
}

QPixmap VariableTreeDelegate::chevronGlyph(const ChevronState chevron) const
{
    QPixmap& glyph = mChevronGlyphs[static_cast<int>(chevron)];
    if (!glyph.isNull() || chevron == ChevronState::None) {
        return glyph;
    }
    glyph = treeRowChevronGlyph(chevron == ChevronState::Open, mChromeInk, mGlyphRatio);
    return glyph;
}

QPixmap VariableTreeDelegate::typeGlyph(const TypeMark type, const QColor& ink, const int cacheSlot) const
{
    QPixmap& glyph = mTypeGlyphs[scmTypeMarkCount * cacheSlot + static_cast<int>(type)];
    if (!glyph.isNull() || type == TypeMark::None) {
        return glyph;
    }
    QString file;
    switch (type) {
    case TypeMark::Table:
        file = qsl(":/icons/editor-variables.svg");
        break;
    case TypeMark::String:
        file = qsl(":/icons/editor-type-string.svg");
        break;
    case TypeMark::Number:
        file = qsl(":/icons/editor-type-number.svg");
        break;
    case TypeMark::Boolean:
        file = qsl(":/icons/editor-type-boolean.svg");
        break;
    case TypeMark::Function:
        file = qsl(":/icons/editor-type-function.svg");
        break;
    case TypeMark::Other:
        file = qsl(":/icons/editor-type-other.svg");
        break;
    case TypeMark::None:
        return glyph;
    }
    glyph = tintedGlyph(uiDesign::glyphPixmap(file), ink).scaled(QSize(scmTreeMarkSize, scmTreeMarkSize) * mGlyphRatio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    glyph.setDevicePixelRatio(mGlyphRatio);
    return glyph;
}

QPixmap VariableTreeDelegate::hiddenGlyph(const QColor& ink, const int cacheSlot) const
{
    QPixmap& glyph = mHiddenGlyphs[cacheSlot];
    if (!glyph.isNull()) {
        return glyph;
    }
    glyph = tintedGlyph(uiDesign::glyphPixmap(qsl(":/icons/editor-hidden.svg")), ink).scaled(QSize(scmTreeMarkSize, scmTreeMarkSize) * mGlyphRatio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    glyph.setDevicePixelRatio(mGlyphRatio);
    return glyph;
}

VariableTreeDelegate::Decoration VariableTreeDelegate::decorationFor(const RowState& state, const int level, const ChevronState chevron, const bool selected) const
{
    syncGlyphRatio();

    const int inkSlot = inkSlotFor(state, selected);
    const int key = (state.known ? 1 : 0) | (static_cast<int>(state.kept) << 1) | (static_cast<int>(chevron) << 3) | (static_cast<int>(state.type) << 5) | (inkSlot << 8) | (level << 10);
    if (const auto cached = mDecorations.constFind(key); cached != mDecorations.constEnd()) {
        return cached.value();
    }
    if (mDecorations.size() >= scmTreeDecorationCacheLimit) {
        mDecorations.clear();
    }

    // The square's room is left even where it is not drawn, so an unkeepable row's
    // name lines up with its siblings'
    const int lead = level * mIndentStep;
    const int keptWidth = state.known ? scmTreeDotDiameter : 0;
    const int markWidth = state.type == TypeMark::None ? 0 : scmTreeMarkSize;
    const int gap = (keptWidth > 0 && markWidth > 0) ? scmTreeDotGap : 0;
    const int slotWidth = std::max(lead + keptWidth + gap + markWidth, 1);

    QPixmap composed(qRound(slotWidth * mGlyphRatio), qRound(scmTreeSlotHeight * mGlyphRatio));
    composed.setDevicePixelRatio(mGlyphRatio);
    composed.fill(Qt::transparent);
    QPainter painter(&composed);
    if (chevron != ChevronState::None) {
        painter.drawPixmap(QPointF(lead - mIndentStep + (mIndentStep - scmTreeChevronBox) / 2.0, (scmTreeSlotHeight - scmTreeChevronBox) / 2.0), chevronGlyph(chevron));
    }
    if (state.kept != KeptState::None) {
        painter.drawPixmap(QPointF(lead, (scmTreeSlotHeight - scmTreeDotDiameter) / 2.0), keptGlyph(state.kept, selected));
    }
    if (markWidth > 0) {
        painter.drawPixmap(QPointF(lead + keptWidth + gap, (scmTreeSlotHeight - scmTreeMarkSize) / 2.0), typeGlyph(state.type, inkForSlot(inkSlot), inkSlot));
    }
    painter.end();

    Decoration decoration;
    decoration.icon = QIcon(composed);
    decoration.size = QSize(slotWidth, scmTreeSlotHeight);
    mDecorations.insert(key, decoration);
    return decoration;
}

void VariableTreeDelegate::initStyleOption(QStyleOptionViewItem* pOption, const QModelIndex& index) const
{
    QStyledItemDelegate::initStyleOption(pOption, index);

    // The square is drawn in the decoration, so no room is reserved for a check indicator
    pOption->features &= ~QStyleOptionViewItem::HasCheckIndicator;

    const RowState state = stateOf(index);
    const int level = treeRowLevelOf(index);
    const ChevronState chevron = chevronOf(index, level);
    // The heading row keeps the icon restyleEditorTreeHeadingIcons() set, at mark
    // size so the heading is as tall as the rows it heads
    if (!state.known && level < 1 && chevron == ChevronState::None) {
        pOption->decorationSize = QSize(scmTreeMarkSize, scmTreeMarkSize);
        return;
    }

    const bool selected = pOption->state & QStyle::State_Selected;
    const Decoration decoration = decorationFor(state, level, chevron, selected);
    pOption->decorationSize = decoration.size;
    pOption->icon = decoration.icon;
    pOption->features |= QStyleOptionViewItem::HasDecoration;

    pOption->text = displayNameFor(index);

    // The tree's stylesheet inks only the chosen row
    if (!selected) {
        const QColor ink = inkForSlot(inkSlotFor(state, false));
        pOption->palette.setColor(QPalette::Text, ink);
        pOption->palette.setColor(QPalette::WindowText, ink);
    }
}

void VariableTreeDelegate::paint(QPainter* pPainter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (index.column() != 0 || option.rect.isEmpty()) {
        QStyledItemDelegate::paint(pPainter, option, index);
        return;
    }

    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    const QWidget* pWidget = opt.widget;
    QStyle* pStyle = pWidget ? pWidget->style() : QApplication::style();

    syncGlyphRatio();
    syncPreviewFont();

    const bool selected = opt.state & QStyle::State_Selected;
    const QColor ink = inkForSlot(inkSlotFor(stateOf(index), selected));
    const RowLayout layout = layoutOf(opt, index);
    opt.text = layout.name;

    pPainter->save();
    pStyle->drawControl(QStyle::CE_ItemViewItem, &opt, pPainter, pWidget);
    pPainter->restore();

    if (selected) {
        paintAccentBar(pPainter, opt.rect, mAccentBar);
    }

    if (layout.hidden.isEmpty() && layout.preview.isEmpty()) {
        return;
    }

    pPainter->save();
    pPainter->setClipRect(opt.rect);
    if (!layout.hidden.isEmpty()) {
        pPainter->drawPixmap(layout.hidden.topLeft(), hiddenGlyph(ink, inkSlotFor(stateOf(index), selected)));
    }
    if (!layout.preview.isEmpty()) {
        pPainter->setFont(mPreviewFont);
        pPainter->setPen(ink);
        pPainter->drawText(layout.previewRect, Qt::AlignRight | Qt::AlignVCenter, layout.preview);
    }
    pPainter->restore();
}

// Asked of the style: the tree carries a stylesheet, so QStyleSheetStyle decides a row's padding
VariableTreeDelegate::RowLayout VariableTreeDelegate::layoutOf(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    syncPreviewFont();

    RowLayout layout;
    const QWidget* pWidget = option.widget;
    QStyle* pStyle = pWidget ? pWidget->style() : QApplication::style();
    const QRect textRect = pStyle->subElementRect(QStyle::SE_ItemViewItemText, &option, pWidget);
    if (textRect.isEmpty()) {
        layout.name = option.text;
        return layout;
    }

    const QFontMetrics nameMetrics(option.font);
    const bool hidden = carriesHiddenMark(index);
    const int hiddenRoom = hidden ? scmTreeMarkSize + scmTreeDotGap : 0;
    layout.name = nameMetrics.elidedText(option.text, Qt::ElideRight, std::max(0, textRect.width() - hiddenRoom));
    layout.nameWidth = nameMetrics.horizontalAdvance(layout.name);
    if (hidden) {
        layout.hidden = QRect(textRect.left() + layout.nameWidth + scmTreeDotGap, textRect.top() + (textRect.height() - scmTreeMarkSize) / 2, scmTreeMarkSize, scmTreeMarkSize);
    }

    const QString preview = index.data(scmRole_variablePreview).toString();
    const int previewRoom = textRect.width() - layout.nameWidth - hiddenRoom - scmTreeDotGap;
    const int previewFloor = scmPreviewMinimumCharacters * std::max(1, mPreviewMetrics.horizontalAdvance(QChar('0')));
    if (preview.isEmpty() || previewRoom < previewFloor) {
        return layout;
    }
    layout.preview = mPreviewMetrics.elidedText(preview, Qt::ElideRight, previewRoom);
    layout.previewRect = QRect(textRect.right() - previewRoom + 1, textRect.top(), previewRoom, textRect.height());
    return layout;
}

QRect VariableTreeDelegate::keptHitRect(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (!mpTree || !index.isValid() || index.column() != 0 || option.rect.isEmpty() || stateOf(index).kept == KeptState::None) {
        return {};
    }

    QStyleOptionViewItem keptOption(option);
    initStyleOption(&keptOption, index);
    const QRect decorationRect = mpTree->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &keptOption, mpTree);
    if (decorationRect.isEmpty()) {
        return {};
    }

    // Only the square: the rest of the leading edge is left to start a drag from
    const int lead = treeRowLevelOf(index) * mIndentStep;
    const QRect square(decorationRect.left() + lead, decorationRect.top() + (decorationRect.height() - scmTreeDotDiameter) / 2, scmTreeDotDiameter, scmTreeDotDiameter);
    return square.adjusted(-scmTreeMarkHitSlack, -scmTreeMarkHitSlack, scmTreeMarkHitSlack, scmTreeMarkHitSlack);
}

QRect VariableTreeDelegate::chevronHitRect(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const int level = treeRowLevelOf(index);
    if (!mpTree || !index.isValid() || index.column() != 0 || option.rect.isEmpty() || chevronOf(index, level) == ChevronState::None) {
        return {};
    }

    QStyleOptionViewItem chevronOption(option);
    initStyleOption(&chevronOption, index);
    const QRect decorationRect = mpTree->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &chevronOption, mpTree);
    if (decorationRect.isEmpty()) {
        return {};
    }
    return QRect(decorationRect.left() + (level - 1) * mIndentStep, option.rect.top(), mIndentStep, option.rect.height());
}

QStyleOptionViewItem VariableTreeDelegate::rowOption(const QModelIndex& index) const
{
    QStyleOptionViewItem option = mpTree->viewItemOption();
    option.rect = mpTree->visualRect(index);
    if (index == mpTree->currentIndex()) {
        option.state |= QStyle::State_HasFocus;
    }
    return option;
}

QRect VariableTreeDelegate::keptHitRect(const QModelIndex& index) const
{
    if (!mpTree) {
        return {};
    }
    return keptHitRect(rowOption(index), index);
}

QRect VariableTreeDelegate::chevronHitRect(const QModelIndex& index) const
{
    if (!mpTree) {
        return {};
    }
    return chevronHitRect(rowOption(index), index);
}

QString VariableTreeDelegate::keptTooltip(const QModelIndex& index) const
{
    switch (stateOf(index).kept) {
    case KeptState::All:
        //: Tooltip on the square at the head of a row in the editor's Variables view, where the profile keeps this variable
        return tr("Kept with the profile - click to change");
    case KeptState::Some:
        //: Tooltip on the square at the head of a table's row in the editor's Variables view, where the profile keeps some of the table's members
        return tr("Some of this table is kept with the profile - click to keep all of it");
    case KeptState::Off:
        //: Tooltip on the square at the head of a row in the editor's Variables view, where the profile does not keep this variable
        return tr("Not kept with the profile - click to keep it");
    case KeptState::None:
        break;
    }
    return {};
}

bool VariableTreeDelegate::helpEvent(QHelpEvent* pEvent, QAbstractItemView* pView, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    if (!pEvent || pEvent->type() != QEvent::ToolTip) {
        return QStyledItemDelegate::helpEvent(pEvent, pView, option, index);
    }

    QString note;
    if (keptHitRect(option, index).contains(pEvent->pos())) {
        note = keptTooltip(index);
    } else {
        QStyleOptionViewItem rowOption(option);
        initStyleOption(&rowOption, index);
        if (layoutOf(rowOption, index).hidden.contains(pEvent->pos())) {
            //: Tooltip on the mark beside the name of a variable in the editor's Variables view that is only listed while hidden variables are being shown
            note = tr("Hidden unless hidden variables are shown");
        }
    }
    if (!note.isEmpty()) {
        QToolTip::showText(pEvent->globalPos(), utils::richText(note), pView);
        return true;
    }
    return QStyledItemDelegate::helpEvent(pEvent, pView, option, index);
}

bool VariableTreeDelegate::eventFilter(QObject* pWatched, QEvent* pEvent)
{
    // QStyledItemDelegate also filters the editor widgets it opens; those stay its own
    if (!mpTree || pWatched != mpTree->viewport()) {
        return QStyledItemDelegate::eventFilter(pWatched, pEvent);
    }

    switch (pEvent->type()) {
    case QEvent::MouseButtonPress:
        // A double click's second press arrives as MouseButtonDblClick, not as a press
        mPressAnswered = false;
        mLastKeptPressIndex = QPersistentModelIndex();
        break;
    case QEvent::MouseMove:
        // The view records the press before it consults the delegate, so a move
        // handed on would start dragging the row out of the tree
        if (mPressAnswered && (static_cast<QMouseEvent*>(pEvent)->buttons() & Qt::LeftButton)) {
            return true;
        }
        break;
    case QEvent::MouseButtonRelease:
        // Eaten wherever it lands: handed on, the view would read it as a click on
        // the row it ends over and load that row into the form
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

bool VariableTreeDelegate::editorEvent(QEvent* pEvent, QAbstractItemModel* pModel, const QStyleOptionViewItem& option, const QModelIndex& index)
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
    // Chevron first: the square's hit slack overlaps it
    const bool onChevron = chevronHitRect(option, index).contains(pressedAt);
    const bool onSquare = !onChevron && keptHitRect(option, index).contains(pressedAt);

    if (eventType == QEvent::MouseButtonDblClick) {
        // The first press already acted, so the second is eaten even off the mark: a
        // double click may drift a few pixels, most of the way out of the square
        if (!onChevron && !onSquare && !(mLastKeptPressIndex.isValid() && mLastKeptPressIndex == index)) {
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

    if (!onSquare) {
        return QStyledItemDelegate::editorEvent(pEvent, pModel, option, index);
    }

    if (QTreeWidgetItem* pItem = mpTree->itemFromIndex(index)) {
        mpTree->setCurrentItem(pItem);
        emit keptToggleRequested();
    }
    mPressAnswered = true;
    mLastKeptPressIndex = index;
    return true;
}

} // namespace uiDesign
