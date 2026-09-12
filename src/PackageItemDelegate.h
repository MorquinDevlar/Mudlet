#ifndef MUDLET_PACKAGEITEMDELEGATE_H
#define MUDLET_PACKAGEITEMDELEGATE_H

/***************************************************************************
 *   Copyright (C) 2025 by Vadim Peretokin - vperetokin@gmail.com         *
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

#include "uiDesign.h"

#include <QColor>
#include <QPixmap>
#include <QStyledItemDelegate>

// One row of the package manager's list: the package's own picture or the
// design's package glyph, its name, and the one line of summary under it. The
// surface under all three - the corner, the hover wash and the accent wash a
// chosen row is filled with - is the shared row recipe
// (uiDesign::itemRowStyleSheet()), which the list carries as a stylesheet; what
// is drawn here is only what stands on it, plus the accent bar down the leading
// edge of the chosen row, which no rule can be the shape of.
class PackageItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit PackageItemDelegate(QObject* parent = nullptr);

    // The inks and the two glyph pixmaps, taken again whenever the appearance
    // moves. The dialog's own style pass is what calls it.
    void restyle(const uiDesign::ThemeTokens& tokens);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // What the row holds clear at its leading edge for the accent bar to stand
    // in: the bar's own width and the air after it. Public because the sheet
    // that draws the row is written from the same number - the bar the delegate
    // paints and the gutter the rule reserves are one measurement, not two.
    static constexpr int cRowGutter = uiDesign::scmAccentBarWidth + 5;

protected:
    // The row's two lines and its picture are drawn below rather than by the
    // style, so what the base is asked for is the row's surface alone
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override;

private:
    // The air above and below the two lines, what is left at the trailing edge,
    // and the gap that parts the name from the summary under it
    static constexpr int cRowPaddingVertical = 6;
    static constexpr int cRowPaddingTrailing = 8;
    static constexpr int cLineSpacing = 2;
    // The picture at the leading edge, at the size the list was already asking
    // its items for, and the gap between it and the words
    static constexpr int cIconSize = 16;
    static constexpr int cIconGap = 8;

    QColor mNameInk;
    QColor mSummaryInk;
    QColor mChosenInk;
    QColor mAccentBar;
    // The design's package glyph, for a package that ships no picture of its
    // own - in the chrome tone, and in the chosen row's ink
    QPixmap mQuietGlyph;
    QPixmap mChosenGlyph;
    int mCaptionSize = 0;
};

#endif // MUDLET_PACKAGEITEMDELEGATE_H
