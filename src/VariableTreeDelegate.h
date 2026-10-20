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

// Written when a row is built or written back, so painting never reaches into Lua.
// Qt::UserRole stays the Lua value type: too much of the Variables view reads it by that number.
inline constexpr int scmRole_variablePreview = Qt::UserRole + 1;
inline constexpr int scmRole_variableKeyIsIndex = Qt::UserRole + 2;
inline constexpr int scmRole_variableHidden = Qt::UserRole + 3;

class VariableTreeDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    Q_DISABLE_COPY(VariableTreeDelegate)
    explicit VariableTreeDelegate(TTreeWidget* pTree);

    [[nodiscard]] static QString variablePreview(TVar* pVar);
    static void setVariableRowData(QTreeWidgetItem* pItem, TVar* pVar, const bool hidden);

    void restyle();

    void initStyleOption(QStyleOptionViewItem* pOption, const QModelIndex& index) const override;
    void paint(QPainter* pPainter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool editorEvent(QEvent* pEvent, QAbstractItemModel* pModel, const QStyleOptionViewItem& option, const QModelIndex& index) override;
    bool eventFilter(QObject* pWatched, QEvent* pEvent) override;
    bool helpEvent(QHelpEvent* pEvent, QAbstractItemView* pView, const QStyleOptionViewItem& option, const QModelIndex& index) override;

    // Viewport coordinates. Public for the tests that aim a synthesized click;
    // the editor's events carry their own option.
    [[nodiscard]] QRect keptHitRect(const QModelIndex& index) const;
    [[nodiscard]] QRect chevronHitRect(const QModelIndex& index) const;

    // Drawn only: the row's own text stays the bare number, since the editor
    // finds the variable by it
    [[nodiscard]] QString displayNameFor(const QModelIndex& index) const;
    [[nodiscard]] QString typeGlyphFile(const QModelIndex& index) const;
    [[nodiscard]] QColor rowInk(const QModelIndex& index, const bool selected) const;
    [[nodiscard]] bool carriesHiddenMark(const QModelIndex& index) const;

signals:
    // The row is the tree's current item by now. The editor applies the toggle,
    // since VarUnit decides which of a table's members may be kept.
    void keptToggleRequested();

private:
    enum class KeptState { None = 0, Off = 1, Some = 2, All = 3 };
    static constexpr int scmKeptStateCount = 4;

    enum class ChevronState { None = 0, Closed = 1, Open = 2 };

    enum class TypeMark { None = 0, Table = 1, String = 2, Number = 3, Boolean = 4, Function = 5, Other = 6 };
    static constexpr int scmTypeMarkCount = 7;

    struct RowState
    {
        bool known = false;
        KeptState kept = KeptState::None;
        TypeMark type = TypeMark::None;
        bool unavailable = false;
    };

    struct Decoration
    {
        QIcon icon;
        QSize size;
    };

    // Shared by paint() and helpEvent(), so a tooltip names what is drawn under the pointer
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
    [[nodiscard]] QStyleOptionViewItem rowOption(const QModelIndex& index) const;

    void syncGlyphRatio() const;
    // Checked on paint, not in restyle(): an interface font change comes without a theme change
    void syncPreviewFont() const;
    [[nodiscard]] qreal glyphRatio() const;
    void clearGlyphs() const;

    [[nodiscard]] QPixmap keptGlyph(const KeptState kept, const bool selected) const;
    [[nodiscard]] QPixmap chevronGlyph(const ChevronState chevron) const;
    [[nodiscard]] QPixmap typeGlyph(const TypeMark type, const QColor& ink, const int cacheSlot) const;
    [[nodiscard]] QPixmap hiddenGlyph(const QColor& ink, const int cacheSlot) const;
    [[nodiscard]] int inkSlotFor(const RowState& state, const bool selected) const;
    [[nodiscard]] QColor inkForSlot(const int slot) const;
    [[nodiscard]] Decoration decorationFor(const RowState& state, const int level, const ChevronState chevron, const bool selected) const;
    [[nodiscard]] QString keptTooltip(const QModelIndex& index) const;
    // The option must already have been through initStyleOption()
    [[nodiscard]] RowLayout layoutOf(const QStyleOptionViewItem& option, const QModelIndex& index) const;

    TTreeWidget* mpTree = nullptr;
    int mIndentStep = 0;
    QColor mChromeInk;
    QColor mSelectedInk;
    QColor mDisabledInk;
    QColor mKeptFill;
    QColor mAccentBar;

    mutable QFont mPreviewFont;
    mutable QFontMetrics mPreviewMetrics{QFont()};
    mutable QFont mMeasuredFrom;
    mutable qreal mGlyphRatio = 0.0;

    static constexpr int scmInkCount = 3;
    mutable QPixmap mTypeGlyphs[scmInkCount * scmTypeMarkCount];
    mutable QPixmap mHiddenGlyphs[scmInkCount];
    mutable QPixmap mKeptGlyphs[2 * scmKeptStateCount];
    mutable QPixmap mChevronGlyphs[3];
    mutable QHash<int, Decoration> mDecorations;

    bool mPressAnswered = false;
    QPersistentModelIndex mLastKeptPressIndex;
};

} // namespace uiDesign

#endif // MUDLET_VARIABLETREEDELEGATE_H
