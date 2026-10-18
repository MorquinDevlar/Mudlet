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

#include "AboutLinkButton.h"

#include <algorithm>

#include <QDesktopServices>
#include <QFontMetrics>
#include <QPainter>
#include <QPalette>
#include <QUrl>

namespace {
// What the card leaves round what it holds, and how far the two lines of words
// are held off the glyph beside them
constexpr int scmPaddingVertical = 10;
constexpr int scmPaddingHorizontal = 12;
constexpr int scmGlyphSize = 18;
constexpr int scmGlyphGap = 11;
// The host is the quieter half of the pair, in the type a URL is set in
constexpr qreal scmHostFontScale = 0.85;
// A wash of the words over the card, the same strength the shell's hover is
// mixed at - opaque here, since a painter has a surface to mix it over
constexpr qreal scmHoverStrength = 0.07;
// The narrowest a card is asked for, in characters of its own type: enough to
// tell one place from another once both lines are elided
constexpr int scmLeastWordsWidth = 9;
} // namespace

namespace uiDesign {

AboutLinkButton::AboutLinkButton(const QString& glyphFile, const QString& name, const QString& host, const QString& url, QWidget* pParent)
: QAbstractButton(pParent)
, mGlyphFile(glyphFile)
, mHost(host)
, mUrl(url)
{
    setText(name);
    setToolTip(url);
    setAccessibleName(name);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(this, &QAbstractButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl(mUrl));
    });
}

void AboutLinkButton::applyTokens(const uiDesign::ThemeTokens& tokens)
{
    mTokens = tokens;
    // Scaled to the slot it is drawn in before it is inked, so that the paint
    // is a 1:1 blit rather than a nearest-neighbour downsample of a 128px
    // picture on every repaint
    const qreal ratio = devicePixelRatioF();
    QPixmap glyph = QPixmap(mGlyphFile).scaled(qRound(scmGlyphSize * ratio), qRound(scmGlyphSize * ratio), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    glyph.setDevicePixelRatio(ratio);
    mGlyph = uiDesign::tintedGlyph(glyph, tokens.accentText);
    // The name is painted in the words' own tone, and the palette is the only
    // place anything outside this widget can read that off - the readability
    // audit among them
    QPalette inks = palette();
    inks.setColor(QPalette::ButtonText, tokens.text);
    inks.setColor(QPalette::WindowText, tokens.text);
    setPalette(inks);
    update();
}

static QFont hostFont(const QFont& base)
{
    return uiDesign::fixedPitchFont(base, scmHostFontScale);
}

QSize AboutLinkButton::sizeHint() const
{
    QFont nameFont = font();
    nameFont.setBold(true);
    const QFontMetrics nameMetrics(nameFont);
    const QFontMetrics theHostMetrics(hostFont(font()));
    const int words = std::max(nameMetrics.horizontalAdvance(text()), theHostMetrics.horizontalAdvance(mHost));
    const int height = 2 * scmPaddingVertical + nameMetrics.height() + theHostMetrics.height();
    return {2 * scmPaddingHorizontal + scmGlyphSize + scmGlyphGap + words, height};
}

QSize AboutLinkButton::minimumSizeHint() const
{
    QFont nameFont = font();
    nameFont.setBold(true);
    const QFontMetrics nameMetrics(nameFont);
    return {2 * scmPaddingHorizontal + scmGlyphSize + scmGlyphGap + scmLeastWordsWidth * nameMetrics.averageCharWidth(), sizeHint().height()};
}

void AboutLinkButton::paintEvent(QPaintEvent* pEvent)
{
    Q_UNUSED(pEvent)
    if (!mTokens.card.isValid()) {
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const bool lit = underMouse() || hasFocus();
    const QColor surface = lit ? uiDesign::blend(mTokens.card, mTokens.text, scmHoverStrength) : mTokens.card;
    // Half a pen inside the widget, so the hairline lands on it rather than
    // half of it off the edge
    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(lit ? mTokens.accent : mTokens.border, 1.0));
    painter.setBrush(surface);
    painter.drawRoundedRect(frame, uiDesign::scmRadiusPanel, uiDesign::scmRadiusPanel);

    QFont nameFont = font();
    nameFont.setBold(true);
    const QFontMetrics nameMetrics(nameFont);
    const QFont theHostFont = hostFont(font());
    const QFontMetrics theHostMetrics(theHostFont);

    const int wordsTop = (height() - nameMetrics.height() - theHostMetrics.height()) / 2;
    const int wordsLeft = scmPaddingHorizontal + scmGlyphSize + scmGlyphGap;
    const int wordsWidth = std::max(0, width() - wordsLeft - scmPaddingHorizontal);

    if (!mGlyph.isNull()) {
        painter.drawPixmap(QPoint(scmPaddingHorizontal, (height() - scmGlyphSize) / 2), mGlyph);
    }

    painter.setFont(nameFont);
    painter.setPen(mTokens.text);
    painter.drawText(QRect(wordsLeft, wordsTop, wordsWidth, nameMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter, nameMetrics.elidedText(text(), Qt::ElideRight, wordsWidth));

    painter.setFont(theHostFont);
    painter.setPen(mTokens.mutedText);
    painter.drawText(
            QRect(wordsLeft, wordsTop + nameMetrics.height(), wordsWidth, theHostMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter, theHostMetrics.elidedText(mHost, Qt::ElideRight, wordsWidth));

    if (hasFocus()) {
        // The keyboard has to say where it is on a card that is otherwise only
        // told apart from its neighbours by the pointer being on it
        painter.setPen(QPen(mTokens.accent, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(frame.adjusted(1.5, 1.5, -1.5, -1.5), uiDesign::scmRadiusPanel - 2, uiDesign::scmRadiusPanel - 2);
    }
}

} // namespace uiDesign
