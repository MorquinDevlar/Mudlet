/***************************************************************************
 *   Copyright (C) 2008-2009 by Heiko Koehn - KoehnHeiko@googlemail.com    *
 *   Copyright (C) 2014 by Ahmed Charles - acharles@outlook.com            *
 *   Copyright (C) 2020 by Stephen Lyons - slysven@virginmedia.com         *
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


#include "dlgSystemMessageArea.h"

#include "mudlet.h"

#include <QResizeEvent>


dlgSystemMessageArea::dlgSystemMessageArea(QWidget* pParentWidget)
: QWidget(pParentWidget)
{
    // init generated dialog
    setupUi(this);

    QPixmap holdPixmap;
    holdPixmap = notificationAreaIconLabelWarning->pixmap(Qt::ReturnByValue);
    holdPixmap.setDevicePixelRatio(5.3);
    notificationAreaIconLabelWarning->setPixmap(holdPixmap);

    holdPixmap = notificationAreaIconLabelError->pixmap(Qt::ReturnByValue);
    holdPixmap.setDevicePixelRatio(5.3);
    notificationAreaIconLabelError->setPixmap(holdPixmap);

    holdPixmap = notificationAreaIconLabelInformation->pixmap(Qt::ReturnByValue);
    holdPixmap.setDevicePixelRatio(5.3);
    notificationAreaIconLabelInformation->setPixmap(holdPixmap);

    slot_applyAppearance();
    connect(mudlet::self(), &mudlet::signal_appearanceChanged, this, &dlgSystemMessageArea::slot_applyAppearance);
}

// A notice is as tall as its words are at the width it has been given, and no
// taller. Its wrapping label answers a plain size hint with the height that text
// would take in a box narrow enough to read comfortably - a guess at a shape,
// not at this shape - and a layout hands a widget spare room up to that guess.
// So both hints come from the layout's own height-for-width at the width the
// notice actually has, which is the one answer true of what is on screen.
QSize dlgSystemMessageArea::sizeHint() const
{
    return heightAtOwnWidth(QWidget::sizeHint());
}

QSize dlgSystemMessageArea::minimumSizeHint() const
{
    return heightAtOwnWidth(QWidget::minimumSizeHint());
}

QSize dlgSystemMessageArea::heightAtOwnWidth(const QSize& fallback) const
{
    QLayout* pLayout = layout();
    if (!pLayout || !pLayout->hasHeightForWidth() || width() <= 0) {
        return fallback;
    }
    const int height = pLayout->totalHeightForWidth(width());
    return height > 0 ? QSize(fallback.width(), height) : fallback;
}

// A different width is a different number of lines, so the height the two hints
// answer with is no longer the one whatever holds this was laid out against
void dlgSystemMessageArea::resizeEvent(QResizeEvent* pEvent)
{
    QWidget::resizeEvent(pEvent);
    if (pEvent->oldSize().width() != pEvent->size().width()) {
        updateGeometry();
    }
}

void dlgSystemMessageArea::slot_applyAppearance()
{
    const bool darkMode = mudlet::self()->inDarkMode();
    const QString background = darkMode ? qsl("rgb(64, 60, 40)") : qsl("rgb(255, 254, 215)");
    const QString textColor = darkMode ? qsl("rgb(230, 230, 230)") : qsl("black");
    frame_notificationArea->setStyleSheet(qsl("QFrame#frame_notificationArea {\n"
                                              "  border: 3px solid;\n"
                                              "  border-radius: 6px;\n"
                                              "  background-color: %1;\n"
                                              "}\n"
                                              "\n"
                                              "QLabel{\n"
                                              "color: %2;\n"
                                              "background-color: %1;\n"
                                              "}")
                                                  .arg(background, textColor));
}
