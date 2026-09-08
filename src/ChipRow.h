#ifndef MUDLET_CHIPROW_H
#define MUDLET_CHIPROW_H

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

#include <QColor>
#include <QFont>
#include <QFrame>
#include <QIcon>
#include <QList>
#include <QString>
#include <QStringList>

class QCompleter;
class QLabel;
class QLineEdit;
class QStandardItemModel;
class QTimer;
class QToolButton;

namespace uiDesign {

class FlowLayout;
class PlaceholderButton;
// Draws one offered name and the word beside it. Defined in ChipRow.cpp, since
// nothing outside the row has anything to say to it.
class SuggestionDelegate;
struct ThemeTokens;

// One name the field can offer, and the short word set beside it saying where
// the name is from. The word is whatever the caller wants read; the row only
// paints it.
struct Suggestion
{
    QString name;
    QString note;
};

// The type a word in a box is set in: the platform's fixed-width face, a shade
// smaller than the words around it. One recipe rather than one per chip family,
// so the ID beside an item's name and the events beside a script's read as the
// same kind of mark. Taken off the widget the chip lies on, because the window's
// own font is what a percentage in a stylesheet would have been relative to.
QFont chipFont(const QWidget* pOn);

// One name in a box, with the cross that takes it away again. Focusable, so a
// row of them can be walked and worked without the mouse.
class Chip : public QFrame
{
    Q_OBJECT

public:
    Q_DISABLE_COPY(Chip)
    Chip(const QString& name, QWidget* pParent);

    void setName(const QString& name);
    [[nodiscard]] QString name() const { return mName; }
    void setRemoveGlyph(const QIcon& glyph);
    // The type, the cross and the height a chip comes to, all of which are the
    // font's business. Public because a chip is measured off the row it lies on
    // rather than off its own font - see the note on the body - so the row is
    // what has to ask for this when that font changes.
    void remeasure();

signals:
    // The user asked to rename this one - by clicking it, or by pressing Enter
    // or F2 while it had the keyboard
    void editRequested();
    void removeRequested();

protected:
    void changeEvent(QEvent* pEvent) override;
    void keyPressEvent(QKeyEvent* pEvent) override;
    void mousePressEvent(QMouseEvent* pEvent) override;

private:
    QLabel* mpLabel = nullptr;
    QToolButton* mpRemove = nullptr;
    QString mName;
};

// A set of short names the user adds to and takes from: a script's events
// today. The names wrap onto as many lines as they need, and the row says so
// through its size hint, so whatever holds it can follow the height rather than
// scrolling it.
//
// Adding and renaming go through one field, which stands where the thing being
// typed will be: after the last chip when a name is being added, and in the
// chip's own place when one is being renamed.
class ChipRow : public QWidget
{
    Q_OBJECT

public:
    Q_DISABLE_COPY(ChipRow)
    explicit ChipRow(QWidget* pParent = nullptr);

    // The names as they stand, in the order they are shown
    [[nodiscard]] QStringList items() const;
    // Replaces the lot. This is the row being told what to show rather than the
    // user changing anything, so nothing is emitted and any open field is shut.
    void setItems(const QStringList& items);
    [[nodiscard]] int count() const;
    // The chip showing the name at that index, for a caller that has to point
    // at one. Null where there is no such index.
    [[nodiscard]] QWidget* chipAt(const int index) const;
    // Where the chip showing that name stands, or -1 for a name the row is not
    // showing. setItems() drops empty and repeated names, so a caller holding an
    // index into what it handed over cannot use it here - the name can.
    [[nodiscard]] int indexOf(const QString& name) const;
    // Hands that chip the keyboard. The row does not scroll, so there is
    // nothing to bring into view.
    void focusItem(const int index);
    // Opens the field for a name that is not there yet
    void beginAdd();

    // The names offered as one is typed into the field, matched anywhere in the
    // name without regard to case, each with the word set beside it. Whatever is
    // already a chip is left out of the offer, since adding it again would only
    // be refused.
    void setSuggestions(const QList<Suggestion>& suggestions);
    // The popup the suggestions are listed in is a window of its own, so the
    // sheet the form is styled by never reaches it - it is inked here instead
    void restyleSuggestions(const ThemeTokens& tokens);

    // What one line of chips is tall - the field the row types into stands on
    // the same line and is never cut down to a chip, so a chip grows to meet it
    // where the form's sheet makes the field the taller - so that whatever leads
    // the row can be set level with the first of them
    [[nodiscard]] int lineHeight() const;

    // Every colour a chip, the field and the note are drawn in, mixed at the
    // moment the window is styled. Appended to the sheet of the form the row is
    // on, so a theme change re-mixes them with everything else.
    [[nodiscard]] static QString styleSheetFor(const ThemeTokens& tokens);
    // The cross on every chip and the plus on the add button. Kept apart from
    // the sheet because a stylesheet can only point at a picture on disk, and
    // these are inked at runtime.
    void restyleGlyphs(const ThemeTokens& tokens);

    // The wrap is what makes the row's height its width's business, and these
    // are how whatever holds it is told so. QWidget declares all three public.
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] bool hasHeightForWidth() const override;
    [[nodiscard]] int heightForWidth(int width) const override;

signals:
    // After every add, rename and removal the user made - never after
    // setItems(), which is the row being filled in rather than edited
    void itemsChanged();
    // The name typed is already one of the chips, so nothing was added
    void duplicateRefused(const QString& name);

protected:
    void changeEvent(QEvent* pEvent) override;
    void resizeEvent(QResizeEvent* pEvent) override;
    bool eventFilter(QObject* pWatched, QEvent* pEvent) override;

private:
    Chip* makeChip(const QString& name);
    // The add button and every chip, measured off the row's own font - which is
    // what a chip's type is a shade smaller than
    void remeasure();
    // Puts the chips, the field and the add button in the order they are read
    // in, since a flow layout has no notion of inserting into the middle of one
    void rebuild();
    void openField(const int index);
    // keepNote leaves whatever the note is saying standing, for the one caller
    // that closes the field precisely because a name was refused
    void closeField(const bool keepNote = false);
    void commitField(const bool stillTyping);
    void removeAt(const int index);
    void showNote(const QString& name);
    void hideNote();
    // Built the first time names are handed over, since a row that is never
    // offered any has no popup to keep
    void makeCompleter();
    // Everything on offer bar the chips already showing, which is what the
    // popup is filled from every time the field opens
    void refreshSuggestionModel();
    // The popup's type and the width it asks for, both the chip font's business
    void measureSuggestions();
    // The word beside a name, in the two inks it is read in: a resting row and
    // the chosen one, which is washed in the accent
    void inkSuggestionNotes();
    // Trimmed of what surrounds it and of the comma that may have ended it
    [[nodiscard]] static QString cleaned(const QString& name);

    FlowLayout* mpFlow = nullptr;
    QList<Chip*> mChips;
    PlaceholderButton* mpAdd = nullptr;
    QLineEdit* mpField = nullptr;
    QLabel* mpNote = nullptr;
    QTimer* mpNoteTimer = nullptr;
    QCompleter* mpCompleter = nullptr;
    QStandardItemModel* mpSuggestionModel = nullptr;
    SuggestionDelegate* mpSuggestionDelegate = nullptr;
    // Every name on offer, in the order they were handed over
    QList<Suggestion> mSuggestions;
    // The popup's sheet and the notes' two inks, all kept for a restyle that
    // came before there was a popup to put them on
    QString mSuggestionSheet;
    QColor mSuggestionNote;
    QColor mSuggestionNoteChosen;
    QIcon mRemoveGlyph;
    // Which chip the open field stands in the place of; -1 while it is a name
    // being added rather than one being changed
    int mEditingIndex = -1;
    bool mFieldOpen = false;
    // Losing focus commits, and committing moves the focus - so the commit is
    // barred from starting itself a second time on the way out
    bool mCommitting = false;
};

} // namespace uiDesign

#endif // MUDLET_CHIPROW_H
