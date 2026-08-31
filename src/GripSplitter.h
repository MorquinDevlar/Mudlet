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

namespace uiDesign {

// The gap between two panes, drawn so that it says it can be dragged: a short
// rounded bar across the middle of it, picked out in the accent colour while the
// pointer is on it. Every colour is mixed at paint time from the application
// palette, so a theme change needs no more than the repaint it brings with it.
class GripSplitterHandle : public QSplitterHandle
{
    Q_OBJECT

public:
    Q_DISABLE_COPY(GripSplitterHandle)
    GripSplitterHandle(const Qt::Orientation orientation, QSplitter* pParent);

    // A strip of content to carry beside the grip; nullptr takes it away again.
    // The handle stays a handle: the content is told not to take the mouse, so a
    // drag anywhere on it still resizes.
    void setContent(QWidget* pContent);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void enterEvent(TEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QPointer<QWidget> mpContent;
    bool mHovered = false;
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

protected:
    QSplitterHandle* createHandle() override;
};

} // namespace uiDesign

#endif // MUDLET_GRIPSPLITTER_H
