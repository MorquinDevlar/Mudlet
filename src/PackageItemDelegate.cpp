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
#include <QMouseEvent>
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
    // A package that is switched off is not doing anything, and its row says so
    // the way a switched-off row in the editor's trees does
    mQuietNameInk = tokens.mutedText;
    mSummaryInk = tokens.mutedText;
    // A chosen row is washed in the accent and both its lines are written in
    // that wash's ink: a row where only the name lit would read as half chosen
    mChosenInk = tokens.accentText;
    mAccentBar = tokens.accent;
    mCaptionSize = uiDesign::typeSize(uiDesign::TypeStep::Caption);

    // The version at the trailing edge of a row is the same reading the head of
    // the details column carries: the design's ok tone, the green a running
    // package's dot is filled in, walked until it can be read on what it stands
    // on. Twice, because a chosen row is not the pane - it is filled with the
    // accent's wash, the one itemRowStyleSheet() writes - and a green walked
    // against the pane can be lost on that fill.
    const QColor okTone = uiDesign::stateColor(uiDesign::scmStateHue_ok, tokens.darkPage);
    mVersionInk = uiDesign::readableOn(tokens.pane, okTone, tokens.text, uiDesign::scmTextMinimumRatio);
    mChosenVersionInk = uiDesign::readableOn(uiDesign::blend(tokens.pane, tokens.accent, uiDesign::scmAccentWashStrength), okTone, tokens.text, uiDesign::scmTextMinimumRatio);

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

    // The same two pictures the editor's trees lead a row with, drawn by the
    // same code: the green that "on" is read as everywhere else, and the chrome
    // tone for a ring that says the package is switched off
    mRunningDot = uiDesign::treeRowDotGlyph(true, uiDesign::stateColor(uiDesign::scmStateHue_ok, tokens.darkPage), ratio);
    mOffDot = uiDesign::treeRowDotGlyph(false, tokens.mutedText, ratio);
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

    // The dot leads the row where the package has a switch at all, and what
    // follows it is held clear of it whether or not one was drawn - so the
    // Explore and Updates views, which have none, are not a list of rows that
    // start somewhere else
    const QVariant enabledData = index.data(cPackageEnabledRole);
    if (enabledData.isValid()) {
        const QRect dot = dotRect(option.rect);
        painter->drawPixmap(dot.topLeft(), enabledData.toBool() ? mRunningDot : mOffDot);
    }

    const RowMetrics metrics = measureRow(option, index);
    if (icon.isNull()) {
        // A package that ships no picture of its own is drawn with the design's
        // package glyph rather than with a gap where one would be
        painter->drawPixmap(metrics.icon, chosen ? mChosenGlyph : mQuietGlyph);
    } else {
        icon.paint(painter, metrics.icon, Qt::AlignCenter);
    }

    if (metrics.textWidth <= 0) {
        painter->restore();
        return;
    }

    const QFont nameFont = option.font;
    const QFont summaryFont = captionFont(option.font);
    const QFontMetrics nameMetrics(nameFont);
    const QFontMetrics summaryMetrics(summaryFont);

    const bool switchedOff = enabledData.isValid() && !enabledData.toBool();
    const QRect nameRect(metrics.textLeft, option.rect.top() + cRowPaddingVertical, metrics.nameWidth, nameMetrics.height());
    if (metrics.nameWidth > 0) {
        painter->setFont(nameFont);
        painter->setPen(chosen ? mChosenInk : (switchedOff ? mQuietNameInk : mNameInk));
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, nameMetrics.elidedText(name, Qt::ElideRight, metrics.nameWidth));
    }

    // The version stands at the far end of the name's own line, flush with what
    // the row leaves clear at its trailing edge, on the caption step and
    // centred on the line the name is written on. A switched-off package keeps
    // it in the green: which version is installed is a fact about the package,
    // not a reading of whether it is running.
    if (!metrics.version.isEmpty()) {
        painter->setFont(summaryFont);
        painter->setPen(versionInk(chosen));
        painter->drawText(QRect(metrics.versionRect.left(), nameRect.top(), metrics.versionRect.width(), nameRect.height()), Qt::AlignRight | Qt::AlignVCenter, metrics.version);
    }

    if (!summary.isEmpty()) {
        const QRect summaryRect(metrics.textLeft, nameRect.bottom() + 1 + cLineSpacing, metrics.textWidth, summaryMetrics.height());
        painter->setFont(summaryFont);
        painter->setPen(chosen ? mChosenInk : mSummaryInk);
        painter->drawText(summaryRect, Qt::AlignLeft | Qt::AlignVCenter, summaryMetrics.elidedText(summary, Qt::ElideRight, metrics.textWidth));
    }

    painter->restore();

    // Over the wash the sheet has just filled the chosen row with, as the
    // leading edge of it: the same bar, at the same width and in the same
    // shape, the editor's trees carry
    if (chosen && !option.rect.isEmpty()) {
        uiDesign::paintAccentBar(painter, option.rect, mAccentBar, uiDesign::scmRadiusPanel);
    }
}

QFont PackageItemDelegate::captionFont(const QFont& rowFont) const
{
    QFont caption = rowFont;
    caption.setPointSize(mCaptionSize);
    return caption;
}

PackageItemDelegate::RowMetrics PackageItemDelegate::measureRow(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    RowMetrics metrics;

    // What follows the dot is held clear of it whether or not one was drawn, so
    // the Explore and Updates views, which have none, are not a list of rows
    // that start somewhere else
    const int dotStep = index.data(cPackageEnabledRole).isValid() ? uiDesign::scmTreeDotDiameter + cDotGap : 0;
    const int leading = option.rect.left() + cRowGutter + dotStep;
    metrics.icon = QRect(leading, option.rect.top() + (option.rect.height() - cIconSize) / 2, cIconSize, cIconSize);
    metrics.textLeft = metrics.icon.right() + 1 + cIconGap;
    metrics.textWidth = option.rect.right() - cRowPaddingTrailing - metrics.textLeft;
    metrics.nameWidth = metrics.textWidth;
    if (metrics.textWidth <= 0) {
        return metrics;
    }

    const QString version = index.data(cPackageVersionRole).toString();
    if (version.isEmpty()) {
        return metrics;
    }
    // The version takes its own width out of the name's line and the air before
    // it as well, so the two never touch however long the name is
    const int versionWidth = QFontMetrics(captionFont(option.font)).horizontalAdvance(version);
    metrics.version = version;
    metrics.versionRect = QRect(option.rect.right() - cRowPaddingTrailing - versionWidth, option.rect.top(), versionWidth, option.rect.height());
    metrics.nameWidth = qMax(0, metrics.textWidth - versionWidth - cVersionGap);
    return metrics;
}

QString PackageItemDelegate::nameDrawn(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const RowMetrics metrics = measureRow(option, index);
    if (metrics.nameWidth <= 0) {
        return {};
    }
    return QFontMetrics(option.font).elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, metrics.nameWidth);
}

QRect PackageItemDelegate::dotRect(const QRect& row)
{
    if (row.isEmpty()) {
        return {};
    }
    return QRect(row.left() + cRowGutter, row.top() + (row.height() - uiDesign::scmTreeDotDiameter) / 2, uiDesign::scmTreeDotDiameter, uiDesign::scmTreeDotDiameter);
}

QRect PackageItemDelegate::dotHitRect(const QRect& row)
{
    const QRect dot = dotRect(row);
    if (dot.isEmpty()) {
        return {};
    }
    return dot.adjusted(-uiDesign::scmTreeMarkHitSlack, -uiDesign::scmTreeMarkHitSlack, uiDesign::scmTreeMarkHitSlack, uiDesign::scmTreeMarkHitSlack);
}

bool PackageItemDelegate::editorEvent(QEvent* pEvent, QAbstractItemModel* pModel, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    if (pEvent->type() != QEvent::MouseButtonRelease || !index.isValid() || !index.data(cPackageEnabledRole).isValid()) {
        return QStyledItemDelegate::editorEvent(pEvent, pModel, option, index);
    }

    auto* pMouseEvent = static_cast<QMouseEvent*>(pEvent);
    if (pMouseEvent->button() != Qt::LeftButton || !dotHitRect(option.rect).contains(pMouseEvent->position().toPoint())) {
        return QStyledItemDelegate::editorEvent(pEvent, pModel, option, index);
    }

    emit packageToggleRequested(index.data(Qt::DisplayRole).toString());
    return true;
}

QSize PackageItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (!index.isValid()) {
        return QStyledItemDelegate::sizeHint(option, index);
    }

    // The version rides the name's own line, so it asks the row for no height
    const int textHeight = QFontMetrics(option.font).height() + cLineSpacing + QFontMetrics(captionFont(option.font)).height();
    return QSize(option.rect.width(), qMax(textHeight, cIconSize) + 2 * cRowPaddingVertical);
}
