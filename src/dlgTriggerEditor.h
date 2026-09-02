#ifndef MUDLET_DLGTRIGGEREDITOR_H
#define MUDLET_DLGTRIGGEREDITOR_H

/***************************************************************************
 *   Copyright (C) 2008-2012 by Heiko Koehn - KoehnHeiko@googlemail.com    *
 *   Copyright (C) 2014 by Ahmed Charles - acharles@outlook.com            *
 *   Copyright (C) 2017-2020 by Ian Adkins - ieadkins@gmail.com            *
 *   Copyright (C) 2015-2018, 2020, 2022-2023 by Stephen Lyons             *
 *                                               - slysven@virginmedia.com *
 *   Copyright (C) 2023 by Lecker Kebap - Leris@mudlet.org                 *
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


#include "EditorCommand.h"
#include "ui_trigger_editor.h"

#include <QPointer>
#include <unordered_map>

#include "TAction.h"
#include "TAlias.h"
#include "TKey.h"
#include "TScript.h"
#include "TTimer.h"
#include "TTreeWidget.h"
#include "TTrigger.h"
#include "TVar.h"
#include "dlgSourceEditorArea.h"
#include "dlgSourceEditorFindArea.h"
#include "dlgSystemMessageArea.h"
#include "dlgTimersMainArea.h"
#include "dlgTriggersMainArea.h"
#include "dlgVarsMainArea.h"
#include "SingleLineTextEdit.h"
#include "EditorPlaceholderButton.h"
#include "EditorUndoStack.h"

#include <QDialog>
#include <QDockWidget>
#include <QFlag>
#include <QHash>
#include <QIcon>
#include <QKeySequence>
#include <QPixmap>
#include <QListWidgetItem>
#include <QScrollArea>
#include <QTreeWidget>
#include <QDesktopServices>
#include <QSet>
#include <QStringList>
#include <QVector>

// Edbee editor includes
#include "edbee/edbee.h"
#include "edbee/models/changes/mergablechangegroup.h"
#include "edbee/models/chardocument/chartextdocument.h"
#include "edbee/models/textdocument.h"
#include "edbee/models/texteditorconfig.h"
#include "edbee/models/textgrammar.h"
#include "edbee/models/textundostack.h"
#include "edbee/models/textautocompleteprovider.h"
#include "edbee/texteditorcommand.h"
#include "edbee/texteditorcontroller.h"
#include "edbee/texteditorwidget.h"
#include "edbee/views/components/texteditorcomponent.h"
#include "edbee/views/textselection.h"

#include "edbee/models/textsearcher.h" // These three are required for search highlighting
#include "edbee/views/texttheme.h"
#include "edbee/views/textrenderer.h"

class dlgTimersMainArea;
class dlgSystemMessageArea;
class dlgSourceEditorArea;
class dlgSourceEditorFindArea;
class dlgTriggersMainArea;
class dlgActionMainArea;
class dlgSearchArea;
class dlgAliasMainArea;
class dlgScriptsMainArea;
class dlgKeysMainArea;
class dlgTriggerPatternEdit;
class QComboBox;
class QLabel;
class QFrame;
class QListWidget;
class QRadioButton;
class QSpinBox;
class QToolButton;
class TAction;
class TKey;
class TVar;
class TConsole;
class dlgVarsMainArea;
class QShortcut;

namespace uiDesign {
class ChipRow;
class EditorSidebarToggle;
class EditorTreeDelegate;
class SearchResultDelegate;
class VariableTreeDelegate;
}


class dlgTriggerEditor : public QMainWindow, private Ui::trigger_editor
{
    Q_OBJECT

    // Allow QTest-based test classes to access private members
    friend class dlgTriggerEditorUndoRedoTest;
    friend class EditorBannerViewSwitchTest;
    friend class EditorCaretReadingTest;
    friend class EditorChromeInkTest;
    friend class EditorChromeShapeTest;
    friend class EditorCodeHeadingTest;
    friend class EditorColumnAlignmentTest;
    friend class EditorColumnFontTest;
    friend class EditorEventChipRowTest;
    friend class EditorFormShellTest;
    friend class EditorIconScaleTest;
    friend class EditorKeyCaptureTest;
    friend class EditorKeyGrabShortcutsTest;
    friend class EditorMinimumSizeTest;
    friend class EditorNoticeGlyphTest;
    friend class EditorOptionsPanelDefaultTest;
    friend class EditorSidebarCollapseTest;
    friend class EditorSplitterRestoreTest;
    friend class EditorStatusBarTest;
    friend class EditorSurfaceToneTest;
    friend class EditorTabAtStartTest;
    friend class EditorTimerIntervalTest;
    friend class EditorToolBarOverflowTest;
    friend class EditorTreeDotClickTest;
    friend class EditorTreeHeadingIconTest;
    friend class EditorTreeRowHeightTest;
    friend class EditorTreeSelectionPillTest;
    friend class EditorVariablesFormTest;
    friend class EditorVariablesTreeTest;
    friend class ReadabilityAuditTest;
    friend class ScriptEventHandlerLifetimeTest;
    friend class VariableEditorWriteBackTest;

    enum SearchDataRole {
        // Value is the ID of the item found MUST BE Qt::UserRole to avoid
        // having to modify existing code that puts it into the item:
        IdRole = Qt::UserRole,
        // Was the "name" field inserted into the search widget tree {as
        // pItem->text(1)} but since we now suppress that for subsequent
        // elements for the same "item" we need to carry the same data
        // internally even when we do not insert the text in the display:
        NameRole = Qt::UserRole + 1,
        // What the Item is (one of the cmXxxxxView values) so we know how to
        // interpret the search result:
        ItemRole = Qt::UserRole + 2,
        // Value of one of SearchDataResultType (below)
        TypeRole = Qt::UserRole + 3,
        // When the result is a pattern or event handler ("Script" item) type or
        // lua script this is the pattern number (0-49 for "Triggers"), (event
        // handler index for "Scripts") or script line (so we know which
        // field/line to jump to)
        PatternOrLineRole = Qt::UserRole + 4,
        // Value is the position (starting at 0, counting in QChars) of the
        // particular find used to position cursor at start of match:
        PositionRole = Qt::UserRole + 5,
        // Value is the index (starting at 0) of the particular find used to
        // disambiguate multiple finds in the same "thing" (so we know which one
        // to jump to) - may not be as much use as it seems...
        IndexRole = Qt::UserRole + 6
    };

    // Classify the search result - so we know where to position the cursor as
    // we implement moving the focus to the origin of the result:
    enum SearchDataResultType {
        // Unset (?):
        SearchResultIsUnknown = 0x0,
        // The contents in the Edbee Editor widget:
        SearchResultIsScript = 0x1,
        // The item's "Name":
        SearchResultIsName = 0x2,
        // Only for "Triggers"/"Aliases" (and only the former has multiples):
        SearchResultIsPattern = 0x3,
        // All but "Variable" - the simple "Command":
        SearchResultIsCommand = 0x4,
        // Only Push-down "Buttons" - the additional "Up" "Command" field:
        SearchResultIsExtraCommand = 0x5,
        // Only "Buttons" - "Css" - unlikely to be useful currently but might be
        //useful in future if we really get into stylesheets:
        SearchResultsIsCss = 0x6,
        // Only "Scripts":
        SearchResultIsEventHandler = 0x7,
        // Only "Variables":
        SearchResultIsValue = 0x8
    };

public:
    // This needs to be public so that the options can be used from the Host class:
    enum SearchOption {
        // Unset:
        SearchOptionNone = 0x0,
        SearchOptionCaseSensitive = 0x1,
        SearchOptionIncludeVariables = 0x2,
        SearchOptionWholeWord = 0x4 /*,
        SearchOptionRegExp = 0x8 */
    };

    Q_DISABLE_COPY(dlgTriggerEditor)
    dlgTriggerEditor(Host*);
    ~dlgTriggerEditor();

    Q_DECLARE_FLAGS(SearchOptions, SearchOption)

    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void focusInEvent(QFocusEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void showEvent(QShowEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void enterEvent(TEnterEvent* event) override;
    bool eventFilter(QObject*, QEvent* event) override;
    bool event(QEvent* event) override;
    // Hide QWidget::setFont() and QWidget::setStyleSheet(): a font has to be carried past the stylesheets in the window, see StyleSheetFontLift
    void setFont(const QFont& font);
    void setStyleSheet(const QString& styleSheet);
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* e) override;
    void updateExtraControlsToggleIcon();
    // The editor's own look, derived from the application palette rather than
    // written out, so that it follows a theme change
    void applyEditorShellStyle();
    void restyleEditorIcons();
    void applyEditorToolbarButtonStyles();
    // Keeps every action on the bar reachable at any length the bar can be
    // given: its groups give their names up in turn as the room runs out and
    // come back with room to spare - see the implementation for the order
    void fitEditorToolBarToItsLength();
    // The four buttons whose wording follows the view say that wording in their
    // tooltip, which is all a button that has given its name up has left
    void updateEditorItemActionToolTips();
    // The panel down the left: what its trees draw a row as, and where the
    // search field sits over them
    void setupEditorPanel();
    // The heading the Lua editor is under, which is also the handle the code
    // pane is resized by
    void setupEditorCodeHeader();
    // What that heading names: a Lua script everywhere but the variables view,
    // where the pane under it holds the chosen variable's value
    [[nodiscard]] QString codeHeaderTitleFor(const EditorViewType view) const;
    // What the strip says about the last save of the item in it, and what it is
    // written in
    void updateEditorCodeHeading();
    // The compiler's sentence, cut to the room the strip has left for it
    void elideCompileNote();
    // Where the note leads, whether it was clicked or reached with the keyboard
    void jumpToCompileErrorLine();
    // The heading speaks for the item in the editor, so switching items puts it
    // back to saying nothing
    void clearCompileState();
    // Bracket a save so that showError() is heard by the heading for as long as
    // it runs, and by nothing else the rest of the time
    void beginSaveErrorCapture();
    void endSaveErrorCapture();
    // The trigger form's options, moved out of the .ui column of group boxes
    // and into a column of cards, with a strip that says what they hold while
    // they are put away
    void buildTriggerOptionsPanel();
    // The five forms that are a fixed set of fields, shelled over their .ui
    // grids: a head row of the name, whatever is typed beside it and the ID
    // pill, with what is left of the grid under it
    void buildEditorFormHeadRows();
    // The events a script is registered for, as a row of chips in the cell
    // beside the "Events" label the .ui file leaves there
    void buildScriptEventRow();
    // A timer's interval as one sentence rather than as a wall clock: the four
    // fields the .ui file holds, laid into the words that say what they are
    void buildTimerIntervalRow();
    // ...and which of the two sentences those words are, which is the whole of
    // the difference between a timer and one offset from the timer above it
    void showTimerIntervalSentence(const bool offsetTimer);
    // ...taken from the timer on show, for whatever moved it rather than chose
    // it: which kind of timer it is, is its parent's business
    void refreshShownTimerIntervalSentence();
    // The keystroke a key is bound to, as a field that listens for it: the hint
    // beside it and the button that forgets the keystroke are built here
    void buildKeyBindingRow();
    // A variable's key and value types as one row under the head row, with the
    // switch that keeps it out of the tree on the row under that
    void buildVariableTypeRows();
    // The field, the hint and the clear button drawn from what the key holds.
    // A group is offered none of them - TKey::match() never matches one.
    void showKeyBinding();
    // ...and the same row while it is waiting for the keystroke to be pressed
    void showKeyBindingListening();
    // Every way out of a grab: a keystroke taken, Escape, the field losing the
    // keyboard, a toolbar button, another view, or the editor going away. A
    // grab takes the editor's shortcuts away and puts a filter on the
    // application, so nothing may end one without undoing both.
    void endKeyGrab();
    // ...which is this, and then the row drawn again from what the key holds.
    // showKeyBinding() calls this rather than endKeyGrab() so that drawing the
    // row also ends a grab, without the two calling each other in a circle.
    void releaseKeyGrab();
    // A click on the field arms the grab, Return or Space does the same from
    // the keyboard, and losing the focus gives up on it
    bool handleKeyBindingFieldEvent(QEvent* pEvent);
    [[nodiscard]] QList<QLabel*> editorFormRowLabels() const;
    [[nodiscard]] QList<QLabel*> editorFormLeadLabels() const;
    // One width for the words leading those forms' rows, so a field starts at
    // the same place whichever row of whichever form it is on
    void alignEditorFormLeadLabels();
    // The one way the panel is opened or closed on purpose, so that both the
    // Options button and the summary strip persist the preference
    void setTriggerOptionsShown(const bool shown);
    // spinBox_lineMargin stays what the save and load paths read; the radio
    // pair is a view of it
    void reflectTriggerMatchMode();
    void restyleTriggerMatchModeChips();
    void updateTriggerOptionsSummary();
    // Opening the panel where the form has no room for it borrows the height
    // from the code pane, and closing it hands that height back
    void refitSplitterForTriggerOptions(const bool shown);
    // What the form column asks for as it stands, floored so the code pane
    // keeps its own minimum whatever the form wants
    int formPaneHeightForItsContents(const int paneTotal) const;
    // The form pane takes the height its contents ask for, unless this view's
    // splitter has been dragged in this session - then it takes that height
    void fitFormPaneToItsContents();
    // Whether the code pane's heading drags the seam in this view, and the
    // column's height when it does not
    void applyFormPaneSeamPolicy();
    void holdFormPaneToItsContents();
    [[nodiscard]] QWidget* currentFormArea() const;
    void updateEditorItemCounts();
    void scheduleEditorItemCountUpdate();
    // The list down the left that switches between what the editor is showing
    void buildEditorSidebar();
    void addEditorSidebarRow(QAction* pAction, const EditorViewType view, const QString& iconFile);
    void addEditorSidebarSeparator();
    void restyleEditorSidebarIcons(const QColor& normal, const QColor& selected);
    // ...and the heading row of each of the seven trees, which carries the same
    // glyph as the sidebar row that opens it
    void restyleEditorTreeHeadingIcons();
    // ...and what a row of the variables tree is drawn from, which is written
    // into the row rather than read off Lua while it is painted
    void refreshVariableRow(QTreeWidgetItem* pItem);
    // ...and how many globals the switch over that tree would bring into it
    void updateHiddenVariablesCount();
    // The toolbar's leading button, and the one place the preference it carries
    // is changed
    void updateEditorSidebarToggle();
    void setEditorSidebarLabelsShown(const bool shown);
    void syncEditorSidebarSelection();
    struct EditorSidebarWidths
    {
        int expanded = 0;
        int collapseBelow = 0;
    };
    [[nodiscard]] EditorSidebarWidths editorSidebarWidths() const;
    // Called wherever the rows, the font or the style they are measured against
    // change, since none of those reach a resize
    void invalidateEditorSidebarWidths();
    void updateEditorSidebarMode();
    // Gives the panel's focus to one of the item trees, or to the search
    // results while those are what the panel is showing instead
    void focusPanelTree(QWidget* pTreeWidget);
    void fillout_form();
    void showError(const QString&);
    void showWarning(const QString&, bool announce = true);
    // The one place the "this item is part of a package" banner is raised from,
    // and what remembers which packages have already had their say
    void showPackageWarning(const QString& packageName, QTreeWidgetItem* pItem = nullptr);
    void showInfo(const QString&);
    void children_icon_triggers(QTreeWidgetItem* pWidgetItemParent);
    void children_icon_alias(QTreeWidgetItem* pWidgetItemParent);
    void children_icon_key(QTreeWidgetItem* pWidgetItemParent);
    void children_icon_timer(QTreeWidgetItem* pWidgetItemParent);
    void children_icon_script(QTreeWidgetItem* pWidgetItemParent);
    void children_icon_action(QTreeWidgetItem* pWidgetItemParent);
    void doCleanReset();
    void writeScript(int id);
    void addVar(bool);
    int canRecast(QTreeWidgetItem*, int newNameType, int newValueType);
    void saveVar();
    void showVariableRenameRefused(TVar*);
    void repopulateVars();
    void changeView(EditorViewType);
    void recurseVariablesUp(QTreeWidgetItem* const, QList<QTreeWidgetItem*>&);
    void recurseVariablesDown(QTreeWidgetItem* const, QList<QTreeWidgetItem*>&);
    void show_vars();
    void setThemeAndOtherSettings(const QString&);
    // Helper to ensure the foreground color for a button is always
    // readable/contrasts with the background when the latter is colored@
    static QString generateButtonStyleSheet(const QColor& color, const bool isEnabled = true);
    // Reader of the above - is a bit simple and may not work if the
    // stylesheetText has more that one item being styled with a "color" and
    // "background-color" attribute:
    static QColor parseButtonStyleSheetColors(const QString& styleSheetText, const bool isToGetForeground = false);
    void activeToggle_action();
    void activeToggle_alias();
    void activeToggle_key();
    void activeToggle_script();
    void activeToggle_timer();
    void activeToggle_trigger();
    void slot_itemMoved(int itemID, int oldParentID, int newParentID, int oldPosition, int newPosition);
    void slot_batchMoveStarted();
    void slot_batchMoveEnded();
    void delete_action();
    void delete_alias();
    void delete_key();
    void delete_script();
    void delete_timer();
    void delete_trigger();
    void delete_variable();
    void setSearchOptions(const SearchOptions);
    void setEditorShowBidi(const bool);
    void showCurrentTriggerItem();
    void hideSystemMessageArea();
    void showIDLabels(const bool);
    void setDisplayFont(const QFont&);

signals:
    void editorClosing();

public slots:
    void slot_toggleHiddenVariables(bool);
    void slot_hideVariable(bool);
    void slot_variableSelected(QTreeWidgetItem*);
    void slot_variableChanged(QTreeWidgetItem*);
    // What a click on the square at the head of a variable's row asks for
    void slot_toggleVariableKept();
    void slot_showVariables();
    void slot_viewErrorsAction();
    void slot_setupPatternControls(const int);
    void slot_soundTrigger();
    void slot_colorizeTriggerSetBgColor();
    void slot_colorizeTriggerSetFgColor();
    void slot_saveSelectedItem();
    void slot_export();
    void slot_import();
    void slot_createModule();
    void slot_viewStatsAction();
    void slot_toggleCentralDebugConsole();
    void slot_nextSection();
    void slot_previousSection();
    void slot_showTimers();
    void slot_showTriggers();
    void slot_showScripts();
    void slot_showAliases();
    void slot_showActions();
    void slot_showKeys();
    void slot_activateMainWindow();
    void slot_treeSelectionChanged();
    void slot_triggerSelected(QTreeWidgetItem* pItem);
    void slot_timerSelected(QTreeWidgetItem* pItem);
    void slot_scriptsSelected(QTreeWidgetItem* pItem);
    void slot_aliasSelected(QTreeWidgetItem* pItem);
    void slot_actionSelected(QTreeWidgetItem* pItem);
    void slot_keySelected(QTreeWidgetItem* pItem);
    void slot_addNewItem();
    void slot_addNewGroup();
    void slot_toggleItemOrGroupActiveFlag();
    void slot_searchMudletItems(const int);
    void slot_itemSelectedInSearchResults(QTreeWidgetItem*);
    void slot_deleteItemOrGroup();
    void slot_openSourceFind();
    void slot_closeSourceFind();
    void slot_sourceFindMove();
    void slot_sourceFindPrevious();
    void slot_sourceFindNext();
    void slot_sourceFindTextChanges();
    void slot_sourceReplace();
    void slot_saveEdits();
    void slot_copyXml();
    void slot_pasteXml();
    // Not used:    void slot_choseActionIcon();
    void slot_showAllTriggerControls(const bool);
    void slot_rightSplitterMoved(const int pos, const int handle);
    void slot_keyGrab();
    void slot_profileSaveAction();
    void slot_profileSaveAsAction();
    void slot_setToolBarIconSize(int);
    void slot_setTreeWidgetIconSize(int);
    void slot_colorTriggerFg();
    void slot_colorTriggerBg();
    void slot_updateCaretPosition(const QString& statusText); // For the source code editor
    // A click on the compile note, heard by the handle the strip is carried by
    void slot_codeHeadingClicked(QWidget* pPiece);
    void slot_profileSaveStarted();
    void slot_profileSaveFinished();
    void slot_editorThemeChanged();
    void slot_smartUndo();
    void slot_smartRedo();
    void slot_updateUndoRedoButtonStates();

private slots:
    void slot_editorSidebarRowChanged(const int row);
    void slot_editorSidebarItemActivated(QListWidgetItem* pItem);
    void slot_changeEditorTextOptions(QTextOption::Flags);
    void slot_toggleIsPushDownButton(int);
    void slot_toggleSearchCaseSensitivity(bool);
    void slot_toggleSearchIncludeVariables(bool);
    void slot_toggleSearchWholeWord(bool);
    void slot_toggleGroupBoxColorizeTrigger(const bool);
    void slot_changedPattern();
    void slot_lineSpacerChanged(int value);
    void slot_clearSearchResults();
    void slot_clearSoundFile();
    void slot_editorContextMenu();
    void slot_visibilityChangedEditorActionsToolbar();
    void slot_floatingChangedEditorActionsToolbar();
    void slot_restoreEditorActionsToolbar();
    void slot_itemEdited();
    void slot_clickedMessageBox(const QString&);
    void slot_addPattern();
    void slot_deletePatternRow();
    void slot_bannerDismissClicked();
    // The close button on the package warning, which is not one of the tip
    // banners and so is not dismissed like one
    void slot_packageWarningDismissed();
    void slot_refreshBannerLinkColors();
    void slot_itemsChanged(EditorViewTypes::EditorViewType viewType, QList<int> affectedItemIDs);

    // Per-property immediate save slots for triggers (create individual undo entries)
    void slot_saveProperty_TriggerName();
    void slot_saveProperty_TriggerCommand();
    void slot_saveProperty_TriggerStayOpen();
    void slot_saveProperty_TriggerLineMargin();
    void slot_saveProperty_TriggerFilterTrigger();
    void slot_saveProperty_TriggerPerlSlashG();
    void slot_saveProperty_TriggerSoundEnabled();
    void slot_saveProperty_TriggerSoundFile();
    void slot_saveProperty_TriggerColorizer();
    void slot_saveProperty_TriggerPattern(int patternIndex);
    void slot_saveProperty_TriggerPatternType(int patternIndex);

    // Per-property immediate save slots for aliases
    void slot_saveProperty_AliasName();
    void slot_saveProperty_AliasPattern();
    void slot_saveProperty_AliasCommand();

    // Per-property immediate save slots for timers
    void slot_saveProperty_TimerName();
    void slot_saveProperty_TimerCommand();
    void slot_saveProperty_TimerTime();

    // Per-property immediate save slots for scripts
    void slot_saveProperty_ScriptName();
    void slot_saveProperty_ScriptEventHandlers();

    // Per-property immediate save slots for keys
    void slot_saveProperty_KeyName();
    void slot_saveProperty_KeyCommand();

    // Per-property immediate save slots for actions (buttons)
    void slot_saveProperty_ActionName();
    void slot_saveProperty_ActionCommandDown();
    void slot_saveProperty_ActionCommandUp();
    void slot_saveProperty_ActionIsPushDown();
    void slot_saveProperty_ActionBarColumns();
    void slot_saveProperty_ActionBarFillerOffset();
    void slot_saveProperty_ActionBarOrientation();
    void slot_saveProperty_ActionBarLocation();
    void slot_saveProperty_ActionButtonRotation();
    void slot_saveProperty_ActionCSS();

public:
    TConsole* mpErrorConsole = nullptr;
    bool mNeedUpdateData = false;

private:
    // One place a search matched: which item it is in and where it leads, plus
    // the line the reader is shown with the query marked inside it. The first
    // half is what setAllSearchData() has always written and what
    // slot_itemSelectedInSearchResults() navigates by; the second half is only
    // ever read by SearchResultDelegate.
    struct SearchResultRow
    {
        EditorViewType view = EditorViewType::cmUnknownView;
        // The kind of thing, already translated: "Trigger", "Alias"...
        QString typeLabel;
        // What the heading row shows - the item's name, or for a variable the
        // Lua expression that reaches it
        QString title;
        QString name;
        int id = 0;
        // Variables are identified by a path rather than by a number
        QStringList variableId;
        QString where;
        // The line the match is on, as it is in the item - the whitespace in it
        // is taken out where the row is put together
        QString snippet;
        // Where the match is in that line, which is what the cursor is put at
        int matchStart = 0;
        SearchDataResultType what = SearchResultIsUnknown;
        int instance = 0;
        int subInstance = 0;
    };
    // Adds the match to the results, opening a heading row for its item if this
    // is the first match found in it
    void addSearchResult(QTreeWidgetItem*& pParent, const SearchResultRow& row);
    // Whether the panel is showing what a search found or the profile's own
    // items; the two take turns rather than sharing the height
    void setSearchResultsShown(const bool shown);

    void populateTriggers();
    void populateTimers();
    void populateScripts();
    void populateAliases();
    void populateActions();
    void populateKeys();
    void saveOpenChanges();
    EditorViewType determineViewFromVisibleTree();
    EditorViewType resolveCurrentView();
    void saveTrigger();
    void saveAlias();
    void computeAliasDescription(TAlias* pT, QString& itemDescription) const;
    void setAliasNormalState(QTreeWidgetItem* pItem, TAlias* pT);
    void showAliasError(QTreeWidgetItem* pItem, const QString& name, const QString& error);
    void showAliasLoopWarning(QTreeWidgetItem* pItem, const QString& name);
    void applyAliasState(QTreeWidgetItem* pItem, TAlias* pT);
    bool aliasSubstitutionLoops(const QString& regex, const QString& substitution) const;
    void saveTimer();
    void saveKey();
    void saveScript();
    void saveAction();
    void readSettings();
    void writeSettings();
    // Where and how big the editor opens: the stored geometry when it is still
    // usable on the screens attached now, a size taken off the profile window
    // when there is nothing stored yet
    void restoreWindowGeometry();
    QSize defaultEditorSize(const QRect& availableArea) const;
    void repositionOnProfileScreen();
    bool onSameScreenAsProfile() const;
    void addScript(bool);
    void addAlias(bool);
    void addTimer(bool);
    void addTrigger(bool);
    void addAction(bool);
    void addKey(bool);
    void timerEvent(QTimerEvent* event) override;

    void selectTriggerByID(int id);
    void selectTimerByID(int id);
    void selectAliasByID(int id);
    void selectScriptByID(int id);
    void selectActionByID(int id);
    void selectKeyByID(int id);

    void clearTriggerForm();
    void clearTimerForm();
    void clearAliasForm();
    void clearScriptForm();
    void clearActionForm();
    void clearKeyForm();
    void clearVarForm();

    void updatePackageItemAccessibility(QTreeWidgetItem* pItem, const QString& currentDescription);

    void expand_child_triggers(TTrigger* pTriggerParent, QTreeWidgetItem* pItem);
    void expand_child_timers(TTimer* pTimerParent, QTreeWidgetItem* pWidgetItemParent);
    void expand_child_scripts(TScript* pTriggerParent, QTreeWidgetItem* pWidgetItemParent);
    void expand_child_alias(TAlias*, QTreeWidgetItem*);
    void expand_child_action(TAction*, QTreeWidgetItem*);
    void expand_child_key(TKey* pTriggerParent, QTreeWidgetItem* pWidgetItemParent);

    void exportTrigger(const QString& fileName);
    void exportTimer(const QString& fileName);
    void exportAlias(const QString& fileName);
    void exportAction(const QString& fileName);
    void exportScript(const QString& fileName);
    void exportKey(const QString& fileName);

    void exportTriggerToClipboard();
    void exportTimerToClipboard();
    void exportAliasToClipboard();
    void exportActionToClipboard();
    void exportScriptToClipboard();
    void exportKeyToClipboard();

    // Multi-selection export functions
    void exportMultipleTriggersToClipboard(const QList<TTrigger*>& triggers);
    void exportMultipleTimersToClipboard(const QList<TTimer*>& timers);
    void exportMultipleAliasesToClipboard(const QList<TAlias*>& aliases);
    void exportMultipleActionsToClipboard(const QList<TAction*>& actions);
    void exportMultipleScriptsToClipboard(const QList<TScript*>& scripts);
    void exportMultipleKeysToClipboard(const QList<TKey*>& keys);

    void placePastedItems(EditorViewType itemType, const QList<int>& itemIDs);

    void clearDocument(edbee::TextEditorWidget* pEditorWidget, const QString& initialText = QString());

    void setAllSearchData(QTreeWidgetItem* pItem,
                          const EditorViewType& type,
                          const QString& name,
                          const int& id,
                          const SearchDataResultType& what,
                          const int& pos = 0,
                          const int& instance = 0,
                          const int& subInstance = 0)
    {
        // Which is it? A Trigger, an alias etc:
        pItem->setData(0, ItemRole, static_cast<int>(type));
        // What is its name:
        pItem->setData(0, NameRole, name);
        // What is its (Unique per Item Type) identifier - note that variables
        // use a different data type (QStringList):
        pItem->setData(0, IdRole, id);
        // What part of the "item" is it: the "name", the "command", the
        // "lua script", etc.:
        pItem->setData(0, TypeRole, what);
        // How far into the line/string is the start of the match, used to
        // position cursor there when chosen in search results
        pItem->setData(0, PositionRole, pos);
        // If it is a script: what line is it on (starting at 0 not 1), if a
        // trigger pattern: which one (0 to 49):
        pItem->setData(0, PatternOrLineRole, instance);
        // If there is more than one match within what the above specify - which
        // one is it, (not all things support/need to support multiples)
        pItem->setData(0, IndexRole, subInstance);
    }

    void setAllSearchData(QTreeWidgetItem* pItem, const QString& name, const QStringList& id, const SearchDataResultType& what, const int& pos = 0, const int& subInstance = 0)
    {
        // Which is it? A Trigger, an alias etc:
        pItem->setData(0, ItemRole, static_cast<int>(EditorViewType::cmVarsView));
        // What is its name:
        pItem->setData(0, NameRole, name);
        // What is its (Unique per item type) identifier - note that things
        // other then variables use a simple integer:
        pItem->setData(0, IdRole, id);
        // What part of the "item" is it: the "name", the "command", the
        // "lua script", etc.:
        pItem->setData(0, TypeRole, what);
        // How far into the line/string is the start of the match, used to
        // position cursor there when chosen in search results
        pItem->setData(0, PositionRole, pos);
        // Not used for variables:
        pItem->setData(0, PatternOrLineRole, 0);
        // If there is more than one match within what the above specify - which
        // one is it, (not all things support/need to support multiples)
        pItem->setData(0, IndexRole, subInstance);
    }

    void searchTriggers(const QString& text);
    void searchAliases(const QString& text);
    void searchScripts(const QString& text);
    void searchActions(const QString& text);
    void searchTimers(const QString& text);
    void searchKeys(const QString& text);
    void searchVariables(const QString& text);
    void recursiveSearchTriggers(TTrigger*, const QString&);
    void recursiveSearchAlias(TAlias*, const QString& text);
    void recursiveSearchScripts(TScript*, const QString& text);
    void recursiveSearchActions(TAction*, const QString& text);
    void recursiveSearchTimers(TTimer*, const QString& text);
    void recursiveSearchKeys(TKey*, const QString& text);
    void recursiveSearchVariables(TVar*, QList<TVar*>&, bool);
    void searchSingleTrigger(TTrigger* trigger, const QString& text);
    void searchSingleAlias(TAlias* alias, const QString& text);
    void searchSingleScript(TScript* script, const QString& text);
    void searchSingleAction(TAction* action, const QString& text);
    void searchSingleTimer(TTimer* timer, const QString& text);
    void searchSingleKey(TKey* key, const QString& text);
    void highlightSearchMatches();
    void
    emitScriptSearchMatches(const QString& scriptText, const QString& searchText, const QString& name, int objectId, const QString& parentLabel, EditorViewType viewType, QTreeWidgetItem*& parent);

    void createSearchOptionIcon();
    // The chevron that reopens the searches already run, which stands in for the
    // combo box's own drop-down - the shell stylesheet gives that no width
    void updateSearchHistoryAction();
    int findSearchMatch(const QString& haystack, const QString& needle, int from = 0) const;
    bool containsSearchMatch(const QString& haystack, const QString& needle) const;
    void clearEditorNotification();
    void runScheduledCleanReset();
    void autoSave();
    void setupPatternControls(const int type, dlgTriggerPatternEdit* pItem);
    void createPatternItem(int index);
    void showPatternItems(int count);
    void updatePatternPlaceholders();
    [[nodiscard]] QString patternPlaceholderText(int patternType) const;
    // What one row being typed in leaves to redo; the count of rows on show is
    // not one of those things - only the Add pattern button and a delete or a
    // move change that
    void handlePatternChange();
    // Pulls the rows in behind a move or a delete, down to a floor of one
    void compactPatternRows();
    void applyPatternWidgetStyle(dlgTriggerPatternEdit* patternWidget);

    // Everything one pattern row holds, so that a row can be taken away or moved
    // without the row widgets themselves being reordered: they are a pool of up
    // to fifty reused rows, and which widget is at which place never changes.
    // The two colour buttons carry their own text and fill, which are read off
    // the pattern text when it is loaded rather than every time it is shown.
    struct PatternRowContent
    {
        int type = 0;
        QString pattern;
        int lineSpacerValue = 0;
        QString foregroundText;
        QString foregroundStyleSheet;
        QString backgroundText;
        QString backgroundStyleSheet;
        bool markSpaces = false;
    };
    [[nodiscard]] PatternRowContent patternRowContent(const dlgTriggerPatternEdit* patternItem) const;
    void setPatternRowContent(dlgTriggerPatternEdit* patternItem, const PatternRowContent& content);
    // Rotates the rows between the two places, so that the one at `from` ends up
    // at `to` and everything it passed over shifts one place the other way
    void movePatternRowContent(const int from, const int to);
    void deletePatternRow(const int row);
    // The last row on show, which is as far as a move or a delete may reach
    [[nodiscard]] int lastVisiblePatternRow() const;
    // Which pattern row the keyboard is in, or -1 when it is somewhere else
    [[nodiscard]] int focusedPatternRow() const;
    // Ctrl+Alt+Up / Ctrl+Alt+Down, the keyboard's answer to dragging a row's grip
    void movePatternRowByKeyboard(const int offset);
    void setupAddPatternButton();
    void updateAddPatternButton();
    void restyleAddPatternIcon();
    // Which row on show a point in mpWidget_triggerItems is over. A point below
    // them all clamps to the last one - a drag let go past the end drops on the
    // final row rather than nowhere - and -1 comes back only when no row is on
    // show at all.
    [[nodiscard]] int patternRowAt(const QPoint& itemsPos) const;
    void showPatternDropIndicator(const int targetRow);
    void endPatternRowDrag(const bool dropped);
    bool handlePatternHandleEvent(QObject* watched, QEvent* event);
    [[nodiscard]] QString patternRowStyleSheet() const;
    // The eight type swatches and the grip, cut for the theme in force and
    // handed to every row that exists
    void restylePatternTypeIcons();
    void applyPatternTypeIcons(QComboBox* pBox) const;
    void applyPatternGripGlyph(QLabel* pHandle) const;
    [[nodiscard]] QIcon patternDeleteIcon() const;
    [[nodiscard]] QColor patternHoverTint() const;
    [[nodiscard]] int patternTypeColumnWidth(const QFont& typeFont) const;

    void keyGrabCallback(const Qt::Key, const Qt::KeyboardModifiers);
    QList<QAction*> toolbarActions() const;
    void suspendShortcuts();
    void restoreShortcuts();

    void showOrHideRestoreEditorActionsToolbarAction();
    void checkForMoreThanOneTriggerItem();
    TTrigger* getTriggerFromTreeItem(QTreeWidgetItem* item);
    TAlias* getAliasFromTreeItem(QTreeWidgetItem* item);
    TScript* getScriptFromTreeItem(QTreeWidgetItem* item);
    TTimer* getTimerFromTreeItem(QTreeWidgetItem* item);
    TKey* getKeyFromTreeItem(QTreeWidgetItem* item);
    TAction* getActionFromTreeItem(QTreeWidgetItem* item);
    void updatePatternTabOrder();
    QWidget* firstFocusablePatternWidget(const dlgTriggerPatternEdit* patternItem) const;
    bool focusNextPatternItem(const dlgTriggerPatternEdit* currentItem);
    bool focusPreviousPatternItem(const dlgTriggerPatternEdit* currentItem);

    bool focusPatternItem(const int row, const Qt::FocusReason reason = Qt::TabFocusReason);
    void setupPatternNavigationShortcuts();


    // The shortcuts a key grab took away, keyed by action, so that ending the
    // grab puts back exactly those
    QHash<QAction*, QKeySequence> mKeyGrabSuspendedShortcuts;

    std::unordered_map<SingleLineTextEdit*, bool> lineEditShouldMarkSpaces;

    QToolBar* toolBar = nullptr;

    // A run of the toolbar's buttons that give their names up together. The
    // list they are held in is the order the giving up happens in, and the
    // reverse of it is the order the names come back in. Undo and Redo are
    // pictures from the start, so they are in neither.
    struct EditorToolBarGroup
    {
        QList<QAction*> actions;
        bool labelsShown = true;
    };
    QList<EditorToolBarGroup> mEditorToolBarGroups;
    // Changing a button's style relays the bar out, which on a torn-off bar is
    // another resize - so the fit is not asked to answer its own answer
    bool mEditorToolBarFitting = false;

    // Every action picture is a monochrome glyph tinted from the palette, so
    // each action is kept beside the resource its picture is drawn from
    QList<QPair<QAction*, QString>> mEditorActionGlyphs;

    // One per item tree, kept so that a theme change can be handed on to the
    // state dots they draw
    QList<uiDesign::EditorTreeDelegate*> mEditorTreeDelegates;
    // ...and the seventh tree's own, for the same reason
    uiDesign::VariableTreeDelegate* mpVariableTreeDelegate = nullptr;
    // How many variables the switch over that tree would bring into it, said
    // quietly at the trailing end of the switch's row
    QPointer<QLabel> mpLabel_hiddenVariablesCount;

    // The strip the splitter handle over the Lua editor carries. Guarded because
    // the handle owns them: on teardown the splitter can take the strip with it
    // while this window is still around to be asked about the heading.
    QPointer<QWidget> mpWidget_editorCodeHeader;
    QPointer<QLabel> mpLabel_editorCodeHeaderIcon;
    // ...and the word on it, which the variables view changes: the pane there
    // holds a value rather than a script
    QPointer<QLabel> mpLabel_editorCodeHeaderTitle;
    // Where the caret is, at the trailing end of the strip. It belongs to the
    // code pane rather than to the window, so it comes and goes with the pane
    // the way the strip carrying it does.
    QPointer<QLabel> mpLabel_editorCaretPosition;
    QPointer<QWidget> mpWidget_editorCompileNote;
    QPointer<QLabel> mpLabel_editorCompileDot;
    QPointer<QLabel> mpLabel_editorCompileMessage;
    // What the heading says about the item the editor is holding: empty is the
    // reading that it compiled, and anything else is what its last save failed
    // with. Only a save of *this* item writes it - showError() is also how a
    // profile load reports every broken item it comes across, and how an
    // activation the engine refuses is announced, neither of which is this.
    QString mEditorCompileMessage;
    // The note as it would read with all the room in the world, since what is
    // shown of it is cut to a strip whose width keeps changing
    QString mEditorCompileNoteText;
    // Which line of the item the compiler stopped on, counting from one as it
    // does, and zero when it named none. Clicking the note moves the caret
    // there.
    int mEditorCompileErrorLine = 0;
    // Open only for the length of one save, so that the showError() belonging
    // to that save can be told from the ones raised around it
    bool mEditorSaveErrorCaptureOpen = false;
    QString mEditorSaveErrorCaptured;

    // The least the code pane is left with, whatever the form column would
    // rather have: below this the editor stops being one anything can be typed
    // into. On the class rather than in the .cpp so that a test can hold the
    // floor to the same number the code keeps it at.
    static constexpr int scmEditorSourcePaneFloor = 120;

    // The trigger form's options panel. The radio pair and the spin box beside
    // it are a view of spinBox_lineMargin, which stays where the trigger is
    // saved from and loaded into.
    QRadioButton* mpRadioButton_matchAny = nullptr;
    QRadioButton* mpRadioButton_matchAll = nullptr;
    QLabel* mpLabel_matchAnyChip = nullptr;
    QLabel* mpLabel_matchAllChip = nullptr;
    QSpinBox* mpSpinBox_matchWithinLines = nullptr;
    QWidget* mpWidget_matchWithinRow = nullptr;
    // Enabled only once the trigger has more than one pattern to combine
    QWidget* mpWidget_matchModeRows = nullptr;
    // ...and this says why, for as long as that is the case
    QLabel* mpLabel_matchModeHint = nullptr;
    // Shown in the panel's place, saying what it holds
    QToolButton* mpButton_triggerOptionsSummary = nullptr;
    // The events a script is registered for, in the cell beside the "Events"
    // label of the scripts form's grid
    uiDesign::ChipRow* mpChipRow_scriptEvents = nullptr;
    // A timer's interval, read as a sentence: the four fields of the .ui file
    // with the words that say what each of them is between them
    QWidget* mpWidget_timerInterval = nullptr;
    // Which sentence is up, so that choosing another timer of the same kind
    // leaves the words where they are
    bool mTimerIntervalRowBuilt = false;
    bool mTimerIntervalOffset = false;
    // What a click on the key binding field will do, said beside it
    QLabel* mpLabel_keyHint = nullptr;
    // ...and the cross that forgets the keystroke, which is only there while
    // there is one to forget
    QToolButton* mpButton_keyClear = nullptr;
    // The three of them together in the cell beside the "Key" label, hidden as
    // one for a key group so that the grid row goes with them
    QPointer<QWidget> mpWidget_keyBindingRow;
    // Height the panel borrowed from the code pane when it was opened, so that
    // closing it can hand back that much and no more. Only a view whose
    // splitter the user has dragged lends anything: everywhere else the open
    // and the close are answered by measuring the form again.
    int mTriggerOptionsBorrowedHeight = 0;
    // Holding the form column to its contents changes the layout it was just
    // measured from, so the pass is barred from re-entering itself
    bool mHoldingFormPaneToItsContents = false;

    QWidget* mpWidget_editorSidebarPane = nullptr;
    QListWidget* mpListWidget_editorSidebar = nullptr;
    // The chevron on the seam between the sidebar and the rest of the window,
    // which is where every platform puts the control that shows and hides a
    // sidebar. It gives the names up and brings them back; the sidebar itself
    // never goes away.
    uiDesign::EditorSidebarToggle* mpToggle_editorSidebar = nullptr;
    // ...and the same for the sidebar's rows
    QList<QPair<QListWidgetItem*, QString>> mEditorSidebarGlyphs;
    // The actions the sidebar rows stand for, which is also what the Ctrl+1 to
    // Ctrl+0 shortcuts are put on and taken off
    QList<QAction*> mEditorViewActions;
    // Held for the rest of the pass through the event loop that a sidebar row's
    // one-off action was asked for in, so that the click and the activation one
    // click can arrive as run it once
    bool mEditorSidebarActionInFlight = false;
    // Measuring the rows costs a pass over every one of them, and a resize asks
    // for the answer on every frame of a drag
    mutable EditorSidebarWidths mEditorSidebarWidths;
    mutable bool mEditorSidebarWidthsKnown = false;
    // What the user asked the sidebar for, kept across sessions under
    // editorSidebarLabelsShown. It is a preference about the names alone: the
    // sidebar is there either way, as a list of names or as a rail of icons.
    bool mEditorSidebarLabelsShown = true;
    // ...and whether the window is currently wide enough to grant it. This one
    // is the window's answer rather than the user's, so it is never stored -
    // a window dragged narrow shows the rail and gives the names back when it
    // is widened, whatever it was in the middle of
    bool mEditorSidebarNamesFit = true;
    // The size every glyph in this window is drawn at, taken from the "Icon
    // size toolbars" preference by slot_setToolBarIconSize(). Starts at the
    // 18px the design language draws a glyph at, which is what that
    // preference's own default maps to.
    int mEditorIconSize = 18;
    // A splitter that has never been shown reports the minimum size hint of a
    // layout that has never been run, and the breakpoint is measured off that -
    // so the answer taken before the first show is one to throw away
    bool mEditorFirstShown = false;
    // A placement the user picked - restored from settings, or dragged to - is
    // the editor's to keep, so it is only moved when it cannot be reached where
    // it is. Until there is one, the editor still follows the profile window
    bool mEditorPlacementChosen = false;
    // Set while the editor moves itself, so its own move is not mistaken for
    // one the user made
    bool mRepositioningEditorWindow = false;

    // The four palette colours every rule and every tinted glyph in the
    // editor's own look is mixed from - the surfaces rather than the tokens
    // taken off them, since a card and a border move only when a page does
    struct EditorShellStyleInputs
    {
        QRgb page = 0;
        QRgb field = 0;
        QRgb text = 0;
        QRgb accent = 0;
        bool operator==(const EditorShellStyleInputs&) const = default;
    };
    EditorShellStyleInputs mEditorShellStyleInputs;
    bool mEditorShellStyleApplied = false;
    QLabel* mpLabel_statusCounts = nullptr;
    QLabel* mpLabel_statusAutosave = nullptr;
    // Counting walks the whole tree, which a fillout would otherwise do once
    // per item added
    QTimer* mpTimer_statusCounts = nullptr;
    bool showHiddenVars = false;

    QTreeWidgetItem* mpActionBaseItem = nullptr;
    QTreeWidgetItem* mpAliasBaseItem = nullptr;
    QTreeWidgetItem* mpKeyBaseItem = nullptr;
    QTreeWidgetItem* mpScriptsBaseItem = nullptr;
    QTreeWidgetItem* mpTimerBaseItem = nullptr;
    QTreeWidgetItem* mpTriggerBaseItem = nullptr;
    QTreeWidgetItem* mpVarBaseItem = nullptr;

    QTreeWidgetItem* mpCurrentActionItem = nullptr;
    QTreeWidgetItem* mpCurrentAliasItem = nullptr;
    QTreeWidgetItem* mpCurrentKeyItem = nullptr;
    QTreeWidgetItem* mpCurrentScriptItem = nullptr;
    QTreeWidgetItem* mpCurrentTimerItem = nullptr;
    QTreeWidgetItem* mpCurrentTriggerItem = nullptr;
    QTreeWidgetItem* mpCurrentVarItem = nullptr;

    EditorViewType mCurrentView = EditorViewType::cmUnknownView;

    QScrollArea* mpScrollArea = nullptr;
    // Holds the trigger options panel, so that a window too short for the four
    // cards scrolls them instead of being held open by them
    QScrollArea* mpScrollArea_triggerOptions = nullptr;
    QWidget* mpWidget_triggerItems = nullptr;
    // this widget holds the errors, trigger patterns, and all other widgets that aren't edbee
    // in it, as a workaround for an extra splitter getting created by Qt below the error msg otherwise
    QWidget* mpNonCodeWidgets = nullptr;
    dlgActionMainArea* mpActionsMainArea = nullptr;
    dlgAliasMainArea* mpAliasMainArea = nullptr;
    dlgKeysMainArea* mpKeysMainArea = nullptr;
    dlgScriptsMainArea* mpScriptsMainArea = nullptr;
    dlgTriggersMainArea* mpTriggersMainArea = nullptr;
    dlgTimersMainArea* mpTimersMainArea = nullptr;
    dlgVarsMainArea* mpVarsMainArea = nullptr;

    dlgSourceEditorArea* mpSourceEditorArea = nullptr;
    dlgSourceEditorFindArea* mpSourceEditorFindArea = nullptr;
    dlgSystemMessageArea* mpSystemMessageArea = nullptr;

    bool mIsGrabKey = false;
    QPointer<Host> mpHost;
    QList<dlgTriggerPatternEdit*> mTriggerPatternEdit;
    // A trigger can hold fifty rows, and these are the same on all of them: the
    // picture on the delete button, the wash the row is hovered with, and how
    // wide the type column is. Thrown away by the theme or the font moving.
    QIcon mPatternDeleteIcon;
    QColor mPatternHoverTint;
    mutable int mPatternTypeColumnWidth = 0;
    int mVisiblePatternCount = 0;
    // Sits in the same column the rows are in, under the last one on show, and
    // paints its own dashed frame
    uiDesign::PlaceholderButton* mpButton_addPattern = nullptr;
    // The line drawn where a dragged row would land. A tracked grab rather than
    // QDrag: the rows are a pool, so what moves is the contents, not a widget.
    QFrame* mpFrame_patternDropIndicator = nullptr;
    int mPatternDragSourceRow = -1;
    int mPatternDragTargetRow = -1;
    QPoint mPatternDragPressPos;
    bool mPatternDragActive = false;
    // Set while a move or a delete is shifting one row's contents into the next,
    // so the chrome that reads every row is left until the shift has finished
    bool mPatternBulkEdit = false;
    QStringList mPatternList;
    QVector<QIcon> mPatternIcons;
    QPixmap mPatternGripGlyph;

    QShortcut* mFirstPatternShortcut = nullptr;
    QShortcut* mLastPatternShortcut = nullptr;
    QVector<QShortcut*> mPatternNavigationShortcuts;
    bool mChangingVar = false;

    QTextDocument* mpSourceEditorDocument = nullptr;
    edbee::TextEditorWidget* mpSourceEditorEdbee = nullptr;
    edbee::TextDocument* mpSourceEditorEdbeeDocument = nullptr;
    edbee::TextSearcher* mpSourceEditorSearcher = nullptr;

    inline static const QRegularExpression csmSimplifyCaretReportRegex{qsl(R"(^(?:\[\*\] )?(.+?) \|)")};
    // What a compiler's report of a failed save is shaped like: the chunk it was
    // handed, the line it stopped on, and what it made of that line
    inline static const QRegularExpression csmCompileErrorLineRegex{qsl(R"(^(?:Lua syntax error:\s*)?\[string ".*?"\]:(\d+):\s*(.*)$)")};

    QAction* mAddItem = nullptr;
    QAction* mDeleteItem = nullptr;
    QAction* mAddGroup = nullptr;
    QAction* mSaveItem = nullptr;

    SearchOptions mSearchOptions = SearchOptionNone;
    QSplitter* searchSplitter;
    // The two halves the splitter holds, so that running a search can put one in
    // front of the other
    QWidget* mpWidget_searchResultsPane = nullptr;
    QWidget* mpWidget_itemTreesPane = nullptr;
    uiDesign::SearchResultDelegate* mpSearchResultDelegate = nullptr;
    // What the results on screen were found by - the length of it is how much of
    // each snippet the marker pen covers
    QString mSearchTerm;

    // This has a menu which the following QActions are inserted into:
    QAction* mpAction_searchOptions = nullptr;
    QAction* mpAction_searchHistory = nullptr;
    QIcon mIcon_searchOptions;

    QAction* mpAction_searchCaseSensitive = nullptr;
    QAction* mpAction_searchIncludeVariables = nullptr;
    QAction* mpAction_searchWholeWord = nullptr;
    // TODO: Add other searchOptions
    // QAction* mpAction_searchRegExp;

    QAction* mProfileSaveAction = nullptr;
    QAction* mProfileSaveAsAction = nullptr;

    // Enables the toolbar to be unhidden if it gets hid:
    QAction* mpAction_restoreEditorActionsToolbar = nullptr;

    // We need to keep a record of these buttons as we have to disable them
    // for the "Variables" view:
    QAction* mpAction_toggleActive = nullptr;
    QAction* mpExportAction = nullptr;
    QAction* mpCreateModuleAction = nullptr;

    // Smart undo/redo actions (route based on focus):
    QAction* mpUndoAction = nullptr;
    QAction* mpRedoAction = nullptr;

    // Undo system for item-level operations (using Qt's QUndoStack framework):
    EditorUndoStack* mpUndoStack = nullptr;

    // Guarded pointer to text editor's undo stack (for safe signal connections):
    QPointer<edbee::TextUndoStack> mpTextUndoStack;

    // Track whether auto-complete provider has been initialized
    static bool smAutoCompleteInitialized;

    // tracks the duration of the "Save Profile As" action so
    // autosave doesn't kick in
    bool mSavingAs = false;

    // keeps track of the dialog reset being queued
    bool mCleanResetQueued = false;

    // tracks whether the initial profile load has completed (to avoid clearing undo stack on refreshes)
    bool mInitialLoadDone = false;

    // Blocks property saves during UI updates (e.g., when loading a selected item or during undo/redo)
    // to prevent recursive saves and duplicate undo entries
    bool mBlockPropertySave = false;

    // profile autosave interval in minutes
    int mAutosaveInterval = 2;

    // The form pane's own height at the moment the options panel was folded
    // away for want of room, and the mark that the fold was the space's doing
    // rather than the reader's. Zero while the panel is on show, or while it is
    // away because it was closed on purpose.
    //
    // The splitter's size rather than the form's: folding the panel away
    // changes what the form is made of, so the form's height at the same
    // splitter position is a different number the instant the fold happens -
    // which is how the panel came to reappear on the next move event, fold
    // again on the one after, and flicker on every pixel of a drag. A splitter
    // size is what the reader is actually dragging and is unmoved by the fold.
    int mTriggerOptionsAutoHiddenAtPaneHeight = 0;

    // Whether the reader has asked for the extra trigger controls in this
    // session. Not stored: the editor opens with the panel closed every time,
    // and the summary strip says what it would have said. Only changed by
    // explicit clicks on the toggle button, not by the transient space-driven
    // auto-collapse - which is what this is read for, since a panel that was
    // folded away for want of room is one to unfold again when the room comes
    // back, and one the reader closed is not:
    bool mShowAllTriggerControls = false;

    // The form pane height the user dragged the right hand splitter to, per
    // view. A view named here keeps that height as the item in it changes; one
    // that is not has its form snapped to whatever the item holds, which is
    // what every view does until a handle is dragged in it. Not stored: a
    // restart has every view snapping again.
    QHash<EditorViewType, int> mDraggedFormPaneHeights;

    struct EditorState
    {
        int caretLine = 0;
        int caretColumn = 0;
        int verticalScrollPos = 0;
        int horizontalScrollPos = 0;
    };

    QMap<EditorViewType, QMap<int, EditorState>> mEditorStates;

    void saveEditorState(EditorViewType viewType, int itemId);
    void restoreEditorState(EditorViewType viewType, int itemId);
    void clearEditorState(EditorViewType viewType, int itemId);

    // approximate max duration "Copy as image" can take in seconds
    int mCopyAsImageMax = 0;

    struct introOption
    {
        QString name;
        QString headline;
        QString contents;
    };

    struct introTextParts
    {
        QString summary;
        QVector<introOption> options;
    };

    QMap<EditorViewType, introTextParts> introAddItem;

    void showIntro(const QString& = QString());
    void showHideableBanner(const QString& content, const QString& bannerKey);
    [[nodiscard]] QString bannerSettingsKey(EditorViewType viewType, const QString& bannerKey) const;
    [[nodiscard]] QString legacyBannerSettingsKey(EditorViewType viewType, const QString& bannerKey) const;
    [[nodiscard]] QString profileSettingsPrefix() const;

    // Banner state tracking
    QTimer* mpBannerUndoTimer = nullptr;
    EditorViewType mLastDismissedBannerView = EditorViewType::cmUnknownView;
    QString mLastDismissedBannerContent;
    QString mCurrentBannerKey;
    QString mLastDismissedBannerKey;
    QSet<QString> mTemporarilyHiddenBanners;

    // The package warning is not a tip banner - it has no settings key and is
    // never restored - so it keeps its own memory: the packages it has already
    // been raised for, whether the close button has put it away for good, and
    // whether the longer message screen readers get has been spoken.
    QSet<QString> mWarnedPackages;
    bool mPackageWarningsDismissed = false;
    bool mPackageWarningAnnounced = false;

    // Banner methods
    void handleBannerDismiss();
    void cancelBannerUndoTimer();
    void showBannerUndoToast();
    void undoBannerDismiss();
    void handlePermanentBannerDismiss();
    bool bannerPermanentlyHidden(EditorViewType viewType, const QString& bannerKey = QString(), bool includeBasePreference = true);
    void setBannerPermanentlyHidden(EditorViewType viewType, const QString& bannerKey, bool hidden);

    QString descActive;
    QString descInactive;
    QString descActiveFolder;
    QString descInactiveFolder;
    QString descError;
    QString descInactiveParent;
    QString descActiveFilterChain;
    QString descInactiveFilterChain;
    QString descActiveOffsetTimer;
    QString descInactiveOffsetTimer;
    QString descNewFolder;
    QString descNewItem;
    QString descPackageItem;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(dlgTriggerEditor::SearchOptions)

#endif // MUDLET_DLGTRIGGEREDITOR_H
