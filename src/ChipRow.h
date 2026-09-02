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

#include <QFont>
#include <QFrame>
#include <QIcon>
#include <QList>
#include <QString>
#include <QStringList>

class QLabel;
class QLineEdit;
class QTimer;
class QToolButton;

namespace uiDesign {

class FlowLayout;
struct ThemeTokens;

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

signals:
    // The user asked to rename this one - by clicking it, or by pressing Enter
    // or F2 while it had the keyboard
    void editRequested();
    void removeRequested();

protected:
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
    // Hands that chip the keyboard. The row does not scroll, so there is
    // nothing to bring into view.
    void focusItem(const int index);
    // Opens the field for a name that is not there yet
    void beginAdd();

    // What one line of chips is tall, so that whatever leads the row can be set
    // level with the first of them
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
    void resizeEvent(QResizeEvent* pEvent) override;
    bool eventFilter(QObject* pWatched, QEvent* pEvent) override;

private:
    Chip* makeChip(const QString& name);
    // Puts the chips, the field and the add button in the order they are read
    // in, since a flow layout has no notion of inserting into the middle of one
    void rebuild();
    void openField(const int index);
    void closeField();
    void commitField(const bool stillTyping);
    void removeAt(const int index);
    void showNote(const QString& name);
    void hideNote();
    // Trimmed of what surrounds it and of the comma that may have ended it
    [[nodiscard]] static QString cleaned(const QString& name);

    FlowLayout* mpFlow = nullptr;
    QList<Chip*> mChips;
    QToolButton* mpAdd = nullptr;
    QLineEdit* mpField = nullptr;
    QLabel* mpNote = nullptr;
    QTimer* mpNoteTimer = nullptr;
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
