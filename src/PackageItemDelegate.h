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
#include <QFont>
#include <QPixmap>
#include <QRect>
#include <QString>
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

    // Which version of the package the row stands for, written onto the item
    // wherever the list is filled: the installed number in the Installed view,
    // the repository's in Explore, and both of them with an arrow between in
    // Updates. A package that names no version leaves the role unset, and then
    // nothing is drawn rather than a gap where a number would be. Qt::UserRole
    // is the row's summary.
    static constexpr int cPackageVersionRole = Qt::UserRole + 1;

    // The inks and the two glyph pixmaps, taken again whenever the appearance
    // moves. The dialog's own style pass is what calls it.
    void restyle(const uiDesign::ThemeTokens& tokens);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // What a row's version is written in, in its two readings - on the pane a
    // plain row stands on, and on the wash a chosen row is filled with. Public
    // because a test reading a version off a grab has nothing else to compare
    // the pixels it finds against.
    [[nodiscard]] QColor versionInk(const bool chosen) const { return chosen ? mChosenVersionInk : mVersionInk; }

    // What a row's first line actually says: the package's name, cut to
    // whatever the version standing at the trailing edge leaves it. Public for
    // the same reason - a test asking whether the version pushed the name into
    // an ellipsis has no other way to ask.
    [[nodiscard]] QString nameDrawn(const QStyleOptionViewItem& option, const QModelIndex& index) const;

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
    // Where everything on a row stands, measured once: the picture at the
    // leading edge, where the words start, what the summary line is given, what
    // is left of the first line once the version has taken its place at the
    // trailing edge, and the version's own box. Both the paint and the reading
    // of the name above ask for it, so the row is laid out in one place.
    struct RowMetrics
    {
        QRect icon;
        int textLeft = 0;
        int textWidth = 0;
        int nameWidth = 0;
        QString version;
        QRect versionRect;
    };
    [[nodiscard]] RowMetrics measureRow(const QStyleOptionViewItem& option, const QModelIndex& index) const;

    // The version's own step: the caption font it is set in
    [[nodiscard]] QFont captionFont(const QFont& rowFont) const;

    // The air above and below the two lines, what is left at the trailing edge,
    // and the gap that parts the name from the summary under it
    static constexpr int cRowPaddingVertical = 6;
    static constexpr int cRowPaddingTrailing = 8;
    static constexpr int cLineSpacing = 2;
    // The picture at the leading edge, at the size the list was already asking
    // its items for, and the gap between it and the words
    static constexpr int cIconSize = 16;
    static constexpr int cIconGap = 8;
    // ...and what parts the name from the version standing at the row's
    // trailing edge, which is that same air: the row has one gap measurement
    // rather than a second one nobody can tell from the first
    static constexpr int cVersionGap = cIconGap;

    QColor mNameInk;
    QColor mSummaryInk;
    QColor mChosenInk;
    QColor mAccentBar;
    // The version at the row's trailing edge, in the same ok tone the head of the
    // details column writes it in - walked against the pane a plain row stands
    // on, and again against the wash a chosen row is filled with, or the tone
    // that reads on the one is lost on the other
    QColor mVersionInk;
    QColor mChosenVersionInk;
    // The design's package glyph, for a package that ships no picture of its
    // own - in the chrome tone, and in the chosen row's ink
    QPixmap mQuietGlyph;
    QPixmap mChosenGlyph;
    int mCaptionSize = 0;
};

#endif // MUDLET_PACKAGEITEMDELEGATE_H
