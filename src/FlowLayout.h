#ifndef MUDLET_FLOWLAYOUT_H
#define MUDLET_FLOWLAYOUT_H

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

#include <QLayout>
#include <QList>
#include <QRect>
#include <QSize>

namespace uiDesign {

// A row of small things that wraps rather than growing sideways: contact chips
// beside a maker's name, and anything else where the count is the data's
// business rather than the layout's.
//
// A QHBoxLayout cannot do this. Its minimum width is the sum of everything in
// it, so three chips make a card 450px wide whatever the column it is in can
// spare, and the card beside it is pushed off the page. Here the minimum is the
// widest single item, because anything wider than the line simply starts a new
// one - and it is that item's *size hint* rather than its minimum, because the
// hint is what the placement below actually gives it, and a minimum measured
// off the smaller of the two would promise a width the row is then drawn wider
// than.
//
// Height therefore depends on width, which is what hasHeightForWidth() tells
// the layout above; the placement pass answers that question and does the
// placing, and runs in either mode from the same code so the two cannot come to
// disagree. That question is asked several times per layout pass, so its last
// answer is kept until something invalidates it.
class FlowLayout : public QLayout
{
    Q_OBJECT

public:
    Q_DISABLE_COPY(FlowLayout)
    FlowLayout(QWidget* pParent, const int horizontalSpacing, const int verticalSpacing);
    ~FlowLayout() override;

    void addItem(QLayoutItem* pItem) override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;

    // Nothing here wants more room than it asked for: a chip is as wide as the
    // word in it, and a line that runs out simply wraps
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    void invalidate() override;

    void setGeometry(const QRect& rect) override;
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override;

private:
    // One pass, in both modes: laying the items out is the only way to know how
    // tall they come to, so answering that question and doing the work are the
    // same walk. Answers the height the items take at that width.
    int placeItems(const QRect& rect, const bool place) const;

    QList<QLayoutItem*> mItems;
    int mHorizontalSpacing = 0;
    int mVerticalSpacing = 0;
    // The last width heightForWidth() was asked about and what it answered. A
    // layout pass asks two or three times running with the same number.
    mutable int mCachedWidth = -1;
    mutable int mCachedHeight = 0;
};

} // namespace uiDesign

#endif // MUDLET_FLOWLAYOUT_H
