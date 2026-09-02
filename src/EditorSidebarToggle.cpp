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

#include "EditorSidebarToggle.h"

#include "uiDesign.h"

#include <QEvent>
#include <QPainter>
#include <QPolygonF>

namespace uiDesign {

namespace {
// A tall pill rather than a circle: it has to read as a handle on the line it
// sits on, and a circle that size reads as a bullet dropped onto it
constexpr int scmToggleWidth = 20;
constexpr int scmToggleHeight = 28;
// The chevron inside it, drawn at the weight the trees' own chevrons are
constexpr qreal scmChevronArm = 3.5;
constexpr qreal scmChevronPenWidth = 1.5;
// The wash the rest of the window tints a hovered control with. Mixed into the
// pill's own fill rather than laid over it, since this is painted rather than
// styled and there is nothing behind it to show through.
constexpr qreal scmHoverWeight = 0.07;
} // namespace

EditorSidebarToggle::EditorSidebarToggle(QWidget* pSeam, QWidget* pParent)
: QAbstractButton(pParent)
, mpSeam(pSeam)
{
    setFixedSize(scmToggleWidth, scmToggleHeight);
    setCursor(Qt::PointingHandCursor);

    if (mpSeam) {
        mpSeam->installEventFilter(this);
    }
    if (pParent) {
        pParent->installEventFilter(this);
    }
    reposition();
}

void EditorSidebarToggle::setPointingLeft(const bool pointingLeft)
{
    if (mPointingLeft == pointingLeft) {
        return;
    }
    mPointingLeft = pointingLeft;
    update();
}

void EditorSidebarToggle::reposition()
{
    QWidget* pParent = parentWidget();
    if (!mpSeam || !pParent) {
        return;
    }
    // Measured through the parent rather than read off the pane's geometry, so
    // that a pane nested a layout or two deep is still found where it is drawn
    const QPoint paneTopLeft = mpSeam->mapTo(pParent, QPoint(0, 0));
    // The seam is the line the pane's trailing edge is drawn on, so the pill is
    // centred on the boundary between the two widgets rather than inside either
    move(paneTopLeft.x() + mpSeam->width() - width() / 2, paneTopLeft.y() + (mpSeam->height() - height()) / 2);
    // Over both of its neighbours: it belongs to neither, and either would
    // otherwise clip the half of it that overlaps the other
    raise();
}

bool EditorSidebarToggle::eventFilter(QObject* pWatched, QEvent* pEvent)
{
    switch (pEvent->type()) {
    case QEvent::Resize:
    case QEvent::Move:
    case QEvent::Show:
        reposition();
        break;
    default:
        break;
    }
    return QAbstractButton::eventFilter(pWatched, pEvent);
}

void EditorSidebarToggle::enterEvent(TEnterEvent* event)
{
    QAbstractButton::enterEvent(event);
    update();
}

void EditorSidebarToggle::leaveEvent(QEvent* event)
{
    QAbstractButton::leaveEvent(event);
    update();
}

void EditorSidebarToggle::paintEvent(QPaintEvent*)
{
    const ThemeTokens tokens = themeTokens();
    const bool lit = isEnabled() && (underMouse() || isDown());

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Half a pixel in, so the hairline is drawn inside the pill rather than
    // half of it falling outside the widget
    const QRectF pill = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = pill.width() / 2.0;
    const QColor frame = hasFocus() ? tokens.accent : tokens.border;
    painter.setPen(QPen(frame, 1.0));
    painter.setBrush(lit ? blend(tokens.card, tokens.text, scmHoverWeight) : tokens.card);
    painter.drawRoundedRect(pill, radius, radius);

    QColor ink = tokens.mutedText;
    if (!isEnabled()) {
        ink = tokens.disabledText;
    } else if (lit) {
        ink = tokens.accentText;
    }
    QPen pen(ink);
    pen.setWidthF(scmChevronPenWidth);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const QPointF centre = pill.center();
    // The arm across the point is shortened, the way the trees' chevrons are,
    // so the mark stays inside its square whichever way round it is drawn
    const qreal reach = scmChevronArm / 1.6;
    QPolygonF stroke;
    if (mPointingLeft) {
        stroke << QPointF(centre.x() + reach, centre.y() - scmChevronArm) << QPointF(centre.x() - reach, centre.y()) << QPointF(centre.x() + reach, centre.y() + scmChevronArm);
    } else {
        stroke << QPointF(centre.x() - reach, centre.y() - scmChevronArm) << QPointF(centre.x() + reach, centre.y()) << QPointF(centre.x() - reach, centre.y() + scmChevronArm);
    }
    painter.drawPolyline(stroke);
}

} // namespace uiDesign
