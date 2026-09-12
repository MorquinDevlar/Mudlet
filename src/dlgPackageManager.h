#ifndef MUDLET_DLGPACKAGEMANAGER_H
#define MUDLET_DLGPACKAGEMANAGER_H

/***************************************************************************
 *   Copyright (C) 2011 by Heiko Koehn - KoehnHeiko@googlemail.com         *
 *   Copyright (C) 2021 by Manuel Wegmann - wegmann.manuel@yahoo.com       *
 *   Copyright (C) 2022 by Stephen Lyons - slysven@virginmedia.com         *
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


#include "PackageItemDelegate.h"

#include "ui_package_manager.h"
#include <QButtonGroup>
#include <QDialog>
#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>
#include <QListWidget>
#include <QTextBrowser>

class Host;
class QAction;
class QHBoxLayout;
class QLabel;
class QEvent;
class QMenu;
class QNetworkReply;
class QPushButton;
class QTabBar;
class dlgSystemMessageArea;

namespace uiDesign {
class GripSplitter;
}

enum class NavigationView { Explore, Installed, Updates };

class dlgPackageManager : public QDialog, public Ui::package_manager
{
    Q_OBJECT

    friend class PackageManagerShellTest;

public:
    Q_DISABLE_COPY(dlgPackageManager)
    explicit dlgPackageManager(QWidget* parent, Host*);
    bool readPackageRepositoryFile();
    void resetPackageList();
    // Re-reads every row's switch off the Host and says again what the details
    // column says about the chosen one. Unlike resetPackageList() it refills
    // nothing, so the view being looked at, the row chosen in it and whatever is
    // typed in the search field all stay where they were - which is what a
    // package being switched on or off has to leave alone.
    void refreshPackageStates();
    QString removePackages(const QStringList& packageNames);

signals:
    void packageManagerClosing(const QString& profileName);

private slots:
    void slot_applyAppearance();
    void slot_installPackageFromFile();
    void slot_installPackageFromRepository();
    void slot_itemChanged(QListWidgetItem*);
    void slot_navigationButtonClicked(int buttonId);
    void slot_openBugWebsite();
    // The list's right-click menu: the same acts the buttons carry, offered
    // where the pointer already is
    void slot_packageContextMenu(const QPoint& pos);
    void slot_openPackageWebsite();
    void slot_onIconDownloaded(QNetworkReply* reply);
    void slot_removePackages();
    void slot_searchTextChanged(const QString& searchText);
    void slot_setPackageList();
    void slot_toggleInstallRepoButton();
    void slot_togglePackageEnabled();
    void slot_toggleRemoveButton();

private:
    // The window in the design language, composed from the recipes in
    // src/uiDesign.h and run again on every appearance change. Every sheet goes
    // on one of the two columns rather than on the dialog: mudlet assigns a
    // profile's Lua stylesheet to a dialog on show, so a sheet set here would
    // simply be replaced by it.
    void applyPackageManagerShellStyle();
    // The shell built over the .ui file: the row of chips the view is chosen
    // from, the glyph inside the search field, the notice under the list, and
    // the two action buttons moved across to the details column
    void buildShell();
    // What the list's right-click menu holds over the given row, in the design's
    // menu: the entries of the view being looked at, and nothing an entry could
    // not act on. Kept apart from the slot that shows it so that a test can read
    // the entries without running a menu's own event loop. The caller owns what
    // comes back.
    QMenu* buildPackageContextMenu(QListWidgetItem* pItem);
    void clearPackageDetails();
    void closeEvent(QCloseEvent* event) override;
    // The split between the two columns is put back here rather than in the
    // constructor: on macOS a native window created before it is shown drops
    // the geometry it is handed, and a splitter dividing a window with no width
    // yet divides nothing
    void showEvent(QShowEvent* event) override;
    // The name at the head of the details column is cut to the room the version
    // beside it leaves, and that room is the window's - so a resize, and the
    // seam between the columns being dragged, both change it
    void resizeEvent(QResizeEvent* event) override;
    // Where the seam between the two columns stood when this window was last
    // closed, or a third of the window for a reader who has never moved it
    void restoreColumnSplit();
    void downloadIcon(const QString& packageName);
    void downloadRepositoryIndex();
    void fillPackageDetails(const QString& name, const QString& title, const QString& author, const QString& version);
    bool hasNewerVersion(const QString& installed, const QString& repo) const;
    // The two numbers a package waiting for an update is named by - the one
    // installed, then the one on the repository. Said once, because the row in
    // the list and the head of the details column beside it say the same thing.
    QString updateVersionText(const QString& installedVersion, const QString& repoVersion) const;
    // ...that pair for one package, or nothing at all where either half is
    // missing and the two would read as a dangling arrow
    QString updateRowVersion(const QString& packageName) const;
    // The version a row carries at its trailing edge, written onto the item
    // wherever the list is filled. A package that names none leaves the role
    // unset, and the delegate draws nothing.
    static void setRowVersion(QListWidgetItem* pItem, const QString& version);
    QString packageHelpUrl(const QString& packageName) const;
    QString packageWebsiteUrl(const QString& packageName) const;
    void comeBackToFront();
    void scheduleHeadlineFit();
    bool eventFilter(QObject* pWatched, QEvent* pEvent) override;
    void populatePackagesWithUpdates();
    void setupNavigationButtons();
    // The words of the package's notes, kept so that an appearance change can
    // lay them out again under the document stylesheet it mixes anew
    void showDescription(const QString& markdown);
    void showPlainDescription(const QString& text);
    void relayDescription();
    // The package's picture at the head of the details column, cut to the
    // field's corner - or, for a package that ships none, the design's package
    // glyph on a card-tone box
    void showPackageIcon(const QPixmap& face);
    void showPackagePlaceholder();
    // The package's name on the headline row, cut to what the version standing
    // beside it leaves. The whole name is kept in mPackageNameShown, so the row
    // can be laid out again - the window resized, the seam dragged, the version
    // growing from one number to two in the Updates view - without eating
    // further into a name that has already been cut once.
    void showPackageName(const QString& name);
    void showImportStatus(const QString& message);
    // The chosen chip follows the view whichever way the view was changed - the
    // bar itself, one of the hidden buttons, or a reset
    void syncViewBar();
    // Whether the named package has a switch at all - a module has none, and a
    // name that is both counts as one
    bool packageIsSwitchable(const QString& packageName) const;
    // What the details column says about the chosen package's switch: which of
    // the two words the button carries, and whether the line explaining what
    // being switched off means is on show at all
    void updatePackageSwitchControls();
    // The one path into the switch, whichever of the two things the reader
    // pressed - the dot on the row or the button in the details column
    void setPackageEnabled(const QString& packageName, const bool enabled);
    void updateUpdatesBadge();

    Host* mpHost = nullptr;
    QButtonGroup* mpNavigationGroup = nullptr;
    NavigationView mCurrentView = NavigationView::Installed;
    QList<QString> mPackagesWithUpdates;
    PackageItemDelegate* mpPackageItemDelegate = nullptr;
    QHash<QString, QJsonObject> packageLookup;
    QJsonArray repositoryPackages;

    QTabBar* mpViewBar = nullptr;
    // The seam between the list column and the details column, which the reader
    // drags. Its sizes are kept under "packageManagerSplitterState".
    uiDesign::GripSplitter* mpSplitter = nullptr;
    bool mSplitterRestored = false;
    // The row the package's name and its version share at the head of the
    // details column, and the whole of that name before it was cut to fit
    QHBoxLayout* mpLayout_headlineRow = nullptr;
    QString mPackageNameShown;
    // The switch in the details column, and the line under the action row that
    // says what a switched-off package is not doing
    QPushButton* mpButton_togglePackage = nullptr;
    QLabel* mpLabel_offNote = nullptr;
    dlgSystemMessageArea* mpNotice = nullptr;
    QAction* mpAction_searchGlyph = nullptr;
    // Install and Update are the same button in two readings, so it carries
    // whichever of the two glyphs the current view's word says it is
    QIcon mInstallGlyph;
    QIcon mUpdateGlyph;
    QString mDescription;
    bool mDescriptionIsMarkdown = true;
};

#endif
