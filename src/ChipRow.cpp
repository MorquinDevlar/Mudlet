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

#include "ChipRow.h"

#include "FlowLayout.h"
#include "uiDesign.h"
#include "utils.h"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QToolButton>

#include <algorithm>

namespace uiDesign {

// A shade smaller than the words around it, which is what makes a chip read as
// a label on something rather than as a line of the form
static constexpr qreal scmChipFontScale = 0.85;
// What a chip leaves round its word. More on the leading edge than the trailing
// one, because the cross that takes the chip away stands in the trailing gap.
static constexpr int scmChipPaddingVertical = 3;
static constexpr int scmChipPaddingLeading = 9;
static constexpr int scmChipPaddingTrailing = 5;
// Between the word and that cross
static constexpr int scmChipGap = 4;
// The cross itself, and the plus on the button that opens the field: measured
// off the chip's own type, so a larger interface font gets a larger glyph
static constexpr qreal scmChipGlyphScale = 0.75;
// What is left between two chips, and between two lines of them
static constexpr int scmChipSpacingAcross = 6;
static constexpr int scmChipSpacingDown = 6;
// How long the "already listed" note stands before it takes itself away
static constexpr int scmChipNoteMilliseconds = 2000;
// Mudlet's own events all start with this, and a script's own read stronger
// than the ones it is only listening for
static constexpr char scmSystemEventPrefix[] = "sys";

QFont chipFont(const QWidget* pOn)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QFont hostFont = pOn->font();
    if (hostFont.pointSizeF() > 0.0) {
        font.setPointSizeF(hostFont.pointSizeF() * scmChipFontScale);
    } else {
        font.setPixelSize(std::max(1, qRound(hostFont.pixelSize() * scmChipFontScale)));
    }
    return font;
}

// The height one chip comes to, which is also what a line of them is tall
static int chipHeightOn(const QWidget* pOn)
{
    return QFontMetrics(chipFont(pOn)).height() + 2 * (scmChipPaddingVertical + scmInputBorderWidth);
}

static int chipGlyphSizeOn(const QWidget* pOn)
{
    return std::max(1, qRound(QFontMetrics(chipFont(pOn)).height() * scmChipGlyphScale));
}

Chip::Chip(const QString& name, QWidget* pParent)
: QFrame(pParent)
{
    setObjectName(qsl("editorChip"));
    setFrameShape(QFrame::NoFrame);
    // A rule naming this widget only paints once it is told that its background
    // is the stylesheet's business rather than the palette's
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::StrongFocus);

    auto* pRow = new QHBoxLayout(this);
    // The frame's padding is left to the layout rather than written into the
    // sheet: a plain QFrame's stylesheet padding moves what is drawn without
    // moving what the layout puts there, so the two would disagree
    pRow->setContentsMargins(scmChipPaddingLeading, scmChipPaddingVertical, scmChipPaddingTrailing, scmChipPaddingVertical);
    pRow->setSpacing(scmChipGap);

    mpLabel = new QLabel(this);
    // Named so that a walk over the window can say which chip it is talking
    // about; the rules below select it through the chip, whose property says
    // whether the name is one of Mudlet's own
    mpLabel->setObjectName(qsl("editorChipLabel"));
    mpLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    pRow->addWidget(mpLabel);

    mpRemove = new QToolButton(this);
    mpRemove->setObjectName(qsl("editorChipRemove"));
    mpRemove->setAutoRaise(true);
    // Tab walks the chips themselves; the cross is reached with Delete instead,
    // which is one stop per chip rather than two
    mpRemove->setFocusPolicy(Qt::NoFocus);
    connect(mpRemove, &QAbstractButton::clicked, this, &Chip::removeRequested);
    pRow->addWidget(mpRemove);

    remeasure();
    setName(name);
}

// Measured off the row rather than off the chip's own font: a stylesheet rule
// naming the chip stops it inheriting anything from what it lies on, so its own
// font answers with the application's rather than with the row's - and the row's
// is what "a shade smaller than the words around it" is a shade smaller than.
void Chip::remeasure()
{
    const QWidget* pOn = parentWidget() ? parentWidget() : this;
    mpLabel->setFont(chipFont(pOn));
    const int glyphSize = chipGlyphSizeOn(pOn);
    mpRemove->setIconSize(QSize(glyphSize, glyphSize));
    mpRemove->setFixedSize(glyphSize, glyphSize);
    setFixedHeight(chipHeightOn(pOn));
    updateGeometry();
}

void Chip::changeEvent(QEvent* pEvent)
{
    QFrame::changeEvent(pEvent);
    if (pEvent->type() == QEvent::FontChange) {
        remeasure();
    }
}

void Chip::setName(const QString& name)
{
    mName = name;
    mpLabel->setText(name);
    //: Tooltip on the cross that takes one event off the row of events a script listens for; %1 is the event's name
    mpRemove->setToolTip(utils::richText(tr("Stop listening for %1").arg(name)));
    //: Accessible name of one event in a script's row of events; %1 is the event's name
    setAccessibleName(tr("Event %1").arg(name));
    setProperty("editorChipSystem", name.startsWith(QLatin1StringView(scmSystemEventPrefix)));
    repolish(this);
    updateGeometry();
}

void Chip::setRemoveGlyph(const QIcon& glyph)
{
    mpRemove->setIcon(glyph);
}

void Chip::keyPressEvent(QKeyEvent* pEvent)
{
    switch (pEvent->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        emit removeRequested();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_F2:
        emit editRequested();
        return;
    default:
        break;
    }
    QFrame::keyPressEvent(pEvent);
}

void Chip::mousePressEvent(QMouseEvent* pEvent)
{
    if (pEvent->button() != Qt::LeftButton) {
        QFrame::mousePressEvent(pEvent);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    emit editRequested();
}

ChipRow::ChipRow(QWidget* pParent)
: QWidget(pParent)
{
    mpFlow = new FlowLayout(this, scmChipSpacingAcross, scmChipSpacingDown);
    mpFlow->setContentsMargins(0, 0, 0, 0);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    mpAdd = new QToolButton(this);
    mpAdd->setObjectName(qsl("editorChipAdd"));
    //: Button at the end of a script's row of event chips that opens a field for a new event name
    mpAdd->setText(tr("Add event"));
    //: Tooltip on the button that opens the field for another event a script should listen for
    mpAdd->setToolTip(utils::richText(tr("Listen for another event")));
    mpAdd->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    remeasure();
    connect(mpAdd, &QAbstractButton::clicked, this, &ChipRow::beginAdd);

    mpField = new QLineEdit(this);
    mpField->setObjectName(qsl("editorChipEditor"));
    //: Placeholder in the field a new event name for a script is typed into
    mpField->setPlaceholderText(tr("event name"));
    mpField->installEventFilter(this);
    mpField->hide();
    connect(mpField, &QLineEdit::textEdited, this, [this](const QString& text) {
        // A comma is how a list is written out, so typing one says the name
        // before it is finished
        const int comma = text.indexOf(QLatin1Char(','));
        if (comma < 0) {
            return;
        }
        const QString rest = text.mid(comma + 1);
        mpField->setText(text.left(comma));
        commitField(true);
        if (mFieldOpen) {
            mpField->setText(rest);
        }
    });

    mpNote = new QLabel(this);
    mpNote->setObjectName(qsl("editorChipNote"));
    mpNote->hide();

    mpNoteTimer = new QTimer(this);
    mpNoteTimer->setObjectName(qsl("editorChipNoteTimer"));
    mpNoteTimer->setSingleShot(true);
    mpNoteTimer->setInterval(scmChipNoteMilliseconds);
    connect(mpNoteTimer, &QTimer::timeout, this, &ChipRow::hideNote);

    rebuild();
}

QStringList ChipRow::items() const
{
    QStringList names;
    names.reserve(mChips.size());
    for (const Chip* pChip : mChips) {
        names << pChip->name();
    }
    return names;
}

void ChipRow::setItems(const QStringList& items)
{
    closeField();
    hideNote();
    qDeleteAll(mChips);
    mChips.clear();
    QStringList taken;
    for (const QString& name : items) {
        // A name with nothing in it is not a chip, and one that is already
        // there would be a second box saying the same word - neither of which
        // the user could have typed here, but a profile file can carry either
        const QString shown = cleaned(name);
        if (shown.isEmpty() || taken.contains(shown)) {
            continue;
        }
        taken << shown;
        mChips.append(makeChip(shown));
    }
    rebuild();
}

int ChipRow::count() const
{
    return mChips.size();
}

QWidget* ChipRow::chipAt(const int index) const
{
    return index >= 0 && index < mChips.size() ? mChips.at(index) : nullptr;
}

int ChipRow::indexOf(const QString& name) const
{
    // What was handed to setItems() is not what a chip ended up showing, so the
    // name looked for is put through the same trim the chips were
    const QString wanted = cleaned(name);
    for (int i = 0; i < mChips.size(); ++i) {
        if (mChips.at(i)->name() == wanted) {
            return i;
        }
    }
    return -1;
}

void ChipRow::focusItem(const int index)
{
    if (Chip* pChip = qobject_cast<Chip*>(chipAt(index)); pChip) {
        pChip->setFocus(Qt::OtherFocusReason);
    }
}

void ChipRow::beginAdd()
{
    openField(-1);
}

int ChipRow::lineHeight() const
{
    return chipHeightOn(this);
}

QSize ChipRow::sizeHint() const
{
    const QSize hint = QWidget::sizeHint();
    // The height the chips have actually wrapped to at the width the row was
    // last given. Whatever holds the row reads its hint rather than asking for
    // a height at a width, so the wrap has to be in the hint or a second line
    // of chips is drawn over whatever is under it.
    if (width() <= 0) {
        return hint;
    }
    return QSize(hint.width(), std::max(0, heightForWidth(width())));
}

bool ChipRow::hasHeightForWidth() const
{
    return true;
}

int ChipRow::heightForWidth(int width) const
{
    return mpFlow->totalHeightForWidth(width);
}

void ChipRow::remeasure()
{
    mpAdd->setFont(chipFont(this));
    mpAdd->setFixedHeight(chipHeightOn(this));
    const int glyphSize = chipGlyphSizeOn(this);
    mpAdd->setIconSize(QSize(glyphSize, glyphSize));
    for (Chip* pChip : mChips) {
        pChip->remeasure();
    }
    updateGeometry();
}

void ChipRow::changeEvent(QEvent* pEvent)
{
    QWidget::changeEvent(pEvent);
    if (pEvent->type() == QEvent::FontChange) {
        remeasure();
    }
}

void ChipRow::resizeEvent(QResizeEvent* pEvent)
{
    QWidget::resizeEvent(pEvent);
    if (pEvent->oldSize().width() != pEvent->size().width()) {
        // A different width is a different number of lines, and the hint above
        // only changes once whatever holds the row has been told to ask again
        updateGeometry();
    }
}

bool ChipRow::eventFilter(QObject* pWatched, QEvent* pEvent)
{
    if (pWatched != mpField) {
        return QWidget::eventFilter(pWatched, pEvent);
    }
    if (pEvent->type() == QEvent::KeyPress) {
        auto* pKey = static_cast<QKeyEvent*>(pEvent);
        switch (pKey->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            commitField(true);
            return true;
        case Qt::Key_Escape:
            closeField();
            return true;
        default:
            break;
        }
    } else if (pEvent->type() == QEvent::FocusOut && mFieldOpen && !mCommitting) {
        // Clicking away from a name that was typed keeps it; clicking away from
        // an empty field is the user giving up on it
        if (cleaned(mpField->text()).isEmpty()) {
            closeField();
        } else {
            commitField(false);
        }
    }
    return QWidget::eventFilter(pWatched, pEvent);
}

Chip* ChipRow::makeChip(const QString& name)
{
    auto* pChip = new Chip(name, this);
    pChip->setRemoveGlyph(mRemoveGlyph);
    connect(pChip, &Chip::removeRequested, this, [this, pChip]() {
        removeAt(mChips.indexOf(pChip));
    });
    connect(pChip, &Chip::editRequested, this, [this, pChip]() {
        openField(mChips.indexOf(pChip));
    });
    return pChip;
}

void ChipRow::rebuild()
{
    while (QLayoutItem* pItem = mpFlow->takeAt(0)) {
        delete pItem;
    }

    bool fieldPlaced = false;
    for (int i = 0; i < mChips.size(); ++i) {
        Chip* pChip = mChips.at(i);
        const bool standingIn = mFieldOpen && i == mEditingIndex;
        if (standingIn) {
            mpFlow->addWidget(mpField);
            fieldPlaced = true;
        }
        // The chip being renamed stays in the layout and is skipped there,
        // rather than being taken out and put back
        pChip->setVisible(!standingIn);
        mpFlow->addWidget(pChip);
    }
    if (mFieldOpen && !fieldPlaced) {
        mpFlow->addWidget(mpField);
    }
    mpFlow->addWidget(mpAdd);
    mpFlow->addWidget(mpNote);

    mpField->setVisible(mFieldOpen);
    mpAdd->setVisible(!mFieldOpen);

    // Tab walks the names in the order they are read in and ends on the button
    // that adds another, which is where the chips themselves end
    QWidget* pPrevious = nullptr;
    for (Chip* pChip : mChips) {
        if (pPrevious) {
            setTabOrder(pPrevious, pChip);
        }
        pPrevious = pChip;
    }
    if (pPrevious) {
        setTabOrder(pPrevious, mpAdd);
    }

    // The row is a different set of things at a different height, and neither
    // the layout nor anything above it knows that until it is told
    mpFlow->invalidate();
    updateGeometry();
}

void ChipRow::openField(const int index)
{
    hideNote();
    mEditingIndex = index;
    mFieldOpen = true;
    const Chip* pChip = qobject_cast<Chip*>(chipAt(index));
    mpField->setText(pChip ? pChip->name() : QString());
    rebuild();
    mpField->setFocus(Qt::OtherFocusReason);
    mpField->selectAll();
}

void ChipRow::closeField(const bool keepNote)
{
    if (!mFieldOpen) {
        return;
    }
    mFieldOpen = false;
    mEditingIndex = -1;
    mpField->clear();
    if (!keepNote) {
        hideNote();
    }
    rebuild();
}

// stillTyping says which way the commit came: Return and a typed comma leave the
// user in the field, while losing the focus means they have gone somewhere else
// to do something else - and only the first of those keeps the field open.
void ChipRow::commitField(const bool stillTyping)
{
    if (!mFieldOpen || mCommitting) {
        return;
    }
    const QString name = cleaned(mpField->text());
    const int editing = mEditingIndex;
    Chip* pEdited = qobject_cast<Chip*>(chipAt(editing));

    if (name.isEmpty()) {
        closeField();
        return;
    }
    if (pEdited && pEdited->name() == name) {
        // The name came back unchanged, so there is nothing to tell anyone
        closeField();
        return;
    }
    for (int i = 0; i < mChips.size(); ++i) {
        if (i != editing && mChips.at(i)->name() == name) {
            showNote(name);
            emit duplicateRefused(name);
            if (!stillTyping) {
                // The user has gone elsewhere, so there is nobody left to type
                // the name over: leaving the field open would leave it standing
                // unfocused with the add button hidden behind it. The note keeps
                // its two seconds, which is what says why nothing was added.
                closeField(true);
            }
            return;
        }
    }

    mCommitting = true;
    if (pEdited) {
        pEdited->setName(name);
        mCommitting = false;
        closeField();
        emit itemsChanged();
        return;
    }

    mChips.append(makeChip(name));
    if (stillTyping) {
        // The field stays open on the name it just took: a script listening for
        // one event usually listens for a second, and reaching for the button
        // again between each is the whole of what made the old pair of controls
        // tedious
        mpField->clear();
        rebuild();
        mpField->setFocus(Qt::OtherFocusReason);
    } else {
        mFieldOpen = false;
        mEditingIndex = -1;
        mpField->clear();
        rebuild();
    }
    mCommitting = false;
    emit itemsChanged();
}

void ChipRow::removeAt(const int index)
{
    if (index < 0 || index >= mChips.size()) {
        return;
    }
    // The cross is not focusable, so an open field still holds the keyboard -
    // and the focus move below would take its FocusOut with it, committing what
    // was typed as a side effect of a click that asked for a removal. The field
    // is settled here instead, by the rule focus-out goes by: a name that was
    // typed is kept, an empty field is given up on. A rename settled this way
    // renames the chip it stands in for, which is not the one going away: the
    // commit leaves the list the same length, so the index below still holds.
    if (mFieldOpen) {
        if (cleaned(mpField->text()).isEmpty()) {
            closeField();
        } else {
            commitField(false);
        }
    }

    // The cross that asked for this is a child of the chip, and its own signal
    // is still on the stack - so the chip is taken out of the row now and freed
    // once that has unwound
    Chip* pGoing = mChips.takeAt(index);
    pGoing->hide();
    pGoing->deleteLater();
    rebuild();

    // Something has to hold the keyboard afterwards, or a run of Delete presses
    // stops after the first one
    mCommitting = true;
    if (!mChips.isEmpty()) {
        mChips.at(std::min(static_cast<qsizetype>(index), mChips.size() - 1))->setFocus(Qt::OtherFocusReason);
    } else if (!mFieldOpen) {
        mpAdd->setFocus(Qt::OtherFocusReason);
    }
    mCommitting = false;
    emit itemsChanged();
}

void ChipRow::showNote(const QString& name)
{
    //: Note beside a script's event field when the name typed is already one of its events
    mpNote->setText(tr("%1 is already listed").arg(name));
    mpNote->show();
    mpNoteTimer->start();
    rebuild();
    // The field stays open on what was refused, ready to be typed over
    mpField->selectAll();
}

void ChipRow::hideNote()
{
    mpNoteTimer->stop();
    if (!mpNote->isHidden()) {
        mpNote->hide();
        rebuild();
    }
}

QString ChipRow::cleaned(const QString& name)
{
    QString trimmed = name.trimmed();
    if (trimmed.endsWith(QLatin1Char(','))) {
        trimmed.chop(1);
    }
    return trimmed.trimmed();
}

void ChipRow::restyleGlyphs(const ThemeTokens& tokens)
{
    mRemoveGlyph = tintedIcon(qsl(":/icons/editor-clear.svg"), tokens);
    for (Chip* pChip : mChips) {
        pChip->setRemoveGlyph(mRemoveGlyph);
    }
    mpAdd->setIcon(tintedIcon(qsl(":/icons/editor-add.svg"), tokens));
}

QString ChipRow::styleSheetFor(const ThemeTokens& tokens)
{
    // A refusal is one of the three readings a state hue carries, and it is
    // written on the page the form is on rather than on a chip
    const QColor noteColor = readableOn(tokens.page, stateColor(scmStateHue_error, tokens.darkPage), tokens.text, scmTextMinimumRatio);
    return qsl("#editorChip { background-color: %1; border: 1px solid %2; border-radius: %3px; }"
               // What the user typed, at full strength...
               "#editorChip QLabel { background: transparent; color: %4; }"
               // ...and Mudlet's own events quieter than that, so a script's own
               // names read stronger than the ones it is only listening for
               "#editorChip[editorChipSystem=\"true\"] QLabel { color: %5; }"
               "#editorChip:focus { border: 1px solid %6; }"
               "#editorChipRemove { border: none; border-radius: %3px; background: transparent; padding: 0px; }"
               "#editorChipRemove:hover { background-color: %7; }"
               // Nothing is written in this one yet, which is what the dashed
               // edge says: the same box as a chip, drawn as an outline of one
               "#editorChipAdd { border: 1px dashed %2; border-radius: %3px; background: transparent; color: %5;"
               " padding: %8px %9px %8px %10px; }"
               "#editorChipAdd:hover { color: %4; border: 1px dashed %6; }"
               "#editorChipNote { background: transparent; color: %11; }")
            .arg(tokens.card.name(),
                 tokens.border.name(),
                 QString::number(scmRadiusChip),
                 tokens.text.name(),
                 tokens.mutedText.name(),
                 tokens.accent.name(),
                 tokens.hoverSoft,
                 QString::number(scmChipPaddingVertical),
                 QString::number(scmChipPaddingTrailing))
            .arg(QString::number(scmChipPaddingLeading), noteColor.name());
}

} // namespace uiDesign
