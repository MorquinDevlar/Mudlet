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
class QNetworkReply;
class QTabBar;
class dlgSystemMessageArea;

enum class NavigationView { Explore, Installed, Updates };

class dlgPackageManager : public QDialog, public Ui::package_manager
{
    Q_OBJECT

public:
    Q_DISABLE_COPY(dlgPackageManager)
    explicit dlgPackageManager(QWidget* parent, Host*);
    bool readPackageRepositoryFile();
    void resetPackageList();
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
    void slot_openPackageWebsite();
    void slot_onIconDownloaded(QNetworkReply* reply);
    void slot_removePackages();
    void slot_searchTextChanged(const QString& searchText);
    void slot_setPackageList();
    void slot_toggleInstallRepoButton();
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
    void clearPackageDetails();
    void closeEvent(QCloseEvent* event) override;
    void downloadIcon(const QString& packageName);
    void downloadRepositoryIndex();
    void fillPackageDetails(const QString& name, const QString& title, const QString& author, const QString& version);
    bool hasNewerVersion(const QString& installed, const QString& repo) const;
    QString packageHelpUrl(const QString& packageName) const;
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
    void showImportStatus(const QString& message);
    // The chosen chip follows the view whichever way the view was changed - the
    // bar itself, one of the hidden buttons, or a reset
    void syncViewBar();
    void updateUpdatesBadge();

    Host* mpHost = nullptr;
    QButtonGroup* mpNavigationGroup = nullptr;
    NavigationView mCurrentView = NavigationView::Installed;
    QList<QString> mPackagesWithUpdates;
    PackageItemDelegate* mpPackageItemDelegate = nullptr;
    QHash<QString, QJsonObject> packageLookup;
    QJsonArray repositoryPackages;

    QTabBar* mpViewBar = nullptr;
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
