/***************************************************************************
 *   Copyright (C) 2026 by Mudlet Developers - mudlet@mudlet.org           *
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

#include "AlphaColourWell.h"

#include "uiDesign.h"
#include "utils.h"

#include <QCoreApplication>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>

namespace uiDesign {

namespace {

// What a plain well leaves above and below its words, so the two kinds of well
// come out one height - see mudlet::mTEXT_ON_BG_STYLESHEET
constexpr int scmWellPaddingVertical = 3;

// Black and white squares four pixels on a side. The picture's own squares are
// three times as tall as they are wide, as it was drawn to be squeezed into a
// 3:1 icon, so it is squeezed by the same amount here.
QPixmap checkerboardTile(const qreal ratio)
{
    QPixmap tile = QPixmap(qsl(":/icons/black_white_transparent_check_1x3_ratio.png")).scaled(QSize(48, 16) * ratio, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    tile.setDevicePixelRatio(ratio);
    return tile;
}

} // namespace

AlphaColourWell::AlphaColourWell(QPushButton* pButton)
: QObject(pButton)
, mpButton(pButton)
{
    pButton->installEventFilter(this);
}

AlphaColourWell* AlphaColourWell::on(QPushButton* pButton)
{
    if (AlphaColourWell* pWell = find(pButton)) {
        return pWell;
    }
    return new AlphaColourWell(pButton);
}

AlphaColourWell* AlphaColourWell::find(const QPushButton* pButton)
{
    if (!pButton) {
        return nullptr;
    }
    for (QObject* pChild : pButton->children()) {
        if (auto* pWell = dynamic_cast<AlphaColourWell*>(pChild)) {
            return pWell;
        }
    }
    return nullptr;
}

void AlphaColourWell::setColour(const QColor& colour)
{
    mColour = colour;
    applyLook();
    mStale = true;
    mpButton->update();
}

QRect AlphaColourWell::inside() const
{
    return mpButton->rect().adjusted(scmInputBorderWidth, scmInputBorderWidth, -scmInputBorderWidth, -scmInputBorderWidth);
}

void AlphaColourWell::applyLook()
{
    QString words;
    QString sheet;
    if (mColour.isValid()) {
        //: Words inside a map colour well in the settings, beside a swatch of the colour: how opaque the colour is, %1 being a whole percentage - 100 for a colour that is not see-through at all
        words = QCoreApplication::translate("AlphaColourWell", "Opacity %1%").arg(qRound(mColour.alphaF() * 100));
        const ThemeTokens tokens = themeTokens();
        // The swatch is as tall as the inside of the well, and the words start
        // the field's padding past it
        const int swatch = mpButton->fontMetrics().height() + 2 * scmWellPaddingVertical;
        sheet = qsl("QPushButton { color: %1; background-color: transparent; border: %2px solid %3; border-radius: %4px; padding: %5px %6px %5px %7px; min-height: 0px; text-align: left; }"
                    " QPushButton:disabled { color: %8; }")
                        .arg(tokens.text.name())
                        .arg(scmInputBorderWidth)
                        .arg(tokens.border.name())
                        .arg(scmRadiusInput)
                        .arg(scmWellPaddingVertical)
                        .arg(scmInputPaddingHorizontal)
                        .arg(swatch + scmInputPaddingHorizontal)
                        .arg(tokens.disabledText.name());
    }
    // Setting either brings the events that call this back again, so only what differs is set
    if (mpButton->text() != words) {
        mpButton->setText(words);
    }
    if (mpButton->styleSheet() != sheet) {
        mpButton->setStyleSheet(sheet);
    }
}

void AlphaColourWell::regenerate()
{
    mStale = false;
    mIcon = QIcon();
    const QSize size = inside().size();
    if (!mColour.isValid() || size.isEmpty()) {
        return;
    }

    const qreal ratio = mpButton->devicePixelRatioF();
    QPixmap picture(size * ratio);
    picture.setDevicePixelRatio(ratio);
    picture.fill(Qt::transparent);
    QPainter painter(&picture);
    painter.setRenderHint(QPainter::Antialiasing);
    // The field, cut to the corner inside the hairline
    const qreal innerRadius = scmRadiusInput - scmInputBorderWidth;
    QPainterPath field;
    field.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(size)), innerRadius, innerRadius);
    painter.fillPath(field, themeTokens().field);
    // The swatch, square and cut to an input's corner
    const QRect swatch(0, 0, size.height(), size.height());
    QPainterPath swatchCorner;
    swatchCorner.addRoundedRect(QRectF(swatch), scmRadiusInput, scmRadiusInput);
    painter.setClipPath(swatchCorner.intersected(field));
    painter.drawTiledPixmap(swatch, checkerboardTile(ratio));
    const bool available = mpButton->isEnabled();
    // Unavailable, the colour keeps its hue and loses most of its saturation
    const QColor shown = available ? mColour : QColor::fromHsl(mColour.hslHue(), mColour.hslSaturation() / 4, mColour.lightness(), mColour.alpha());
    painter.fillRect(swatch, shown);
    painter.end();
    // The unavailable picture has to be given to the icon as the one for its
    // Disabled mode, explicitly: given as the normal one, the built-in icon
    // engine would assume it is for the normal state and grey it out
    // completely by automagic means instead of showing the partial
    // (desaturating) effect drawn above
    mIcon.addPixmap(picture, available ? QIcon::Normal : QIcon::Disabled, QIcon::Off);
}

bool AlphaColourWell::eventFilter(QObject* pWatched, QEvent* pEvent)
{
    if (pWatched != mpButton) {
        return QObject::eventFilter(pWatched, pEvent);
    }
    switch (pEvent->type()) {
    case QEvent::FontChange:
    case QEvent::LanguageChange:
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
        if (mColour.isValid()) {
            applyLook();
        }
        mStale = true;
        break;
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::EnabledChange:
    case QEvent::StyleChange:
    case QEvent::DevicePixelRatioChange:
        mStale = true;
        break;
    case QEvent::Paint: {
        if (mStale) {
            regenerate();
        }
        if (!mIcon.isNull()) {
            // Before the button paints: its words and its hairline are then
            // drawn over this
            QPainter painter(mpButton);
            mIcon.paint(&painter, inside(), Qt::AlignCenter, mpButton->isEnabled() ? QIcon::Normal : QIcon::Disabled, QIcon::Off);
        }
        break;
    }
    default:
        break;
    }
    return QObject::eventFilter(pWatched, pEvent);
}

} // namespace uiDesign
