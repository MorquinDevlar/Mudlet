/***************************************************************************
 *   Copyright (C) 2011 by Heiko Koehn - KoehnHeiko@googlemail.com         *
 *   Copyright (C) 2021 by Manuel Wegmann - wegmann.manuel@yahoo.com       *
 *   Copyright (C) 2022, 2026 by Stephen Lyons - slysven@virginmedia.com   *
 *   Copyright (C) 2025 by Lecker Kebap - Leris@mudlet.org                 *
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


#include "dlgPackageManager.h"

#include "GripSplitter.h"
#include "dlgSystemMessageArea.h"
#include "mudlet.h"
#include "uiDesign.h"

#include <QCloseEvent>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPointer>
#include <QProgressDialog>
#include <QSettings>
#include <QTabBar>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVersionNumber>

using namespace std::chrono_literals;

#include <algorithm>

// What each of the two columns leaves round what it holds: the inset the strip
// of chips holds its own pane at, so the search field under the chips starts on
// the line the first chip does
static constexpr int scmPackagesColumnInset = uiDesign::scmTabPaneInset;

// The package's picture at the head of the details column. Smaller than the
// 96px square the .ui file pinned: it stands beside a name and two lines of
// words rather than over them.
static constexpr int scmPackagesIconSize = 48;
// ...and the glyph drawn in its place for a package that ships no picture,
// with air left round it on the box it is centred on
static constexpr int scmPackagesIconGlyphSize = 24;
// The glyph on each of the four buttons in the details column and on the one
// under the list: a button's word with a picture beside it, not a picture
// standing in for the word
static constexpr int scmPackagesButtonGlyphSize = 16;
// What parts the two facts at the head of the details column - the package's
// name and which version of it this is. Wider than the gap between two words,
// because they are two readings rather than one sentence.
static constexpr int scmPackagesCaptionGap = 12;
// What the details column is never dragged narrower than: a line of the
// package's notes and the row of buttons over them both need this much before
// they start wrapping into a column of single words
static constexpr int scmPackagesDetailsMinimumWidth = 320;
// ...and where the seam stands for a reader who has never moved it: a column of
// names beside a column of notes wants about a third of the window
static constexpr int scmPackagesListShareDenominator = 3;
// Where the split the reader dragged is kept between sessions
static constexpr char scmPackagesSplitterKey[] = "packageManagerSplitterState";


dlgPackageManager::dlgPackageManager(QWidget* parent, Host* pHost)
: QDialog(parent)
, mpHost(pHost)
{
    setupUi(this);
    connect(lineEdit_searchBar, &QLineEdit::textChanged, this, &dlgPackageManager::slot_searchTextChanged);
    connect(mpHost->mpConsole, &QWidget::destroyed, this, &dlgPackageManager::close);
    packageList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(packageList, &QWidget::customContextMenuRequested, this, &dlgPackageManager::slot_packageContextMenu);
    connect(packageList, &QListWidget::currentItemChanged, this, &dlgPackageManager::slot_itemChanged);
    connect(packageList, &QListWidget::itemSelectionChanged, this, &dlgPackageManager::slot_toggleInstallRepoButton);
    connect(packageList, &QListWidget::itemSelectionChanged, this, &dlgPackageManager::slot_toggleRemoveButton);
    connect(pushButton_installFile, &QAbstractButton::clicked, this, &dlgPackageManager::slot_installPackageFromFile);
    connect(pushButton_installRepo, &QAbstractButton::clicked, this, &dlgPackageManager::slot_installPackageFromRepository);
    connect(pushButton_remove, &QAbstractButton::clicked, this, &dlgPackageManager::slot_removePackages);
    connect(pushButton_report, &QAbstractButton::clicked, this, &dlgPackageManager::slot_openBugWebsite);
    connect(pushButton_website, &QAbstractButton::clicked, this, &dlgPackageManager::slot_openPackageWebsite);

    //: Package manager - window title
    setWindowTitle(tr("Package Manager - %1").arg(mpHost->getName()));

    pushButton_website->hide();
    pushButton_report->hide();

    setupNavigationButtons();

    packageList->setSortingEnabled(true);

    mpPackageItemDelegate = new PackageItemDelegate(this);
    packageList->setItemDelegate(mpPackageItemDelegate);
    connect(mpPackageItemDelegate, &PackageItemDelegate::packageToggleRequested, this, [this](const QString& packageName) {
        setPackageEnabled(packageName, !mpHost->packageEnabled(packageName));
    });

    buildShell();

    repositoryPackages = QJsonArray();
    if (!readPackageRepositoryFile()) {
        downloadRepositoryIndex();
    }

    pushButton_installRepo->setEnabled(false);
    mCurrentView = NavigationView::Installed;
    mpNavigationGroup->button(static_cast<int>(NavigationView::Installed))->setChecked(true);
    syncViewBar();
    slot_setPackageList();

    applyPackageManagerShellStyle();
    connect(mudlet::self(), &mudlet::signal_appearanceChanged, this, &dlgPackageManager::slot_applyAppearance);

    setAttribute(Qt::WA_DeleteOnClose);
}

// The shell over the .ui file: nothing here changes what a control does, only
// where it stands and what kind of control it reads as. The .ui file keeps
// every object name, so every connection and every test entry point survives.
void dlgPackageManager::buildShell()
{
    // The two columns tile the window, so the tones they are painted in reach
    // its edges and the seam between them runs the whole height
    mainVerticalLayout->setContentsMargins(0, 0, 0, 0);
    mainVerticalLayout->setSpacing(0);
    horizontalLayout_2->setContentsMargins(0, 0, 0, 0);
    horizontalLayout_2->setSpacing(0);
    verticalLayout_left->setContentsMargins(scmPackagesColumnInset, scmPackagesColumnInset, scmPackagesColumnInset, scmPackagesColumnInset);
    verticalLayout_right->setContentsMargins(scmPackagesColumnInset, scmPackagesColumnInset, scmPackagesColumnInset, scmPackagesColumnInset);
    uiDesign::markAsShellSurface(leftPanel);
    uiDesign::markAsShellSurface(rightPanel);

    // ...and the seam between them is a handle the reader drags rather than a
    // hairline: a list of long package names and a set of notes with pictures in
    // it want different shares of the window, and the fixed row the .ui file
    // held them in offered neither. Both panes come out of that row, since a
    // splitter takes its panes as its own children.
    uiDesign::removeFromLayoutTree(horizontalLayout_2, leftPanel);
    uiDesign::removeFromLayoutTree(horizontalLayout_2, rightPanel);
    mpSplitter = new uiDesign::GripSplitter(Qt::Horizontal, this);
    mpSplitter->setObjectName(qsl("packagesSplitter"));
    uiDesign::markAsShellSurface(mpSplitter);
    mpSplitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mpSplitter->setChildrenCollapsible(false);
    mpSplitter->addWidget(leftPanel);
    mpSplitter->addWidget(rightPanel);
    // Said of each pane as well as of the splitter: a pane that was never told
    // answers QSplitter::isCollapsible() with the default rather than with what
    // setChildrenCollapsible() left
    mpSplitter->setCollapsible(0, false);
    mpSplitter->setCollapsible(1, false);
    // Whatever the window gains goes to the details column; the list column
    // keeps the width it was dragged to
    mpSplitter->setStretchFactor(0, 0);
    mpSplitter->setStretchFactor(1, 1);
    // The .ui file capped the list column at 400px, which is what a row that
    // could not be dragged needed to keep the details column usable. The handle
    // says how wide each column is now, so only the floor is kept: a cap a drag
    // stops dead at reads as a handle that has broken.
    leftPanel->setMaximumWidth(QWIDGETSIZE_MAX);
    rightPanel->setMinimumWidth(scmPackagesDetailsMinimumWidth);
    mainVerticalLayout->insertWidget(mainVerticalLayout->indexOf(horizontalLayout_2), mpSplitter);
    // A drag changes what the name at the head of the details column has room
    // for, and the name is cut to that room
    // Whatever moves the seam or the window, the details column is what the
    // headline's room is measured off, so its resize is what asks for a fit:
    // a drag, a restored split (setSizes() announces nothing) and the window
    // growing all pass through here
    rightPanel->installEventFilter(this);

    // The three views, as the row of chips every other strip in the design is
    // drawn as. The buttons the .ui file put them on stay - hidden, still in
    // their group, still the thing every slot and every test presses - and the
    // bar presses them rather than doing the work itself, so there is one path
    // into a view change however it was asked for.
    headerBar->hide();
    mpViewBar = new QTabBar(leftPanel);
    mpViewBar->setObjectName(qsl("packagesViewBar"));
    mpViewBar->setExpanding(false);
    mpViewBar->setDrawBase(false);
    mpViewBar->setFocusPolicy(Qt::TabFocus);
    uiDesign::markAsShellSurface(mpViewBar);
    //: Package manager - what the row of Explore / Installed / Updates chips is, read out to a screen reader
    mpViewBar->setAccessibleName(tr("Packages to show"));
    {
        // The first tab added becomes the current one and says so - which
        // pressed the hidden Explore button before the window had chosen its
        // view. The bar is told which tab is current once the view is known.
        const QSignalBlocker held(mpViewBar);
        for (const QAbstractButton* pButton :
             {static_cast<QAbstractButton*>(pushButton_explore), static_cast<QAbstractButton*>(pushButton_installed), static_cast<QAbstractButton*>(pushButton_updates)}) {
            const int index = mpViewBar->addTab(pButton->text());
            mpViewBar->setAccessibleTabName(index, pButton->text());
        }
    }
    uiDesign::prepareTabStrip(mpViewBar);
    // Held to the leading edge rather than given the column's width: a bar
    // asks the style where its tabs go, and the macOS style answers
    // Qt::AlignCenter - which put the three chips in the middle of the column
    // on the light appearance and at its leading edge on the dark one, where
    // the base style is Fusion. The strip starts where the field under it does.
    verticalLayout_left->insertWidget(0, mpViewBar, 0, Qt::AlignLeft);
    connect(mpViewBar, &QTabBar::currentChanged, this, [this](const int index) {
        QAbstractButton* pButton = mpNavigationGroup->button(index);
        if (pButton && !pButton->isChecked()) {
            pButton->click();
        }
    });

    // The glyph inside the search field, on its leading edge, the way the
    // settings dialog's search carries one. Inked in the style pass below.
    mpAction_searchGlyph = lineEdit_searchBar->addAction(QIcon(), QLineEdit::LeadingPosition);

    // The rows are drawn by PackageItemDelegate on the shared row recipe, which
    // washes a row under the pointer and fills the chosen one. A banded list
    // underneath that would be a second thing saying which row is which.
    packageList->setAlternatingRowColors(false);
    // A row elides what will not fit, so there is never anything off to the
    // side to scroll to - and a bar across the foot of the column saying
    // otherwise is chrome standing in for nothing
    packageList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // The line of words the window has to say something with, drawn as the one
    // notice the editor and the connection dialog are drawn with. It replaces
    // the label the .ui file put here, which is left hidden rather than taken
    // out: nothing reads it, and a .ui edit to delete it would buy nothing.
    label_importStatus->hide();
    mpNotice = new dlgSystemMessageArea(leftPanel);
    mpNotice->setObjectName(qsl("packagesNotice"));
    mpNotice->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Maximum);
    if (QLayout* pNoticeLayout = mpNotice->layout()) {
        pNoticeLayout->setContentsMargins(0, 0, 0, 0);
    }
    // Only one of the three readings is ever used here: what the window has to
    // report is a package it could not install
    mpNotice->notificationAreaIconLabelError->hide();
    mpNotice->notificationAreaIconLabelInformation->hide();
    mpNotice->notificationAreaIconLabelWarning->show();
    mpNotice->hide();
    verticalLayout_left->insertWidget(verticalLayout_left->indexOf(label_importStatus) + 1, mpNotice);
    connect(mpNotice->messageAreaCloseButton, &QAbstractButton::clicked, mpNotice, &QWidget::hide);

    // Installing from a file is the one thing the list column does that is not
    // about the package chosen in it, so it stands under the list on its own,
    // the whole width of the column.
    //: Package manager - button under the list of packages, which opens a file picker
    pushButton_installFile->setText(tr("Install from a file"));
    pushButton_installFile->setSizePolicy(QSizePolicy::Expanding, pushButton_installFile->sizePolicy().verticalPolicy());

    // Install, Update and Remove act on what is chosen in the details column,
    // so they stand in that column with Website and Report rather than under
    // the list. The spacer moves with them: the two buttons that lead somewhere
    // else sit at the trailing end of the row.
    uiDesign::removeFromLayoutTree(horizontalLayout, pushButton_installRepo);
    uiDesign::removeFromLayoutTree(horizontalLayout, pushButton_remove);
    QLayoutItem* pTrailingSpacer = horizontalLayout_4->takeAt(horizontalLayout_4->indexOf(horizontalSpacer));
    horizontalLayout_4->insertWidget(0, pushButton_installRepo);
    horizontalLayout_4->insertWidget(1, pushButton_remove);
    if (pTrailingSpacer) {
        horizontalLayout_4->insertItem(2, pTrailingSpacer);
    }

    // The switch, first in the row: what a reader does most often to an
    // installed package is not removing it. Its word says what pressing it will
    // do rather than what the package currently is, so it is set from the
    // chosen package rather than once here.
    mpButton_togglePackage = new QPushButton(rightPanel);
    mpButton_togglePackage->setObjectName(qsl("packagesToggleButton"));
    mpButton_togglePackage->hide();
    connect(mpButton_togglePackage, &QAbstractButton::clicked, this, &dlgPackageManager::slot_togglePackageEnabled);
    horizontalLayout_4->insertWidget(0, mpButton_togglePackage);

    // ...and under the row, the one line that says what a package being off
    // actually means. Only ever on show while the chosen package is off: a line
    // explaining a state nothing is in is chrome standing in for nothing.
    mpLabel_offNote = new QLabel(rightPanel);
    mpLabel_offNote->setObjectName(qsl("packagesOffNote"));
    mpLabel_offNote->setWordWrap(true);
    //: Package manager - line under the details column's buttons, shown only while the chosen package is switched off
    mpLabel_offNote->setText(tr("Off: its triggers, aliases, scripts, timers, keys and buttons are not running. Turn it on to start them again."));
    mpLabel_offNote->hide();
    verticalLayout_right->insertWidget(verticalLayout_right->indexOf(horizontalLayout_4) + 1, mpLabel_offNote);

    // The two buttons that leave the window were a picture with no word and no
    // face at all, which read as decoration rather than as somewhere to press
    pushButton_website->setFlat(false);
    pushButton_report->setFlat(false);
    //: Package manager - button that opens the package's own web page
    pushButton_website->setText(tr("Website"));
    //: Package manager - button that opens the page an issue with the package is reported on
    pushButton_report->setText(tr("Report an issue"));

    // The head of the details column: the picture, the name in the title step
    // of the type scale, and one caption line carrying the author and version
    label_icon->setFixedSize(scmPackagesIconSize, scmPackagesIconSize);
    // The picture is cut and scaled before it reaches the label, at the screen's
    // own pixel ratio; letting the label scale it again undoes both
    label_icon->setScaledContents(false);
    label_icon->clear();

    QFont nameFont = font();
    nameFont.setPointSize(uiDesign::typeSize(uiDesign::TypeStep::Title));
    nameFont.setWeight(QFont::DemiBold);
    label_packageName->setFont(nameFont);

    // The version stands beside the name rather than on a caption line under
    // the summary: which version of a package this is, is read at the moment
    // its name is, and three lines apart the two were two separate facts. The
    // .ui pinned the label 100px wide and right-aligned it away from the
    // author at the far end of a row; neither is what a number beside a name
    // needs.
    label_version->setMinimumWidth(0);
    // Its words held to the foot of its box, which the row makes as deep as the
    // name's - that is what puts the number on the name's own baseline
    label_version->setAlignment(Qt::AlignLeading | Qt::AlignLeft | Qt::AlignBottom);
    // Maximum across, so the number never takes more room than it needs and
    // never pushes the name off the row; free to grow down, so its box is the
    // row's full depth for that bottom alignment to mean anything
    label_version->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    // ...and the name likewise takes its own words' width and no more, so the
    // version stands next to it rather than at the far end of the column
    label_packageName->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    label_author->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    label_author->setFont(font());
    label_version->setFont(font());

    uiDesign::removeFromLayoutTree(horizontalLayout_3, label_version);
    // What is left on the caption line is the author alone, with the room left
    // over after it
    horizontalLayout_3->setSpacing(scmPackagesCaptionGap);
    horizontalLayout_3->addStretch(1);

    mpLayout_headlineRow = new QHBoxLayout();
    mpLayout_headlineRow->setObjectName(qsl("packagesHeadlineRow"));
    mpLayout_headlineRow->setContentsMargins(0, 0, 0, 0);
    mpLayout_headlineRow->setSpacing(scmPackagesCaptionGap);
    const int namePlace = verticalLayout_details->indexOf(label_packageName);
    uiDesign::removeFromLayoutTree(verticalLayout_details, label_packageName);
    mpLayout_headlineRow->addWidget(label_packageName);
    mpLayout_headlineRow->addWidget(label_version);
    mpLayout_headlineRow->addStretch(1);
    verticalLayout_details->insertLayout(namePlace, mpLayout_headlineRow);

    // Name and version, then the one line saying what the package is for, then
    // the author under both: the .ui file had the caption between the name and
    // the summary, which read as a heading interrupted
    if (QLayoutItem* pCaptionRow = verticalLayout_details->takeAt(verticalLayout_details->indexOf(horizontalLayout_3))) {
        verticalLayout_details->addItem(pCaptionRow);
    }
    // The notes field's own hairline is the seam between the head of the column
    // and what is under it, so a rule across the column would be a second one
    line->hide();
}

void dlgPackageManager::clearPackageDetails()
{
    label_icon->clear();
    label_icon->setStyleSheet(QString());
    showPlainDescription(QString());
    mPackageNameShown.clear();
    label_packageName->clear();
    label_title->clear();
    label_author->clear();
    label_version->clear();
    pushButton_website->hide();
    pushButton_report->hide();
    if (mpButton_togglePackage) {
        mpButton_togglePackage->hide();
    }
    if (mpLabel_offNote) {
        mpLabel_offNote->hide();
    }
}

// The package's picture, cut to the field's corner at the screen's own pixel
// ratio - the games list's chips are cut the same way, and a square picture
// beside the rounded controls round it would read as a different kind of thing
void dlgPackageManager::showPackageIcon(const QPixmap& face)
{
    if (face.isNull()) {
        showPackagePlaceholder();
        return;
    }

    // No frame and no fill: what is drawn is the picture itself
    label_icon->setStyleSheet(QString());

    const qreal ratio = label_icon->devicePixelRatioF();
    QPixmap cut(QSize(scmPackagesIconSize, scmPackagesIconSize) * ratio);
    cut.setDevicePixelRatio(ratio);
    cut.fill(Qt::transparent);

    QPainter painter(&cut);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath corner;
    corner.addRoundedRect(QRectF(0, 0, scmPackagesIconSize, scmPackagesIconSize), uiDesign::scmRadiusInput, uiDesign::scmRadiusInput);
    painter.setClipPath(corner);
    painter.drawPixmap(QRect(0, 0, scmPackagesIconSize, scmPackagesIconSize), face);
    painter.end();

    label_icon->setPixmap(cut);
}

// ...and what stands there for a package that ships none: the design's package
// glyph in the chrome tone, centred on a box drawn as a card is
void dlgPackageManager::showPackagePlaceholder()
{
    const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
    label_icon->setStyleSheet(qsl("#label_icon { background-color: %1; border: %2px solid %3; border-radius: %4px; }")
                                      .arg(tokens.card.name(), QString::number(uiDesign::scmInputBorderWidth), tokens.border.name(), QString::number(uiDesign::scmRadiusInput)));

    const qreal ratio = label_icon->devicePixelRatioF();
    QPixmap glyph = uiDesign::tintedGlyph(uiDesign::glyphPixmap(qsl(":/icons/packages-package.svg")), tokens.mutedText)
                            .scaled(QSize(scmPackagesIconGlyphSize, scmPackagesIconGlyphSize) * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    glyph.setDevicePixelRatio(ratio);
    label_icon->setPixmap(glyph);
}

void dlgPackageManager::downloadIcon(const QString& packageName)
{
    QString iconPath;

    if (packageLookup.contains(packageName)) {
        QJsonObject packageObj = packageLookup.value(packageName);
        if (packageObj.contains(qsl("icon"))) {
            iconPath = packageObj[qsl("icon")].toString();
        } else {
            showPackagePlaceholder();
            return;
        }
    }

    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(qsl("https://github.com/Mudlet/mudlet-package-repository/raw/refs/heads/main/") + iconPath));
    request.setTransferTimeout(10000);
    QNetworkReply* reply = manager->get(request);
    reply->setProperty("packageName", packageName);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        slot_onIconDownloaded(reply);
    });
}

void dlgPackageManager::downloadRepositoryIndex()
{
    const QString outputPath = mudlet::getMudletPath(enums::profileHomePath, mpHost->getName() + QDir::separator() + qsl("mpkg.packages.json"));
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(qsl("https://raw.githubusercontent.com/Mudlet/mudlet-package-repository/refs/heads/main/packages/mpkg.packages.json")));
    request.setTransferTimeout(20000);
    QNetworkReply* reply = manager->get(request);
    // Parented so that closing the dialog before the download has finished takes
    // it with it: the handler below is the only other thing that deletes it, and
    // the reply that would call it is a grandchild of this dialog.
    QFile* file = new QFile(outputPath, this);

    if (!file->open(QIODevice::WriteOnly)) {
        file->deleteLater();
        reply->deleteLater();
        manager->deleteLater();
        return;
    }

    QObject::connect(reply, &QNetworkReply::readyRead, [file, reply]() {
        const QByteArray data = reply->readAll();
        if (file->write(data) != data.size()) {
            qWarning() << "dlgPackageManager::downloadRepositoryIndex() ERROR - failed to write downloaded data:" << file->errorString();
            reply->abort();
        }
    });

    QObject::connect(reply, &QNetworkReply::finished, [reply, file, manager, this]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "dlgPackageManager::downloadRepositoryIndex() ERROR - network request failed:" << reply->errorString();
        } else {
            const QByteArray data = reply->readAll();
            if (!data.isEmpty() && file->write(data) != data.size()) {
                qWarning() << "dlgPackageManager::downloadRepositoryIndex() ERROR - failed to write final data:" << file->errorString();
            }
        }
        file->close();
        reply->deleteLater();
        file->deleteLater();
        manager->deleteLater();
        readPackageRepositoryFile();
        populatePackagesWithUpdates();
        updateUpdatesBadge();
    });
}

void dlgPackageManager::fillPackageDetails(const QString& name, const QString& title, const QString& author, const QString& version)
{
    label_title->setText(title);
    //: Package manager - who wrote the package, on the caption line under its name. %1 is the author's name
    label_author->setText(author.isEmpty() ? QString() : tr("by %1").arg(author));
    // The bare number, with no word in front of it: it stands beside the name
    // in a tone of its own, which is what says what it is. Set before the name,
    // because what is left of the row after it is what the name is cut to.
    label_version->setText(version);
    showPackageName(name);
}

void dlgPackageManager::showPackageName(const QString& name)
{
    mPackageNameShown = name;
    // The room the headline row leaves the name: the whole of the row less the
    // version standing beside it and the gap between them. Measured off the row
    // rather than off the label, whose width is still the last package's until
    // the layout has run again - and a name set at its full length would push
    // the version off the column before that happened.
    int room = label_packageName->width();
    if (mpLayout_headlineRow && mpLayout_headlineRow->geometry().width() > 0) {
        room = mpLayout_headlineRow->geometry().width() - scmPackagesCaptionGap - label_version->sizeHint().width();
    }
    const QFontMetrics metrics(label_packageName->font());
    label_packageName->setText(metrics.elidedText(name, Qt::ElideRight, std::max(0, room)));
}

// The words of the package's notes, kept as they were handed over: the document
// they are laid out in carries a stylesheet mixed from the tokens, so an
// appearance change has to lay them out again rather than leave the headings
// and code blocks of the last theme on screen
void dlgPackageManager::showDescription(const QString& markdown)
{
    mDescription = markdown;
    mDescriptionIsMarkdown = true;
    packageDescription->setMarkdown(markdown);
}

void dlgPackageManager::showPlainDescription(const QString& text)
{
    mDescription = text;
    mDescriptionIsMarkdown = false;
    packageDescription->setPlainText(text);
}

void dlgPackageManager::relayDescription()
{
    if (mDescriptionIsMarkdown) {
        packageDescription->setMarkdown(mDescription);
    } else {
        packageDescription->setPlainText(mDescription);
    }
}

bool dlgPackageManager::hasNewerVersion(const QString& installed, const QString& repo) const
{
    const QVersionNumber installedVersion = QVersionNumber::fromString(installed);
    const QVersionNumber repoVersion = QVersionNumber::fromString(repo);
    return repoVersion > installedVersion;
}

QString dlgPackageManager::updateVersionText(const QString& installedVersion, const QString& repoVersion) const
{
    //: Package manager - the version beside a package's name in the Updates view: the one installed, then the one waiting. %1 is the installed version, %2 the new one
    return tr("%1 → %2").arg(installedVersion, repoVersion);
}

QString dlgPackageManager::updateRowVersion(const QString& packageName) const
{
    const QString installedVersion = mpHost ? mpHost->mPackageInfo.value(packageName).value(qsl("version")) : QString();
    const QString repoVersion = packageLookup.value(packageName).value(qsl("version")).toString();
    if (installedVersion.isEmpty() || repoVersion.isEmpty()) {
        return {};
    }
    return updateVersionText(installedVersion, repoVersion);
}

void dlgPackageManager::setRowVersion(QListWidgetItem* pItem, const QString& version)
{
    if (!pItem || version.isEmpty()) {
        return;
    }
    pItem->setData(PackageItemDelegate::cPackageVersionRole, version);
}

void dlgPackageManager::populatePackagesWithUpdates()
{
    mPackagesWithUpdates.clear();

    for (const QString& packageName : std::as_const(mpHost->mInstalledPackages)) {
        if (!packageLookup.contains(packageName)) {
            continue;
        }

        const QJsonObject repoPackage = packageLookup.value(packageName);
        const QString repoVersion = repoPackage.value(qsl("version")).toString();
        const auto packageInfo = mpHost->mPackageInfo.value(packageName);
        const QString installedVersion = packageInfo.value(qsl("version"));

        if (!installedVersion.isEmpty() && !repoVersion.isEmpty() && hasNewerVersion(installedVersion, repoVersion)) {
            mPackagesWithUpdates.append(packageName);
        }
    }
}

bool dlgPackageManager::readPackageRepositoryFile()
{
    QFile file(mudlet::getMudletPath(enums::profileHomePath, mpHost->getName() + QDir::separator() + qsl("mpkg.packages.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray data = file.readAll();
    if (data.isEmpty()) {
        qWarning() << "Repository file is empty";
        return false;
    }

    const QJsonDocument doc(QJsonDocument::fromJson(data));
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "Invalid JSON in repository file";
        return false;
    }

    QJsonObject obj = doc.object();
    if (!obj.contains("packages") || !obj["packages"].isArray()) {
        qWarning() << "Repository file corrupt: missing 'packages' array";
        return false;
    }

    repositoryPackages = obj[qsl("packages")].toArray();
    packageLookup.clear();
    for (const QJsonValue& val : std::as_const(repositoryPackages)) {
        QJsonObject pkg = val.toObject();
        packageLookup.insert(pkg["mpackage"].toString(), pkg);
    }

    populatePackagesWithUpdates();
    updateUpdatesBadge();

    return true;
}

void dlgPackageManager::resetPackageList()
{
    if (!mpHost) {
        return;
    }

    clearPackageDetails();
    mCurrentView = NavigationView::Installed;
    mpNavigationGroup->button(static_cast<int>(NavigationView::Installed))->setChecked(true);
    syncViewBar();
    packageList->clear();

    for (int i = 0; i < mpHost->mInstalledPackages.size(); i++) {
        auto item = new QListWidgetItem();
        item->setText(mpHost->mInstalledPackages.at(i));

        auto packageInfo{mpHost->mPackageInfo.value(item->text())};
        const auto title = packageInfo.value(qsl("title"));
        if (!title.isEmpty()) {
            item->setData(Qt::UserRole, title);
        }
        setRowVersion(item, packageInfo.value(qsl("version")));
        const auto iconName = packageInfo.value(qsl("icon"));
        if (!iconName.isEmpty()) {
            const auto iconDir = mudlet::getMudletPath(enums::profileDataItemPath, mpHost->getName(), qsl("%1/.mudlet/Icon/%2").arg(mpHost->mInstalledPackages.at(i), iconName));
            item->setIcon(QIcon(iconDir));
        }
        packageList->addItem(item);
    }

    populatePackagesWithUpdates();
    updateUpdatesBadge();
    packageList->setCurrentRow(0);
    slot_toggleInstallRepoButton();
    slot_toggleRemoveButton();
    refreshPackageStates();
}

// Installing, removing or switching a package can put this window behind the
// profile's: a package's toolbars are dock widgets of the main window, and
// showing one - a floating one above all - brings that window forward over this
// dialog. So every act this window starts ends by asking for the front again,
// once the event loop has run whatever the act deferred (the install event,
// the toolbars' own show).
void dlgPackageManager::comeBackToFront()
{
    QTimer::singleShot(0ms, this, [this]() {
        if (!isVisible()) {
            return;
        }
        raise();
        activateWindow();
    });
}

void dlgPackageManager::refreshPackageStates()
{
    if (!mpHost) {
        return;
    }

    // Only the Installed view has a switch to report. A row in the other two
    // stands for a package on the repository, which leaves the role unset and is
    // drawn with no dot at all.
    if (mCurrentView == NavigationView::Installed) {
        for (int row = 0; row < packageList->count(); ++row) {
            QListWidgetItem* pItem = packageList->item(row);
            if (!packageIsSwitchable(pItem->text())) {
                pItem->setData(PackageItemDelegate::cPackageEnabledRole, QVariant());
                continue;
            }
            const bool enabled = mpHost->packageEnabled(pItem->text());
            pItem->setData(PackageItemDelegate::cPackageEnabledRole, enabled);
            // The dot is painted rather than being a control of its own, so what
            // a screen reader says about the row is the only place its reading
            // can be put
            pItem->setData(Qt::AccessibleDescriptionRole,
                           enabled
                                   //: Package manager - read out after an installed package's name when the package is running
                                   ? tr("running")
                                   //: Package manager - read out after an installed package's name when the package is switched off
                                   : tr("off"));
        }
    }

    updatePackageSwitchControls();
    packageList->viewport()->update();
}

// A module is re-imported with every item forced on at each start and its file
// lives outside the profile, so Host::setPackageEnabled() refuses one - and a
// switch that is refused is worse than no switch at all. A name that is both a
// package and a module is a module for this purpose.
bool dlgPackageManager::packageIsSwitchable(const QString& packageName) const
{
    return mpHost && !packageName.isEmpty() && mpHost->mInstalledPackages.contains(packageName) && !mpHost->mInstalledModules.contains(packageName);
}

// What the details column says about the chosen package's switch. Hidden
// outright outside the Installed view: nothing listed there is installed, so
// there is nothing to switch.
void dlgPackageManager::updatePackageSwitchControls()
{
    if (!mpButton_togglePackage || !mpLabel_offNote) {
        return;
    }

    const QListWidgetItem* pItem = packageList->currentItem();
    const bool switchable = mCurrentView == NavigationView::Installed && pItem && packageIsSwitchable(pItem->text());
    mpButton_togglePackage->setVisible(switchable);
    if (!switchable) {
        mpLabel_offNote->hide();
        return;
    }

    const bool enabled = mpHost->packageEnabled(pItem->text());
    if (enabled) {
        //: Package manager - button in the details column that switches the chosen package off
        mpButton_togglePackage->setText(tr("Turn off"));
        //: Package manager - what a screen reader reads for the button that switches the chosen package off. %1 is the package's name
        mpButton_togglePackage->setAccessibleName(tr("Turn off the %1 package").arg(pItem->text()));
    } else {
        //: Package manager - button in the details column that switches the chosen package back on
        mpButton_togglePackage->setText(tr("Turn on"));
        //: Package manager - what a screen reader reads for the button that switches the chosen package back on. %1 is the package's name
        mpButton_togglePackage->setAccessibleName(tr("Turn on the %1 package").arg(pItem->text()));
    }
    mpLabel_offNote->setVisible(!enabled);
}

void dlgPackageManager::setPackageEnabled(const QString& packageName, const bool enabled)
{
    if (!mpHost || packageName.isEmpty()) {
        return;
    }
    const auto [switched, why] = mpHost->setPackageEnabled(packageName, enabled);
    if (switched) {
        return;
    }
    // The reason the Host gives is written for a script's error line rather than
    // for a reader, and what is left once modules are held back above is a save
    // in flight or a row the profile has moved on from - both of which the same
    // sentence covers
    qWarning() << "dlgPackageManager::setPackageEnabled() ERROR - could not switch" << packageName << ":" << why;
    //: Package manager - shown under the list when a package could not be switched on or off. %1 is the package's name
    showImportStatus(tr("%1 could not be switched just now. Please try again in a moment.").arg(packageName));
    comeBackToFront();
}

void dlgPackageManager::slot_togglePackageEnabled()
{
    const QListWidgetItem* pItem = packageList->currentItem();
    if (!pItem || !mpHost) {
        return;
    }
    setPackageEnabled(pItem->text(), !mpHost->packageEnabled(pItem->text()));
}

// Everything here acts on the row that is chosen, so the row the pointer is
// over is made that row first - which also moves the details column onto it, so
// what the menu is about is what the window is showing behind it. A row that is
// already part of a selection leaves that selection alone: Remove and Install
// both act on every selected row, and a right-click inside a selection that
// threw the rest of it away would be a trap.
void dlgPackageManager::slot_packageContextMenu(const QPoint& pos)
{
    QListWidgetItem* pItem = packageList->itemAt(pos);
    if (!pItem) {
        return;
    }
    packageList->setCurrentItem(pItem, pItem->isSelected() ? QItemSelectionModel::NoUpdate : QItemSelectionModel::ClearAndSelect);

    // Held through a guard rather than a bare pointer: exec() runs its own event
    // loop, and the profile going away while the menu is open closes this window
    // and takes the menu parented to it with it
    QPointer<QMenu> pMenu = buildPackageContextMenu(pItem);
    if (!pMenu) {
        return;
    }
    pMenu->exec(packageList->viewport()->mapToGlobal(pos));
    if (pMenu) {
        pMenu->deleteLater();
    }
}

QMenu* dlgPackageManager::buildPackageContextMenu(QListWidgetItem* pItem)
{
    if (!pItem || !mpHost) {
        return nullptr;
    }

    auto* pMenu = new QMenu(this);
    pMenu->setObjectName(qsl("packagesContextMenu"));
    // Built at the moment it is needed, so it takes both halves of the recipe
    // from here rather than from the window's style pass
    pMenu->setStyleSheet(uiDesign::menuStyleSheet(uiDesign::themeTokens()));
    uiDesign::letPopupsTakeTheFieldsCorner(pMenu);

    if (mCurrentView == NavigationView::Installed) {
        // A module has no switch, and an entry that cannot act is one the reader
        // has to read before working that out - so it is left out rather than
        // shown greyed
        if (packageIsSwitchable(pItem->text())) {
            const bool enabled = mpHost->packageEnabled(pItem->text());
            //: Package manager - the list's right-click menu, over an installed package. The same two words the switch in the details column carries.
            QAction* pToggle = pMenu->addAction(enabled ? tr("Turn off") : tr("Turn on"));
            pToggle->setObjectName(qsl("packagesMenuToggle"));
            connect(pToggle, &QAction::triggered, this, &dlgPackageManager::slot_togglePackageEnabled);
        }
        //: Package manager - the list's right-click menu, over an installed package: takes it off the profile
        QAction* pRemove = pMenu->addAction(tr("Remove"));
        pRemove->setObjectName(qsl("packagesMenuRemove"));
        connect(pRemove, &QAction::triggered, this, &dlgPackageManager::slot_removePackages);
        return pMenu;
    }

    // Explore and Updates list what is on the repository, and fetching one is
    // the one act either offers - the same button in its two readings, so the
    // same entry with the word the view gives it
    QAction* pInstall = pMenu->addAction(mCurrentView == NavigationView::Updates
                                                 //: Package manager - the list's right-click menu in the Updates view: brings the package up to date
                                                 ? tr("Update")
                                                 //: Package manager - the list's right-click menu in the Explore view: fetches the package
                                                 : tr("Install"));
    pInstall->setObjectName(qsl("packagesMenuInstall"));
    connect(pInstall, &QAction::triggered, this, &dlgPackageManager::slot_installPackageFromRepository);
    return pMenu;
}

void dlgPackageManager::setupNavigationButtons()
{
    mpNavigationGroup = new QButtonGroup(this);
    mpNavigationGroup->setExclusive(true);

    mpNavigationGroup->addButton(pushButton_explore, static_cast<int>(NavigationView::Explore));
    mpNavigationGroup->addButton(pushButton_installed, static_cast<int>(NavigationView::Installed));
    mpNavigationGroup->addButton(pushButton_updates, static_cast<int>(NavigationView::Updates));
    syncViewBar();

    connect(mpNavigationGroup, &QButtonGroup::idClicked, this, &dlgPackageManager::slot_navigationButtonClicked);
}

// The chip that is filled says which view is on show, whichever of the three
// ways that view was chosen. Its own signal is held while it is moved, or the
// bar would press the button that has just pressed it.
void dlgPackageManager::syncViewBar()
{
    if (!mpViewBar) {
        return;
    }
    const QSignalBlocker held(mpViewBar);
    mpViewBar->setCurrentIndex(static_cast<int>(mCurrentView));
}

void dlgPackageManager::slot_installPackageFromFile()
{
    QSettings& settings = *mudlet::getQSettings();
    QString lastDir = settings.value(qsl("lastFileDialogLocation"), QDir::homePath()).toString();

    //: Package manager - import packages from file dialog (multi-select enabled)
    //: Package manager - file filter for supported package types (mpackage, zip, xml)
    const QStringList fileNames = QFileDialog::getOpenFileNames(this, tr("Import Mudlet Package"), lastDir, tr("Mudlet Packages (*.mpackage *.zip *.xml)"));
    if (fileNames.isEmpty()) {
        return;
    }

    lastDir = QFileInfo(fileNames.first()).absolutePath();
    settings.setValue(qsl("lastFileDialogLocation"), lastDir);

    QStringList failedPackages;

    for (const QString& fileName : fileNames) {
        if (mpHost->installPackage(fileName, enums::PackageModuleType::Package).first) {
            mpHost->waitForProfileSave();
        } else {
            const QString baseName = QFileInfo(fileName).fileName();
            failedPackages << baseName;
            qWarning() << "dlgPackageManager::slot_installPackageFromFile() ERROR - failed to import" << baseName;
        }
    }

    resetPackageList();
    comeBackToFront();

    if (!failedPackages.isEmpty()) {
        //: Package manager - status message shown when some packages failed to import. %1 is a comma-separated list of package names
        showImportStatus(tr("Failed to import: %1").arg(failedPackages.join(qsl(", "))));
    }
}

void dlgPackageManager::slot_installPackageFromRepository()
{
    const QList<QListWidgetItem*> selected = packageList->selectedItems();
    if (selected.isEmpty()) {
        return;
    }

    //: Package manager - cancel button text for download progress dialog
    auto progress = new QProgressDialog(tr("Downloading packages..."), tr("Cancel"), 0, 0, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setAutoClose(true);
    progress->setMinimumDuration(0);
    progress->show();

    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    auto pendingDownloads = std::make_shared<QHash<QString, QString>>();
    auto remainingDownloads = std::make_shared<int>(selected.size());
    auto activeReplies = std::make_shared<QList<QNetworkReply*>>();
    auto cancelled = std::make_shared<bool>(false);
    bool repoError = false;

    // Installs whatever was downloaded and puts the batch away. It tears down the
    // progress dialog and the network manager, so it has to run exactly once: from
    // the reply that takes the outstanding count to zero, or from after the loop
    // when the count is already zero by the time it ends. The guard is what keeps
    // that true if the refusals below are ever rearranged to count themselves
    // before their message box rather than after it, since the box runs an event
    // loop of its own in which a download can finish
    auto batchFinished = std::make_shared<bool>(false);
    auto finishBatch = [this, pendingDownloads, manager, progress, batchFinished]() {
        if (*batchFinished) {
            return;
        }
        *batchFinished = true;

        QStringList failedPackages;

        for (auto it = pendingDownloads->begin(); it != pendingDownloads->end(); ++it) {
            const QString& packageName = it.key();
            const QString& filePath = it.value();

            if (mpHost) {
                // Ahead of both calls below, because the previous pass's install
                // leaves a save in flight: during a save an uninstall is refused
                // outright, and an install is put off until the save finishes - long
                // after the archive is deleted below.
                mpHost->waitForProfileSave();
                bool readyToInstall = true;
                // An update is an uninstall followed by an install, and an
                // uninstall forgets that the package was switched off - a
                // package taken away and fetched again from scratch is meant to
                // run. An update is not that: the reader switched this package
                // off and did not ask for it back on.
                const bool wasSwitchedOff = !mpHost->packageEnabled(packageName);
                if (mpHost->mInstalledPackages.contains(packageName)) {
                    readyToInstall = mpHost->uninstallPackage(packageName, enums::PackageModuleType::Package);
                    if (!readyToInstall) {
                        // installing over a package still listed as installed
                        // fails as "already installed", so the update would go
                        // missing without ever being named as a failure
                        failedPackages << packageName;
                        qWarning() << "dlgPackageManager::slot_installPackageFromRepository() ERROR - could not remove the installed" << packageName << "to update it";
                    }
                }
                if (readyToInstall) {
                    if (mpHost->installPackage(filePath, enums::PackageModuleType::Package).first) {
                        if (wasSwitchedOff) {
                            mpHost->setPackageEnabled(packageName, false);
                        }
                    } else {
                        failedPackages << packageName;
                        qWarning() << "dlgPackageManager::slot_installPackageFromRepository() ERROR - failed to install" << packageName;
                    }
                }
            }
            QFile::remove(filePath);
        }

        progress->reset();
        // QProgressDialog::closeEvent emits canceled(), so this runs the cancel
        // handler below on the way out of every batch. Harmless only because it
        // comes after the loop above: by now there is nothing left for that
        // handler to abort, and the files it removes are already gone
        progress->close();
        progress->deleteLater();
        manager->deleteLater();

        resetPackageList();
        comeBackToFront();

        if (!failedPackages.isEmpty()) {
            //: Package manager - status message shown when some packages downloaded from the repository failed to install. %1 is a comma-separated list of package names
            showImportStatus(tr("Failed to install: %1").arg(failedPackages.join(qsl(", "))));
        }
    };

    QObject::connect(progress, &QProgressDialog::canceled, [activeReplies, pendingDownloads, manager, progress, cancelled]() {
        *cancelled = true;
        for (QNetworkReply* reply : *activeReplies) {
            if (reply) {
                reply->abort();
            }
        }
        for (const QString& filePath : pendingDownloads->values()) {
            QFile::remove(filePath);
        }
        pendingDownloads->clear();
        manager->deleteLater();
        progress->deleteLater();
    });

    for (QListWidgetItem* item : selected) {
        const QString packageName = item->text();

        QJsonObject foundObj;
        if (packageLookup.contains(packageName)) {
            foundObj = packageLookup.value(packageName);
        }

        if (foundObj.isEmpty()) {
            //: Package manager: package couldn't be downloaded
            QMessageBox::warning(this, tr("Installation Failed"), tr("Package '%1' not found in repository").arg(packageName));
            repoError = true;
            (*remainingDownloads.get())--;
            continue;
        }

        QString remoteFileName = foundObj.value("filename").toString();
        remoteFileName = QFileInfo(remoteFileName).fileName();
        if (remoteFileName.isEmpty()) {
            //: Package manager: package couldn't be downloaded
            QMessageBox::warning(this, tr("Installation Failed"), tr("Package '%1' not found in repository").arg(packageName));
            repoError = true;
            (*remainingDownloads.get())--;
            continue;
        }

        const QByteArray encoded = QUrl::toPercentEncoding(remoteFileName);
        const QString outDir = mudlet::getMudletPath(enums::profileHomePath, mpHost->getName());
        const QString outPath = outDir + QDir::separator() + remoteFileName;
        QNetworkRequest request(QUrl(qsl("https://github.com/Mudlet/mudlet-package-repository/raw/refs/heads/main/packages/%1").arg(QString::fromUtf8(encoded))));
        request.setTransferTimeout(30000);
        QNetworkReply* reply = manager->get(request);

        // Parented, so closing the package manager mid-download takes the file, and
        // the handle it is holding on the half-written package, with it. Once a
        // download is under way nothing else would: the reply handler below is the
        // only other thing that disposes of it, and it has this dialog for its
        // context object, so it never runs once the dialog is gone
        QFile* file = new QFile(outPath, this);
        if (!file->open(QIODevice::WriteOnly)) {
            qWarning() << "dlgPackageManager::slot_installPackageFromRepository() ERROR - could not open" << outPath << "for writing:" << file->errorString();
            //: Package manager: the downloaded package could not be written to the profile folder. %1 is the package name, %2 the reason given by the operating system
            QMessageBox::warning(this, tr("Installation Failed"), tr("Package '%1' could not be saved to your profile folder: %2").arg(packageName, file->errorString()));
            (*remainingDownloads.get())--;
            file->deleteLater();
            reply->deleteLater();
            continue;
        }

        // Tracked only once the file it writes into is open. The failure above
        // deletes the reply, and this list holds raw pointers that a cancel calls
        // abort() on, so a deleted reply must never reach it
        activeReplies->append(reply);

        QObject::connect(reply, &QNetworkReply::readyRead, [file, reply]() {
            const QByteArray data = reply->readAll();
            if (file->write(data) != data.size()) {
                qWarning() << "dlgPackageManager::slot_installMultiple() ERROR - failed to write downloaded data:" << file->errorString();
                reply->abort();
            }
        });

        pendingDownloads->insert(packageName, outPath);

        QObject::connect(reply, &QNetworkReply::finished, this, [reply, file, this, outPath, packageName, pendingDownloads, remainingDownloads, cancelled, activeReplies, finishBatch]() {
            const QByteArray data = reply->readAll();
            if (!data.isEmpty() && file->write(data) != data.size()) {
                qWarning() << "dlgPackageManager::slot_installMultiple() ERROR - failed to write final data:" << file->errorString();
            }
            file->close();
            reply->deleteLater();
            file->deleteLater();

            activeReplies->removeOne(reply);

            if (*cancelled) {
                QFile::remove(outPath);
                return;
            }

            if (reply->error() != QNetworkReply::NoError) {
                //: Package manager: network error, package couldn't be downloaded
                QMessageBox::warning(this, tr("Installation Failed"), tr("Package '%1' could not be downloaded due to a network error").arg(packageName));
                pendingDownloads->remove(packageName);
            }

            // Every download that gets past the cancel check above has to reach
            // this exactly once. Zero is the only count that ends the batch, so a
            // second decrement steps over it and leaves the progress dialog up for
            // the rest of the session
            if (--(*remainingDownloads.get()) == 0) {
                finishBatch();
            }
        });
    }

    // The count can already be zero here: a selection refused above never had a
    // download, and a reply can finish inside the modal message box those refusals
    // put up, before the loop that started it has run out. The reply handler is the
    // only other place the count is checked, so without this the batch is never
    // installed and the progress dialog never closes
    if (*remainingDownloads.get() == 0) {
        finishBatch();
    }

    if (repoError) {
        downloadRepositoryIndex();
    }
}

void dlgPackageManager::slot_itemChanged(QListWidgetItem* pItem)
{
    if (!pItem) {
        return;
    }

    clearPackageDetails();
    updatePackageSwitchControls();

    QString packageName = pItem->text();

    if (mCurrentView == NavigationView::Installed) {
        auto packageInfo{mpHost->mPackageInfo.value(packageName)};
        if (packageInfo.isEmpty()) {
            showPlainDescription(QString());
            return;
        }

        if (packageLookup.contains(packageName)) {
            pushButton_website->show();
            pushButton_report->show();
        }

        QString description = packageInfo.value(qsl("description"));
        if (!description.isEmpty()) {
            QString packageDir = mudlet::self()->getMudletPath(enums::profileDataItemPath, mpHost->getName(), packageName);
            description.replace(QLatin1String("$packagePath"), packageDir);
            showDescription(description);
        }

        auto iconName = packageInfo.value(qsl("icon"));
        if (!iconName.isEmpty()) {
            const auto iconDir = mudlet::getMudletPath(enums::profileDataItemPath, mpHost->getName(), qsl("%1/.mudlet/Icon/%2").arg(packageName, iconName));
            showPackageIcon(QPixmap(iconDir));
        } else {
            showPackagePlaceholder();
        }

        fillPackageDetails(packageName, packageInfo.value(qsl("title")), packageInfo.value(qsl("author")), packageInfo.value(qsl("version")));

    } else if (mCurrentView == NavigationView::Explore) {
        pushButton_website->show();
        pushButton_report->show();
        downloadIcon(packageName);

        if (packageLookup.contains(packageName)) {
            QJsonObject packageObj = packageLookup.value(packageName);
            fillPackageDetails(
                    packageObj.value(qsl("mpackage")).toString(), packageObj.value(qsl("title")).toString(), packageObj.value(qsl("author")).toString(), packageObj.value(qsl("version")).toString());
            showDescription(packageObj.value(qsl("description")).toString());
        }
    } else if (mCurrentView == NavigationView::Updates) {
        pushButton_website->show();
        pushButton_report->show();
        downloadIcon(packageName);

        if (packageLookup.contains(packageName)) {
            QJsonObject packageObj = packageLookup.value(packageName);
            const auto packageInfo = mpHost->mPackageInfo.value(packageName);
            const QString installedVersion = packageInfo.value(qsl("version"));
            const QString repoVersion = packageObj.value(qsl("version")).toString();

            fillPackageDetails(packageObj.value(qsl("mpackage")).toString(), packageObj.value(qsl("title")).toString(), packageObj.value(qsl("author")).toString(), repoVersion);

            label_version->setText(updateVersionText(installedVersion, repoVersion));
            // Two numbers where fillPackageDetails() cut the name to fit beside
            // one, so the name is cut again to what is left now
            showPackageName(mPackageNameShown);

            showDescription(packageObj.value(qsl("description")).toString());
        }
    }
}

void dlgPackageManager::slot_navigationButtonClicked(int buttonId)
{
    mCurrentView = static_cast<NavigationView>(buttonId);
    syncViewBar();
    lineEdit_searchBar->clear();
    slot_setPackageList();
}

void dlgPackageManager::slot_applyAppearance()
{
    applyPackageManagerShellStyle();
}

void dlgPackageManager::slot_onIconDownloaded(QNetworkReply* reply)
{
    if (!reply) {
        return;
    }

    const QString requestedPackage = reply->property("packageName").toString();
    const QListWidgetItem* currentItem = packageList->currentItem();

    if (!currentItem || currentItem->text() != requestedPackage) {
        reply->deleteLater();
        reply->manager()->deleteLater();
        return;
    }

    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray imageData = reply->readAll();
        QPixmap pixmap;
        pixmap.loadFromData(imageData);
        showPackageIcon(pixmap);
    } else {
        // A picture that could not be fetched is a package with no picture, and
        // is drawn the same way rather than with a stand-in of Mudlet's own
        showPackagePlaceholder();
    }
    reply->deleteLater();
    reply->manager()->deleteLater();
}

void dlgPackageManager::slot_openBugWebsite()
{
    const QListWidgetItem* currentItem = packageList->currentItem();
    if (!currentItem) {
        return;
    }

    mudlet::self()->openWebPage(qsl("https://github.com/Mudlet/mudlet-package-repository/issues/new?template=package-bug-or-issue.md&title=[Package%20Bug]%20") + currentItem->text());
}

QString dlgPackageManager::packageHelpUrl(const QString& packageName) const
{
    // a help URL set by the package's author takes precedence over the generic repository website
    const QString url = mpHost->mPackageInfo.value(packageName).value(qsl("helpURL"));
    if (!url.isEmpty()) {
        return url;
    }
    return packageLookup.value(packageName).value(qsl("helpURL")).toString();
}

void dlgPackageManager::slot_openPackageWebsite()
{
    const QListWidgetItem* currentItem = packageList->currentItem();
    if (!currentItem) {
        return;
    }

    mudlet::self()->openWebPage(packageWebsiteUrl(currentItem->text()));
}

// The package's own page on the repository site, unless its author named a
// help page. The site addresses a package by a slug rather than by its name:
// lower case, every run of anything that is not a letter or a digit folded to
// one hyphen - "Achaean System" is /packages/achaean-system, "generic_mapper"
// is /packages/generic-mapper. The site's own listing page, with the name as a
// fragment, is what this opened before, which left the reader on the front
// page scrolled to roughly the right place.
QString dlgPackageManager::packageWebsiteUrl(const QString& packageName) const
{
    const QString helpUrl = packageHelpUrl(packageName);
    if (!helpUrl.isEmpty()) {
        return helpUrl;
    }

    QString slug;
    bool pendingHyphen = false;
    for (const QChar c : packageName.toLower()) {
        if (c.isLetterOrNumber()) {
            if (pendingHyphen && !slug.isEmpty()) {
                slug.append(QLatin1Char('-'));
            }
            pendingHyphen = false;
            slug.append(c);
        } else {
            pendingHyphen = true;
        }
    }
    return qsl("https://packages.mudlet.org/packages/") + slug;
}

// Returns what to tell the user about the removals that did not happen, empty
// when they all did. Separate from the slot so the wording can be tested: the
// dialog has no Lua entry point and the box below blocks.
QString dlgPackageManager::removePackages(const QStringList& packageNames)
{
    QStringList refusedWhileSaving;
    QStringList noLongerInstalled;
    for (const QString& package : packageNames) {
        if (mpHost->uninstallPackage(package, enums::PackageModuleType::Package)) {
            continue;
        }
        // A save in progress is the refusal the user can do something about. The
        // other one is a package that has gone since the row was drawn - a
        // sibling in this very selection can take it away from its sysUninstall
        // handler - and the listing rebuild uninstallPackage() does clears that
        // up, rather than blaming a save that is not running.
        if (mpHost->currentlySavingProfile()) {
            refusedWhileSaving << package;
        } else {
            noLongerInstalled << package;
        }
    }

    QStringList sentences;
    if (!refusedWhileSaving.isEmpty()) {
        //: %1 is a comma separated list of the packages that are still installed
        sentences << tr("These could not be removed while the profile is being saved: %1. Please try again in a moment.").arg(refusedWhileSaving.join(qsl(", ")));
    }
    if (!noLongerInstalled.isEmpty()) {
        //: %1 is a comma separated list of the packages that turned out not to be installed any more
        sentences << tr("These are no longer installed, so there was nothing to remove: %1.").arg(noLongerInstalled.join(qsl(", ")));
    }
    return sentences.join(qsl(" "));
}

void dlgPackageManager::slot_removePackages()
{
    const QList<QListWidgetItem*> selectedItems = packageList->selectedItems();
    QStringList selectedPackages;

    for (QListWidgetItem* item : selectedItems) {
        selectedPackages << item->text();
    }

    const QString msg = removePackages(selectedPackages);
    if (!msg.isEmpty()) {
        //: Title of the dialog that says why a package the user asked to remove was not removed
        QMessageBox::warning(this, tr("Removal failed"), msg);
    }

    populatePackagesWithUpdates();
    updateUpdatesBadge();
    comeBackToFront();
}

void dlgPackageManager::slot_searchTextChanged(const QString& searchText)
{
    packageList->clear();

    if (mCurrentView == NavigationView::Installed) {
        for (const auto& value : std::as_const(mpHost->mPackageInfo)) {
            const QString name = value.value(qsl("mpackage"));
            const QString title = value.value(qsl("title"));
            const QString description = value.value(qsl("description"));
            const QString author = value.value(qsl("author"));

            if (name.contains(searchText, Qt::CaseInsensitive) || title.contains(searchText, Qt::CaseInsensitive) || description.contains(searchText, Qt::CaseInsensitive)
                || author.contains(searchText, Qt::CaseInsensitive)) {
                QListWidgetItem* item = new QListWidgetItem(name);
                if (!title.isEmpty()) {
                    item->setData(Qt::UserRole, title);
                }
                setRowVersion(item, value.value(qsl("version")));
                const auto iconName = value.value(qsl("icon"));
                if (!iconName.isEmpty()) {
                    const auto iconDir = mudlet::getMudletPath(enums::profileDataItemPath, mpHost->getName(), qsl("%1/.mudlet/Icon/%2").arg(name, iconName));
                    item->setIcon(QIcon(iconDir));
                }
                packageList->addItem(item);
            }
        }
    } else if (mCurrentView == NavigationView::Explore) {
        for (const QJsonValue& value : std::as_const(repositoryPackages)) {
            const QJsonObject pkg = value.toObject();
            const QString name = pkg[qsl("mpackage")].toString();
            const QString title = pkg[qsl("title")].toString();
            const QString description = pkg[qsl("description")].toString();
            const QString author = pkg[qsl("author")].toString();

            if (name.contains(searchText, Qt::CaseInsensitive) || title.contains(searchText, Qt::CaseInsensitive) || description.contains(searchText, Qt::CaseInsensitive)
                || author.contains(searchText, Qt::CaseInsensitive)) {
                QListWidgetItem* item = new QListWidgetItem(name);
                if (!title.isEmpty()) {
                    item->setData(Qt::UserRole, title);
                }
                setRowVersion(item, pkg[qsl("version")].toString());
                if (pkg.contains(qsl("icon"))) {
                    const QPixmap pixmap(pkg[qsl("icon")].toString());
                    item->setIcon(QIcon(pixmap));
                }
                packageList->addItem(item);
            }
        }
    } else if (mCurrentView == NavigationView::Updates) {
        for (const QString& packageName : std::as_const(mPackagesWithUpdates)) {
            const auto packageInfo = mpHost->mPackageInfo.value(packageName);
            const QString title = packageInfo.value(qsl("title"));
            const QString description = packageInfo.value(qsl("description"));
            const QString author = packageInfo.value(qsl("author"));

            if (packageName.contains(searchText, Qt::CaseInsensitive) || title.contains(searchText, Qt::CaseInsensitive) || description.contains(searchText, Qt::CaseInsensitive)
                || author.contains(searchText, Qt::CaseInsensitive)) {
                QListWidgetItem* item = new QListWidgetItem(packageName);
                if (!title.isEmpty()) {
                    item->setData(Qt::UserRole, title);
                }
                setRowVersion(item, updateRowVersion(packageName));
                const auto iconName = packageInfo.value(qsl("icon"));
                if (!iconName.isEmpty()) {
                    const auto iconDir = mudlet::getMudletPath(enums::profileDataItemPath, mpHost->getName(), qsl("%1/.mudlet/Icon/%2").arg(packageName, iconName));
                    item->setIcon(QIcon(iconDir));
                }
                packageList->addItem(item);
            }
        }
    }

    refreshPackageStates();
}

void dlgPackageManager::slot_setPackageList()
{
    if (!mpHost) {
        return;
    }

    if (!lineEdit_searchBar->text().isEmpty()) {
        slot_searchTextChanged(lineEdit_searchBar->text());
        return;
    }

    packageList->clear();
    clearPackageDetails();

    if (mCurrentView == NavigationView::Installed) {
        for (int i = 0; i < mpHost->mInstalledPackages.size(); i++) {
            auto item = new QListWidgetItem();
            item->setText(mpHost->mInstalledPackages.at(i));
            const auto packageInfo{mpHost->mPackageInfo.value(item->text())};
            const auto iconName = packageInfo.value(qsl("icon"));
            const auto title = packageInfo.value(qsl("title"));
            if (!title.isEmpty()) {
                item->setData(Qt::UserRole, title);
            }
            setRowVersion(item, packageInfo.value(qsl("version")));
            if (!iconName.isEmpty()) {
                const auto iconDir = mudlet::getMudletPath(enums::profileDataItemPath, mpHost->getName(), qsl("%1/.mudlet/Icon/%2").arg(mpHost->mInstalledPackages.at(i), iconName));
                item->setIcon(QIcon(iconDir));
            }
            packageList->addItem(item);
        }
    } else if (mCurrentView == NavigationView::Explore) {
        // A repository index that is not on disk yet leaves the view empty
        // rather than leaving this function: the two buttons at the foot still
        // have to be told which view they are standing in
        if (readPackageRepositoryFile()) {
            for (const QJsonValue& packageVal : std::as_const(repositoryPackages)) {
                auto item = new QListWidgetItem();
                const QJsonObject packageObj = packageVal.toObject();
                const QString packageName = packageObj.value("mpackage").toString();
                const QString title = packageObj.value("title").toString();
                item->setText(packageName);
                if (!title.isEmpty()) {
                    item->setData(Qt::UserRole, title);
                }
                setRowVersion(item, packageObj.value(qsl("version")).toString());
                packageList->addItem(item);
            }
        }
    } else if (mCurrentView == NavigationView::Updates) {
        populatePackagesWithUpdates();

        if (mPackagesWithUpdates.isEmpty()) {
            //: Package manager - message shown in description area when no updates are available
            showPlainDescription(tr("All packages are up to date."));
        }

        for (const QString& packageName : std::as_const(mPackagesWithUpdates)) {
            auto item = new QListWidgetItem();
            item->setText(packageName);
            const auto packageInfo{mpHost->mPackageInfo.value(packageName)};
            const auto iconName = packageInfo.value(qsl("icon"));
            const auto title = packageInfo.value(qsl("title"));
            if (!title.isEmpty()) {
                item->setData(Qt::UserRole, title);
            }
            // The same two numbers the details column says, since a row in this
            // view stands for a package that is installed and behind
            setRowVersion(item, updateRowVersion(packageName));
            if (!iconName.isEmpty()) {
                const auto iconDir = mudlet::getMudletPath(enums::profileDataItemPath, mpHost->getName(), qsl("%1/.mudlet/Icon/%2").arg(packageName, iconName));
                item->setIcon(QIcon(iconDir));
            }
            packageList->addItem(item);
        }
    }

    packageList->setCurrentRow(0);
    // Which of the two acts the details column offers is the view's, not the
    // selection's, and a view change is what has just happened
    slot_toggleInstallRepoButton();
    slot_toggleRemoveButton();
    refreshPackageStates();
}

void dlgPackageManager::slot_toggleInstallRepoButton()
{
    // Installing is what the Explore and Updates views are for, and the button
    // is unavailable in the Installed view rather than merely disabled there: a
    // row of buttons where one can never be pressed is a row with a hole in it
    pushButton_installRepo->setVisible(mCurrentView != NavigationView::Installed);

    if (mCurrentView == NavigationView::Explore || mCurrentView == NavigationView::Updates) {
        const QList selection = packageList->selectedItems();
        const int selectionCount = selection.size();
        pushButton_installRepo->setEnabled(selectionCount);
        // Bringing a package up to date and fetching one for the first time are
        // two different acts, so the one button says which it is with its glyph
        // as well as with its word
        pushButton_installRepo->setIcon(mCurrentView == NavigationView::Updates ? mUpdateGlyph : mInstallGlyph);

        // One row chosen is the ordinary case, and "Update 1" reads as a count
        // of something the reader never asked to count. The number is only said
        // where there is more than one of them, and then as a sentence rather
        // than as a tally in brackets.
        if (mCurrentView == NavigationView::Updates) {
            if (selectionCount > 1) {
                //: Message on button in package manager to bring several chosen packages up to date. %n is the number of selected packages.
                pushButton_installRepo->setText(tr("Update %n packages", nullptr, selectionCount));
            } else {
                //: Message on button in package manager to bring the chosen package up to date; also its wording with nothing chosen, where the button is disabled.
                pushButton_installRepo->setText(tr("Update"));
            }
            //: Tooltip for button in package manager when in Updates view
            pushButton_installRepo->setToolTip(tr("Update selected packages"));
        } else {
            if (selectionCount > 1) {
                //: Message on button in package manager to fetch several chosen packages. %n is the number of selected packages.
                pushButton_installRepo->setText(tr("Install %n packages", nullptr, selectionCount));
            } else {
                //: Message on button in package manager to fetch the chosen package; also its wording with nothing chosen, where the button is disabled.
                pushButton_installRepo->setText(tr("Install"));
            }
            //: Tooltip for button in package manager when in Explore view
            pushButton_installRepo->setToolTip(tr("Install package from repository"));
        }
    } else {
        //: Message on button in package manager initially and when the view is the "Installed" one
        pushButton_installRepo->setText(tr("Install"));
        pushButton_installRepo->setEnabled(false);
        pushButton_installRepo->setIcon(mInstallGlyph);
        //: Tooltip for button in package manager when in Explore view
        pushButton_installRepo->setToolTip(tr("Install package from repository"));
    }
}

void dlgPackageManager::slot_toggleRemoveButton()
{
    // ...and removing is only ever something the Installed view offers
    pushButton_remove->setVisible(mCurrentView == NavigationView::Installed);

    if (mCurrentView == NavigationView::Installed) {
        const QList selection = packageList->selectedItems();
        const int selectionCount = selection.size();
        pushButton_remove->setEnabled(selectionCount);
        if (selectionCount > 1) {
            //: Message on button in package manager to take several chosen packages off the profile. %n is the number of selected packages.
            pushButton_remove->setText(tr("Remove %n packages", nullptr, selectionCount));
        } else {
            //: Message on button in package manager to take the chosen package off the profile; also its wording with nothing chosen, where the button is disabled.
            pushButton_remove->setText(tr("Remove"));
        }
    } else {
        //: Message on button in package manager initially and when the view is NOT the "Installed" one
        pushButton_remove->setText(tr("Remove"));
        pushButton_remove->setEnabled(false);
    }
}

// What this ever has to report is a package that did not install, so it is the
// notice's warning reading. No timer takes it away again: the reader is being
// told which of the packages they asked for did not arrive, and a line that
// leaves after four seconds is a line they can miss. Its own cross is what
// closes it.
void dlgPackageManager::showImportStatus(const QString& message)
{
    if (!mpNotice) {
        return;
    }
    mpNotice->notificationAreaIconLabelError->hide();
    mpNotice->notificationAreaIconLabelInformation->hide();
    mpNotice->notificationAreaIconLabelWarning->show();
    mpNotice->notificationAreaMessageBox->setText(message);
    mpNotice->show();
}

void dlgPackageManager::updateUpdatesBadge()
{
    if (mPackagesWithUpdates.size()) {
        //: Package manager - navigation button showing one or more available updates
        pushButton_updates->setText(tr("Updates (%1)").arg(mPackagesWithUpdates.size()));
    } else {
        //: Package manager - navigation button for when there are no updates
        pushButton_updates->setText(tr("Updates"));
    }
    // The chip carries the same count the button does: the button is what every
    // slot and every test presses, and the chip is what the reader sees
    if (mpViewBar && mpViewBar->count() > static_cast<int>(NavigationView::Updates)) {
        const int updates = static_cast<int>(NavigationView::Updates);
        mpViewBar->setTabText(updates, pushButton_updates->text());
        mpViewBar->setAccessibleTabName(updates, pushButton_updates->text());
    }
}

// The window in the design language, composed from the recipes: the pane the
// list column is on and the page the details column is, the row of chips, the
// fields, the buttons, the rows of the list and the two scroll areas.
//
// Every sheet goes on one of the two columns rather than on the dialog, because
// mudlet assigns a profile's Lua stylesheet to a dialog on show - so a sheet
// set on the window would be replaced by it, and a rule of the profile's is
// beaten by one of these, which names the container it is scoped to.
void dlgPackageManager::applyPackageManagerShellStyle()
{
    const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();

    // The list column: a surface of its own beside the page. No hairline down
    // its trailing edge - the splitter's handle is the seam that parts the two
    // now, and it draws one of its own.
    const QString leftRules = qsl("#leftPanel { background-color: %1; }").arg(tokens.pane.name())
                              // The ::pane and ::tab-bar halves of this land on a plain widget
                              // and do nothing; what draws the three chips is its QTabBar rules
                              + uiDesign::tabBarStyleSheet(qsl("#leftPanel"), tokens) + uiDesign::inputStyleSheet(tokens, qsl("#leftPanel")) + uiDesign::buttonStyleSheet(tokens, qsl("#leftPanel"));
    leftPanel->setStyleSheet(leftRules);

    // The rows, drawn by the recipe the editor's item trees are drawn by -
    // PackageItemDelegate is what stands on the surface these rules fill. On
    // the list itself, the way each of those trees carries its own copy.
    packageList->setStyleSheet(uiDesign::itemRowStyleSheet(qsl("QListWidget#packageList"), tokens, tokens.pane, PackageItemDelegate::cRowGutter)
                               + uiDesign::scrollBarStyleSheet(qsl("QListWidget#packageList"), tokens, tokens.pane));
    // The two inks the delegate writes a row's name in, said in the list's
    // palette as well: a view is asked what its rows are drawn in from there,
    // and a palette left at the platform's answers white on the accent's wash
    QPalette rowInks = packageList->palette();
    rowInks.setColor(QPalette::Text, tokens.text);
    rowInks.setColor(QPalette::HighlightedText, tokens.accentText);
    packageList->setPalette(rowInks);

    // What the version beside the name is written in: not chrome, but the one
    // reading the head of the column carries - this package is here, and it is
    // this one of its lives. The design's ok tone, the same green a running
    // package's dot is filled in, walked until it can be read on the page it
    // stands on. It names no size of its own, so it keeps the window's body
    // font rather than becoming a second heading beside the name.
    const QColor versionInk = uiDesign::readableOn(tokens.page, uiDesign::stateColor(uiDesign::scmStateHue_ok, tokens.darkPage), tokens.text, uiDesign::scmTextMinimumRatio);
    // The version sits on the name's baseline: both labels start at the top of
    // the headline row, and the version's words are held to the foot of a box
    // the row makes as deep as the name's - which leaves the two baselines
    // their descents apart, so the number is lifted by the difference.
    const int baselineLift = std::max(0, QFontMetrics(label_packageName->font()).descent() - QFontMetrics(label_version->font()).descent());
    label_version->setContentsMargins(0, 0, 0, baselineLift);

    // The details column, on the window's own surface: the name in the title
    // step with the version beside it, the summary in the body ink, and the
    // author as one caption line of chrome under them
    const QString rightRules = qsl("#rightPanel { background-color: %1; }").arg(tokens.page.name()) + uiDesign::inputStyleSheet(tokens, qsl("#rightPanel"))
                               + uiDesign::buttonStyleSheet(tokens, qsl("#rightPanel")) + qsl("#label_packageName, #label_title { color: %1; background: transparent; }").arg(tokens.text.name())
                               + qsl("#label_author { color: %1; background: transparent; font-size: %2pt; font-weight: normal; }")
                                         .arg(tokens.mutedText.name(), QString::number(uiDesign::typeSize(uiDesign::TypeStep::Caption)))
                               + qsl("#label_version { color: %1; background: transparent; font-weight: normal; }").arg(versionInk.name())
                               // What a switched-off package is not doing is a
                               // line of chrome, so it is written in the tone
                               // every other label on the column is
                               + qsl("#packagesOffNote { color: %1; background: transparent; }").arg(tokens.mutedText.name())
                               + uiDesign::scrollBarStyleSheet(qsl("#packageDescription"), tokens, tokens.field);
    rightPanel->setStyleSheet(rightRules);

    // What the seam between the two columns is painted with. A handle carrying
    // no heading draws a hairline down its middle and carries each neighbour's
    // own tone up to it, and a stylesheet is what fills these - so each column
    // says what it was filled with rather than the handle guessing at it.
    leftPanel->setProperty(uiDesign::scmProp_paneTone, tokens.pane);
    rightPanel->setProperty(uiDesign::scmProp_paneTone, tokens.page);
    if (mpSplitter && mpSplitter->handle(1)) {
        mpSplitter->handle(1)->update();
    }

    // The notes themselves. The field the words are laid out in is the input
    // recipe's; what the words are laid out *as* is the document's own
    // stylesheet, which is the only place a QTextDocument takes a colour from -
    // withLinkColour() is for a QLabel, and a document honours "a { color }".
    packageDescription->document()->setDefaultStyleSheet(
            qsl("body { color: %1; }"
                "h1, h2, h3, h4, h5, h6 { color: %1; }"
                // A package's notes are read in this column rather than on a page of
                // their own, so their headings come off the same four-step scale the
                // rest of the window is set in - left to the document's own idea of
                // one, an h1 came out at twice the words under it
                "h1 { font-size: %6pt; }"
                "h2 { font-size: %7pt; }"
                "h3, h4, h5, h6 { font-size: %8pt; }"
                "a { color: %2; }"
                "pre, code { background-color: %3; color: %1; border: %4px solid %5; }")
                    .arg(tokens.text.name(), tokens.accentText.name(), tokens.page.name(), QString::number(uiDesign::scmInputBorderWidth), tokens.border.name())
                    .arg(QString::number(uiDesign::typeSize(uiDesign::TypeStep::Display)),
                         QString::number(uiDesign::typeSize(uiDesign::TypeStep::Title)),
                         QString::number(uiDesign::typeSize(uiDesign::TypeStep::Body))));
    relayDescription();

    // Every glyph, taken again: a picture inked for the appearance that has
    // just been left is the one thing a theme change cannot fix by itself
    if (mpAction_searchGlyph) {
        mpAction_searchGlyph->setIcon(QIcon(uiDesign::tintedGlyph(uiDesign::glyphPixmap(qsl(":/icons/settings-search.svg")), tokens.mutedText)));
    }
    mInstallGlyph = uiDesign::tintedIcon(qsl(":/icons/packages-install.svg"), tokens);
    mUpdateGlyph = uiDesign::tintedIcon(qsl(":/icons/packages-update.svg"), tokens);
    pushButton_installFile->setIcon(uiDesign::tintedIcon(qsl(":/icons/packages-install-file.svg"), tokens));
    pushButton_remove->setIcon(uiDesign::tintedIcon(qsl(":/icons/editor-delete.svg"), tokens));
    pushButton_website->setIcon(uiDesign::tintedIcon(qsl(":/icons/about-homepage.svg"), tokens));
    pushButton_report->setIcon(uiDesign::tintedIcon(qsl(":/icons/about-bug.svg"), tokens));
    // The power glyph the editor switches an item on and off with, so the two
    // switches in the application are the one picture
    mpButton_togglePackage->setIcon(uiDesign::tintedIcon(qsl(":/icons/editor-activate.svg"), tokens));
    for (QAbstractButton* pButton : {static_cast<QAbstractButton*>(pushButton_installFile),
                                     static_cast<QAbstractButton*>(pushButton_installRepo),
                                     static_cast<QAbstractButton*>(mpButton_togglePackage),
                                     static_cast<QAbstractButton*>(pushButton_remove),
                                     static_cast<QAbstractButton*>(pushButton_website),
                                     static_cast<QAbstractButton*>(pushButton_report)}) {
        pButton->setIconSize(QSize(scmPackagesButtonGlyphSize, scmPackagesButtonGlyphSize));
    }
    // Install carries whichever of the two glyphs the view it is standing in
    // says it is, which the toggles below are what decide
    slot_toggleInstallRepoButton();
    slot_toggleRemoveButton();
    updatePackageSwitchControls();

    // The cross that closes the notice is this window's rather than the
    // notice's own, the way the editor's banner cross is
    if (QToolButton* pClose = mpNotice ? mpNotice->messageAreaCloseButton : nullptr) {
        pClose->setIcon(uiDesign::tintedIcon(qsl(":/icons/editor-clear.svg"), tokens));
        pClose->setIconSize(QSize(scmPackagesButtonGlyphSize, scmPackagesButtonGlyphSize));
    }

    // The picture at the head of the details column is inked from the tokens
    // when the package ships none of its own, and a sheet on that label is the
    // one thing that says the glyph rather than a picture is what is showing
    if (!label_icon->styleSheet().isEmpty()) {
        showPackagePlaceholder();
    }

    mpPackageItemDelegate->restyle(tokens);

    uiDesign::keepClickFocusOffControls(leftPanel);
    uiDesign::keepClickFocusOffControls(rightPanel);
    uiDesign::letPopupsTakeTheFieldsCorner(leftPanel);
    uiDesign::letPopupsTakeTheFieldsCorner(rightPanel);
}

void dlgPackageManager::closeEvent(QCloseEvent* event)
{
    // Where the reader left the seam between the two columns, for the next time
    // this window is opened
    if (mpSplitter) {
        if (QSettings* pSettings = mudlet::getQSettings()) {
            pSettings->setValue(QString::fromLatin1(scmPackagesSplitterKey), mpSplitter->saveState());
        }
    }
    if (mudlet::self() && !mudlet::self()->isGoingDown() && mpHost && !mpHost->isClosingDown()) {
        emit packageManagerClosing(mpHost->getName());
    }
    QDialog::closeEvent(event);
}

void dlgPackageManager::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    // Only the first show: a reader who drags the seam and then hides and
    // reopens the window without closing it would otherwise find it back where
    // the last session left it
    if (!mSplitterRestored) {
        mSplitterRestored = true;
        restoreColumnSplit();
    }
    scheduleHeadlineFit();
}

void dlgPackageManager::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    // setupUi() resizes the window before it has built the labels this reads,
    // and that resize is posted rather than sent - but only the shell being in
    // place makes that safe to rely on
    if (!mpLayout_headlineRow) {
        return;
    }
}

// The name at the head of the details column is cut to the room its row has,
// and that room is only known once the layout has run for the new size - which
// happens after the resize event, not inside it. Deferred to the event loop so
// the row is measured as it will be seen; and asked for after the column split
// is restored, since setSizes() moves the seam without saying so.
bool dlgPackageManager::eventFilter(QObject* pWatched, QEvent* pEvent)
{
    if (pWatched == rightPanel && pEvent->type() == QEvent::Resize) {
        scheduleHeadlineFit();
    }
    return QDialog::eventFilter(pWatched, pEvent);
}

void dlgPackageManager::scheduleHeadlineFit()
{
    if (mPackageNameShown.isEmpty()) {
        return;
    }
    QTimer::singleShot(0ms, this, [this]() {
        showPackageName(mPackageNameShown);
    });
}

void dlgPackageManager::restoreColumnSplit()
{
    if (!mpSplitter) {
        return;
    }

    QSettings* pSettings = mudlet::getQSettings();
    const QByteArray saved = pSettings ? pSettings->value(QString::fromLatin1(scmPackagesSplitterKey)).toByteArray() : QByteArray();
    if (!saved.isEmpty() && mpSplitter->restoreState(saved)) {
        // restoreState() puts back the handle width that was serialized with
        // the sizes, so a state written by a Mudlet whose handle was another
        // width has to be told this one again
        mpSplitter->setHandleWidth(uiDesign::GripSplitter::scmHandleThickness);
        return;
    }

    const int across = mpSplitter->width() - uiDesign::GripSplitter::scmHandleThickness;
    if (across <= 0) {
        return;
    }
    const int list = std::max(leftPanel->minimumWidth(), across / scmPackagesListShareDenominator);
    mpSplitter->setSizes({list, across - list});
}
