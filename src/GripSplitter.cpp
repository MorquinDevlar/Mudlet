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

#include "GripSplitter.h"

#include "uiDesign.h"

#include <QPainter>

#include <algorithm>

namespace uiDesign {

namespace {
constexpr int scmGripLength = 36;
constexpr int scmGripThickness = 3;
// What a handle carrying content is at least as tall as. Room enough that the
// chip on the code pane's heading is a chip on a bar rather than the bar
// itself: what is left above and below it is what says the two are different
// things.
constexpr int scmHeaderThickness = 32;
// Room left around that content, so a larger font is not up against either pane
constexpr int scmHeaderPadding = 2;
// The seam a plain handle draws down its middle, and what that line widens to
// while the pointer is on it - the way a split view says which of its edges is
// about to be dragged
constexpr int scmSeamThickness = 1;
constexpr int scmSeamHoveredThickness = 3;

// What a pane was painted with. A stylesheet is what fills a window's columns
// and a palette knows nothing about one, so the pane is asked rather than
// guessed at - and one that never said is taken for a piece of the page.
QColor paneTone(const QWidget* pPane, const QColor& fallback)
{
    if (!pPane) {
        return fallback;
    }
    const QColor tone = qvariant_cast<QColor>(pPane->property(scmProp_paneTone));
    return tone.isValid() ? tone : fallback;
}
} // namespace

GripSplitterHandle::GripSplitterHandle(const Qt::Orientation orientation, QSplitter* pParent)
: QSplitterHandle(orientation, pParent)
{
}

void GripSplitterHandle::setContent(QWidget* pContent)
{
    if (mpContent == pContent) {
        return;
    }
    if (mpContent) {
        mpContent->hide();
        mpContent->setParent(nullptr);
    }
    mpContent = pContent;
    if (!mpContent) {
        updateGeometry();
        update();
        return;
    }

    mpContent->setParent(this);
    // Every piece of it, not just the strip: the attribute speaks for one widget
    // alone, and a label left out of it would swallow the press that starts a drag
    mpContent->setAttribute(Qt::WA_TransparentForMouseEvents);
    for (QWidget* pChild : mpContent->findChildren<QWidget*>()) {
        pChild->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    mpContent->setGeometry(rect());
    mpContent->show();
    updateGeometry();
    update();
}

QSize GripSplitterHandle::sizeHint() const
{
    QSize hint = QSplitterHandle::sizeHint();
    if (!mpContent) {
        return hint;
    }

    // The splitter asks the handle how thick it is, so one handle can be thicker
    // than the rest without the others hearing about it
    const QSize contentHint = mpContent->sizeHint();
    if (orientation() == Qt::Vertical) {
        hint.setHeight(std::max(scmHeaderThickness, contentHint.height() + 2 * scmHeaderPadding));
    } else {
        hint.setWidth(std::max(scmHeaderThickness, contentHint.width() + 2 * scmHeaderPadding));
    }
    return hint;
}

void GripSplitterHandle::resizeEvent(QResizeEvent* event)
{
    QSplitterHandle::resizeEvent(event);
    if (mpContent) {
        mpContent->setGeometry(rect());
    }
}

void GripSplitterHandle::enterEvent(TEnterEvent* event)
{
    QSplitterHandle::enterEvent(event);
    mHovered = true;
    update();
}

void GripSplitterHandle::leaveEvent(QEvent* event)
{
    QSplitterHandle::leaveEvent(event);
    mHovered = false;
    update();
}

void GripSplitterHandle::paintSeam(QPainter& painter, const ThemeTokens& tokens) const
{
    // A handle carrying nothing is a line rather than a band: the two panes are
    // drawn up to a one pixel seam and the rest of the width the mouse needs is
    // each pane's own tone carried on to it, so that what has to be nine pixels
    // wide to be draggable reads as no gap at all. Hovered, the seam widens to
    // three and takes the accent, which is the whole of what says it can be
    // dragged - a grip pill on a line this thin would be the only thing on it.
    QSplitter* pSplitter = splitter();
    const int index = pSplitter ? pSplitter->indexOf(const_cast<GripSplitterHandle*>(this)) : -1;
    const QColor before = paneTone(index > 0 ? pSplitter->widget(index - 1) : nullptr, tokens.page);
    const QColor after = paneTone(index >= 0 ? pSplitter->widget(index) : nullptr, tokens.page);

    const QColor seamColor = mHovered ? tokens.accent : tokens.separator;
    const int thickness = mHovered ? scmSeamHoveredThickness : scmSeamThickness;
    // The seam is centred on the handle, so an odd width grows either side of
    // the middle pixel the resting line is drawn on
    if (orientation() == Qt::Vertical) {
        const int middle = (height() - scmSeamThickness) / 2;
        painter.fillRect(QRect(0, 0, width(), middle), before);
        painter.fillRect(QRect(0, middle + scmSeamThickness, width(), height() - middle - scmSeamThickness), after);
        painter.fillRect(QRect(0, middle - (thickness - scmSeamThickness) / 2, width(), thickness), seamColor);
        return;
    }
    const int middle = (width() - scmSeamThickness) / 2;
    painter.fillRect(QRect(0, 0, middle, height()), before);
    painter.fillRect(QRect(middle + scmSeamThickness, 0, width() - middle - scmSeamThickness, height()), after);
    painter.fillRect(QRect(middle - (thickness - scmSeamThickness) / 2, 0, thickness, height()), seamColor);
}

void GripSplitterHandle::paintEvent(QPaintEvent*)
{
    const ThemeTokens tokens = themeTokens();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (!mpContent) {
        paintSeam(painter, tokens);
        return;
    }

    // A handle given a heading to carry is the top of the pane under it, so it
    // is drawn with that pane's own corner: the page shows through outside the
    // arc and the strip fills everything inside it. Only the top is cut - what
    // is under the pane is the next pane down or the window's own edge, neither
    // of which the heading has anything to say about - so the rounded rectangle
    // is drawn a corner's worth taller than the handle and its bottom two
    // corners fall outside what is painted.
    painter.fillRect(rect(), tokens.page);
    painter.setPen(Qt::NoPen);
    painter.setBrush(tokens.separator);
    QRectF strip(rect());
    if (orientation() == Qt::Vertical) {
        strip.setHeight(strip.height() + scmRadiusPanel);
    } else {
        strip.setWidth(strip.width() + scmRadiusPanel);
    }
    painter.drawRoundedRect(strip, scmRadiusPanel, scmRadiusPanel);

    // Halfway from the hairline a border gets away with to the words beside it:
    // a grip is small and has to be findable without being a rule across the pane
    const QColor gripColor = mHovered ? tokens.accent : blend(tokens.border, tokens.mutedText, 0.5);
    QRectF gripRect;
    if (orientation() == Qt::Vertical) {
        gripRect = QRectF((width() - scmGripLength) / 2.0, (height() - scmGripThickness) / 2.0, scmGripLength, scmGripThickness);
    } else {
        gripRect = QRectF((width() - scmGripThickness) / 2.0, (height() - scmGripLength) / 2.0, scmGripThickness, scmGripLength);
    }
    painter.setBrush(gripColor);
    painter.drawRoundedRect(gripRect, scmGripThickness / 2.0, scmGripThickness / 2.0);
}

GripSplitter::GripSplitter(QWidget* pParent)
: QSplitter(pParent)
{
    setHandleWidth(scmHandleThickness);
}

GripSplitter::GripSplitter(const Qt::Orientation orientation, QWidget* pParent)
: QSplitter(orientation, pParent)
{
    setHandleWidth(scmHandleThickness);
}

QSplitterHandle* GripSplitter::createHandle()
{
    return new GripSplitterHandle(orientation(), this);
}

void GripSplitter::setHeaderHandle(const int index, QWidget* pContent)
{
    auto* pHandle = qobject_cast<GripSplitterHandle*>(handle(index));
    if (!pHandle) {
        return;
    }
    pHandle->setContent(pContent);
    // The splitter took the handle's thickness when it last laid itself out, and
    // that answer has just changed
    refresh();
}

} // namespace uiDesign
