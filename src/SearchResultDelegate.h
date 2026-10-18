#ifndef MUDLET_SEARCHRESULTDELEGATE_H
#define MUDLET_SEARCHRESULTDELEGATE_H

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

#include <QColor>
#include <QFont>
#include <QHash>
#include <QPixmap>
#include <QPointer>
#include <QStyledItemDelegate>

class QTreeWidget;

// What kind of item a heading stands for. Declared rather than included: the
// definition lives in EditorCommand.h, which brings the undo stack with it.
namespace EditorViewTypes {
enum class EditorViewType : int;
}
using EditorViewTypes::EditorViewType;

namespace uiDesign {

// The editor's search results, drawn rather than tabulated. What used to be four
// columns of text - kind, name, where, what - reads as a list of the items that
// matched, each with the places inside it that did: the item's name on a heading
// row with its kind as a chip beside it, and under it one row per match saying
// where in the item it is and showing the line it is on with the query marked in
// it.
//
// It is one delegate rather than a widget per row because a search of a large
// profile is thousands of rows, and a row that only exists while it is on screen
// costs nothing when it is not.
class SearchResultDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    // What a row is: the item that matched, one of the places inside it, or the
    // line saying nothing did
    enum RowKind { ItemRow = 0, MatchRow = 1, NoticeRow = 2 };

    // The data the drawing is done from. The navigation roles a row carries
    // alongside these - dlgTriggerEditor::SearchDataRole, which says where a row
    // leads and is private to that class - start at Qt::UserRole and are not
    // read here, except for the one below. The presentation roles start well
    // clear of them so that a navigation role added later needs nothing here.
    enum Role {
        // Mirrors dlgTriggerEditor::ItemRole - an EditorViewType as an int, and
        // also what kind of thing the heading's glyph and chip stand for
        ViewTypeRole = Qt::UserRole + 2,
        // One of RowKind
        RowKindRole = Qt::UserRole + 32,
        // The name shown on a heading row, or the words on a notice row
        TitleRole = Qt::UserRole + 33,
        // The kind of thing the heading stands for, in the chip beside its name
        TypeLabelRole = Qt::UserRole + 34,
        // Where inside the item a match is: "Name", "Pattern 2", "Lua 14"...
        WhereRole = Qt::UserRole + 35,
        // The line the match is on
        SnippetRole = Qt::UserRole + 36,
        // Where the query starts in that line, and how long it is - both in
        // QChars into the snippet, which is not always where the match is in the
        // item itself (a line has its tabs widened before it is shown)
        MatchStartRole = Qt::UserRole + 37,
        MatchLengthRole = Qt::UserRole + 38
    };

    Q_DISABLE_COPY(SearchResultDelegate)
    explicit SearchResultDelegate(QTreeWidget* pTree);

    // Re-read the colours off the application palette and drop the tinted
    // glyphs; called from the same pass that restyles the rest of the editor
    void restyle();

    void paint(QPainter* pPainter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    [[nodiscard]] QPixmap typeGlyph(const EditorViewType viewType, const bool selected) const;
    [[nodiscard]] static QFont scaledFont(const QFont& base, const qreal factor);
    void paintItemRow(QPainter* pPainter, const QStyleOptionViewItem& option, const QModelIndex& index, const QRect& contentRect) const;
    void paintMatchRow(QPainter* pPainter, const QStyleOptionViewItem& option, const QModelIndex& index, const QRect& contentRect) const;
    void paintNoticeRow(QPainter* pPainter, const QStyleOptionViewItem& option, const QModelIndex& index, const QRect& contentRect) const;

    QPointer<QTreeWidget> mpTree;
    QColor mTitleColor;
    QColor mMutedColor;
    QColor mChipBorder;
    QColor mSelectedText;
    QColor mMarkerColor;
    QColor mMarkerText;

    // Every row of a long list is measured, and the two answers only move with
    // the font the tree is drawn in - so they are taken once, in restyle()
    QFont mNameFont;
    QFont mChipFont;
    int mItemRowHeight = 0;
    int mMatchRowHeight = 0;

    // Seven kinds of thing times drawn-normally and drawn-on-a-chosen-row, and
    // every row of a long list asks for one
    mutable QHash<int, QPixmap> mTypeGlyphs;
    mutable qreal mTypeGlyphRatio = 0.0;
};

} // namespace uiDesign

#endif // MUDLET_SEARCHRESULTDELEGATE_H
