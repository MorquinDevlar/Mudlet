#ifndef MUDLET_DLGPACKAGEEXPORTER_H
#define MUDLET_DLGPACKAGEEXPORTER_H

/***************************************************************************
 *   Copyright (C) 2008-2012 by Heiko Koehn - KoehnHeiko@googlemail.com    *
 *   Copyright (C) 2014 by Ahmed Charles - acharles@outlook.com            *
 *   Copyright (C) 2019-2020, 2022 by Stephen Lyons                        *
 *                                               - slysven@virginmedia.com *
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

#include <QDialog>
#include <QFileInfo>
#include <QPointer>
#include <QTextEdit>
#include <zip.h>

#include <atomic>
#include <memory>

class ExporterTreeDelegate;
class Host;
class QCloseEvent;
class QGroupBox;
template <typename T>
class QFutureWatcher;
class QLabel;
class QResizeEvent;
class QShowEvent;
class TAction;
class TAlias;
class TKey;
class TScript;
class TTimer;
class TTrigger;
class dlgSystemMessageArea;

namespace uiDesign {
class ChipRow;
class GripSplitter;
}

#if defined(LIBZIP_VERSION_MAJOR) && defined(LIBZIP_VERSION_MINOR) && ((LIBZIP_VERSION_MAJOR > 1) || (LIBZIP_VERSION_MAJOR == 1) && (LIBZIP_VERSION_MINOR >= 7))
// libzip 1.7.0 supports cancelling archiving in progress
#define LIBZIP_SUPPORTS_CANCELLING TRUE
#endif

class QTreeWidget;
class QTreeWidgetItem;

namespace Ui {
class dlgPackageExporter;
}

class dlgPackageExporter : public QDialog
{
    Q_OBJECT

    friend class PackageExporterShellTest;

public:
    Q_DISABLE_COPY(dlgPackageExporter)
    explicit dlgPackageExporter(QWidget* parent, Host*);
    ~dlgPackageExporter();

    // Methods to preselect items when opened from trigger editor
    void preselectTrigger(QTreeWidgetItem* item);
    void preselectTimer(QTreeWidgetItem* item);
    void preselectAlias(QTreeWidgetItem* item);
    void preselectScript(QTreeWidgetItem* item);
    void preselectAction(QTreeWidgetItem* item);
    void preselectKey(QTreeWidgetItem* item);

    // Set module creation mode
    void setModuleCreationMode(bool isModule);
    void recurseTree(QTreeWidgetItem*, QList<QTreeWidgetItem*>&);
    void listTriggers();
    void recurseTriggers(TTrigger*, QTreeWidgetItem*);
    void listAliases();
    void recurseAliases(TAlias*, QTreeWidgetItem*);
    void listScripts();
    void recurseScripts(TScript*, QTreeWidgetItem*);
    void listKeys();
    void recurseKeys(TKey*, QTreeWidgetItem*);
    void listActions();
    void recurseActions(TAction*, QTreeWidgetItem*);
    void listTimers();
    void recurseTimers(TTimer*, QTreeWidgetItem*);
    TTrigger* triggerOf(const QMap<QTreeWidgetItem*, int>&, QTreeWidgetItem*) const;
    TTimer* timerOf(const QMap<QTreeWidgetItem*, int>&, QTreeWidgetItem*) const;
    TAlias* aliasOf(const QMap<QTreeWidgetItem*, int>&, QTreeWidgetItem*) const;
    TAction* actionOf(const QMap<QTreeWidgetItem*, int>&, QTreeWidgetItem*) const;
    TScript* scriptOf(const QMap<QTreeWidgetItem*, int>&, QTreeWidgetItem*) const;
    TKey* keyOf(const QMap<QTreeWidgetItem*, int>&, QTreeWidgetItem*) const;
    static void copy_directory(const QString& fromDir, const QString& toDir, bool overwrite);
    QMap<QTreeWidgetItem*, int> triggerMap;
    QMap<QTreeWidgetItem*, int> modTriggerMap;
    QMap<QTreeWidgetItem*, int> aliasMap;
    QMap<QTreeWidgetItem*, int> modAliasMap;
    QMap<QTreeWidgetItem*, int> scriptMap;
    QMap<QTreeWidgetItem*, int> modScriptMap;
    QMap<QTreeWidgetItem*, int> keyMap;
    QMap<QTreeWidgetItem*, int> modKeyMap;
    QMap<QTreeWidgetItem*, int> actionMap;
    QMap<QTreeWidgetItem*, int> modActionMap;
    QMap<QTreeWidgetItem*, int> timerMap;
    QMap<QTreeWidgetItem*, int> modTimerMap;
    // This will hold the absolute pathFileName for the XML file that will
    // contain the Mudlet items to go into the package:
    QString mXmlPathFileName;
    QString mPlainDescription;
    QStringList mDescriptionImages;

    // Module creation mode flag
    bool mIsModuleCreationMode = false;

public slots:
    void slot_addFiles();
    void slot_exportPackage();

private slots:
    void slot_addDependency();
    void slot_removeDependency();
    void slot_importIcon();
    void slot_removeIcon();
    void slot_openPackageLocation();
    void slot_packageChanged(int);
    void slot_updateLocationPlaceholder();
    void slot_recountItems(QTreeWidgetItem* item);
    void slot_rightClickOnItems(const QPoint& point);
    void slot_cancelExport();
    void slot_applyAppearance();
    // The row of chips is a view over the hidden combo box the export writes
    // into, so every edit the reader makes on the row is written back to it
    void slot_dependencyChipsChanged();

protected:
    bool eventFilter(QObject* obj, QEvent* evt) override;
    void closeEvent(QCloseEvent* event) override;
    // The split between the two columns is put back here rather than in the
    // constructor: on macOS a native window created before it is shown drops
    // the geometry it is handed
    void showEvent(QShowEvent* event) override;
    // The path the package is saved to is cut from its leading end to whatever
    // the foot leaves it, which is the window's width
    void resizeEvent(QResizeEvent* event) override;

private:
    // The shell built over the .ui file: the two columns in a splitter, the
    // tree of things to export on one and the package's details on the other,
    // and the foot carrying where it saves to, the notice and the buttons.
    // Nothing here changes what a control does, only where it stands and what
    // kind of control it reads as - so every object name, every connection and
    // every translation of the .ui file survives.
    void buildShell();
    // The three halves of it, each pulling what it holds out of the .ui file's
    // fold and putting it where the reader is meant to read it
    void buildSelectionColumn();
    void buildDetailsColumn();
    void buildFoot();
    // ...and the two rows of the form that are more than a word over a field:
    // the package's picture, and the zone the files it ships are dropped on
    void buildIconRow();
    void buildAssetsRow();
    // The window in the design language, composed from the recipes in
    // src/uiDesign.h and run again on every appearance change. Every sheet goes
    // on one of the shell's containers rather than on the dialog: mudlet
    // assigns a profile's Lua stylesheet to a dialog on show, so a sheet set
    // here would simply be replaced by it.
    void applyPackageExporterShellStyle();
    // Where the seam between the two columns stood when this window was last
    // closed, or a third of the window for a reader who has never moved it
    void restoreColumnSplit();
    // The heading over the tree and the chip beside it, which say the same
    // thing the group box's title does - that title is still written, since the
    // dialog's own tests read the count off it
    void showSelectionCount(const int itemsToExport);
    // The words the two headings and the save caption read, which differ
    // between exporting a package and creating a module
    void showModeWords();
    // The chips the reader works the required packages through, filled from the
    // hidden combo box the export actually reads
    void syncDependencyChips();
    // ...and what the row offers as one is typed: the installed packages, and
    // the modules as well while a module is being made
    void refreshDependencySuggestions();
    // The notice under the form, in the reading the result asks for. The words
    // are kept so that an appearance change can ink the links in them again.
    void showNotice(const QString& richText, const uiDesign::NoticeKind kind);
    // The path in the foot, cut from its leading end to the room the row leaves
    // it, with the whole of it on the tooltip
    void fitSavePath();
    // The height of the list of files to ship: every file plus one empty line,
    // taken out of the description's room above it. Once the description is down
    // to four lines the list stops growing and scrolls instead.
    void fitAssetsList();
    // The unit glyphs on the six top-level rows and the folder glyph on every
    // folder under them, taken again whenever the appearance moves
    void restyleTreeIcons(const uiDesign::ThemeTokens& tokens);
    // The icon box on the form: the design's package glyph while no picture has
    // been chosen, and the picture itself once one has
    void showIconPlaceholder(const uiDesign::ThemeTokens& tokens);
    void showChosenIcon();
    // The prompt over the drop zone, which is only there while the list is empty
    void showAssetsPrompt();
    // Whether every field, tick and file stands as it did when the window opened.
    // The folder the package is written to is a preference rather than something
    // the reader filled in here, so it is not part of the answer.
    bool formIsFresh() const;
    // ...and whether any of the words on the form were written here rather than
    // loaded out of a package, which is the one thing starting over cannot give back
    bool formHasTypedWords() const;
    // Puts the form back to how the window opened
    void startOver();
    // ...asked for first whenever there are typed words to lose. The one place that
    // rule lives, for the button on the top row and the picker's first entry alike.
    void requestStartOver();
    // The button is always on show and only available once there is something on
    // the form to clear
    void refreshStartOverButton();
    void appendToDetails(const QString&, const QString&);
    void displayResultMessage(const QString&, const bool isSuccessMessage = true);
    void uncheckAllChildren();
    int countRecursive(QTreeWidgetItem* item, int count) const;
    int countCheckedItems() const;
    void checkChildren(QTreeWidgetItem* item) const;
    // A folder at the root of a unit carrying the package's own name: XMLimport
    // makes that folder again on install, so it stands for what is inside it and
    // never goes into the file or the count itself. The name is the one typed on
    // the form or the package chosen in the picker.
    bool packageFolder(const QTreeWidgetItem* pItem) const;
    // What every folder's box shows: the dash while some of what is inside it is
    // ticked, and for a folder named after the package, its contents entirely
    void refreshFolderMarks();
    // One folder, after its own children have been derived; the answer says
    // whether anything that goes into the file is ticked under this row
    bool refreshFolderMark(QTreeWidgetItem* pItem);
    QString getActualPath() const;
    static const int isTopFolder = 1;
    static std::pair<bool, QString> writeFileToZip(const QString& archiveFileName, const QString& fileSystemFileName, zip* archive);
    static std::pair<bool, QString> zipPackage(const QString& stagingDirName,
                                               const QString& packagePathFileName,
                                               const QString& xmlPathFileName,
                                               const QString& packageName,
                                               const QString& packageComment,
                                               std::shared_ptr<std::atomic<bool>> cancelled);
    static std::pair<bool, QString> copyAssetsToTmp(const QStringList& assetPaths, const QString& tempPath);
    QFileInfo copyIconToTmp(const QString& tempPath) const;
    QString normalizedHelpUrl() const;
    void writeConfigFile(const QString& stagingDirName, const QFileInfo& iconFile, const QString& packageDescription);
    void exportXml(bool& isOk,
                   QList<QTreeWidgetItem*>& trigList,
                   QList<QTreeWidgetItem*>& timerList,
                   QList<QTreeWidgetItem*>& aliasList,
                   QList<QTreeWidgetItem*>& actionList,
                   QList<QTreeWidgetItem*>& scriptList,
                   QList<QTreeWidgetItem*>& keyList);
    void markExportItems(QList<QTreeWidgetItem*>& trigList,
                         QList<QTreeWidgetItem*>& timerList,
                         QList<QTreeWidgetItem*>& aliasList,
                         QList<QTreeWidgetItem*>& actionList,
                         QList<QTreeWidgetItem*>& scriptList,
                         QList<QTreeWidgetItem*>& keyList);
    QString copyNewImagesToTmp(const QString& tempPath) const;
    static void cleanupUnusedImages(const QString& tempPath, const QString& plainDescription);
    void checkToEnableExportButton();
    void populateDependencies();

    std::unique_ptr<Ui::dlgPackageExporter> ui;
    QPointer<Host> mpHost;
    QTreeWidget* mpExportSelection = nullptr;
    QPointer<QPushButton> mExportButton;
    QPointer<QPushButton> mCancelButton;
    QPointer<QPushButton> mCloseButton;
    QTreeWidgetItem* mpTriggers = nullptr;
    QTreeWidgetItem* mpAliases = nullptr;
    QTreeWidgetItem* mpTimers = nullptr;
    QTreeWidgetItem* mpScripts = nullptr;
    QTreeWidgetItem* mpKeys = nullptr;
    QTreeWidgetItem* mpButtons = nullptr;
    QGroupBox* mpSelectionText = nullptr;
    QString mPackageName;
    QString mPackagePath;
    QString mPackagePathFileName;
    QString mPackageIconPath;
    QString mPackageConfig;
    QString mPackageComment;
    bool mCheckChildren = true;
    // Shared by every exporter, so that only one export runs at a time; held until the zip ends, even after a Cancel
    inline static bool mExportingPackage = false;
    static void releaseExportLock();
    static void recheckEveryExportButton();
    // This dialog's export has put the busy cursor up and not yet taken it down
    bool mExportRunning = false;
    QFutureWatcher<std::pair<bool, QString>>* mpZipWatcher = nullptr;
    // Read on the zip's worker thread by libzip's cancel callback
    std::shared_ptr<std::atomic<bool>> mpZipCancelled;
    bool mRecountPending = false;

    // The shell. The three containers the sheets go on, and what the shell adds
    // to each of them - everything the .ui file already named keeps its name.
    QWidget* mpSelectionColumn = nullptr;
    QWidget* mpDetailsColumn = nullptr;
    QWidget* mpFoot = nullptr;
    // The seam between the two columns, which the reader drags. Its sizes are
    // kept under "packageExporterSplitterState".
    uiDesign::GripSplitter* mpSplitter = nullptr;
    bool mSplitterRestored = false;
    // Held while the list of files is being resized: the resize runs the column's
    // layout again, which is what asked for the fit in the first place
    bool mFittingAssets = false;
    ExporterTreeDelegate* mpTreeDelegate = nullptr;
    QLabel* mpLabel_selectionHeading = nullptr;
    QLabel* mpLabel_selectionCount = nullptr;
    QLabel* mpLabel_selectionHint = nullptr;
    QLabel* mpLabel_detailsHeading = nullptr;
    QLabel* mpLabel_nameCaption = nullptr;
    QLabel* mpLabel_detailsHint = nullptr;
    QLabel* mpLabel_descriptionHint = nullptr;
    QLabel* mpLabel_assetsHint = nullptr;
    QLabel* mpLabel_assetsDropHint = nullptr;
    QLabel* mpLabel_assetsEmpty = nullptr;
    QLabel* mpLabel_saveCaption = nullptr;
    QLabel* mpLabel_savePath = nullptr;
    uiDesign::ChipRow* mpChipRow_dependencies = nullptr;
    QPointer<QPushButton> mpButton_startOver;
    dlgSystemMessageArea* mpNotice = nullptr;
    // The whole path the foot shows before it was cut to the room it has, and
    // the words of the notice before the links in them were inked
    QString mSavePathShown;
    QString mNoticeRichText;

    // How the window opened, which is where starting over puts the form back to.
    // The author is remembered between sessions, the version and the description's
    // template come out of the .ui file - and the template is a different one while
    // a module is being made.
    QString mFreshAuthor;
    QString mFreshVersion;
    QString mFreshDescription;
    // ...and what choosing an installed package last wrote onto the form, so that a
    // field still holding what was loaded into it does not read as something the
    // reader typed
    struct LoadedWords
    {
        QString name;
        QString author;
        QString title;
        QString helpUrl;
        QString version;
        QString description;
    };
    LoadedWords mLoadedWords;
    // Which entry the picker stood on before the one it is on, to put it back when
    // starting over from its first entry is declined
    int mPackageListWasAt = 0;

signals:
    void signal_exportLocationChanged(const QString& location);
    void packageExporterClosing(const QString& profileName);
};

class dlgPackageExporterDescription : public QTextEdit
{
    Q_OBJECT

public:
    Q_DISABLE_COPY_MOVE(dlgPackageExporterDescription)
    explicit dlgPackageExporterDescription(QWidget* pW = nullptr);
    ~dlgPackageExporterDescription();
    bool canInsertFromMimeData(const QMimeData* source) const override;
    void insertFromMimeData(const QMimeData* source) override;
};

#endif // MUDLET_DLGPACKAGEEXPORTER_H
