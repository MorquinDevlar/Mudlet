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
#include <QStyle>

namespace uiDesign {

// A row of small things that runs on to the next line when it reaches the edge,
// the way words do. Every box layout would rather squeeze its items or push the
// window wider; this one keeps each item at the size it asks for and wraps
// instead, which is what a set of chips has to do to be readable at any width.
//
// The height therefore depends on the width, so this answers heightForWidth()
// and whatever holds it has to ask - see ChipRow::sizeHint(), which is what
// carries the wrapped height up to the column the form is in.
class FlowLayout : public QLayout
{
public:
    // What the row asks for when it is asked how wide it would like to be. A
    // row that is given its own width wants its items on one line; a row inside
    // a column must not widen that column by the number of items it happens to
    // hold, so it asks only for the widest one and wraps for the rest.
    enum class Width {
        OneLine,
        WidestItem,
    };

    Q_DISABLE_COPY(FlowLayout)
    // A spacing of -1 takes the style's own idea of what goes between two
    // controls, which is what a layout the caller says nothing about should be
    explicit FlowLayout(QWidget* pParent = nullptr, const int horizontalSpacing = -1, const int verticalSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem* pItem) override;
    [[nodiscard]] int count() const override;
    [[nodiscard]] QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;
    [[nodiscard]] Qt::Orientations expandingDirections() const override;
    [[nodiscard]] bool hasHeightForWidth() const override;
    [[nodiscard]] int heightForWidth(int width) const override;
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
    void invalidate() override;

    void setWidthPolicy(const Width policy);

    [[nodiscard]] int horizontalSpacing() const;
    [[nodiscard]] int verticalSpacing() const;

private:
    // The one pass everything else is answered from: laying the items out and
    // measuring what that comes to are the same walk, so a height can never
    // disagree with the geometry the items are actually given.
    int runItems(const QRect& rect, const bool place) const;
    [[nodiscard]] int styleSpacing(const QStyle::PixelMetric metric) const;

    QList<QLayoutItem*> mItems;
    int mHorizontalSpacing = -1;
    int mVerticalSpacing = -1;
    Width mWidthPolicy = Width::OneLine;
    // The last width heightForWidth() was asked about and what it answered: a
    // single layout pass asks two or three times running with the same number.
    mutable int mCachedWidth = -1;
    mutable int mCachedHeight = 0;
};

} // namespace uiDesign

#endif // MUDLET_FLOWLAYOUT_H
