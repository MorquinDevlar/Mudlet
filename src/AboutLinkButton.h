#ifndef MUDLET_ABOUTLINKBUTTON_H
#define MUDLET_ABOUTLINKBUTTON_H

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

#include <QAbstractButton>
#include <QPixmap>
#include <QString>

#include "uiDesign.h"

namespace uiDesign {

// One of the six places the About dialog offers to go: a glyph, the name of the
// place, and the host under it so that the reader can see where a click leads
// before making it.
//
// Painted rather than assembled out of labels inside a frame, because the whole
// card is the click target: a QLabel over a QPushButton eats the press, and a
// stylesheet cannot set two lines of a button's text in two different tones.
class AboutLinkButton : public QAbstractButton
{
    Q_OBJECT

public:
    Q_DISABLE_COPY(AboutLinkButton)
    AboutLinkButton(const QString& glyphFile, const QString& name, const QString& host, const QString& url, QWidget* pParent = nullptr);

    // The colours come from the palette at the moment the shell is styled, and
    // again whenever the appearance moves
    void applyTokens(const uiDesign::ThemeTokens& tokens);
    QString url() const { return mUrl; }

    QSize sizeHint() const override;
    // Deliberately smaller than the hint: the name and the host are elided to
    // whatever the card is given, and a minimum as wide as either would make a
    // long translation scroll the whole page sideways
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* pEvent) override;

private:
    QString mGlyphFile;
    QString mHost;
    QString mUrl;
    QPixmap mGlyph;
    uiDesign::ThemeTokens mTokens;
};

} // namespace uiDesign

#endif // MUDLET_ABOUTLINKBUTTON_H
