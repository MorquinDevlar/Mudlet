#ifndef MUDLET_EDITORTREEDELEGATE_H
#define MUDLET_EDITORTREEDELEGATE_H

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

#include "TTreeWidget.h"

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QPointer>
#include <QSize>
#include <QStyledItemDelegate>

class Host;

namespace uiDesign {

class EditorTreeDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    Q_DISABLE_COPY(EditorTreeDelegate)

    enum class DotState { Off = 0, WantedOn = 1, Running = 2 };

    enum class RowMark { None = 0, Folder = 1, Filter = 2, OffsetTimer = 3, Error = 4, NewFolder = 5, NewItem = 6, Package = 7 };

    // Folders view (TreeType::None) only: a row names its TreeType in this role beside the id,
    // and a row with neither is drawn as a group of folders, with no switch
    static constexpr int scmRole_rowTreeType = Qt::UserRole + 40;

    EditorTreeDelegate(TTreeWidget* pTree, const TreeType treeType, Host* pHost);

    // Must be called on a theme change: the cached glyphs keep the old inks until then
    void restyle();

    // Must match the accessible description the editor gives an unsaved item's or folder's row
    void setNewDescriptions(const QString& item, const QString& folder);

    void initStyleOption(QStyleOptionViewItem* pOption, const QModelIndex& index) const override;

    void paint(QPainter* pPainter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    bool editorEvent(QEvent* pEvent, QAbstractItemModel* pModel, const QStyleOptionViewItem& option, const QModelIndex& index) override;

    // On the viewport: the view reaches editorEvent() only through edit(), which gives up on an
    // invalid index, so the rest of a press that leaves its row would never arrive there
    bool eventFilter(QObject* pWatched, QEvent* pEvent) override;

    // This and chevronHitRect() are for tests: the editor's own events carry the option they are measured against
    [[nodiscard]] QRect dotHitRect(const QModelIndex& index) const;

    [[nodiscard]] QRect chevronHitRect(const QModelIndex& index) const;

    // For tests, which assert against these rather than against the drawn pictures
    [[nodiscard]] DotState dotStateOf(const QModelIndex& index) const;
    [[nodiscard]] RowMark markOf(const QModelIndex& index) const;

signals:
    // No argument: the row is made the tree's current one first, and the slot reads that
    void toggleRequested();

private:
    enum class ChevronState { None = 0, Closed = 1, Open = 2 };

    static constexpr int scmRowMarkCount = 8;
    static constexpr int scmDotStateCount = 3;

    struct ItemState
    {
        bool known = false;
        // A group of folders has no switch, but keeps the dot's room so its mark lines up with its siblings'
        bool dotSlot = false;
        DotState dot = DotState::Off;
        RowMark mark = RowMark::None;
    };

    struct Decoration
    {
        QIcon icon;
        QSize size;
    };

    [[nodiscard]] QRect dotHitRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;
    [[nodiscard]] QRect chevronHitRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;
    [[nodiscard]] QStyleOptionViewItem rowOption(const QModelIndex& index) const;

    void clearDecorations() const;
    // Call before reading either cache, not while filling one: a screen change clears what a lookup would return
    void syncGlyphRatio() const;
    [[nodiscard]] qreal glyphRatio() const;
    [[nodiscard]] ItemState stateOf(const QModelIndex& index) const;
    // One deeper in the Folders view: it has no heading, so its top rows stand where an item tree's first items do
    [[nodiscard]] int rowLevelOf(const QModelIndex& index) const;
    void paintTypeLabel(QPainter* pPainter, const QStyleOptionViewItem& option, const QModelIndex& index) const;
    [[nodiscard]] ChevronState chevronOf(const QModelIndex& index, const int level) const;
    [[nodiscard]] QPixmap dotGlyph(const DotState state, const bool broken) const;
    [[nodiscard]] QPixmap chevronGlyph(const ChevronState state) const;
    [[nodiscard]] QPixmap markGlyph(const RowMark mark, const bool selected) const;
    [[nodiscard]] Decoration decorationFor(const ItemState& state, const int level, const ChevronState chevron, const bool selected) const;

    TTreeWidget* mpTree = nullptr;
    TreeType mTreeType = TreeType::None;
    QPointer<Host> mpHost;
    int mIndentStep = 0;
    QColor mRunningDot;
    QColor mQuietDot;
    QColor mChevronInk;
    QColor mMarkInk;
    QColor mErrorMarkInk;
    QColor mSelectedMarkInk;
    QColor mAccentBar;
    QString mNewItemDescription;
    QString mNewFolderDescription;
    bool mPressAnswered = false;
    QPersistentModelIndex mLastDotPressIndex;

    // Cached because laying a tree out asks every one of its rows for a size
    mutable QPixmap mDotGlyphs[2 * scmDotStateCount];
    // Indexed by ChevronState, so slot 0 (None) stays empty
    mutable QPixmap mChevronGlyphs[3];
    mutable QPixmap mMarkGlyphs[2 * scmRowMarkCount];
    mutable qreal mDotGlyphRatio = 0.0;
    // Composed once rather than per paint and per measure; keyed on the leading edge's inputs alone,
    // so a tree of any size fills it with a handful of entries
    mutable QHash<int, Decoration> mDecorations;
};

} // namespace uiDesign

#endif // MUDLET_EDITORTREEDELEGATE_H
