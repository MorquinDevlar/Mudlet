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

#ifndef MUDLET_CONTENTSCROLLAREA_H
#define MUDLET_CONTENTSCROLLAREA_H

#include <QScrollArea>

namespace uiDesign {

// A scroll area sized by what it holds rather than by its own scroll bars.
//
// QScrollArea answers sizeHint() with its widget's hint as it stood the first
// time anyone asked, and minimumSizeHint() with room for both scroll bars
// whether or not either is showing. Under a form held to its contents both are
// wrong: the first sizes the area for the rows some earlier item had, and the
// second holds it a scroll bar's row taller than the rows it shows, which is
// empty space under the last of them. This one asks its widget each time, and
// its minimum height is its frame alone - an explicit minimum set on it still
// holds, since a layout reads that first.
class ContentScrollArea : public QScrollArea
{
    Q_OBJECT

public:
    Q_DISABLE_COPY(ContentScrollArea)
    explicit ContentScrollArea(QWidget* pParent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    // A hint that follows the widget's has to say when it has moved: the widget
    // is laid out afresh by a request posted to itself, which is heard here
    // through the filter QScrollArea already keeps on it, and passed up as this
    // area's own geometry having changed - or the layout holding this area
    // would go on using the answer it cached
    bool eventFilter(QObject* pWatched, QEvent* pEvent) override;
};

} // namespace uiDesign

#endif // MUDLET_CONTENTSCROLLAREA_H
