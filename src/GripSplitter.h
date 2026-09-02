#ifndef MUDLET_GRIPSPLITTER_H
#define MUDLET_GRIPSPLITTER_H

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

#include "utils.h"

#include <QPointer>
#include <QSplitter>
#include <QSplitterHandle>

class QMouseEvent;
class QPainter;

namespace uiDesign {

struct ThemeTokens;

// The seam between two panes. A handle carrying nothing is drawn as one: a
// hairline down the middle with each pane's own tone carried up to it, which
// widens to three pixels of the accent while the pointer is on it. A handle
// given a heading to carry is drawn as a strip deep enough to read the heading
// in, and says the same thing the same way - a band of the accent along the
// edge the drag moves, which is the top of the strip for a vertical splitter.
// Every colour is mixed at paint time from the application palette, so a theme
// change needs no more than the repaint it brings with it.
class GripSplitterHandle : public QSplitterHandle
{
    Q_OBJECT

public:
    Q_DISABLE_COPY(GripSplitterHandle)
    GripSplitterHandle(const Qt::Orientation orientation, QSplitter* pParent);

    // A strip of content to carry; nullptr takes it away again. The handle
    // stays a handle: the content is told not to take the mouse, so a drag
    // anywhere on it still resizes.
    void setContent(QWidget* pContent);

    // Whether the panes either side of this handle can be resized through it.
    // A heading over a pane whose height is its contents' is a heading and
    // nothing else: it loses the grip that says otherwise, the cursor that
    // offers the drag, and the drag itself.
    void setResizes(const bool resizes);
    [[nodiscard]] bool resizes() const { return mResizes; }

    // One piece of that content to hear clicks for; nullptr takes it away
    // again. The content takes no mouse events of its own, so a click on the
    // piece is the handle's to notice: pressed and released inside it without
    // the pointer having gone a drag's worth away in between is emitted as
    // clicked(), and the pointer over it is a link's rather than the edge's.
    void setClickable(QWidget* pPiece);

    [[nodiscard]] QSize sizeHint() const override;

signals:
    void clicked(QWidget* pPiece);

protected:
    bool event(QEvent* event) override;
    // A cursor is only worked out as the pointer moves, and the piece that
    // hears clicks can be shown or taken away while the pointer stands still on
    // it - so the handle also listens to the piece coming and going
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    // What a handle carrying nothing is drawn as: the seam, and each pane's own
    // tone carried up to it
    void paintSeam(QPainter& painter, const ThemeTokens& tokens) const;
    void resizeEvent(QResizeEvent* event) override;
    void enterEvent(TEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // Whether a point in the handle's own coordinates is on the piece that
    // hears clicks - false while there is none, or while it is hidden
    [[nodiscard]] bool overClickable(const QPoint& point) const;
    // What the pointer is at a point in the handle's own coordinates: a link's
    // over the piece that hears clicks, and the edge's everywhere else
    void placeCursor(const QPoint& point);

    QPointer<QWidget> mpContent;
    QPointer<QWidget> mpClickable;
    // Where the press being held was made, on the screen rather than on the
    // handle: the handle follows the pointer as the panes are dragged, so
    // measured against itself a drag goes nowhere at all
    QPoint mPressedAt;
    // Whether the press being held ever travelled a drag's worth. The panes
    // follow the pointer, so a drag that wandered off and came back releases
    // where it started - which the release point on its own reads as a click.
    bool mDragged = false;
    bool mHovered = false;
    bool mResizes = true;
};

// A splitter whose handles are the ones above, and which can hand one of them a
// widget to carry - turning the gap between two panes into the heading of the
// pane below it.
class GripSplitter : public QSplitter
{
    Q_OBJECT

public:
    Q_DISABLE_COPY(GripSplitter)
    // Wide enough to be aimed at with a mouse, narrow enough not to read as a
    // pane. Public because QSplitter::restoreState() puts back the handle width
    // that was serialized with the sizes - a state saved before the grips
    // existed carries the old one - so whoever restores has to say it again.
    static constexpr int scmHandleThickness = 9;

    explicit GripSplitter(QWidget* pParent = nullptr);
    GripSplitter(const Qt::Orientation orientation, QWidget* pParent = nullptr);

    // The handle above the widget at `index`. A handle only exists once the
    // widget under it does, so call this after the panes have been added.
    void setHeaderHandle(const int index, QWidget* pContent);

    // Whether the handle above the widget at `index` resizes anything; see
    // GripSplitterHandle::setResizes(). Answers false where there is no such
    // handle to tell.
    bool setHandleResizes(const int index, const bool resizes);
    [[nodiscard]] bool handleResizes(const int index) const;

protected:
    QSplitterHandle* createHandle() override;
};

} // namespace uiDesign

#endif // MUDLET_GRIPSPLITTER_H
