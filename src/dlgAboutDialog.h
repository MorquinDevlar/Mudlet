#ifndef MUDLET_DLGABOUTDIALOG_H
#define MUDLET_DLGABOUTDIALOG_H

/***************************************************************************
 *   Copyright (C) 2008-2009 by Heiko Koehn - KoehnHeiko@googlemail.com    *
 *   Copyright (C) 2014 by Ahmed Charles - acharles@outlook.com            *
 *   Copyright (C) 2022 by Stephen Lyons - slysven@virginmedia.com         *
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


#include "ui_about_dialog.h"
#include "uiDesign.h"

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QList>
#include <QMap>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QVector>

class QGridLayout;
class QGroupBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QToolButton;

namespace uiDesign {
class AboutLinkButton;
class AboutSupporterBanner;
} // namespace uiDesign

struct aboutMaker
{
    bool big = false;
    QString name;
    QString discord;
    QString github;
    QString email;
    QString description;
};

// One component Mudlet ships that somebody else wrote: what it is called, whose
// it is, which licence it comes under - the chip at the end of its row - and
// that licence as shipped. The body is never translated: only the English form
// of a licence is the legally definitive one.
struct aboutThirdParty
{
    QString name;
    QString copyright;
    QString licenceKind;
    QString body;
};

class dlgAboutDialog : public QDialog, public Ui::about_dialog
{
    Q_OBJECT

public:
    Q_DISABLE_COPY(dlgAboutDialog)
    explicit dlgAboutDialog(QWidget* parent = nullptr);

    // The two lists the pages are built from, which are data rather than
    // layout: public so that a test can hold the rows it can see against what
    // this build actually ships, rather than repeating the #if blocks itself
    static QVector<aboutThirdParty> thirdPartyComponents();
    static QList<QPair<QString, QString>> buildInfoRows();

    // The picture this dialog is showing. getSplashScreen() answers a random
    // Easter egg on 1 April, so asking it a second time is not a way to find
    // out what is on screen.
    const QImage& splashImage() const { return mSplash; }

protected:
    void resizeEvent(QResizeEvent* pEvent) override;
    // A move to a screen of another ratio arrives here, and every glyph and the
    // artwork are rasterised for the ratio they were built at
    bool event(QEvent* pEvent) override;

private slots:
    // The nav, the footer's licence link and the tests all come in through here
    void showPage(const QString& key);
    void slot_copyBuildInformation();
    void slot_restoreCopyButton();
    void slot_applyAppearance();

private:
    // A chip drawn as rich text, which is the only way a QLabel reaches a glyph
    // tinted at runtime - so what it was made of is kept to make it again when
    // the theme moves
    struct ContactChip
    {
        QPointer<QLabel> pLabel;
        QString glyphFile;
        QString text;
    };

    void buildShell();
    QWidget* buildArtColumn();
    QWidget* buildContentColumn();
    QWidget* buildMudletPage();
    QWidget* buildSupportersPage();
    QWidget* buildLicensePage();
    QWidget* buildThirdPartyPage();

    QScrollArea* createScrollPage(const QString& key);
    QGroupBox* createCard(const QString& title, QWidget* pParent);
    QWidget* createSection(const QString& title, const QString& note, QWidget* pParent);
    QWidget* createContactChips(const aboutMaker& maker, QWidget* pParent);
    QWidget* createMakerCard(const aboutMaker& maker, QWidget* pParent);
    QWidget* createMoreRow(const aboutMaker& maker, QWidget* pParent);
    QWidget* createSeparatorLine(QWidget* pParent);
    QPushButton* createCopyButton(const QString& objectName, const QString& label, QWidget* pParent);
    QLabel* createParagraph(const QString& text, QWidget* pParent);
    // Every label that can hold a link goes through here: the colour of an
    // anchor is written into the text rather than answered by a palette
    void setRichText(QLabel* pLabel, const QString& text);

    void applyShellStyle();
    void restyleArtwork(const uiDesign::ThemeTokens& tokens);
    void restyleContactChips(const uiDesign::ThemeTokens& tokens);
    void restyleRichText(const uiDesign::ThemeTokens& tokens);
    QIcon copyButtonIcon(const bool copied, const uiDesign::ThemeTokens& tokens) const;
    void updateArtColumnWidth();
    void layOutLinkButtons(const int columns);
    void setLicenseText(const uiDesign::ThemeTokens& tokens);
    QString buildInformationText() const;

    static QVector<aboutMaker> makers();

    QWidget* mpWidget_shell = nullptr;
    QWidget* mpWidget_artColumn = nullptr;
    QStackedWidget* mpStackedWidget_pages = nullptr;
    QGridLayout* mpLayout_links = nullptr;
    QTimer* mpTimer_copyFeedback = nullptr;
    QPointer<QPushButton> mpButton_showingCopied;

    QMap<QString, int> mPageIndexes;
    QMap<QString, QToolButton*> mNavButtons;
    QList<uiDesign::AboutLinkButton*> mLinkButtons;
    QList<uiDesign::AboutSupporterBanner*> mBanners;
    QList<QToolButton*> mThirdPartyToggles;
    QList<QPushButton*> mCopyButtons;
    QList<ContactChip> mContactChips;

    // The picture as getSplashScreen() hands it over, kept so that the label can
    // be redrawn at the column's other width and with the theme's own hairline
    QImage mSplash;
    bool mNarrow = false;
    // Whether the link cards have been put in the grid at all. The art column's
    // width only has to be re-done when the mode changes, but the first pass
    // has to happen whatever the mode started as.
    bool mArtColumnPlaced = false;
    // What the anchors currently carry, and what the licence was last set in:
    // both are written into text rather than answered by a palette, so re-doing
    // them costs a re-parse of every label and of the whole GPL
    QColor mInkedLinkColour;
    QString mLicenceInkKey;
};

#endif // MUDLET_DLGABOUTDIALOG_H
