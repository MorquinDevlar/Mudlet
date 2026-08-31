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
// What a handle carrying content is at least as tall as
constexpr int scmHeaderThickness = 26;
// Room left around that content, so a larger font is not up against either pane
constexpr int scmHeaderPadding = 2;
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

void GripSplitterHandle::paintEvent(QPaintEvent*)
{
    const ThemeTokens tokens = themeTokens();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (mpContent) {
        // A bar of its own, rather than a strip of whichever pane it is nearer
        painter.fillRect(rect(), tokens.page);
        painter.setPen(tokens.border);
        if (orientation() == Qt::Vertical) {
            painter.drawLine(0, 0, width(), 0);
            painter.drawLine(0, height() - 1, width(), height() - 1);
        } else {
            painter.drawLine(0, 0, 0, height());
            painter.drawLine(width() - 1, 0, width() - 1, height());
        }
    }

    // Halfway from the hairline a border gets away with to the words beside it:
    // a grip is small and has to be findable without being a rule across the pane
    const QColor gripColor = mHovered ? tokens.accent : blend(tokens.border, tokens.mutedText, 0.5);
    QRectF gripRect;
    if (orientation() == Qt::Vertical) {
        gripRect = QRectF((width() - scmGripLength) / 2.0, (height() - scmGripThickness) / 2.0, scmGripLength, scmGripThickness);
    } else {
        gripRect = QRectF((width() - scmGripThickness) / 2.0, (height() - scmGripLength) / 2.0, scmGripThickness, scmGripLength);
    }
    painter.setPen(Qt::NoPen);
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
