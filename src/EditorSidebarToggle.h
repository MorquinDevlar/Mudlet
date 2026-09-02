#ifndef MUDLET_EDITORSIDEBARTOGGLE_H
#define MUDLET_EDITORSIDEBARTOGGLE_H

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

#include "utils.h"

#include <QAbstractButton>
#include <QPointer>

namespace uiDesign {

// The control that gives a sidebar's names up and brings them back, drawn on
// the seam between that sidebar and the rest of the window - where Finder and
// VS Code put the same control. A toolbar is no place for it: the user can drag
// one to another edge of the window or float it, and the control that belongs
// to the column down the left would go with it.
//
// It paints itself rather than carrying a picture: a pill of the card tone with
// the border hairline round it and a chevron drawn with a pen, pointing the way
// the sidebar will go when it is pressed.
class EditorSidebarToggle : public QAbstractButton
{
    Q_OBJECT

public:
    Q_DISABLE_COPY(EditorSidebarToggle)
    // pSeam is the pane whose trailing edge the pill straddles. pParent has to
    // be the widget holding both that pane and whatever is beside it, so that
    // the pill is drawn over the two of them rather than clipped by either.
    EditorSidebarToggle(QWidget* pSeam, QWidget* pParent);

    // Which way the chevron points, which is what pressing it will do: left
    // takes the names away, right brings them back
    void setPointingLeft(const bool pointingLeft);
    [[nodiscard]] bool pointingLeft() const { return mPointingLeft; }

    // Back onto the seam, centred down the pane's height. Asked for by itself
    // whenever either widget it is measured from moves or resizes, so a caller
    // only needs it where neither of those happens.
    void reposition();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* pWatched, QEvent* pEvent) override;
    void enterEvent(TEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QPointer<QWidget> mpSeam;
    bool mPointingLeft = true;
};

} // namespace uiDesign

#endif // MUDLET_EDITORSIDEBARTOGGLE_H
