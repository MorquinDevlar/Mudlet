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

#include "PackageItemDelegate.h"

#include "utils.h"

#include <QFontMetrics>
#include <QIcon>
#include <QModelIndex>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QWidget>

PackageItemDelegate::PackageItemDelegate(QObject* parent)
: QStyledItemDelegate(parent)
{
    restyle(uiDesign::themeTokens());
}

void PackageItemDelegate::restyle(const uiDesign::ThemeTokens& tokens)
{
    // The name is the value on the row - what the package is called - so it
    // takes the ink a typed value does; the summary under it is chrome
    mNameInk = tokens.text;
    mSummaryInk = tokens.mutedText;
    // A chosen row is washed in the accent and both its lines are written in
    // that wash's ink: a row where only the name lit would read as half chosen
    mChosenInk = tokens.accentText;
    mAccentBar = tokens.accent;
    mCaptionSize = uiDesign::typeSize(uiDesign::TypeStep::Caption);

    // Cut at the ratio of the window the list is in, so the glyph is drawn at
    // the screen's resolution rather than blown up from a 16px square
    const auto* pOwner = qobject_cast<const QWidget*>(parent());
    const qreal ratio = pOwner ? pOwner->devicePixelRatioF() : 1.0;
    const auto glyphInked = [ratio](const QColor& colour) {
        QPixmap glyph =
                uiDesign::tintedGlyph(uiDesign::glyphPixmap(qsl(":/icons/packages-package.svg")), colour).scaled(QSize(cIconSize, cIconSize) * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        glyph.setDevicePixelRatio(ratio);
        return glyph;
    };
    mQuietGlyph = glyphInked(mSummaryInk);
    mChosenGlyph = glyphInked(mChosenInk);
}

void PackageItemDelegate::initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    // Everything on the row is drawn below, so the base is left with the row's
    // surface: the corner, the hover wash and the accent wash the sheet gives
    // it, and nothing on top of them
    option->text.clear();
    option->icon = QIcon();
    option->features &= ~QStyleOptionViewItem::HasDecoration;
}

void PackageItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (!index.isValid()) {
        return;
    }

    // The row's surface, which is the shared recipe's doing
    QStyledItemDelegate::paint(painter, option, index);

    const bool chosen = option.state & QStyle::State_Selected;
    const QString name = index.data(Qt::DisplayRole).toString();
    const QString summary = index.data(Qt::UserRole).toString();
    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();

    painter->save();

    const int leading = option.rect.left() + cRowGutter;
    const QRect iconRect(leading, option.rect.top() + (option.rect.height() - cIconSize) / 2, cIconSize, cIconSize);
    if (icon.isNull()) {
        // A package that ships no picture of its own is drawn with the design's
        // package glyph rather than with a gap where one would be
        painter->drawPixmap(iconRect, chosen ? mChosenGlyph : mQuietGlyph);
    } else {
        icon.paint(painter, iconRect, Qt::AlignCenter);
    }

    const int textLeft = iconRect.right() + 1 + cIconGap;
    const int textWidth = option.rect.right() - cRowPaddingTrailing - textLeft;
    if (textWidth <= 0) {
        painter->restore();
        return;
    }

    QFont nameFont = option.font;
    QFont summaryFont = option.font;
    summaryFont.setPointSize(mCaptionSize);
    const QFontMetrics nameMetrics(nameFont);
    const QFontMetrics summaryMetrics(summaryFont);

    const QRect nameRect(textLeft, option.rect.top() + cRowPaddingVertical, textWidth, nameMetrics.height());
    painter->setFont(nameFont);
    painter->setPen(chosen ? mChosenInk : mNameInk);
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, nameMetrics.elidedText(name, Qt::ElideRight, textWidth));

    if (!summary.isEmpty()) {
        const QRect summaryRect(textLeft, nameRect.bottom() + 1 + cLineSpacing, textWidth, summaryMetrics.height());
        painter->setFont(summaryFont);
        painter->setPen(chosen ? mChosenInk : mSummaryInk);
        painter->drawText(summaryRect, Qt::AlignLeft | Qt::AlignVCenter, summaryMetrics.elidedText(summary, Qt::ElideRight, textWidth));
    }

    painter->restore();

    // Over the wash the sheet has just filled the chosen row with, as the
    // leading edge of it: the same bar, at the same width and in the same
    // shape, the editor's trees carry
    if (chosen && !option.rect.isEmpty()) {
        uiDesign::paintAccentBar(painter, option.rect, mAccentBar, uiDesign::scmRadiusPanel);
    }
}

QSize PackageItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (!index.isValid()) {
        return QStyledItemDelegate::sizeHint(option, index);
    }

    QFont summaryFont = option.font;
    summaryFont.setPointSize(mCaptionSize);
    const int textHeight = QFontMetrics(option.font).height() + cLineSpacing + QFontMetrics(summaryFont).height();
    return QSize(option.rect.width(), qMax(textHeight, cIconSize) + 2 * cRowPaddingVertical);
}
