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
#include <QIcon>
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
class EditorSidebarToggle;
class EditorTreeDelegate;
class SearchResultDelegate;
}


class dlgTriggerEditor : public QMainWindow, private Ui::trigger_editor
{
    Q_OBJECT

    // Allow QTest-based test classes to access private members
    friend class dlgTriggerEditorUndoRedoTest;
    friend class EditorBannerViewSwitchTest;
    friend class EditorChromeShapeTest;
    friend class EditorColumnAlignmentTest;
    friend class EditorIconScaleTest;
    friend class EditorMinimumSizeTest;
    friend class EditorOptionsPanelDefaultTest;
    friend class EditorSidebarCollapseTest;
    friend class EditorSplitterRestoreTest;
    friend class EditorSurfaceToneTest;
    friend class EditorTreeDotClickTest;
    friend class EditorTreeHeadingIconTest;
    friend class EditorTreeRowHeightTest;
    friend class EditorTreeSelectionPillTest;
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
    void focusInEvent(QFocusEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void showEvent(QShowEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void enterEvent(TEnterEvent* event) override;
    bool eventFilter(QObject*, QEvent* event) override;
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* e) override;
    void updateExtraControlsToggleIcon();
    // The editor's own look, derived from the application palette rather than
    // written out, so that it follows a theme change
    void applyEditorShellStyle();
    void restyleEditorIcons();
    void applyEditorToolbarButtonStyles();
    // The panel down the left: what its trees draw a row as, and where the
    // search field sits over them
    void setupEditorPanel();
    // The heading the Lua editor is under, which is also the handle the code
    // pane is resized by
    void setupEditorCodeHeader();
    void updateEditorCompileChip();
    // The chip speaks for the item in the editor, so switching items puts it
    // back to saying nothing
    void clearCompileState();
    // Bracket a save so that showError() is heard by the chip for as long as it
    // runs, and by nothing else the rest of the time
    void beginSaveErrorCapture();
    void endSaveErrorCapture();
    // The trigger form's options, moved out of the .ui column of group boxes
    // and into a column of cards, with a strip that says what they hold while
    // they are put away
    void buildTriggerOptionsPanel();
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
    void fitFormPaneToItsContents();
    // Every view puts the right hand splitter back to the sizes it was last left
    // at, which is also the one thing that can take the geometry the trigger
    // options panel borrowed against out from under it
    void restoreRightSplitterState(const QByteArray& savedState);
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
    void slot_scriptMainAreaDeleteHandler();
    void slot_scriptMainAreaAddHandler();
    void slot_scriptMainAreaEditHandler();
    void slot_scriptMainAreaClearHandlerSelection(QListWidgetItem*);
    void slot_keyGrab();
    void slot_profileSaveAction();
    void slot_profileSaveAsAction();
    void slot_setToolBarIconSize(int);
    void slot_setTreeWidgetIconSize(int);
    void slot_colorTriggerFg();
    void slot_colorTriggerBg();
    void slot_updateStatusBar(const QString& statusText); // For the source code editor
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
    void setShortcuts(const bool active = true);
    void setShortcuts(QList<QAction*> actionList, const bool active = true);

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


    // PLACEMARKER 3/3 save button texts need to be kept in sync
    // Note: Shortcut values use Qt's portable format (Ctrl+S) which Qt maps correctly per-platform
    // Keys use tr() to match translated action labels; values are not translated (they're key sequences)
    std::unordered_map<QString, QString> mButtonShortcuts = {{tr("Save Item"), qsl("Ctrl+S")},
                                                             {tr("Save Trigger"), qsl("Ctrl+S")},
                                                             {tr("Save Timer"), qsl("Ctrl+S")},
                                                             {tr("Save Alias"), qsl("Ctrl+S")},
                                                             {tr("Save Script"), qsl("Ctrl+S")},
                                                             {tr("Save Button"), qsl("Ctrl+S")},
                                                             {tr("Save Key"), qsl("Ctrl+S")},
                                                             {tr("Save Variable"), qsl("Ctrl+S")},
                                                             {tr("Save Profile"), qsl("Ctrl+Shift+S")},
                                                             {tr("Triggers"), qsl("Ctrl+1")},
                                                             {tr("Aliases"), qsl("Ctrl+2")},
                                                             {tr("Scripts"), qsl("Ctrl+3")},
                                                             {tr("Timers"), qsl("Ctrl+4")},
                                                             {tr("Keys"), qsl("Ctrl+5")},
                                                             {tr("Variables"), qsl("Ctrl+6")},
                                                             {tr("Buttons"), qsl("Ctrl+7")},
                                                             {tr("Errors"), qsl("Ctrl+8")},
                                                             {tr("Statistics"), qsl("Ctrl+9")},
                                                             {tr("Debug"), qsl("Ctrl+0")}};

    std::unordered_map<SingleLineTextEdit*, bool> lineEditShouldMarkSpaces;

    QToolBar* toolBar = nullptr;

    // Every action picture is a monochrome glyph tinted from the palette, so
    // each action is kept beside the resource its picture is drawn from
    QList<QPair<QAction*, QString>> mEditorActionGlyphs;

    // One per item tree, kept so that a theme change can be handed on to the
    // state dots they draw
    QList<uiDesign::EditorTreeDelegate*> mEditorTreeDelegates;

    // The strip the splitter handle over the Lua editor carries. Guarded because
    // the handle owns them: on teardown the splitter can take the strip with it
    // while this window is still around to be asked about the chip.
    QPointer<QWidget> mpWidget_editorCodeHeader;
    QPointer<QLabel> mpLabel_editorCodeHeaderIcon;
    QPointer<QWidget> mpWidget_editorCompileChip;
    QPointer<QLabel> mpLabel_editorCompileDot;
    QPointer<QLabel> mpLabel_editorCompileState;
    // What the chip says about the item the editor is holding: empty is the
    // reading that it compiled, and anything else is what its last save failed
    // with. Only a save of *this* item writes it - showError() is also how a
    // profile load reports every broken item it comes across, and how an
    // activation the engine refuses is announced, neither of which is this.
    QString mEditorCompileMessage;
    // Open only for the length of one save, so that the showError() belonging
    // to that save can be told from the ones raised around it
    bool mEditorSaveErrorCaptureOpen = false;
    QString mEditorSaveErrorCaptured;

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
    // Height the panel borrowed from the code pane when it was opened, so that
    // closing it can hand back that much and no more
    int mTriggerOptionsBorrowedHeight = 0;
    // The last sizes the user dragged the right hand splitter to while a
    // trigger was on show, restored when the panel is opened again
    QList<int> mTriggerRightSplitterSizes;

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

    bool mIsScriptsMainAreaEditHandler = false;
    // Not owned, and does not outlive a
    // listWidget_script_registered_event_handlers->clear()
    QListWidgetItem* mpScriptsMainAreaEditHandlerItem = nullptr;
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

    inline static const QRegularExpression csmSimplifyStatusBarRegex{qsl(R"(^(?:\[\*\] )?(.+?) \|)")};

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

    // The space recorded for the left side for "items" in the trigger area
    // so as to be able to fit the right side with the extra controls,
    // determined the first time the area is shrunk down by the user:
    int mTriggerMainAreaMinimumHeightToShowAll = 0;

    // Whether the reader has asked for the extra trigger controls in this
    // session. Not stored: the editor opens with the panel closed every time,
    // and the summary strip says what it would have said. Only changed by
    // explicit clicks on the toggle button, not by the transient space-driven
    // auto-collapse - which is what this is read for, since a panel that was
    // folded away for want of room is one to unfold again when the room comes
    // back, and one the reader closed is not:
    bool mShowAllTriggerControls = false;

    // tracks location of the splitter in the trigger editor for each tab
    QByteArray mTriggerEditorSplitterState;
    QByteArray mAliasEditorSplitterState;
    QByteArray mScriptEditorSplitterState;
    QByteArray mActionEditorSplitterState;
    QByteArray mKeyEditorSplitterState;
    QByteArray mTimerEditorSplitterState;
    QByteArray mVarEditorSplitterState;

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
