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

#include "uiDesign.h"

#include "TKeySequenceEdit.h"
#include "utils.h"

#include <QAbstractButton>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionGroupBox>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <tuple>
#include <utility>

namespace uiDesign {

namespace {
// The saturation and the two lightnesses every state colour is mixed at
constexpr qreal scmStateSaturation = 0.55;
constexpr qreal scmStateLightnessOnDark = 0.58;
constexpr qreal scmStateLightnessOnLight = 0.36;
// The hue and saturation of a highlighter pen; only the lightness comes off the
// page, the way a state colour is mixed
constexpr qreal scmMarkerHue = 0.13;
constexpr qreal scmMarkerSaturation = 0.9;
constexpr qreal scmMarkerLightnessOnDark = 0.34;
constexpr qreal scmMarkerLightnessOnLight = 0.72;
// How far a card is lifted off the page it lies on. A dark page takes a fraction
// of the light one: the same step in absolute lightness reads as a much larger
// one where there is less light to begin with.
constexpr qreal scmCardLiftOnDark = 0.06;
constexpr qreal scmCardLiftOnLight = 0.55;
// ...and what a card has to gain over its page to read as lifted at all, before
// the page is the one that has to move. macOS answers white to Window and Base
// alike on its light appearance, and a card lightened off a white page lands
// back on it.
constexpr int scmMinimumCardLift = 6;
constexpr qreal scmPageDropUnderCard = 0.05;
} // namespace

bool alignInLayoutTree(QLayout* pLayout, const QWidget* pWidget, const Qt::Alignment alignment)
{
    for (int i = 0, total = pLayout->count(); i < total; ++i) {
        QLayoutItem* pItem = pLayout->itemAt(i);
        if (pItem->widget() == pWidget) {
            pItem->setAlignment(alignment);
            return true;
        }
        if (QLayout* pChildLayout = pItem->layout(); pChildLayout && alignInLayoutTree(pChildLayout, pWidget, alignment)) {
            return true;
        }
    }
    return false;
}

bool removeFromLayoutTree(QLayout* pLayout, QWidget* pWidget)
{
    for (int i = 0, total = pLayout->count(); i < total; ++i) {
        QLayoutItem* pItem = pLayout->itemAt(i);
        if (pItem->widget() == pWidget) {
            delete pLayout->takeAt(i);
            pLayout->invalidate();
            return true;
        }
        if (QLayout* pChildLayout = pItem->layout(); pChildLayout && removeFromLayoutTree(pChildLayout, pWidget)) {
            return true;
        }
    }
    return false;
}

void detachFromLayout(QWidget* pWidget)
{
    QWidget* pParent = pWidget->parentWidget();
    if (QLayout* pLayout = pParent ? pParent->layout() : nullptr; pLayout) {
        removeFromLayoutTree(pLayout, pWidget);
    }
}

// A layout tells the layouts above it that it has changed by *posting* a layout
// request, and nothing between a change and the measurement that judges it runs
// an event loop to deliver one. Both halves below are needed: invalidate() drops
// what a layout worked out about its items, while what it caches about a
// *widget* - the size hints, in the layout item it made for it - goes only with
// updateGeometry() on that widget.
void invalidateLayoutsUpTo(QWidget* pWidget, const QWidget* pTop)
{
    for (QWidget* pAncestor = pWidget; pAncestor; pAncestor = pAncestor->parentWidget()) {
        if (QLayout* pLayout = pAncestor->layout(); pLayout) {
            pLayout->invalidate();
        }
        pAncestor->updateGeometry();
        if (pAncestor == pTop) {
            return;
        }
    }
}

void markAsShellSurface(QWidget* pWidget)
{
    pWidget->setProperty("settingsSurface", true);
}

void insertGridRowAtTop(QGridLayout* pGrid, QWidget* pWidget)
{
    const int rows = pGrid->rowCount();
    const int columns = std::max(1, pGrid->columnCount());
    QList<std::pair<int, int>> rowProperties;
    rowProperties.reserve(rows);
    for (int row = 0; row < rows; ++row) {
        rowProperties.append({pGrid->rowStretch(row), pGrid->rowMinimumHeight(row)});
    }

    QList<std::tuple<QLayoutItem*, int, int, int, int>> items;
    items.reserve(pGrid->count());
    while (pGrid->count()) {
        int row = 0;
        int column = 0;
        int rowSpan = 1;
        int columnSpan = 1;
        pGrid->getItemPosition(0, &row, &column, &rowSpan, &columnSpan);
        items.append({pGrid->takeAt(0), row, column, rowSpan, columnSpan});
    }

    pGrid->addWidget(pWidget, 0, 0, 1, columns);
    for (const auto& [pItem, row, column, rowSpan, columnSpan] : items) {
        pGrid->addItem(pItem, row + 1, column, rowSpan, columnSpan, pItem->alignment());
    }
    pGrid->setRowStretch(0, 0);
    pGrid->setRowMinimumHeight(0, 0);
    for (int row = 0; row < rows; ++row) {
        pGrid->setRowStretch(row + 1, rowProperties.at(row).first);
        pGrid->setRowMinimumHeight(row + 1, rowProperties.at(row).second);
    }
}

void makeChevronRow(QAbstractButton* pButton)
{
    pButton->setProperty("settingsChevronRow", true);
    pButton->setCursor(Qt::PointingHandCursor);
    pButton->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
}

void collectFocusableInLayoutOrder(const QLayout* pLayout, QList<QWidget*>& chain)
{
    for (int i = 0, total = pLayout->count(); i < total; ++i) {
        QLayoutItem* pItem = pLayout->itemAt(i);
        if (QWidget* pWidget = pItem->widget(); pWidget) {
            if ((pWidget->focusPolicy() & Qt::TabFocus) == Qt::TabFocus) {
                chain.append(pWidget);
            }
            if (const QLayout* pChildLayout = pWidget->layout(); pChildLayout) {
                collectFocusableInLayoutOrder(pChildLayout, chain);
            }
        } else if (const QLayout* pChildLayout = pItem->layout(); pChildLayout) {
            collectFocusableInLayoutOrder(pChildLayout, chain);
        }
    }
}

QString spotlightStyleSheet(const QColor& accent, const qreal strength)
{
    return qsl("#settingsSpotlight { border: 2px solid rgba(%1, %2, %3, %4); border-radius: 8px; background-color: rgba(%1, %2, %3, %5); }")
            .arg(QString::number(accent.red()), QString::number(accent.green()), QString::number(accent.blue()), QString::number(strength, 'f', 3), QString::number(strength * 0.08, 'f', 3));
}

QString foldForSearch(const QString& text)
{
    QString plain;
    plain.reserve(text.size());
    bool inTag = false;
    for (const QChar character : text) {
        if (character == QLatin1Char('<')) {
            inTag = true;
        } else if (character == QLatin1Char('>')) {
            inTag = false;
        } else if (!inTag && character != QLatin1Char('&')) {
            plain.append(character);
        }
    }

    const QString decomposed = plain.normalized(QString::NormalizationForm_KD);
    QString folded;
    folded.reserve(decomposed.size());
    for (const QChar character : decomposed) {
        if (character.category() != QChar::Mark_NonSpacing) {
            folded.append(character);
        }
    }
    return folded.simplified().toCaseFolded();
}

QString visibleTextOf(const QWidget* pWidget)
{
    if (const auto* pLabel = qobject_cast<const QLabel*>(pWidget); pLabel) {
        return pLabel->text();
    }
    if (const auto* pGroupBox = qobject_cast<const QGroupBox*>(pWidget); pGroupBox) {
        return pGroupBox->title();
    }
    if (const auto* pButton = qobject_cast<const QAbstractButton*>(pWidget); pButton) {
        return pButton->text();
    }
    return QString();
}

void collectSearchText(const QWidget* pWidget, QStringList& parts)
{
    parts << pWidget->property(scmProp_searchKeywords).toString() << pWidget->toolTip();
    const auto* pComboBox = qobject_cast<const QComboBox*>(pWidget);
    if (!pComboBox) {
        parts << visibleTextOf(pWidget);
        return;
    }
    // ...but not what a font picker lists: those are the fonts installed on this
    // machine, and they make any card holding one a result for "color" or "mono"
    if (qobject_cast<const QFontComboBox*>(pWidget)) {
        return;
    }
    for (int i = 0, total = pComboBox->count(); i < total; ++i) {
        parts << pComboBox->itemText(i);
    }
}

QString highlightTextOf(const QWidget* pWidget)
{
    const QString text = visibleTextOf(pWidget);
    if (text.isEmpty()) {
        return QString();
    }
    const QString keywords = pWidget->property(scmProp_searchKeywords).toString();
    return keywords.isEmpty() ? text : qsl("%1 %2").arg(text, keywords);
}

bool wordEnoughToSearch(const QStringList& needles)
{
    for (const QString& needle : needles) {
        if (needle.size() >= 2) {
            return true;
        }
        switch (needle.at(0).script()) {
        case QChar::Script_Han:
        case QChar::Script_Hiragana:
        case QChar::Script_Katakana:
        case QChar::Script_Hangul:
            return true;
        default:
            break;
        }
    }
    return false;
}

void repolish(QWidget* pWidget)
{
    pWidget->style()->unpolish(pWidget);
    pWidget->style()->polish(pWidget);
    pWidget->update();
}

void setSearchMatch(QWidget* pWidget, const QVariant& matched)
{
    pWidget->setProperty("searchMatch", matched);
    repolish(pWidget);
}

QColor blend(const QColor& from, const QColor& to, const qreal amount)
{
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * amount, from.greenF() + (to.greenF() - from.greenF()) * amount, from.blueF() + (to.blueF() - from.blueF()) * amount);
}

QString rgba(const QColor& color, const qreal alpha)
{
    return qsl("rgba(%1, %2, %3, %4)").arg(QString::number(color.red()), QString::number(color.green()), QString::number(color.blue()), QString::number(alpha, 'f', 3));
}

ThemeTokens themeTokens()
{
    // The application's palette rather than any one widget's: a stylesheet
    // freezes the palette of what it is set on, so a profile's Lua stylesheet
    // leaves a window holding the theme it was shown in - and even without one,
    // the palette change is an event still undelivered while a window is
    // restyling itself. qApp's palette is swapped synchronously by
    // mudlet::setAppearance(), so it is already the new one.
    const QPalette themePalette = QApplication::palette();
    ThemeTokens tokens;
    tokens.text = themePalette.color(QPalette::WindowText);
    tokens.accent = themePalette.color(QPalette::Highlight);
    // The window's own colour, not the colour of an input field: a page mixed
    // off Base is a page darker than the fields lying on it, which is the one
    // way round three surfaces cannot be read as depth.
    tokens.page = themePalette.color(QPalette::Window);
    tokens.darkPage = tokens.page.lightness() < 128;
    tokens.field = themePalette.color(QPalette::Base);
    tokens.card = blend(tokens.page, QColor(Qt::white), tokens.darkPage ? scmCardLiftOnDark : scmCardLiftOnLight);
    // Where a palette leaves no room above the page - a white Window under a
    // white Base - the card keeps the window's colour and the page steps down
    // instead, so that the pair still reads in the order it means
    if (tokens.card.lightness() - tokens.page.lightness() < scmMinimumCardLift) {
        tokens.card = tokens.page;
        tokens.page = blend(tokens.page, QColor(Qt::black), scmPageDropUnderCard);
    }
    tokens.border = blend(tokens.page, tokens.text, tokens.darkPage ? 0.22 : 0.18);
    tokens.mutedText = blend(tokens.page, tokens.text, 0.70);
    tokens.disabledText = blend(tokens.page, tokens.text, 0.32);
    tokens.accentText = tokens.darkPage ? blend(tokens.accent, QColor(Qt::white), 0.45) : blend(tokens.accent, QColor(Qt::black), 0.2);
    tokens.marker = QColor::fromHslF(scmMarkerHue, scmMarkerSaturation, tokens.darkPage ? scmMarkerLightnessOnDark : scmMarkerLightnessOnLight);
    tokens.hoverSoft = rgba(tokens.text, 0.07);
    tokens.accentSoft = rgba(tokens.accent, 0.14);
    return tokens;
}

QColor stateColor(const qreal hue, const bool darkPage)
{
    return QColor::fromHslF(hue, scmStateSaturation, darkPage ? scmStateLightnessOnDark : scmStateLightnessOnLight);
}

QString scrollBarStyleSheet(const QString& selectorPrefix, const ThemeTokens& tokens)
{
    return qsl("%1 QScrollBar:vertical { background-color: %2; width: 12px; margin: 0px; border: none; }"
               "%1 QScrollBar:horizontal { background-color: %2; height: 12px; margin: 0px; border: none; }"
               "%1 QScrollBar::handle:vertical { background-color: %3; border-radius: 5px; min-height: 32px; margin: 1px; }"
               "%1 QScrollBar::handle:horizontal { background-color: %3; border-radius: 5px; min-width: 32px; margin: 1px; }"
               "%1 QScrollBar::handle:hover { background-color: %4; }"
               "%1 QScrollBar::add-line, %1 QScrollBar::sub-line { width: 0px; height: 0px; }"
               "%1 QScrollBar::add-page, %1 QScrollBar::sub-page { background-color: %2; }")
            .arg(selectorPrefix, tokens.page.name(), blend(tokens.page, tokens.text, 0.22).name(), blend(tokens.page, tokens.text, 0.40).name());
}

QPixmap tintedGlyph(const QPixmap& source, const QColor& color)
{
    QPixmap glyph = source;
    QPainter painter(&glyph);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(glyph.rect(), color);
    painter.end();
    return glyph;
}

int measuredCardTitleInset(QWidget* pParent, const QString& indicatorRules, const char* cardProperty)
{
    const auto titleLeft = [&](const bool checkable) {
        QGroupBox box(pParent);
        box.setProperty(cardProperty, true);
        box.setCheckable(checkable);
        // Never shown or read, but a box with no title has no label to place
        box.setTitle(qsl("Aa"));
        // Its own rather than the shell's, which is the string being built
        box.setStyleSheet(indicatorRules);
        QStyleOptionGroupBox option;
        option.initFrom(&box);
        option.subControls = QStyle::SC_GroupBoxFrame | QStyle::SC_GroupBoxLabel;
        if (checkable) {
            option.subControls |= QStyle::SC_GroupBoxCheckBox;
            option.state |= QStyle::State_On;
        }
        option.text = box.title();
        option.textAlignment = Qt::AlignLeft;
        option.lineWidth = 0;
        option.midLineWidth = 0;
        return box.style()->subControlRect(QStyle::CC_GroupBox, &option, QStyle::SC_GroupBoxLabel, &box).left();
    };
    return qMax(0, titleLeft(true) - titleLeft(false));
}

QString inlineGlyph(const QPixmap& glyph)
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    glyph.save(&buffer, "PNG");
    return qsl(R"(<img src="data:image/png;base64,%1" width="18" height="18">)").arg(QString::fromLatin1(png.toBase64()));
}

QVariant controlValue(const QObject* pControl)
{
    if (const auto* pGroupBox = qobject_cast<const QGroupBox*>(pControl)) {
        return pGroupBox->isCheckable() ? QVariant(pGroupBox->isChecked()) : QVariant();
    }
    if (const auto* pCheckBox = qobject_cast<const QCheckBox*>(pControl)) {
        // The check state rather than isChecked(), for the tri-state boxes
        return QVariant::fromValue(pCheckBox->checkState());
    }
    if (const auto* pButton = qobject_cast<const QAbstractButton*>(pControl)) {
        if (qobject_cast<const QPushButton*>(pControl) || qobject_cast<const QToolButton*>(pControl)) {
            return {};
        }
        return pButton->isChecked();
    }
    if (const auto* pFontComboBox = qobject_cast<const QFontComboBox*>(pControl)) {
        return pFontComboBox->currentFont();
    }
    if (const auto* pComboBox = qobject_cast<const QComboBox*>(pControl)) {
        return pComboBox->currentIndex();
    }
    if (const auto* pSpinBox = qobject_cast<const QSpinBox*>(pControl)) {
        return pSpinBox->value();
    }
    if (const auto* pDoubleSpinBox = qobject_cast<const QDoubleSpinBox*>(pControl)) {
        return pDoubleSpinBox->value();
    }
    if (const auto* pDateTimeEdit = qobject_cast<const QDateTimeEdit*>(pControl)) {
        return pDateTimeEdit->dateTime();
    }
    if (const auto* pLineEdit = qobject_cast<const QLineEdit*>(pControl)) {
        return pLineEdit->text();
    }
    return {};
}

bool beingTypedInto(const QObject* pControl)
{
    const auto* pLineEdit = qobject_cast<const QLineEdit*>(pControl);
    return pLineEdit && pLineEdit->hasFocus() && pLineEdit->isModified();
}

SettingsSnapshot::SettingsSnapshot(const QWidget& owner, const QMap<QString, QKeySequence>& shortcuts)
: mOwner(owner)
, mCurrentShortcuts(shortcuts)
{
}

bool SettingsSnapshot::carriesValue(const QObject* pControl)
{
    return controlValue(pControl).isValid();
}

void SettingsSnapshot::take()
{
    const QHash<const QObject*, QVariant> previous = mValues;
    mValues.clear();
    for (const auto* pWidget : mOwner.findChildren<QWidget*>()) {
        const QVariant value = controlValue(pWidget);
        if (!value.isValid()) {
            continue;
        }
        // The apply this snapshot follows left a half-typed field alone, so what
        // it was last populated with has to stand until that edit finishes
        if (const auto it = previous.constFind(pWidget); beingTypedInto(pWidget) && it != previous.constEnd()) {
            mValues.insert(pWidget, *it);
            continue;
        }
        mValues.insert(pWidget, value);
    }
    mShortcuts = mCurrentShortcuts;
}

void SettingsSnapshot::take(const QObject* pControl)
{
    mValues.insert(pControl, controlValue(pControl));
}

bool SettingsSnapshot::dirty(const QObject* pControl) const
{
    // The debounce is shared, so the apply about to read this was very likely
    // started by another control's edit
    if (beingTypedInto(pControl)) {
        return false;
    }
    const auto it = mValues.constFind(pControl);
    if (it == mValues.constEnd()) {
        // A control that came into being after the last snapshot:
        return true;
    }
    return *it != controlValue(pControl);
}

// For a setting spread over several controls - the borders, the Discord privacy
// flags - one of them changing means the write happens. What is written is
// still composed control by control: an undirty control contributes the value
// the Host holds now rather than what it shows, which a script may have moved
// on from (#10165). Members that are separate settings take their own guard.
bool SettingsSnapshot::anyDirty(const QList<const QObject*>& controls) const
{
    for (const auto* pControl : controls) {
        if (dirty(pControl)) {
            return true;
        }
    }
    return false;
}

bool SettingsSnapshot::shortcutsDirty() const
{
    return mCurrentShortcuts != mShortcuts;
}

bool SettingsSnapshot::shortcutDirty(const QString& key) const
{
    return mCurrentShortcuts.value(key) != mShortcuts.value(key);
}

bool SettingsSnapshot::pendingEdits(const QTimer* pApplyTimer, const QLineEdit* pSearchField) const
{
    // Whatever the settings say, what the controls hold is the user's until the
    // apply has run - and the refresh at the end of it re-reads them anyway
    if (pApplyTimer && pApplyTimer->isActive()) {
        return true;
    }
    for (const auto* pWidget : mOwner.findChildren<QWidget*>()) {
        if (pWidget == pSearchField || !carriesValue(pWidget)) {
            continue;
        }
        // dirty() answers false for a field being typed into, which is exactly
        // the edit that must not be written over here - so it is asked separately
        if (beingTypedInto(pWidget) || dirty(pWidget)) {
            return true;
        }
    }
    if (shortcutsDirty()) {
        return true;
    }
    // A shortcut editor holds a capture until editingFinished, so one showing
    // anything other than what it last committed is an edit in progress
    for (auto it = mEditors.cbegin(), end = mEditors.cend(); it != end; ++it) {
        if (it.value() && it.value()->keySequence() != mCurrentShortcuts.value(it.key())) {
            return true;
        }
    }
    return false;
}

TKeySequenceEdit* SettingsSnapshot::editorFor(const QString& key) const
{
    return mEditors.value(key).data();
}

void SettingsSnapshot::addEditor(const QString& key, TKeySequenceEdit* pEditor)
{
    mEditors.insert(key, pEditor);
}

} // namespace uiDesign
