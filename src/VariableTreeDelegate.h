#ifndef MUDLET_VARIABLETREEDELEGATE_H
#define MUDLET_VARIABLETREEDELEGATE_H

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
#include <QFontMetrics>
#include <QHash>
#include <QIcon>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <QStyledItemDelegate>

class QTreeWidgetItem;
class TTreeWidget;
class TVar;

namespace uiDesign {

// What a row of the Variables view carries besides its name. Written when the
// row is built and refreshed when the variable behind it is written back, so
// that painting a row never reaches into Lua's tree or counts a table's members
// - a tree of several thousand rows is laid out and scrolled through the same
// data every other view is.
//
// Qt::UserRole is already the row's Lua value type and stays where it is: too
// much of the Variables view reads it by that number.
inline constexpr int scmRole_variablePreview = Qt::UserRole + 1;
inline constexpr int scmRole_variableKeyIsIndex = Qt::UserRole + 2;
inline constexpr int scmRole_variableHidden = Qt::UserRole + 3;

// The seventh tree drawn as the other six are. A row leads with the square that
// says whether the variable is kept with the profile, then the mark that says
// what kind of value it holds, then its name - and, at the trailing edge, as
// much of the value itself as the panel's width leaves room for.
//
// The kept square is a switch rather than a picture, so the presses that land on
// it are answered here and seen through by a filter on the viewport, exactly as
// EditorTreeDelegate answers the presses on an item's state dot. What it asks
// for is a toggle rather than performing one: which of a table's members may be
// kept is VarUnit's business, and the editor is what holds it.
class VariableTreeDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    Q_DISABLE_COPY(VariableTreeDelegate)
    explicit VariableTreeDelegate(TTreeWidget* pTree);

    // What the trailing edge of a row says about the value behind it: a string
    // in quotes, a number as it stands, a table as how much is in it. Static
    // because it is written into the row wherever a row is built - the walk that
    // reads Lua's globals in, and the two places the editor rebuilds one row.
    [[nodiscard]] static QString variablePreview(TVar* pVar);
    // ...and all four of the things a row is drawn from, written at once
    static void setVariableRowData(QTreeWidgetItem* pItem, TVar* pVar, const bool hidden);

    // Re-read the colours off the application palette; called from the same pass
    // that restyles the rest of the editor
    void restyle();

    void initStyleOption(QStyleOptionViewItem* pOption, const QModelIndex& index) const override;
    void paint(QPainter* pPainter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool editorEvent(QEvent* pEvent, QAbstractItemModel* pModel, const QStyleOptionViewItem& option, const QModelIndex& index) override;
    bool eventFilter(QObject* pWatched, QEvent* pEvent) override;
    // The kept square says three different things, so what it says is the
    // tooltip while the pointer is on it rather than one line for the whole row
    bool helpEvent(QHelpEvent* pEvent, QAbstractItemView* pView, const QStyleOptionViewItem& option, const QModelIndex& index) override;

    // Where a row's kept square can be clicked, in the viewport's coordinates,
    // and where its chevron can be. The editor never asks - the events that need
    // these arrive with the option they are measured against. The tests that aim
    // a synthesized click at one do.
    [[nodiscard]] QRect keptHitRect(const QModelIndex& index) const;
    [[nodiscard]] QRect chevronHitRect(const QModelIndex& index) const;

    // A number key is a place in a list rather than a name, so it is drawn as
    // one. The row's own text stays the bare number: the editor reaches a
    // variable by it.
    [[nodiscard]] QString displayNameFor(const QModelIndex& index) const;
    // Which file the mark beside the name is cut from, empty for a row that
    // stands for no variable
    [[nodiscard]] QString typeGlyphFile(const QModelIndex& index) const;
    // What the row's name, mark and preview are all written in
    [[nodiscard]] QColor rowInk(const QModelIndex& index, const bool selected) const;
    [[nodiscard]] bool carriesHiddenMark(const QModelIndex& index) const;

signals:
    // Sent once the row to keep or stop keeping is the tree's current one, so
    // there is nothing to carry
    void keptToggleRequested();

private:
    // Whether the profile keeps this variable, in the three readings a row can
    // be in - and the fourth that says the row is not one the user may keep at
    // all, which is drawn as no square rather than as an empty one
    enum class KeptState { None = 0, Off = 1, Some = 2, All = 3 };
    static constexpr int scmKeptStateCount = 4;

    enum class ChevronState { None = 0, Closed = 1, Open = 2 };

    // What kind of value the row holds, which is the whole of what the mark
    // beside its name says
    enum class TypeMark { None = 0, Table = 1, String = 2, Number = 3, Boolean = 4, Function = 5, Other = 6 };
    static constexpr int scmTypeMarkCount = 7;

    // Everything a row's leading edge is composed from, read off the row itself
    struct RowState
    {
        // Whether the row stands for a variable at all - a tree's own heading
        // row does not
        bool known = false;
        KeptState kept = KeptState::None;
        TypeMark type = TypeMark::None;
        // A variable Lua will not let the profile keep is written in the tone an
        // unavailable word is written in, and carries no square
        bool unavailable = false;
    };

    struct Decoration
    {
        QIcon icon;
        QSize size;
    };

    // Where the three things after the row's leading edge end up: the name, the
    // mark that says the row is hidden, and the preview of the value. Worked out
    // once and used by both the painting and the tooltips, so that what the
    // pointer is told it is over is what is actually drawn there.
    struct RowLayout
    {
        QString name;
        int nameWidth = 0;
        QRect hidden;
        QString preview;
        QRect previewRect;
    };

    [[nodiscard]] RowState stateOf(const QModelIndex& index) const;
    [[nodiscard]] ChevronState chevronOf(const QModelIndex& index, const int level) const;
    [[nodiscard]] QRect keptHitRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;
    [[nodiscard]] QRect chevronHitRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;
    // What the view would have laid a row out from, for the two rectangles above
    // when they are asked for outside an event that carries one
    [[nodiscard]] QStyleOptionViewItem rowOption(const QModelIndex& index) const;

    void syncGlyphRatio() const;
    // The monospace face the preview is set in is measured off the tree, so a
    // change of interface font has to reach it without a theme change
    void syncPreviewFont() const;
    [[nodiscard]] qreal glyphRatio() const;
    void clearGlyphs() const;

    [[nodiscard]] QPixmap keptGlyph(const KeptState kept, const bool selected) const;
    [[nodiscard]] QPixmap chevronGlyph(const ChevronState chevron) const;
    [[nodiscard]] QPixmap typeGlyph(const TypeMark type, const QColor& ink, const int cacheSlot) const;
    [[nodiscard]] QPixmap hiddenGlyph(const QColor& ink, const int cacheSlot) const;
    // Which of the three inks a row is drawn in, as a slot in the glyph caches
    [[nodiscard]] int inkSlotFor(const RowState& state, const bool selected) const;
    [[nodiscard]] QColor inkForSlot(const int slot) const;
    [[nodiscard]] Decoration decorationFor(const RowState& state, const int level, const ChevronState chevron, const bool selected) const;
    [[nodiscard]] QString keptTooltip(const QModelIndex& index) const;
    // Measured from an option that has already been through initStyleOption()
    [[nodiscard]] RowLayout layoutOf(const QStyleOptionViewItem& option, const QModelIndex& index) const;

    TTreeWidget* mpTree = nullptr;
    // What the view was indenting each level by before it was asked to stop, so
    // that a row sits exactly where it used to and the chevron stands where the
    // branch arrow stood
    int mIndentStep = 0;
    QColor mChromeInk;
    QColor mSelectedInk;
    QColor mDisabledInk;
    // The green "on" is read as everywhere else in the editor, which is what
    // says a variable is kept
    QColor mKeptFill;
    QColor mAccentBar;

    mutable QFont mPreviewFont;
    mutable QFontMetrics mPreviewMetrics{QFont()};
    mutable QFont mMeasuredFrom;
    mutable qreal mGlyphRatio = 0.0;

    // Three inks and seven readings is every type mark a tree of any size can
    // ask for; the same three for the mark that says a row is hidden
    static constexpr int scmInkCount = 3;
    mutable QPixmap mTypeGlyphs[scmInkCount * scmTypeMarkCount];
    mutable QPixmap mHiddenGlyphs[scmInkCount];
    mutable QPixmap mKeptGlyphs[2 * scmKeptStateCount];
    mutable QPixmap mChevronGlyphs[3];
    mutable QHash<int, Decoration> mDecorations;

    // Set by a press the square or the chevron answered and cleared by the
    // release that closes it, wherever that release lands
    bool mPressAnswered = false;
    // The row of the press the square answered, which is what the second press
    // of a double click is read against
    QPersistentModelIndex mLastKeptPressIndex;
};

} // namespace uiDesign

#endif // MUDLET_VARIABLETREEDELEGATE_H
