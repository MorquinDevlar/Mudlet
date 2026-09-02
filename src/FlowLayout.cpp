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

#include "FlowLayout.h"

#include <QMargins>
#include <QWidget>

namespace uiDesign {

FlowLayout::FlowLayout(QWidget* pParent, const int horizontalSpacing, const int verticalSpacing)
: QLayout(pParent)
, mHorizontalSpacing(horizontalSpacing)
, mVerticalSpacing(verticalSpacing)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem* pItem = takeAt(0)) {
        delete pItem;
    }
}

void FlowLayout::addItem(QLayoutItem* pItem)
{
    mItems.append(pItem);
    invalidate();
}

void FlowLayout::invalidate()
{
    mCachedWidth = -1;
    mCachedHeight = 0;
    QLayout::invalidate();
}

int FlowLayout::count() const
{
    return static_cast<int>(mItems.size());
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return mItems.value(index, nullptr);
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    if (index < 0 || index >= mItems.size()) {
        return nullptr;
    }
    QLayoutItem* pItem = mItems.takeAt(index);
    invalidate();
    return pItem;
}

int FlowLayout::heightForWidth(int width) const
{
    if (width == mCachedWidth) {
        return mCachedHeight;
    }
    // The rectangle's own position is nothing to do with how tall the rows come
    // to, so a walk that places nothing can start anywhere
    mCachedWidth = width;
    mCachedHeight = placeItems(QRect(0, 0, width, 0), false);
    return mCachedHeight;
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    placeItems(rect, true);
}

QSize FlowLayout::minimumSize() const
{
    // The widest single item, not the sum of them: anything that will not fit
    // on the line it is on starts the next one, so one item's width is the
    // narrowest this can be drawn at without clipping. Measured off the size
    // hint, which is what placeItems() gives each item - a minimum read off
    // minimumSize() would be the smaller of the two and promise a width the row
    // is then drawn wider than.
    QSize widest;
    for (const QLayoutItem* pItem : mItems) {
        widest = widest.expandedTo(pItem->sizeHint());
    }
    const QMargins margins = contentsMargins();
    return widest + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

int FlowLayout::placeItems(const QRect& rect, const bool place) const
{
    const QMargins margins = contentsMargins();
    const QRect inside = rect.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
    int x = inside.x();
    int y = inside.y();
    int lineHeight = 0;

    for (QLayoutItem* pItem : mItems) {
        const QSize wanted = pItem->sizeHint();
        const int nextX = x + wanted.width() + mHorizontalSpacing;
        // Wrap when this item would run past the right edge - unless it is the
        // first on its line, where there is nowhere further back to put it and
        // wrapping would leave an empty row above it
        if (lineHeight > 0 && nextX - mHorizontalSpacing > inside.right() + 1) {
            x = inside.x();
            y += lineHeight + mVerticalSpacing;
            lineHeight = 0;
        }
        if (place) {
            pItem->setGeometry(QRect(QPoint(x, y), wanted));
        }
        x += wanted.width() + mHorizontalSpacing;
        lineHeight = qMax(lineHeight, wanted.height());
    }
    return y + lineHeight - rect.y() + margins.bottom();
}

} // namespace uiDesign
