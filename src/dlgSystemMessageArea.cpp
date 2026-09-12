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


dlgSystemMessageArea::dlgSystemMessageArea(QWidget* pParentWidget)
: QWidget(pParentWidget)
{
    // init generated dialog
    setupUi(this);

    if (QLayout* pNoticeLayout = frame_notificationArea->layout()) {
        pNoticeLayout->setContentsMargins(uiDesign::scmNoticePaddingHorizontal, uiDesign::scmNoticePaddingVertical, uiDesign::scmNoticePaddingHorizontal, uiDesign::scmNoticePaddingVertical);
        pNoticeLayout->setSpacing(uiDesign::scmNoticeSpacing);
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
    frame_notificationArea->setStyleSheet(uiDesign::noticeStyleSheet(qsl("QFrame#frame_notificationArea"), tokens));
    // The picture beside the words, in the shared hand: the .ui file ships a
    // 64px full-colour bitmap on each of the three labels, and the notice is a
    // line or two of text with one glyph beside it
    uiDesign::applyNoticeGlyph(notificationAreaIconLabelInformation, uiDesign::NoticeKind::Information, tokens);
    uiDesign::applyNoticeGlyph(notificationAreaIconLabelWarning, uiDesign::NoticeKind::Warning, tokens);
    uiDesign::applyNoticeGlyph(notificationAreaIconLabelError, uiDesign::NoticeKind::Error, tokens);
    // The words of the notice are named on the label itself rather than left to
    // the descendant rule above, which does not reach them: the area is hidden
    // while the window round it is styled and is polished only when a notice
    // brings it out, and that polish writes the application's own ink into the
    // label's palette. A sheet the label carries survives it.
    notificationAreaMessageBox->setStyleSheet(qsl("color: %1;").arg(tokens.mutedText.name()));
}
