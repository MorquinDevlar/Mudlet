#ifndef MUDLET_EDITORPLACEHOLDERBUTTON_H
#define MUDLET_EDITORPLACEHOLDERBUTTON_H

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

#include <QColor>
#include <QMargins>
#include <QToolButton>

namespace uiDesign {

// A button that reads as the place one more of something goes, rather than as a
// control: a fine dashed frame with the action named inside it.
//
// The frame is painted rather than asked of a stylesheet, because Qt strokes a
// "1px dashed" border as a run of squares as long as the gaps between them -
// coarse enough beside the rest of the editor to read as a different drawing.
// Everything else about the button is still the stylesheet's, so the margins the
// sheet holds it away from its neighbours by are handed over rather than guessed
// at: they are what the widget's own rectangle is inset by before the frame is
// drawn in it.
class PlaceholderButton : public QToolButton
{
public:
    Q_DISABLE_COPY(PlaceholderButton)
    explicit PlaceholderButton(QWidget* pParent);

    // In the order a QAbstractButton is drawn by: the resting frame, the one
    // under the pointer or the keyboard, and the one for a button there is no
    // longer anything to add
    void setFrameColors(const QColor& resting, const QColor& active, const QColor& disabled);
    void setFrameMargins(const QMargins& margins);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QColor mRestingColor;
    QColor mActiveColor;
    QColor mDisabledColor;
    QMargins mFrameMargins;
};

} // namespace uiDesign

#endif // MUDLET_EDITORPLACEHOLDERBUTTON_H
