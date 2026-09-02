#ifndef MUDLET_EDITORTREEROWMETRICS_H
#define MUDLET_EDITORTREEROWMETRICS_H

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

#include <QtCore/qnamespace.h>

#include <algorithm>

class QColor;
class QModelIndex;
class QPixmap;

// What a row of one of the editor's trees is measured and drawn to. Two
// delegates draw those rows - EditorTreeDelegate for the six trees a profile's
// own items live in, VariableTreeDelegate for the seventh - and a row of one has
// to be the same height, held in by the same room and led by marks of the same
// size as a row of the other, or the panel reads as two lists that happen to be
// stacked. So the numbers live here rather than once per delegate.
namespace uiDesign {

// The dot at the head of an item row, and the square at the head of a variable
// row, are drawn in a slot of this size
inline constexpr int scmTreeDotDiameter = 9;
// Between that mark and the one that follows it
inline constexpr int scmTreeDotGap = 6;
// A stroked shape is drawn centred on its path, so a hollow mark is pulled in by
// half a pen to end up the same size as a filled one
inline constexpr qreal scmTreeHollowPenWidth = 1.5;
// How far past its square a click still counts as one on the mark - a 9px target
// is not one to ask for accuracy on
inline constexpr int scmTreeMarkHitSlack = 2;
// The square the chevron is drawn in, centred in the indentation step it stands
// in - the same square the dot is drawn in, so the two read as one row of marks
inline constexpr int scmTreeChevronBox = 9;
// Half the height of the chevron's stroke inside that square
inline constexpr qreal scmTreeChevronArm = 2.6;
inline constexpr qreal scmTreeChevronPenWidth = 1.4;
// The square every mark is drawn in - a folder's, an error's, a tree's own
// heading, a variable's type - which is the size the panel's other list draws
// its glyphs at (scmGlyphSize in SearchResultDelegate.cpp)
inline constexpr int scmTreeMarkSize = 16;
// ...and the room every row leaves for the mark, which is the same room whether
// the row has one or not. That is what makes the hover fill on a tree's heading
// and the selection pill on an item under it the one height.
inline constexpr int scmTreeSlotHeight = std::max({scmTreeMarkSize, scmTreeDotDiameter, scmTreeChevronBox});
// What each level is held in by if the view had none to report, which it always
// does have - a style with no metric for it is what this stands in for
inline constexpr int scmTreeFallbackIndentStep = 20;
// A tree indented deeply enough would otherwise grow a decoration cache a depth
// at a time
inline constexpr int scmTreeDecorationCacheLimit = 256;
// A click carrying one of these is the selection being worked on rather than a
// row being switched on or off
inline constexpr Qt::KeyboardModifiers scmTreeSelectionModifiers = Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier;

// How deep in the tree a row is. The view counts the same thing to work out how
// far to indent a row, and keeps the answer to itself.
int treeRowLevelOf(const QModelIndex& index);

// The handle that folds a row away, pointing the way the row will open: along
// the list while it is folded, down into it once it is not. Drawn rather than
// taken from a file, since it is two strokes and both delegates want it in
// whatever ink the tree's chrome is currently in.
QPixmap treeRowChevronGlyph(const bool open, const QColor& ink, const qreal ratio);

} // namespace uiDesign

#endif // MUDLET_EDITORTREEROWMETRICS_H
