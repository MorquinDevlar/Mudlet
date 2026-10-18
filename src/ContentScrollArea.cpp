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

#include "ContentScrollArea.h"

#include <QEvent>

namespace uiDesign {

ContentScrollArea::ContentScrollArea(QWidget* pParent)
: QScrollArea(pParent)
{
}

QSize ContentScrollArea::sizeHint() const
{
    // What QScrollArea::sizeHint() works out, without the cache it keeps the
    // widget's answer in: the frame round the widget's own hint, capped at the
    // same multiple of the font Qt caps it at, so that a long list does not
    // ask the window to be as tall as every one of its rows
    const int frame = 2 * frameWidth();
    const int fontHeight = fontMetrics().height();
    QSize hint(frame, frame);
    if (const QWidget* pContent = widget()) {
        hint += widgetResizable() ? pContent->sizeHint() : pContent->size();
    } else {
        hint += QSize(12 * fontHeight, 8 * fontHeight);
    }
    return hint.boundedTo(QSize(36 * fontHeight, 24 * fontHeight));
}

QSize ContentScrollArea::minimumSizeHint() const
{
    // Only the height is this class's own: the width Qt answers with is made
    // of scroll bar metrics too, but it is a few dozen pixels under anything a
    // column of fields is ever laid out at, so it is left as it was
    return QSize(QScrollArea::minimumSizeHint().width(), 2 * frameWidth());
}

bool ContentScrollArea::eventFilter(QObject* pWatched, QEvent* pEvent)
{
    if (pWatched == widget() && pEvent->type() == QEvent::LayoutRequest) {
        updateGeometry();
    }
    return QScrollArea::eventFilter(pWatched, pEvent);
}

} // namespace uiDesign
