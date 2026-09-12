#ifndef MUDLET_DLGNOTEPAD_H
#define MUDLET_DLGNOTEPAD_H

/***************************************************************************
 *   Copyright (C) 2008-2009 by Heiko Koehn - KoehnHeiko@googlemail.com    *
 *   Copyright (C) 2014 by Ahmed Charles - acharles@outlook.com            *
 *   Copyright (C) 2018, 2022, 2025 by Stephen Lyons                       *
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


#include "ui_notes_editor.h"
#include "uiDesign.h"
#include <QColor>
#include <QList>
#include <QPointer>

class Host;
class QCloseEvent;
class QEvent;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QShortcut;
class QTimer;
class QTimerEvent;
class QToolButton;


class dlgNotepad : public QMainWindow, public Ui::notes_editor
{
    Q_OBJECT

    friend class NotepadShellTest;

public:
    Q_DISABLE_COPY(dlgNotepad)
    explicit dlgNotepad(Host*);
    ~dlgNotepad();

    void save();
    void restore();
    void saveSettings();
    void restoreSettings();
    void setFont(const QFont&);

signals:
    void notepadClosing(const QString& profileName);

public slots:
    int addTab(const QString& name = QString(), const QString& content = QString());
    void closeTab(int index);
    void renameTab(int index);

private slots:
    void slot_tabCloseRequested(int index);
    void slot_tabContextMenu(const QPoint& pos);
    void slot_addTabClicked();
    void slot_textChanged();
    void slot_sendAll();
    void slot_sendLine();
    void slot_sendSelection();
    void slot_sendNextLine();
    void slot_stopSending();
    void slot_showFindBar();
    void slot_hideFindBar();
    void slot_findNext();
    void slot_findPrevious();
    void slot_findTextChanged(const QString& text);
    void slot_currentTabChanged(int index);
    void slot_applyAppearance();

private:
    void timerEvent(QTimerEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    QPlainTextEdit* currentTextEdit() const;
    void applyNotepadShellStyle();
    void setupSendStrip();
    // The size the strip's words and field are set in: that of the bar's
    // buttons, which on macOS is smaller than a label's own. Whole points, so
    // that no fraction ever reaches a stylesheet.
    [[nodiscard]] int stripWordPointSize() const;
    void setupAddTabButton();
    void updateTabClosability();
    // Whether the notes at those indices may go: one with nothing in it is no
    // loss and goes without a word, anything else is put to the reader first
    [[nodiscard]] bool okToDiscardNotes(const QList<int>& indices);
    void closeOtherTabs(int keptIndex);
    void setupFindBar();
    void highlightAllMatches();
    void clearSearchHighlights();
    bool migrateOldNotesFile();
    void startSendingLines(const QStringList& lines);
    // Both readings the strip carries about the note in front of the reader:
    // how far each send button reaches, and whether sending with no prefix
    // would put a page of prose on the command line
    void updateSendStrip();
    void updateSendReach();
    // The word leading the prefix field stands as far from the seam as Send
    // all's word does on the other side of it
    void alignPrefixLeadWord();
    void updateNoPrefixWarning();
    // Which of the warning's three forms the strip has room for: the whole
    // sentence, the short form, or nothing at all beside the cue in the field
    void fitNoPrefixNote();
    // Which of the strip's two readings is on show: the buttons and the prefix
    // field, or the progress of a send and the way to stop it
    void setSending(const bool sending);
    void updateSendingReading();
    void finishSending(const bool stopped);

    QPointer<Host> mpHost;
    QToolButton* mpAddTabButton = nullptr;
    bool mNeedToSave = false;
    // What the find bar marks its hits with, mixed in the style pass so that
    // both follow an appearance change: the marker pen on the match the cursor
    // is on, and a wash of it towards the field on every other one
    QColor mCurrentMatchInk;
    QColor mOtherMatchInk;
    QAction* action_stop = nullptr;
    QAction* action_sendControlsSeparator = nullptr;
    QAction* action_prependText = nullptr;
    QAction* action_prependTextLabel = nullptr;
    QLabel* label_prependText = nullptr;
    QLineEdit* lineEdit_prependText = nullptr;
    // The warning's cue, carried inside the field at its trailing end: it costs
    // the bar no room, so it stands at every width the window is dragged to
    QAction* action_prefixWarning = nullptr;
    // What the field says about itself, put back when the warning goes: while
    // it applies the field carries the whole sentence instead
    QString mPrefixFieldToolTip;
    // What the strip shows while a send runs, in place of the buttons and the
    // field: the bar makes an action for each, which is what shows and hides it
    QAction* action_sendingDot = nullptr;
    QAction* action_sendingText = nullptr;
    QAction* action_sendingBar = nullptr;
    QAction* action_noPrefixNote = nullptr;
    QLabel* mpLabel_sendingDot = nullptr;
    QLabel* mpLabel_sendingText = nullptr;
    QProgressBar* mpProgressBar_sending = nullptr;
    QWidget* mpWidget_noPrefixNote = nullptr;
    QLabel* mpLabel_noPrefixDot = nullptr;
    QLabel* mpLabel_noPrefixText = nullptr;
    // The whole sentence, which the label shows where the strip has room for
    // the whole of it and nowhere else
    QString mNoPrefixNoteText;
    // Whether the warning applies at all, which is not the same as whether the
    // strip is showing it: at a width neither form fits in the reading comes
    // off the bar while this stays true, and a wider window brings it back
    bool mNoPrefixWarningApplies = false;
    // The four pictures on the strip, re-inked by the style pass the way every
    // other window's toolbar glyphs are
    QList<uiDesign::ActionGlyph> mNotepadActionGlyphs;
    QStringList mLinesToSend;
    QTimer* mSendTimer = nullptr;
    int mCurrentLineIndex = 0;
    // Counted rather than taken from the list's size: an empty line is skipped
    // on its way out, so it is no part of what the reader was told would go
    int mLinesToSendCount = 0;
    int mLinesSent = 0;
    bool mSending = false;

    QWidget* mpFindBar = nullptr;
    QLineEdit* mpFindLineEdit = nullptr;
    QToolButton* mpFindPrevButton = nullptr;
    QToolButton* mpFindNextButton = nullptr;
    QToolButton* mpFindCloseButton = nullptr;
    QShortcut* mpFindShortcut = nullptr;
};

#endif // MUDLET_DLGNOTEPAD_H
