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
// here. An item whose picture says something a dot cannot - a folder's colour, a
// filter chain, an offset timer, an error, an unsaved addition - keeps that
// picture beside the dot; everything else loses it.
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
    // it stands for has never been saved - which for a timer, a button or a key
    // is the only record that it is new, as none of the three has a checkIfNew()
    void setNewItemDescription(const QString& description) { mNewItemDescription = description; }

    // Where a row's dot can be clicked, in viewport coordinates; a null
    // rectangle for a row that has no dot. The dot's own square grown by a
    // couple of pixels - a 9px target is not one to ask for accuracy on - and no
    // more than that, so the rest of the row's leading edge is still somewhere a
    // drag can be started from.
    [[nodiscard]] QRect dotHitRect(const QModelIndex& index) const;

    void initStyleOption(QStyleOptionViewItem* pOption, const QModelIndex& index) const override;

private:
    // Three readings of one shape, in the order they are cached in
    enum class DotState { Off = 0, WantedOn = 1, Running = 2 };

    // What a row is drawn from, resolved from the item its id names rather than
    // from the picture the row happens to be carrying
    struct ItemState
    {
        bool known = false;
        DotState dot = DotState::Off;
        // Whether the row's own picture is worth keeping beside the dot
        bool keepIcon = false;
    };

    // What a row's decoration ends up being: the dot on its own, or the dot with
    // the picture the row kept beside it
    struct Decoration
    {
        QIcon icon;
        QSize size;
    };
    // Keyed on the dot's reading and on the identity of the *pixmap* the row's
    // picture was drawn from rather than of the QIcon holding it: every row
    // built from one resource file has a QIcon of its own, and those all have
    // different cache keys, while the pixmaps they hand out are the one cached
    // pixmap that file loaded as. A tree of folders is then one composed
    // picture rather than one per row.
    using DecorationKey = QPair<int, qint64>;

    void clearDecorations() const;
    [[nodiscard]] qreal glyphRatio() const;
    [[nodiscard]] ItemState stateOf(const QModelIndex& index) const;
    [[nodiscard]] QPixmap dotGlyph(const DotState state) const;
    [[nodiscard]] Decoration decorationFor(const ItemState& state, const QPixmap& keptGlyph, const QSize& keptSize) const;

    TTreeWidget* mpTree = nullptr;
    TreeType mTreeType = TreeType::None;
    QPointer<Host> mpHost;
    QColor mRunningDot;
    QColor mQuietDot;
    QString mNewItemDescription;

    // Three shapes are all a whole tree of rows needs, and a tree being laid out
    // asks every one of its rows for a size
    mutable QPixmap mDotGlyphs[3];
    mutable qreal mDotGlyphRatio = 0.0;
    // ...and the finished decorations built from them, so that a row carrying a
    // picture is composed once rather than once per paint and once per measure.
    // A row with no picture is a dot on its own, of which there are three - kept
    // apart from the rest so that a tree wide enough to overflow the composed
    // ones cannot throw the three away with them.
    mutable Decoration mDotDecorations[3];
    mutable QHash<DecorationKey, Decoration> mDecorations;
    // The size the view asks its pictures to be drawn at is a preference, and
    // one the composed decorations are measured against - so it is kept out of
    // their key by emptying them when it moves
    mutable QSize mDecorationBaseSize;
};

} // namespace uiDesign

#endif // MUDLET_EDITORTREEDELEGATE_H
