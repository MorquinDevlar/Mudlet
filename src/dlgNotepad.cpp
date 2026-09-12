/***************************************************************************
 *   Copyright (C) 2008-2009 by Heiko Koehn - KoehnHeiko@googlemail.com    *
 *   Copyright (C) 2014 by Ahmed Charles - acharles@outlook.com            *
 *   Copyright (C) 2017-2018, 2025 by Stephen Lyons                        *
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


#include "dlgNotepad.h"

#include "mudlet.h"
#include "uiDesign.h"

#include <QCloseEvent>
#include <QDir>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QSaveFile>
#include <QShortcut>
#include <QStringConverter>
#include <QTextDocument>
#include <QTextStream>
#include <QTimer>
#include <QApplication>
#include <QToolButton>

#include <algorithm>
#include <chrono>

using namespace std::chrono;

const QString jsonNotesFileName{qsl("notes.json")};
const QString local8BitEncodedNotesFileName{qsl("notes.txt")};
const QString utf8EncodedNotesFileName{qsl("notes_utf8.txt")};

// How far a match the cursor is not on is taken from the field it lies in
// towards the marker pen: far enough to be picked out of a page of notes, and
// short of the full pen, which is what says which of them the cursor is on
constexpr qreal scmNotepadQuietMatchWash = 0.35;

// Where a note stops looking like a list of things to do. Six lines or fewer is
// what a sequence of commands looks like - a shopping run, a set of directions -
// and a note longer than that is a diary. Sending a diary with nothing in front
// of each line puts prose on the command line, which is what the strip's warning
// is about, so this is the line between the two.
constexpr int scmNotepadWarnAtLines = 6;
// The gap QCommonStyle leaves between a tool button's picture and its word
// when the two sit side by side - hard-coded there, not a style metric
constexpr int scmQtToolButtonPictureGap = 4;

// What is left between the dot, the words and the thread of a reading: the
// bar's own spacing parts one control from the next, and the pieces of one
// reading are nearer each other than that.
constexpr int scmNotepadReadingGap = 5;

// The dot a reading on the strip begins with. The same size the editor's compile
// note is read by; scmEditorCompileDotDiameter is that window's own constant, so
// this window carries its own copy of the number rather than reaching for it.
constexpr int scmNotepadReadingDotDiameter = 8;

// The thread of progress under a send: a line rather than a trough, so it says
// how far along the send is without becoming a control. Its corner is half its
// own height, which is what makes the ends round rather than cut.
constexpr int scmNotepadProgressHeight = 4;
constexpr int scmNotepadProgressWidth = 90;

// A field the strip has warned the reader about. No pseudo-state can say it, so
// the rule that draws the hairline selects on this.
constexpr char scmProp_notepadFieldState[] = "notepadFieldState";

// The picture the warning is carried by inside that field. The size the editor
// draws the glyph on its banner at; scmEditorBannerGlyphSize is that window's
// own constant, so this window carries its own copy of the number rather than
// reaching for it.
constexpr int scmNotepadFieldCueSize = 20;

// What a count of lines counts: slot_sendNextLine() skips an empty one, so
// counting the empties would promise the reader more than the game will see
static int sendableLineCount(const QStringList& lines)
{
    int count = 0;
    for (const QString& line : lines) {
        if (!line.isEmpty()) {
            ++count;
        }
    }
    return count;
}

// A selection comes back with the paragraph separator QTextCursor works in
// rather than with newlines, which is what slot_sendSelection() undoes as well
static QStringList linesOf(QString text)
{
    return text.replace(QChar::ParagraphSeparator, QChar::LineFeed).split(QChar::LineFeed);
}

dlgNotepad::dlgNotepad(Host* pH)
: mpHost(pH)
{
    setupUi(this);

    // Nothing ever writes to the status bar, and an empty one leaves a strip of
    // the platform's own drawing across the foot of the window under a toolbar
    // this window draws itself
    statusbar->hide();

    setupSendStrip();
    setupAddTabButton();

    // What no rule can say about the strip: the band the platform fills the bar
    // with behind the tabs, and the box a tab's cross is sized in. Said before
    // the notes are restored, so every tab's cross is caught as it is made.
    uiDesign::prepareTabStrip(tabWidget->tabBar());

    tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabWidget->tabBar(), &QWidget::customContextMenuRequested, this, &dlgNotepad::slot_tabContextMenu);
    connect(tabWidget, &QTabWidget::tabCloseRequested, this, &dlgNotepad::slot_tabCloseRequested);

    connect(action_stop, &QAction::triggered, this, &dlgNotepad::slot_stopSending);
    connect(action_sendAll, &QAction::triggered, this, &dlgNotepad::slot_sendAll);
    connect(action_sendLine, &QAction::triggered, this, &dlgNotepad::slot_sendLine);
    connect(action_sendSelection, &QAction::triggered, this, &dlgNotepad::slot_sendSelection);
    connect(lineEdit_prependText, &QLineEdit::textChanged, this, &dlgNotepad::updateNoPrefixWarning);

    if (mpHost) {
        restore();
        restoreSettings();
    }

    setupFindBar();
    connect(tabWidget, &QTabWidget::currentChanged, this, &dlgNotepad::slot_currentTabChanged);

    updateSendStrip();
    applyNotepadShellStyle();
    connect(mudlet::self(), &mudlet::signal_appearanceChanged, this, &dlgNotepad::slot_applyAppearance);

    startTimer(2min);
}

// The strip at the foot of the window, in the two readings it has: what a send
// would reach, and what a send is doing. Everything of both is on the bar at
// once and the visibility of the actions is what says which reading is on show,
// so nothing is built or torn down as a send starts and ends.
//
// The bar makes the button that stands for an action, so the shape it is drawn
// in, the word beside its glyph and the name a screen reader announces are set
// on that button rather than on the action behind it.
void dlgNotepad::setupSendStrip()
{
    //: Button at the foot of the notepad that sends the line the cursor is on to the game
    action_sendLine->setText(tr("Send line"));
    //: Tooltip of the notepad's Send line button
    action_sendLine->setToolTip(tr("Send the line the cursor is on to the game, with the prefix in front of it."));
    //: Tooltip of the notepad's Send selection button
    action_sendSelection->setToolTip(tr("Send the selected lines one at a time, the prefix in front of each."));
    //: Tooltip of the notepad's Send all button
    action_sendAll->setToolTip(tr("Send every line of this note one at a time, the prefix in front of each."));

    //: Button at the foot of the notepad that stops a send that is running
    action_stop = new QAction(tr("Stop"), this);
    //: Tooltip of the notepad's Stop button
    action_stop->setToolTip(tr("Stop sending. Lines already sent stay sent."));

    mNotepadActionGlyphs = {{action_sendLine, qsl(":/icons/notepad-send-line.svg"), QString()},
                            {action_sendSelection, qsl(":/icons/notepad-send-selection.svg"), QString()},
                            {action_sendAll, qsl(":/icons/notepad-send-all.svg"), QString()},
                            {action_stop, qsl(":/icons/notepad-stop.svg"), QString()}};

    // The seam the .ui file puts after the three send buttons parts them from
    // the prefix; while a send runs there is neither, so it goes away with them
    for (QAction* pAction : toolBar->actions()) {
        if (pAction->isSeparator()) {
            action_sendControlsSeparator = pAction;
            break;
        }
    }

    //: Word leading the field in the notepad that says what is put in front of every line sent
    label_prependText = new QLabel(tr("Before each line"), this);
    label_prependText->setObjectName(qsl("notepadPrefixLabel"));
    action_prependTextLabel = toolBar->addWidget(label_prependText);

    lineEdit_prependText = new QLineEdit(this);
    lineEdit_prependText->setObjectName(qsl("notepadPrefixField"));
    //: Example of what to type into the notepad's prefix field - a game command that takes the rest of the line
    lineEdit_prependText->setPlaceholderText(tr("e.g. say"));
    //: Accessible name of the notepad's prefix field, announced by a screen reader
    lineEdit_prependText->setAccessibleName(tr("Text to put before each line"));
    //: Tooltip of the notepad's prefix field. "say" and "tell" are game commands and are examples rather than words to translate.
    mPrefixFieldToolTip = tr("Put this in front of every line sent, for example say or tell Morquin.");
    lineEdit_prependText->setToolTip(mPrefixFieldToolTip);
    lineEdit_prependText->setClearButtonEnabled(true);

    // The warning's cue, the way the connection dialog carries the eye that
    // reveals a password: inside the field rather than on the bar, so it takes
    // no room from the strip and is there to be read at any width. Its picture
    // is inked in the style pass with everything else the page is mixed from.
    action_prefixWarning = new QAction(this);
    action_prefixWarning->setObjectName(qsl("notepadPrefixWarning"));
    action_prefixWarning->setVisible(false);
    lineEdit_prependText->addAction(action_prefixWarning, QLineEdit::TrailingPosition);
    // A line edit asks to be given whatever room there is, which on a bar would
    // leave nothing for the reading at the trailing end. What it holds is a
    // command and a name at most, so it takes the width Qt measures for a field
    // of its font instead - and the slack goes to the spacer below.
    lineEdit_prependText->setSizePolicy(QSizePolicy::Preferred, lineEdit_prependText->sizePolicy().verticalPolicy());
    action_prependText = toolBar->addWidget(lineEdit_prependText);

    // What stands where the buttons and the field were while a send runs: the
    // dot, how far along it is, the thread under that, and the one way to stop
    mpLabel_sendingDot = new QLabel(this);
    mpLabel_sendingDot->setObjectName(qsl("notepadSendingDot"));
    mpLabel_sendingDot->setFixedSize(scmNotepadReadingDotDiameter, scmNotepadReadingDotDiameter);
    action_sendingDot = toolBar->addWidget(mpLabel_sendingDot);

    mpLabel_sendingText = new QLabel(this);
    mpLabel_sendingText->setObjectName(qsl("notepadSendingText"));
    action_sendingText = toolBar->addWidget(mpLabel_sendingText);

    mpProgressBar_sending = new QProgressBar(this);
    mpProgressBar_sending->setObjectName(qsl("notepadSendingBar"));
    // The reading beside it is the number; the bar is the shape of how far along
    // the send is, and a percentage written across it says the same thing twice
    mpProgressBar_sending->setTextVisible(false);
    mpProgressBar_sending->setFixedHeight(scmNotepadProgressHeight);
    mpProgressBar_sending->setFixedWidth(scmNotepadProgressWidth);
    action_sendingBar = toolBar->addWidget(mpProgressBar_sending);

    toolBar->addAction(action_stop);

    // What holds the warning at the trailing end of the strip, whatever the
    // strip is carrying at its leading one
    auto* pSpacer = new QWidget(this);
    pSpacer->setObjectName(qsl("notepadStripSpacer"));
    pSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    uiDesign::markAsShellSurface(pSpacer);
    toolBar->addWidget(pSpacer);

    // A dot and the words after it, the way the editor's compile note is read:
    // one widget, so the pair is shown and hidden as the one reading it is
    mpWidget_noPrefixNote = new QWidget(this);
    mpWidget_noPrefixNote->setObjectName(qsl("notepadNoPrefixNote"));
    uiDesign::markAsShellSurface(mpWidget_noPrefixNote);
    auto* pNoteLayout = new QHBoxLayout(mpWidget_noPrefixNote);
    pNoteLayout->setContentsMargins(0, 0, 0, 0);
    pNoteLayout->setSpacing(scmNotepadReadingGap);
    mpLabel_noPrefixDot = new QLabel(mpWidget_noPrefixNote);
    mpLabel_noPrefixDot->setObjectName(qsl("notepadNoPrefixDot"));
    mpLabel_noPrefixDot->setFixedSize(scmNotepadReadingDotDiameter, scmNotepadReadingDotDiameter);
    pNoteLayout->addWidget(mpLabel_noPrefixDot);
    mpLabel_noPrefixText = new QLabel(mpWidget_noPrefixNote);
    mpLabel_noPrefixText->setObjectName(qsl("notepadNoPrefixText"));
    pNoteLayout->addWidget(mpLabel_noPrefixText);
    action_noPrefixNote = toolBar->addWidget(mpWidget_noPrefixNote);
    // A bar with more on it than it has room for posts its tail into a
    // drop-down, and the warning is the last thing on this one - so which of
    // the warning's forms fits is measured on every resize instead
    toolBar->installEventFilter(this);

    // Every button on the bar carries a word beside its picture: what each of
    // them sends is the whole of what tells them apart
    const int glyphSize = qRound(toolBar->fontMetrics().height() * 0.9);
    toolBar->setIconSize(QSize(glyphSize, glyphSize));
    const QList<QPair<QAction*, QString>> announced{//: Accessible name of the notepad's Send line button, announced by a screen reader
                                                    {action_sendLine, tr("Send the current line")},
                                                    //: Accessible name of the notepad's Send selection button, announced by a screen reader
                                                    {action_sendSelection, tr("Send the selected lines")},
                                                    //: Accessible name of the notepad's Send all button, announced by a screen reader
                                                    {action_sendAll, tr("Send all lines")},
                                                    //: Accessible name of the notepad's Stop button, announced by a screen reader
                                                    {action_stop, tr("Stop sending")}};
    for (const auto& button : announced) {
        if (auto* pButton = qobject_cast<QToolButton*>(toolBar->widgetForAction(button.first))) {
            pButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            pButton->setAccessibleName(button.second);
        }
    }

    setSending(false);
}

// A tool button is given the platform's tool button font - macOS hands out a
// smaller one than a label gets - so a word or a field set beside those buttons
// in the window's own size sat larger than them and off their line. The size is
// carried by the bar's sheet rather than set on each word: a stylesheet's
// polish resolves a widget's font again from its parent, and took a font set
// directly straight back off the label on the window's first show. It is read
// off the application's class font rather than off a button, which only takes
// that font when it is polished for the first show, after the sheet is built.
// Not a step of uiDesign::typeSize(): that scale is measured off the
// application font, and what these words have to line up with is the platform's
// tool button font, which on macOS is a different number entirely. Rounded to a
// whole point on the way out, since a fraction in a sheet is a size the parser
// reads and no metric ever comes to.
int dlgNotepad::stripWordPointSize() const
{
    const qreal buttonSize = QApplication::font("QToolButton").pointSizeF();
    return std::max(1, qRound(buttonSize > 0.0 ? buttonSize : toolBar->font().pointSizeF()));
}

void dlgNotepad::setupAddTabButton()
{
    mpAddTabButton = new QToolButton(this);
    mpAddTabButton->setObjectName(qsl("notepadAddTab"));
    mpAddTabButton->setToolTip(tr("Add new note tab (Ctrl+T)"));
    mpAddTabButton->setAutoRaise(false);
    connect(mpAddTabButton, &QToolButton::clicked, this, &dlgNotepad::slot_addTabClicked);
    tabWidget->setCornerWidget(mpAddTabButton, Qt::TopRightCorner);

    auto* newTabShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_T), this);
    connect(newTabShortcut, &QShortcut::activated, this, &dlgNotepad::slot_addTabClicked);
}

// closeTab() keeps the last note, and the context menu offers no close for it,
// so a cross on a sole tab would promise what it cannot do. Toggling the whole
// bar rather than one tab's button also gives the chip its width back.
void dlgNotepad::updateTabClosability()
{
    tabWidget->setTabsClosable(tabWidget->count() > 1);
}

void dlgNotepad::setupFindBar()
{
    mpFindBar = new QWidget(this);
    mpFindBar->setObjectName(qsl("notepadFindBar"));
    auto* layout = new QHBoxLayout(mpFindBar);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(2);

    mpFindLineEdit = new QLineEdit(mpFindBar);
    mpFindLineEdit->setObjectName(qsl("notepadFindField"));
    //: Placeholder text for the search field in notepad
    mpFindLineEdit->setPlaceholderText(tr("Find"));
    mpFindLineEdit->setClearButtonEnabled(true);
    mpFindLineEdit->installEventFilter(this);

    // The three glyphs are set in the style pass, which is also where a theme
    // change re-inks them
    mpFindPrevButton = new QToolButton(mpFindBar);
    mpFindPrevButton->setObjectName(qsl("notepadFindPrevious"));
    mpFindPrevButton->setToolTip(tr("Find previous"));
    mpFindPrevButton->setAutoRaise(false);

    mpFindNextButton = new QToolButton(mpFindBar);
    mpFindNextButton->setObjectName(qsl("notepadFindNext"));
    mpFindNextButton->setToolTip(tr("Find next"));
    mpFindNextButton->setAutoRaise(false);

    mpFindCloseButton = new QToolButton(mpFindBar);
    mpFindCloseButton->setObjectName(qsl("notepadFindClose"));
    mpFindCloseButton->setToolTip(tr("Close find bar"));
    mpFindCloseButton->setAutoRaise(false);

    layout->addWidget(mpFindLineEdit, 1);
    layout->addWidget(mpFindPrevButton);
    layout->addWidget(mpFindNextButton);
    layout->addWidget(mpFindCloseButton);

    verticalLayout->addWidget(mpFindBar);
    mpFindBar->hide();

    connect(mpFindLineEdit, &QLineEdit::textChanged, this, &dlgNotepad::slot_findTextChanged);
    connect(mpFindPrevButton, &QToolButton::clicked, this, &dlgNotepad::slot_findPrevious);
    connect(mpFindNextButton, &QToolButton::clicked, this, &dlgNotepad::slot_findNext);
    connect(mpFindCloseButton, &QToolButton::clicked, this, &dlgNotepad::slot_hideFindBar);

    mpFindShortcut = new QShortcut(QKeySequence::Find, this);
    connect(mpFindShortcut, &QShortcut::activated, this, &dlgNotepad::slot_showFindBar);
}

void dlgNotepad::slot_addTabClicked()
{
    addTab();
    tabWidget->setCurrentIndex(tabWidget->count() - 1);
}

void dlgNotepad::setFont(const QFont& font)
{
    for (int i = 0; i < tabWidget->count(); ++i) {
        if (auto* textEdit = qobject_cast<QPlainTextEdit*>(tabWidget->widget(i))) {
            textEdit->setFont(font);
        }
    }
}

void dlgNotepad::slot_applyAppearance()
{
    applyNotepadShellStyle();
}

// The window in the design language: the page and the strip of tabs on it, the
// notes drawn as the fields they are typed into, the find bar between them and
// the bar of actions at the foot.
//
// Every sheet goes on a widget rather than on the window, because a profile's
// Lua stylesheet is assigned to the window itself on every show - so a sheet
// set here would be replaced by it, and a rule of the profile's is beaten by
// one of these, which names the container it is scoped to.
void dlgNotepad::applyNotepadShellStyle()
{
    const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();

    // The page under the tabs, the strip of chips across it, the notes as
    // fields and the find bar's own surface. inputStyleSheet() is scoped to the
    // central widget rather than left unscoped: the prepend field lives on the
    // toolbar, which is not under this, and takes the same recipe there.
    const QString centralRules =
            qsl("QWidget#centralwidget { background-color: %1; }").arg(tokens.page.name()) + uiDesign::tabBarStyleSheet(qsl("#tabWidget"), tokens)
            + uiDesign::inputStyleSheet(tokens, qsl("#centralwidget"))
            // A row of its own between the notes and the bar, so it carries the
            // seam a bar does - on the edge that parts it from what is above it
            + qsl("QWidget#notepadFindBar { background-color: %1; border-top: %2px solid %3; }").arg(tokens.page.name(), QString::number(uiDesign::scmInputBorderWidth), tokens.border.name())
            // The four buttons that are a picture and nothing else - the one
            // that adds a note and the three the find bar is worked from -
            // drawn the way the editor draws its clear-sound-file button: a
            // frame round a glyph this small reads as a second control
            + qsl("QToolButton#notepadAddTab, QWidget#notepadFindBar QToolButton"
                  " { border: none; border-radius: %1px; background: transparent; padding: 2px; }"
                  "QToolButton#notepadAddTab:hover, QWidget#notepadFindBar QToolButton:hover { background-color: %2; }")
                      .arg(QString::number(uiDesign::scmRadiusChip), tokens.hoverSoft);
    centralwidget->setStyleSheet(centralRules);

    // The one state colour the strip reads in: what a send with no prefix in
    // front of it would do is a warning rather than a fault, and the words are
    // walked off the page they are written on until they can be read there
    const QColor warningColour = uiDesign::stateColor(uiDesign::scmStateHue_warning, tokens.darkPage);
    const QColor warningInk = uiDesign::readableOn(tokens.page, warningColour, tokens.text, uiDesign::scmTextMinimumRatio);

    // The bar is at the foot of the window, so its seam is on top. The field
    // that says what goes in front of every line takes the same recipe every
    // other field in the window does, and the two readings on the strip - how
    // far along a send is, and what a send with no prefix would come to - are
    // the only words on it that are not chrome.
    const QString toolBarRules = uiDesign::toolBarStyleSheet(qsl("QToolBar#toolBar"), uiDesign::ToolBarSeam::Top, tokens)
                                 + uiDesign::inputStyleSheet(tokens, qsl("QToolBar#toolBar"))
                                 // The word leading that field is the bar's own scaffolding, so it
                                 // is written in the tone every other word of chrome is
                                 + qsl("QToolBar#toolBar QLabel { color: %1; }").arg(tokens.mutedText.name())
                                 // ...and set in the size the bar's buttons are, see stripWordPointSize()
                                 + qsl("QToolBar#toolBar QLabel, QToolBar#toolBar QLineEdit { font-size: %1pt; }").arg(QString::number(stripWordPointSize()))
                                 // Written after the shared rules so it wins the specificity tie
                                 // against QLineEdit:focus, and in the same border shorthand that
                                 // rule writes: what the reader was warned about stays said while
                                 // they are typing the answer to it
                                 + qsl("QToolBar#toolBar QLineEdit[%1=\"warning\"] { border: %2px solid %3; }")
                                           .arg(QString::fromLatin1(scmProp_notepadFieldState), QString::number(uiDesign::scmInputBorderWidth), warningColour.name())
                                 // Both readings are named down to the label, or the chrome rule
                                 // above beats them on the length of its selector. The air either
                                 // side of the sending words is the label's own: the bar's spacing
                                 // parts one control from the next, and a dot and a thread are
                                 // neither.
                                 + qsl("QToolBar#toolBar QLabel#notepadSendingText { color: %1; padding: 0px %2px; }").arg(tokens.accentText.name(), QString::number(scmNotepadReadingGap))
                                 + qsl("QToolBar#toolBar QLabel#notepadNoPrefixText { color: %1; }").arg(warningInk.name())
                                 // A thread rather than a trough: the corner is half the bar's own
                                 // height, which is what rounds its ends instead of cutting them
                                 + qsl("#notepadSendingBar { border: none; background-color: %1; border-radius: %3px; }"
                                       "#notepadSendingBar::chunk { background-color: %2; border-radius: %3px; }")
                                           .arg(tokens.accentSoft, tokens.accent.name(), QString::number(scmNotepadProgressHeight / 2));
    toolBar->setStyleSheet(toolBarRules);
    alignPrefixLeadWord();

    // The dot each reading begins with, drawn where the words after it cannot
    // be: one is the accent, because a send running is this window doing what
    // it was asked, and the other is the warning hue
    mpLabel_sendingDot->setStyleSheet(qsl("#notepadSendingDot { background-color: %1; border-radius: %2px; }").arg(tokens.accent.name(), QString::number(scmNotepadReadingDotDiameter / 2)));
    mpLabel_noPrefixDot->setStyleSheet(qsl("#notepadNoPrefixDot { background-color: %1; border-radius: %2px; }").arg(warningColour.name(), QString::number(scmNotepadReadingDotDiameter / 2)));

    // The cue inside the prefix field, in the same hue as the dot and drawn at
    // the size the editor's banner draws its own warning glyph. It is the whole
    // of the warning at a width the words cannot be read whole at, so it is
    // re-inked here with everything else the appearance moves.
    if (action_prefixWarning) {
        const qreal glyphRatio = lineEdit_prependText->devicePixelRatioF();
        QPixmap cue = uiDesign::tintedGlyph(uiDesign::glyphPixmap(qsl(":/icons/editor-notice-warning.svg")), warningColour)
                              .scaled(QSize(scmNotepadFieldCueSize, scmNotepadFieldCueSize) * glyphRatio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        cue.setDevicePixelRatio(glyphRatio);
        action_prefixWarning->setIcon(QIcon(cue));
    }

    uiDesign::restyleActionGlyphs(mNotepadActionGlyphs, tokens);
    mpAddTabButton->setIcon(uiDesign::tintedIcon(qsl(":/icons/editor-add.svg"), tokens));
    mpFindPrevButton->setIcon(uiDesign::tintedIcon(qsl(":/icons/notepad-chevron-up.svg"), tokens));
    mpFindNextButton->setIcon(uiDesign::tintedIcon(qsl(":/icons/editor-chevron-down.svg"), tokens));
    mpFindCloseButton->setIcon(uiDesign::tintedIcon(qsl(":/icons/editor-clear.svg"), tokens));

    uiDesign::keepClickFocusOffControls(centralwidget);
    uiDesign::keepClickFocusOffControls(toolBar);
    uiDesign::letPopupsTakeTheFieldsCorner(centralwidget);
    uiDesign::letPopupsTakeTheFieldsCorner(toolBar);

    // The marker pen on the match the cursor is on, and a wash of it on the
    // rest, mixed here so that both follow an appearance change - the ones
    // already drawn are laid down again below
    mCurrentMatchInk = tokens.marker;
    mOtherMatchInk = uiDesign::blend(tokens.field, tokens.marker, scmNotepadQuietMatchWash);
    if (mpFindBar->isVisible()) {
        highlightAllMatches();
    }

    // What the words of the warning come to is the font's answer, and a theme
    // change is one of the things that moves it
    fitNoPrefixNote();
}

dlgNotepad::~dlgNotepad()
{
    if (mpHost && mpHost->mpNotePad) {
        save();
        mpHost->mpNotePad = nullptr;
    }
}

int dlgNotepad::addTab(const QString& name, const QString& content)
{
    // The note is drawn by the shell's own sheet, which reaches it through the
    // central widget: a stylesheet set on the note itself would beat that one,
    // and a profile's Lua stylesheet still reaches the window as a whole. The
    // display font is the profile's and stays here.
    auto* textEdit = new QPlainTextEdit(this);
    if (mpHost) {
        textEdit->setFont(mpHost->getDisplayFont());
    }
    if (!content.isEmpty()) {
        textEdit->setPlainText(content);
    }

    connect(textEdit, &QPlainTextEdit::textChanged, this, &dlgNotepad::slot_textChanged);
    // What the strip says about this note - how far each button reaches, and
    // whether sending it with no prefix is worth a word - is true of the note as
    // it is now, so it is taken again whenever the note or the selection moves
    connect(textEdit, &QPlainTextEdit::textChanged, this, &dlgNotepad::updateSendStrip);
    connect(textEdit, &QPlainTextEdit::selectionChanged, this, &dlgNotepad::updateSendStrip);

    //: Default name for a new note tab
    const QString tabName = name.isEmpty() ? tr("New Note") : name;
    const int index = tabWidget->addTab(textEdit, tabName);
    updateTabClosability();
    return index;
}

// Nothing keeps a copy of a closed note - save() writes the tabs that are left,
// so the text of one closed by a slip of the finger is gone for good. A note
// with nothing in it is no loss and goes without a word; for anything else the
// whole act is put to the reader once, with Cancel as both the default and the
// escape, so that Return and Escape alike keep the note.
bool dlgNotepad::okToDiscardNotes(const QList<int>& indices)
{
    int withText = 0;
    for (const int index : indices) {
        if (index < 0 || index >= tabWidget->count()) {
            continue;
        }
        auto* pNote = qobject_cast<QPlainTextEdit*>(tabWidget->widget(index));
        if (pNote && !pNote->toPlainText().trimmed().isEmpty()) {
            ++withText;
        }
    }

    if (!withText) {
        return true;
    }

    QString ask;
    QString consequence;
    QString destroy;
    if (indices.size() == 1) {
        //: Dialog asking the reader to confirm closing a note that has text in it. %1 is the note's name.
        ask = tr("Close the note \"%1\"?").arg(tabWidget->tabText(indices.constFirst()));
        //: Body of the dialog asking the reader to confirm closing a note that has text in it
        consequence = tr("Its text will be lost. Mudlet keeps no copy of a closed note.");
        //: Button on that dialog which goes ahead and closes the one note it asked about
        destroy = tr("Close Note");
    } else {
        //: Dialog asking the reader to confirm closing every note but the one they picked
        ask = tr("Close the other notes?");
        //: Body of that dialog. %n is how many of the notes being closed have text in them.
        consequence = tr("Text in %n of them will be lost.", "", withText);
        //: Button on that dialog which goes ahead and closes every note but the one the reader picked
        destroy = tr("Close Notes");
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    // A sheet on the note's own window on macOS, rather than a box over the
    // whole application: what is being asked about is this window's
    box.setWindowModality(Qt::WindowModal);
    box.setText(ask);
    box.setInformativeText(consequence);
    box.addButton(destroy, QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.setEscapeButton(QMessageBox::Cancel);
    box.exec();

    return box.buttonRole(box.clickedButton()) == QMessageBox::DestructiveRole;
}

void dlgNotepad::closeTab(int index)
{
    if (index < 0 || index >= tabWidget->count()) {
        return;
    }

    if (tabWidget->count() <= 1) {
        return;
    }

    if (!okToDiscardNotes({index})) {
        return;
    }

    save();

    QWidget* widget = tabWidget->widget(index);
    tabWidget->removeTab(index);
    delete widget;
    updateTabClosability();

    mNeedToSave = true;
}

// The whole sweep is one act, so it is asked about once rather than a note at a
// time - and taken from the back, since removing a tab renumbers the ones after
// it
void dlgNotepad::closeOtherTabs(int keptIndex)
{
    if (keptIndex < 0 || keptIndex >= tabWidget->count() || tabWidget->count() <= 1) {
        return;
    }

    QList<int> others;
    for (int i = 0; i < tabWidget->count(); ++i) {
        if (i != keptIndex) {
            others.append(i);
        }
    }

    if (!okToDiscardNotes(others)) {
        return;
    }

    save();

    for (int i = tabWidget->count() - 1; i >= 0; --i) {
        if (i != keptIndex) {
            QWidget* widget = tabWidget->widget(i);
            tabWidget->removeTab(i);
            delete widget;
        }
    }
    updateTabClosability();

    mNeedToSave = true;
}

void dlgNotepad::renameTab(int index)
{
    if (index < 0 || index >= tabWidget->count()) {
        return;
    }

    const QString currentName = tabWidget->tabText(index);
    bool ok = false;
    //: Dialog title for renaming a note tab
    const QString newName = QInputDialog::getText(this,
                                                  tr("Rename Note Tab"),
                                                  //: Label for the input field when renaming a note tab
                                                  tr("New name:"),
                                                  QLineEdit::Normal,
                                                  currentName,
                                                  &ok);
    if (ok && !newName.isEmpty() && newName != currentName) {
        tabWidget->setTabText(index, newName);
        mNeedToSave = true;
    }
}

void dlgNotepad::slot_tabCloseRequested(int index)
{
    closeTab(index);
}

void dlgNotepad::slot_tabContextMenu(const QPoint& pos)
{
    const int tabIndex = tabWidget->tabBar()->tabAt(pos);

    QMenu menu(this);
    // Built at the moment it is needed, so it takes both halves of the recipe
    // from here rather than from the window's style pass
    menu.setStyleSheet(uiDesign::menuStyleSheet(uiDesign::themeTokens()));
    uiDesign::letPopupsTakeTheFieldsCorner(&menu);

    //: Context menu action to create a new note tab
    QAction* newTabAction = menu.addAction(tr("New Tab"));
    connect(newTabAction, &QAction::triggered, this, [this]() {
        addTab();
        tabWidget->setCurrentIndex(tabWidget->count() - 1);
    });

    if (tabIndex >= 0) {
        menu.addSeparator();

        //: Context menu action to rename a note tab
        QAction* renameAction = menu.addAction(tr("Rename Tab"));
        connect(renameAction, &QAction::triggered, this, [this, tabIndex]() {
            renameTab(tabIndex);
        });

        if (tabWidget->count() > 1) {
            //: Context menu action to close a note tab
            QAction* closeAction = menu.addAction(tr("Close Tab"));
            connect(closeAction, &QAction::triggered, this, [this, tabIndex]() {
                closeTab(tabIndex);
            });

            menu.addSeparator();

            //: Context menu action to close all note tabs except the clicked one
            QAction* closeOthersAction = menu.addAction(tr("Close Other Tabs"));
            connect(closeOthersAction, &QAction::triggered, this, [this, tabIndex]() {
                closeOtherTabs(tabIndex);
            });
        }
    }

    menu.exec(tabWidget->tabBar()->mapToGlobal(pos));
}

QPlainTextEdit* dlgNotepad::currentTextEdit() const
{
    return qobject_cast<QPlainTextEdit*>(tabWidget->currentWidget());
}

void dlgNotepad::save()
{
    const QString directoryPath = mudlet::getMudletPath(enums::profileHomePath, mpHost->getName());
    const QString fileName = mudlet::getMudletPath(enums::profileDataItemPath, mpHost->getName(), jsonNotesFileName);

    const QDir dir;
    if (!dir.exists(directoryPath)) {
        dir.mkpath(directoryPath);
    }

    QJsonObject root;
    root.insert(qsl("version"), 1);

    QJsonArray tabsArray;
    for (int i = 0; i < tabWidget->count(); ++i) {
        QJsonObject tabObj;
        tabObj.insert(qsl("name"), tabWidget->tabText(i));

        if (auto* textEdit = qobject_cast<QPlainTextEdit*>(tabWidget->widget(i))) {
            tabObj.insert(qsl("content"), textEdit->toPlainText());
        }

        tabsArray.append(tabObj);
    }

    root.insert(qsl("tabs"), tabsArray);
    root.insert(qsl("activeTab"), tabWidget->currentIndex());

    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "dlgNotepad::save: failed to open file for writing:" << file.errorString();
        return;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qDebug() << "dlgNotepad::save: error saving notepad contents:" << file.errorString();
    }

    mNeedToSave = false;
}

bool dlgNotepad::migrateOldNotesFile()
{
    QString oldFileName = mudlet::getMudletPath(enums::profileDataItemPath, mpHost->getName(), utf8EncodedNotesFileName);
    bool useUtf8 = true;

    if (!QFile::exists(oldFileName)) {
        oldFileName = mudlet::getMudletPath(enums::profileDataItemPath, mpHost->getName(), local8BitEncodedNotesFileName);
        useUtf8 = false;

        if (!QFile::exists(oldFileName)) {
            return false;
        }
    }

    QFile file(oldFileName);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "dlgNotepad::migrateOldNotesFile: failed to open file for reading:" << file.errorString();
        return false;
    }

    QTextStream fileStream(&file);
    if (!useUtf8) {
        fileStream.setEncoding(QStringEncoder::Encoding::System);
    }

    const QString content = fileStream.readAll();
    file.close();

    //: Name for the migrated notes tab when upgrading from single-note to tabbed notepad
    addTab(tr("Notes"), content);

    return true;
}

void dlgNotepad::restore()
{
    const QString fileName = mudlet::getMudletPath(enums::profileDataItemPath, mpHost->getName(), jsonNotesFileName);

    if (QFile::exists(fileName)) {
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly)) {
            qDebug() << "dlgNotepad::restore: failed to open file for reading:" << file.errorString();
            //: Default name for the first note tab
            addTab(tr("Notes"));
            return;
        }

        const QByteArray data = file.readAll();
        file.close();

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

        if (parseError.error != QJsonParseError::NoError) {
            qDebug() << "dlgNotepad::restore: JSON parse error:" << parseError.errorString();
            addTab(tr("Notes"));
            return;
        }

        if (!doc.isObject()) {
            qDebug() << "dlgNotepad::restore: JSON root is not an object";
            addTab(tr("Notes"));
            return;
        }

        const QJsonObject root = doc.object();
        const QJsonArray tabsArray = root.value(qsl("tabs")).toArray();

        if (tabsArray.isEmpty()) {
            addTab(tr("Notes"));
        } else {
            for (const QJsonValue& tabValue : tabsArray) {
                const QJsonObject tabObj = tabValue.toObject();
                const QString name = tabObj.value(qsl("name")).toString();
                const QString content = tabObj.value(qsl("content")).toString();
                addTab(name, content);
            }

            const int activeTab = root.value(qsl("activeTab")).toInt(0);
            if (activeTab >= 0 && activeTab < tabWidget->count()) {
                tabWidget->setCurrentIndex(activeTab);
            }
        }
    } else if (!migrateOldNotesFile()) {
        addTab(tr("Notes"));
    }
}

void dlgNotepad::slot_textChanged()
{
    mNeedToSave = true;
}

void dlgNotepad::timerEvent(QTimerEvent* event)
{
    Q_UNUSED(event)

    if (!mNeedToSave) {
        return;
    }

    save();
}

void dlgNotepad::slot_sendAll()
{
    auto* textEdit = currentTextEdit();
    if (!textEdit) {
        return;
    }

    const QString allText = textEdit->toPlainText();
    const QStringList lines = allText.split('\n');
    startSendingLines(lines);
}

void dlgNotepad::slot_sendLine()
{
    auto* textEdit = currentTextEdit();
    if (!textEdit) {
        return;
    }

    QTextCursor cursor = textEdit->textCursor();
    cursor.select(QTextCursor::LineUnderCursor);
    const QString line = cursor.selectedText();

    if (!line.isEmpty()) {
        startSendingLines(QStringList{line});
    }
}

void dlgNotepad::slot_sendSelection()
{
    auto* textEdit = currentTextEdit();
    if (!textEdit) {
        return;
    }

    QString selectedText = textEdit->textCursor().selectedText();

    if (!selectedText.isEmpty()) {
        const QStringList lines = selectedText.replace(QChar(0x2029), qsl("\n")).split('\n');
        startSendingLines(lines);
    }
}

void dlgNotepad::startSendingLines(const QStringList& lines)
{
    mLinesToSend = lines;
    mCurrentLineIndex = 0;
    mLinesSent = 0;
    mLinesToSendCount = sendableLineCount(lines);

    if (!mSendTimer) {
        mSendTimer = new QTimer(this);
        connect(mSendTimer, &QTimer::timeout, this, &dlgNotepad::slot_sendNextLine);
    }

    setSending(true);
    updateSendingReading();
    //: Announced when the notepad starts sending a note to the game. %n is how many lines will go.
    mudlet::self()->announce(mLinesToSendCount == 1 ? tr("Sending 1 line") : tr("Sending %n lines", "", mLinesToSendCount));
    mSendTimer->start(300ms);
}

void dlgNotepad::slot_sendNextLine()
{
    if (mCurrentLineIndex >= mLinesToSend.size()) {
        finishSending(false);
        return;
    }

    const QString line = mLinesToSend[mCurrentLineIndex++];
    if (!line.isEmpty() && mpHost) {
        mpHost->send(lineEdit_prependText->text() + line);
        ++mLinesSent;
        updateSendingReading();
    }

    if (mCurrentLineIndex >= mLinesToSend.size()) {
        finishSending(false);
    }
}

void dlgNotepad::slot_stopSending()
{
    finishSending(true);
}

// The words on the strip while a send runs, and the thread under them
void dlgNotepad::updateSendingReading()
{
    if (!mpLabel_sendingText || !mpProgressBar_sending) {
        return;
    }
    //: Reading on the notepad's send strip while lines are going to the game. %1 is how many have gone, %2 how many there are in all.
    mpLabel_sendingText->setText(tr("Sending %1 of %2").arg(QString::number(mLinesSent), QString::number(mLinesToSendCount)));
    // A send of nothing at all still has a bar, and a bar with no range is the
    // busy indicator Qt draws when it is asked for one
    mpProgressBar_sending->setMaximum(qMax(1, mLinesToSendCount));
    mpProgressBar_sending->setValue(mLinesSent);
}

void dlgNotepad::finishSending(const bool stopped)
{
    if (mSendTimer) {
        mSendTimer->stop();
    }

    if (mSending) {
        if (stopped) {
            //: Announced when a send from the notepad was stopped part way. %1 is how many lines went before it stopped, %2 how many there were in all.
            mudlet::self()->announce(tr("Stopped after %1 of %2 lines").arg(QString::number(mLinesSent), QString::number(mLinesToSendCount)));
        } else {
            //: Announced when the notepad has sent a note to the game. %n is how many lines went.
            mudlet::self()->announce(mLinesSent == 1 ? tr("Sent 1 line") : tr("Sent %n lines", "", mLinesSent));
        }
    }

    mLinesToSend.clear();
    mCurrentLineIndex = 0;
    setSending(false);
}

// Which of the strip's two readings is on show. Every action is on the bar all
// along, so this is the whole of the change from one to the other - and Stop is
// hidden rather than merely unavailable, since there is nothing to stop.
void dlgNotepad::setSending(const bool sending)
{
    mSending = sending;

    action_sendLine->setVisible(!sending);
    action_sendSelection->setVisible(!sending);
    action_sendAll->setVisible(!sending);
    if (action_sendControlsSeparator) {
        action_sendControlsSeparator->setVisible(!sending);
    }
    if (action_prependTextLabel) {
        action_prependTextLabel->setVisible(!sending);
    }
    if (action_prependText) {
        action_prependText->setVisible(!sending);
    }

    if (action_sendingDot) {
        action_sendingDot->setVisible(sending);
    }
    if (action_sendingText) {
        action_sendingText->setVisible(sending);
    }
    if (action_sendingBar) {
        action_sendingBar->setVisible(sending);
    }
    if (action_stop) {
        action_stop->setVisible(sending);
    }

    updateNoPrefixWarning();
}

// Both readings the strip carries about the note in front of the reader
void dlgNotepad::updateSendStrip()
{
    updateSendReach();
    updateNoPrefixWarning();
}

// How far each button reaches, on the button itself: a count read before the
// click is what says whether this is the note that was meant
void dlgNotepad::updateSendReach()
{
    auto* pTextEdit = currentTextEdit();
    const int inTheNote = pTextEdit ? sendableLineCount(linesOf(pTextEdit->toPlainText())) : 0;
    const int inTheSelection = pTextEdit ? sendableLineCount(linesOf(pTextEdit->textCursor().selectedText())) : 0;

    // The singular is its own string rather than a plural form: with no
    // catalogue loaded Qt hands back the source text as written, and this is a
    // window most people read in English
    //: Button at the foot of the notepad. %n is how many lines of the note would go to the game, one at a time.
    action_sendAll->setText(inTheNote == 1 ? tr("Send all (1 line)") : tr("Send all (%n lines)", "", inTheNote));
    action_sendAll->setEnabled(inTheNote > 0);

    if (inTheSelection > 0) {
        //: Button at the foot of the notepad. %n is how many of the note's lines are selected.
        action_sendSelection->setText(inTheSelection == 1 ? tr("Send selection (1 line)") : tr("Send selection (%n lines)", "", inTheSelection));
    } else {
        //: Button at the foot of the notepad, with nothing selected to say a count of
        action_sendSelection->setText(tr("Send selection"));
    }
    action_sendSelection->setEnabled(inTheSelection > 0);
    alignPrefixLeadWord();
}

// The seam between Send all and the lead word parts two words, and they have to
// stand the same distance from it. A tool button holds its word a padding in
// from its edge and then some: QToolButton::sizeHint() asks for two spaces
// more than the word measures and the stylesheet style adds three pixels on
// top, and QCommonStyle draws the word left-aligned after the picture - so all
// of that slack lands after the word, on the seam's side. A label holds its
// word at its edge. The word is therefore held in by what the button's own
// geometry says it holds its word in by: the button's width less its frame,
// its padding, the picture and the gap Qt leaves after it, and the word.
void dlgNotepad::alignPrefixLeadWord()
{
    const auto* pSendAll = qobject_cast<const QToolButton*>(toolBar->widgetForAction(action_sendAll));
    if (!pSendAll || !label_prependText) {
        return;
    }
    const int frame = uiDesign::scmInputBorderWidth + uiDesign::scmToolBarButtonPaddingHorizontal;
    const int picture = pSendAll->iconSize().width() + scmQtToolButtonPictureGap;
    const int word = pSendAll->fontMetrics().horizontalAdvance(pSendAll->text());
    const int afterTheWord = std::max(0, pSendAll->sizeHint().width() - 2 * frame - picture - word);
    const QMargins held(uiDesign::scmToolBarButtonPaddingHorizontal + afterTheWord, 0, uiDesign::scmToolBarButtonPaddingHorizontal, 0);
    if (label_prependText->contentsMargins() == held) {
        return;
    }
    label_prependText->setContentsMargins(held);
    // A wider word is less room for the warning at the strip's end, which was
    // fitted to the room there was
    fitNoPrefixNote();
}

// The one thing about a send that is worth saying before it happens: with no
// prefix, every line of the note is a command to the game. Short notes are left
// alone - a handful of lines is what a sequence of commands looks like, and
// saying it there would be saying it about nothing.
void dlgNotepad::updateNoPrefixWarning()
{
    if (!mpWidget_noPrefixNote || !action_noPrefixNote) {
        return;
    }

    auto* pTextEdit = currentTextEdit();
    const int inTheNote = pTextEdit ? sendableLineCount(linesOf(pTextEdit->toPlainText())) : 0;
    const bool warn = !mSending && lineEdit_prependText->text().isEmpty() && inTheNote > scmNotepadWarnAtLines;

    // Whether the warning applies is this; whether the strip has room to say it
    // in words is the fit's, and it is the fit that takes the reading off the bar
    mNoPrefixWarningApplies = warn;
    if (warn) {
        //: Warning at the foot of the notepad, shown when a long note would be sent with nothing in front of its lines. %n is how many lines the note holds.
        mNoPrefixNoteText = tr("No prefix, so %n lines go to the game as commands", "", inTheNote);
        // Whatever of it the strip has room for, the whole of it is here
        mpWidget_noPrefixNote->setToolTip(mNoPrefixNoteText);
        fitNoPrefixNote();
    } else {
        action_noPrefixNote->setVisible(false);
    }

    // ...and here, on the cue in the field and on the field itself, which is
    // where a reader is looking when the answer to the warning is typed
    if (action_prefixWarning) {
        action_prefixWarning->setToolTip(warn ? mNoPrefixNoteText : QString());
        action_prefixWarning->setVisible(warn);
    }
    lineEdit_prependText->setToolTip(warn ? mNoPrefixNoteText : mPrefixFieldToolTip);

    const QString state = warn ? qsl("warning") : QString();
    if (lineEdit_prependText->property(scmProp_notepadFieldState).toString() != state) {
        lineEdit_prependText->setProperty(scmProp_notepadFieldState, state.isEmpty() ? QVariant() : QVariant(state));
        uiDesign::repolish(lineEdit_prependText);
    }
}

// The warning is the last thing on the strip, and a bar with more on it than it
// has room for posts its tail into a drop-down - which would put the one reading
// that matters at that moment behind a chevron. The strip's own content wants
// more width than the window opens at, so there is rarely room for the sentence:
// cutting it left the reader a fragment ending mid-word, which is worse than no
// words at all. So the words take whichever of three forms the room is enough
// for - the sentence, the short form, or nothing beside the cue in the field,
// which says it at any width. Measured against the bar's own hint rather than
// added up here, so the separators, the spacing and the padding a stylesheet
// hands out are counted the way Qt counts them.
void dlgNotepad::fitNoPrefixNote()
{
    if (!mpLabel_noPrefixText || !action_noPrefixNote || !mNoPrefixWarningApplies || mNoPrefixNoteText.isEmpty()) {
        return;
    }
    // A bar out of room hides what it could not fit, and what is hidden is no
    // part of what the bar asks for - so the reading is put back before the
    // measurement, or the room would be measured as if it were not there
    action_noPrefixNote->setVisible(true);
    mpWidget_noPrefixNote->show();
    mpLabel_noPrefixText->clear();
    // A layout answers with what it worked out last time until it is told the
    // question changed, and the words were only just taken off the label
    uiDesign::invalidateLayoutsUpTo(mpLabel_noPrefixText, toolBar);
    const int roomForTheWords = toolBar->width() - toolBar->sizeHint().width();

    //: Short form of the notepad's no-prefix warning, on the strip where there is no room for the whole sentence, which stays on the tooltip
    const QStringList forms{mNoPrefixNoteText, tr("No prefix")};
    for (const QString& words : forms) {
        // What the label asks for with those words in it, which is the measure
        // the layout will take of it in a moment
        mpLabel_noPrefixText->setText(words);
        if (mpLabel_noPrefixText->sizeHint().width() <= roomForTheWords) {
            return;
        }
    }

    // Neither form fits. The dot with nothing after it says a colour and no
    // more, so the whole reading goes and the field's cue carries the warning.
    // The action is what goes rather than the widget: a bar shows a widget it
    // has room for again on its next layout, whatever the widget was told.
    mpLabel_noPrefixText->clear();
    action_noPrefixNote->setVisible(false);
}

void dlgNotepad::saveSettings()
{
    if (!mpHost) {
        return;
    }

    mpHost->writeProfileIniData(qsl("Notepad/WindowState"), QString::fromLatin1(saveState().toBase64()));
    // The send controls have no fold to remember any more, so the key an older
    // notepad wrote for it is taken out rather than left in every profile
    mpHost->removeProfileIniData(qsl("Notepad/SendControlsVisible"));
}

void dlgNotepad::restoreSettings()
{
    if (!mpHost) {
        return;
    }

    const QString windowStateStr = mpHost->readProfileIniData(qsl("Notepad/WindowState"));
    if (!windowStateStr.isEmpty()) {
        restoreState(QByteArray::fromBase64(windowStateStr.toLatin1()));
    }
}

void dlgNotepad::closeEvent(QCloseEvent* event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}

// The signal above is emitted when a reader picks an appearance in the settings.
// A reader on "follow the system" who changes the system's theme instead moves
// the application's palette without it, and the only word this window gets of
// that is the style change Qt sends every widget - which is what the arrow this
// button used to carry was re-inked from, and is now what the whole shell is.
void dlgNotepad::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);

    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        applyNotepadShellStyle();
    }
}

bool dlgNotepad::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == toolBar && event->type() == QEvent::Resize) {
        fitNoPrefixNote();
        return QMainWindow::eventFilter(obj, event);
    }

    if (obj == mpFindLineEdit && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ShiftModifier) {
                slot_findPrevious();
            } else {
                slot_findNext();
            }
            return true;
        }
        if (keyEvent->key() == Qt::Key_Escape) {
            slot_hideFindBar();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void dlgNotepad::slot_showFindBar()
{
    mpFindBar->show();
    mpFindLineEdit->setFocus();
    mpFindLineEdit->selectAll();
    highlightAllMatches();
}

void dlgNotepad::slot_hideFindBar()
{
    mpFindBar->hide();
    clearSearchHighlights();
    if (auto* textEdit = currentTextEdit()) {
        textEdit->setFocus();
    }
}

void dlgNotepad::slot_findNext()
{
    auto* textEdit = currentTextEdit();
    if (!textEdit || mpFindLineEdit->text().isEmpty()) {
        return;
    }

    if (!textEdit->find(mpFindLineEdit->text())) {
        textEdit->moveCursor(QTextCursor::Start);
        textEdit->find(mpFindLineEdit->text());
    }
    highlightAllMatches();
}

void dlgNotepad::slot_findPrevious()
{
    auto* textEdit = currentTextEdit();
    if (!textEdit || mpFindLineEdit->text().isEmpty()) {
        return;
    }

    if (!textEdit->find(mpFindLineEdit->text(), QTextDocument::FindBackward)) {
        textEdit->moveCursor(QTextCursor::End);
        textEdit->find(mpFindLineEdit->text(), QTextDocument::FindBackward);
    }
    highlightAllMatches();
}

void dlgNotepad::slot_findTextChanged(const QString& text)
{
    Q_UNUSED(text)
    highlightAllMatches();

    auto* textEdit = currentTextEdit();
    if (textEdit && !mpFindLineEdit->text().isEmpty()) {
        QTextCursor cursor = textEdit->textCursor();
        cursor.movePosition(QTextCursor::Start);
        textEdit->setTextCursor(cursor);
        textEdit->find(mpFindLineEdit->text());
    }
}

void dlgNotepad::slot_currentTabChanged(int index)
{
    Q_UNUSED(index)
    updateSendStrip();
    if (mpFindBar->isVisible()) {
        highlightAllMatches();
    }
}

void dlgNotepad::highlightAllMatches()
{
    auto* textEdit = currentTextEdit();
    if (!textEdit) {
        return;
    }

    QList<QTextEdit::ExtraSelection> extraSelections;
    const QString searchText = mpFindLineEdit->text();

    if (searchText.isEmpty()) {
        textEdit->setExtraSelections(extraSelections);
        return;
    }

    QTextDocument* doc = textEdit->document();
    QTextCursor cursor(doc);
    QTextCursor currentCursor = textEdit->textCursor();

    while (!cursor.isNull() && !cursor.atEnd()) {
        cursor = doc->find(searchText, cursor);
        if (!cursor.isNull()) {
            QTextEdit::ExtraSelection selection;
            selection.cursor = cursor;

            if (cursor.selectionStart() == currentCursor.selectionStart() && cursor.selectionEnd() == currentCursor.selectionEnd()) {
                selection.format.setBackground(mCurrentMatchInk);
            } else {
                selection.format.setBackground(mOtherMatchInk);
            }

            extraSelections.append(selection);
        }
    }

    textEdit->setExtraSelections(extraSelections);
}

void dlgNotepad::clearSearchHighlights()
{
    auto* textEdit = currentTextEdit();
    if (textEdit) {
        textEdit->setExtraSelections(QList<QTextEdit::ExtraSelection>());
    }
}
