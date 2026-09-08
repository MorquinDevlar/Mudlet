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
#include "uiDesign.h"

#include <QResizeEvent>

// What the notice leaves round the line or two of words in it, and what stands
// between the picture, those words and the cross that dismisses them. The .ui
// file's 3px was the margin of a box drawn round 64px pictures.
static constexpr int scmNoticePaddingHorizontal = 10;
static constexpr int scmNoticePaddingVertical = 8;
static constexpr int scmNoticeSpacing = 8;


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

    if (QLayout* pNoticeLayout = frame_notificationArea->layout()) {
        pNoticeLayout->setContentsMargins(scmNoticePaddingHorizontal, scmNoticePaddingVertical, scmNoticePaddingHorizontal, scmNoticePaddingVertical);
        pNoticeLayout->setSpacing(scmNoticeSpacing);
    }
    // The .ui file puts the close button over a spacer tall enough for a 64px
    // picture; what the notice holds now is a line or two of text, and the
    // spacer only has to keep the button on the first of them
    verticalSpacer_closeButton->changeSize(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);

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

// The notice owns its own look. It used to carry two: this one and a second
// written onto the same frame by the editor's shell style, and a widget with
// two sheets shows whichever landed last - which on an appearance change was
// this one, so the design's hairline came back as the old 3px near-black band.
void dlgSystemMessageArea::slot_applyAppearance()
{
    const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
    // A notice rather than a strip of highlighter pen: the accent the rest of
    // the editor points with, and the picture beside the words is what says
    // which of the three readings this one is
    frame_notificationArea->setStyleSheet(qsl("QFrame#frame_notificationArea { background-color: %1; border: 1px solid %2; border-radius: %4px; }"
                                              "QFrame#frame_notificationArea QLabel { background: transparent; color: %3; }")
                                                  .arg(tokens.accentSoft, tokens.accent.name(), tokens.mutedText.name(), QString::number(uiDesign::scmRadiusPanel)));
    // The words of the notice are named on the label itself rather than left to
    // the descendant rule above, which does not reach them: the area is hidden
    // while the window round it is styled and is polished only when a notice
    // brings it out, and that polish writes the application's own ink into the
    // label's palette. A sheet the label carries survives it.
    notificationAreaMessageBox->setStyleSheet(qsl("color: %1;").arg(tokens.mutedText.name()));
}
