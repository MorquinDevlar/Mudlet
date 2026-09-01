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

// A row in one of the editor's item trees leads with a dot saying whether the
// thing it stands for is on. Where the state used to be spelt out by swapping
// between four near-identical tick-box pictures, there is now one shape in three
// readings: filled where the item is running, filled but quiet where the user
// has it switched on inside a switched-off group, and hollow where it is off.
//
// The dot is drawn into the row's decoration rather than over the top of it, so
// the view reserves room for it by itself and no measurement has to be repeated
// here. What a dot cannot say - that the row is a folder, a filter chain, an
// offset timer, a broken item, an addition nobody has saved yet - is said by a
// mark beside it, drawn from the same monochrome set the rest of the window is
// and inked from the same palette. Every mark is drawn at one size, so a row
// carrying one is exactly as tall as a row carrying none and a tree of them is
// one ladder of evenly spaced rows.
//
// The room a row is held in by its depth, and the chevron that folds a group
// away, are drawn into that same decoration - so the view is asked for no
// indentation at all and a row is one rectangle from the panel's edge to its
// far side. That is what lets the selection be a single pill: where the view
// indents, it paints the column the chevron stands in itself, in the style's own
// selection colour, and no stylesheet rule reaches the several cells a nested
// row's column is drawn as.
class EditorTreeDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    Q_DISABLE_COPY(EditorTreeDelegate)
    EditorTreeDelegate(TTreeWidget* pTree, const TreeType treeType, Host* pHost);

    // Re-read the colours off the application palette; called from the same pass
    // that restyles the rest of the editor, so a theme change reaches the dots
    void restyle();

    // What the editor writes into a row's accessible description while the item
    // it stands for has never been saved - one string for an item and one for a
    // group. The only record of it there is for any of the six types: TTrigger,
    // TAlias and TScript keep an mIsNew as well, but it is set on construction
    // and cleared only by a save from this editor, so everything loaded out of
    // the profile carries it for the whole session.
    void setNewDescriptions(const QString& item, const QString& folder);

    void initStyleOption(QStyleOptionViewItem* pOption, const QModelIndex& index) const override;

    // The dot is the switch the row draws and the chevron is the handle that
    // folds it away, so the presses that land on either are answered here rather
    // than by the view - which is where Qt answers the clicks on an item's check
    // box from, and for the same reason. Only the presses: the view asks a
    // delegate about a mouse event by way of edit(), which is given an index and
    // gives up on an invalid one, so nothing here hears the rest of a press
    // whose pointer has left the row it started on.
    bool editorEvent(QEvent* pEvent, QAbstractItemModel* pModel, const QStyleOptionViewItem& option, const QModelIndex& index) override;

    // Installed on the tree's viewport, which every mouse event reaches whether
    // or not it lands on a row - including the release that Qt's implicit grab
    // delivers there from wherever the pointer ended up. That is where a press
    // the dot or the chevron answered is seen through to its end: the moves it
    // would otherwise become a drag of the row on are swallowed here, and so is
    // the release that closes it - which the view would otherwise read as a
    // click on the row, and reload the row over whatever is being edited. A
    // switch is not a handle.
    bool eventFilter(QObject* pWatched, QEvent* pEvent) override;

    // Where a row's dot can be clicked, in the viewport's coordinates. The
    // editor never asks: the events that need this arrive with the option it is
    // measured against. The tests that aim a synthesized click at a dot do.
    [[nodiscard]] QRect dotHitRect(const QModelIndex& index) const;

    // ...and where its chevron can be, which is empty for a row with nothing
    // folded inside it. The cell the view used to draw the branch arrow in,
    // which is why it is a whole indentation step wide rather than the size of
    // the glyph in it.
    [[nodiscard]] QRect chevronHitRect(const QModelIndex& index) const;

signals:
    // Sent once the row to switch is the tree's current one, so there is nothing
    // to carry: this reaches the slot that a double click on a row reaches by
    // way of the tree's own itemActivated(), and that slot reads the tree
    void toggleRequested();

private:
    // Three readings of one shape, in the order they are cached in
    enum class DotState { Off = 0, WantedOn = 1, Running = 2 };

    // Whether the row holds anything, and which way round the handle that folds
    // it is drawn. A row at the top of a tree is never given one: the view drew
    // no arrow beside those either, because the trees are asked for an
    // undecorated root and the one row at that depth is the tree's own heading.
    enum class ChevronState { None = 0, Closed = 1, Open = 2 };

    // What the row is, where the dot does not say it. Resolved from the item the
    // row's id names rather than from the picture the row happens to be
    // carrying, which is no longer looked at.
    enum class RowMark { None = 0, Folder = 1, Filter = 2, OffsetTimer = 3, Error = 4, NewFolder = 5, NewItem = 6 };
    static constexpr int scmRowMarkCount = 7;

    // What a row is drawn from, all of it read off the item its id names
    struct ItemState
    {
        bool known = false;
        DotState dot = DotState::Off;
        RowMark mark = RowMark::None;
    };

    // What a row's decoration ends up being: the room its depth holds it in and
    // the chevron standing in the last step of that room, then the dot, then the
    // mark - however many of those the row has
    struct Decoration
    {
        QIcon icon;
        QSize size;
    };

    // Where a row's dot can be clicked, in viewport coordinates; a null
    // rectangle for a row that has no dot. The dot's own square grown by a
    // couple of pixels - a 9px target is not one to ask for accuracy on - and no
    // more than that, so the rest of the row's leading edge is still somewhere a
    // drag can be started from. Measured against the option the view laid the
    // row out from, which is the one handed to editorEvent().
    [[nodiscard]] QRect dotHitRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;
    // ...and where its chevron can be, measured the same way
    [[nodiscard]] QRect chevronHitRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;
    // What the view would have laid a row out from, for the two rectangles above
    // when they are asked for outside an event that carries one
    [[nodiscard]] QStyleOptionViewItem rowOption(const QModelIndex& index) const;

    void clearDecorations() const;
    // A change of screen is noticed here, and what it throws away is exactly
    // what a decoration lookup would otherwise hand back - so it is asked
    // before either cache is looked in rather than while one is being filled
    void syncGlyphRatio() const;
    [[nodiscard]] qreal glyphRatio() const;
    [[nodiscard]] ItemState stateOf(const QModelIndex& index) const;
    [[nodiscard]] ChevronState chevronOf(const QModelIndex& index, const int level) const;
    [[nodiscard]] QPixmap dotGlyph(const DotState state) const;
    [[nodiscard]] QPixmap chevronGlyph(const ChevronState state) const;
    // The mark's glyph, tinted and cut to the one size every row's is drawn at.
    // A chosen row's is inked in the colour its name is written in, which the
    // tree's stylesheet names and this is handed by restyle().
    [[nodiscard]] QPixmap markGlyph(const RowMark mark, const bool selected) const;
    [[nodiscard]] Decoration decorationFor(const ItemState& state, const int level, const ChevronState chevron, const bool selected) const;

    TTreeWidget* mpTree = nullptr;
    TreeType mTreeType = TreeType::None;
    QPointer<Host> mpHost;
    // What the view was indenting each level by before it was asked to stop, so
    // that a row sits exactly where it used to and the chevron stands where the
    // branch arrow stood
    int mIndentStep = 0;
    QColor mRunningDot;
    QColor mQuietDot;
    QColor mChevronInk;
    QColor mMarkInk;
    // What the tree's stylesheet writes a chosen row's name in, so that the mark
    // beside that name is the one colour with it
    QColor mSelectedMarkInk;
    QString mNewItemDescription;
    QString mNewFolderDescription;
    // Set by a press the dot or the chevron answered and cleared by the release
    // that closes it, wherever that release lands - and by the next press either
    // way, so that a press whose release never arrived cannot leave this saying
    // otherwise
    bool mPressAnswered = false;
    // The row of the press the dot answered, which is what the second press of a
    // double click is read against. A double click is allowed a few pixels of
    // drift by the platform, and that is most of the dot's small square.
    QPersistentModelIndex mLastDotPressIndex;

    // Three shapes are all a whole tree of rows needs, and a tree being laid out
    // asks every one of its rows for a size
    mutable QPixmap mDotGlyphs[3];
    // ...and two for the chevron, kept in the same order the state is numbered
    // in with the leading slot for the row that has none left unused
    mutable QPixmap mChevronGlyphs[3];
    // ...and one mark per reading in each of the two inks, which is every
    // picture a tree of any size can ask for
    mutable QPixmap mMarkGlyphs[2 * scmRowMarkCount];
    mutable qreal mDotGlyphRatio = 0.0;
    // ...and the finished decorations built from them, so that a row is composed
    // once rather than once per paint and once per measure. Keyed on everything
    // the leading edge is composed from and nothing else - the dot's reading,
    // the chevron's, the mark, whether the row is chosen, how far in it is held
    // - so a tree of any size fills this with a handful of entries.
    mutable QHash<int, Decoration> mDecorations;
};

} // namespace uiDesign

#endif // MUDLET_EDITORTREEDELEGATE_H
