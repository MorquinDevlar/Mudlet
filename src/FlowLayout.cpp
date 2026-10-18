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

#include <QWidget>

#include <algorithm>

namespace uiDesign {

FlowLayout::FlowLayout(QWidget* pParent, const int horizontalSpacing, const int verticalSpacing)
: QLayout(pParent)
, mHorizontalSpacing(horizontalSpacing)
, mVerticalSpacing(verticalSpacing)
{
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

void FlowLayout::setWidthPolicy(const Width policy)
{
    if (mWidthPolicy == policy) {
        return;
    }
    mWidthPolicy = policy;
    invalidate();
}

int FlowLayout::count() const
{
    return mItems.count();
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return mItems.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    if (index < 0 || index >= mItems.count()) {
        return nullptr;
    }
    QLayoutItem* pItem = mItems.takeAt(index);
    invalidate();
    return pItem;
}

// Neither way: a row of chips is as wide as its chips and as tall as the lines
// they came to, and room given to it beyond that is room nothing fills
Qt::Orientations FlowLayout::expandingDirections() const
{
    return {};
}

bool FlowLayout::hasHeightForWidth() const
{
    return true;
}

int FlowLayout::heightForWidth(int width) const
{
    if (width == mCachedWidth) {
        return mCachedHeight;
    }
    // Where the rectangle sits has nothing to do with how tall the lines come
    // to, so a walk that places nothing can start anywhere
    mCachedWidth = width;
    mCachedHeight = runItems(QRect(0, 0, width, 0), false);
    return mCachedHeight;
}

// What the items come to on one line, which is the width the row would rather
// have. Whatever holds it is free to give it less, and the wrap answers for the
// height that costs.
QSize FlowLayout::sizeHint() const
{
    if (mWidthPolicy == Width::WidestItem) {
        return minimumSize();
    }

    const QMargins margins = contentsMargins();
    int width = 0;
    int height = 0;
    bool first = true;
    for (const QLayoutItem* pItem : mItems) {
        if (pItem->isEmpty()) {
            continue;
        }
        const QSize hint = pItem->sizeHint();
        width += hint.width() + (first ? 0 : horizontalSpacing());
        height = std::max(height, hint.height());
        first = false;
    }
    return QSize(width + margins.left() + margins.right(), height + margins.top() + margins.bottom());
}

// One item wide: anything narrower cannot show a chip at all, and anything
// wider would stop the column the row is in from being dragged smaller
QSize FlowLayout::minimumSize() const
{
    const QMargins margins = contentsMargins();
    QSize smallest;
    for (const QLayoutItem* pItem : mItems) {
        if (pItem->isEmpty()) {
            continue;
        }
        // Under WidestItem the hint rather than the minimum, because the hint
        // is what runItems() actually gives each item: a minimum read off the
        // smaller of the two would promise a width the row is then drawn wider
        // than, and this policy's whole job is to be that promise.
        smallest = smallest.expandedTo(mWidthPolicy == Width::WidestItem ? pItem->sizeHint() : pItem->minimumSize());
    }
    return QSize(smallest.width() + margins.left() + margins.right(), smallest.height() + margins.top() + margins.bottom());
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    runItems(rect, true);
}

int FlowLayout::horizontalSpacing() const
{
    return mHorizontalSpacing >= 0 ? mHorizontalSpacing : styleSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const
{
    return mVerticalSpacing >= 0 ? mVerticalSpacing : styleSpacing(QStyle::PM_LayoutVerticalSpacing);
}

int FlowLayout::runItems(const QRect& rect, const bool place) const
{
    const QMargins margins = contentsMargins();
    const QRect content = rect.marginsRemoved(margins);
    const int gapAcross = horizontalSpacing();
    const int gapDown = verticalSpacing();
    int x = content.x();
    int y = content.y();
    int lineHeight = 0;

    for (QLayoutItem* pItem : mItems) {
        if (pItem->isEmpty()) {
            continue;
        }
        const QSize hint = pItem->sizeHint();
        // A line with nothing on it yet takes the item however wide it is: a
        // chip wider than the row is drawn over the edge rather than dropped
        if (lineHeight > 0 && x + hint.width() > content.right() + 1) {
            x = content.x();
            y += lineHeight + gapDown;
            lineHeight = 0;
        }
        if (place) {
            pItem->setGeometry(QRect(QPoint(x, y), hint));
        }
        x += hint.width() + gapAcross;
        lineHeight = std::max(lineHeight, hint.height());
    }

    return y + lineHeight - rect.y() + margins.bottom();
}

int FlowLayout::styleSpacing(const QStyle::PixelMetric metric) const
{
    const QWidget* pParent = parentWidget();
    if (!pParent) {
        return 0;
    }
    // QCommonStyle answers -1 for "whatever the layout says", and this layout's
    // answer is the thing being asked for
    return std::max(0, pParent->style()->pixelMetric(metric, nullptr, pParent));
}

} // namespace uiDesign
