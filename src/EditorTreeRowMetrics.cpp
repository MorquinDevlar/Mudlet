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

#include "EditorTreeRowMetrics.h"

#include <QColor>
#include <QModelIndex>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>

namespace uiDesign {

int treeRowLevelOf(const QModelIndex& index)
{
    int level = 0;
    for (QModelIndex ancestor = index.parent(); ancestor.isValid(); ancestor = ancestor.parent()) {
        ++level;
    }
    return level;
}

QPixmap treeRowChevronGlyph(const bool open, const QColor& ink, const qreal ratio)
{
    QPixmap glyph(qRound(scmTreeChevronBox * ratio), qRound(scmTreeChevronBox * ratio));
    glyph.setDevicePixelRatio(ratio);
    glyph.fill(Qt::transparent);

    QPainter painter(&glyph);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink);
    pen.setWidthF(scmTreeChevronPenWidth);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const qreal centre = scmTreeChevronBox / 2.0;
    // The arm across the point is shortened, so that the mark stays inside its
    // square whichever way round it is drawn
    const qreal reach = scmTreeChevronArm / 1.6;
    QPolygonF stroke;
    if (open) {
        stroke << QPointF(centre - scmTreeChevronArm, centre - reach) << QPointF(centre, centre + reach) << QPointF(centre + scmTreeChevronArm, centre - reach);
    } else {
        stroke << QPointF(centre - reach, centre - scmTreeChevronArm) << QPointF(centre + reach, centre) << QPointF(centre - reach, centre + scmTreeChevronArm);
    }
    painter.drawPolyline(stroke);
    // Before the pixmap is handed out: a copy taken while a painter is still
    // active on the original is a copy of something being written to
    painter.end();
    return glyph;
}

} // namespace uiDesign
