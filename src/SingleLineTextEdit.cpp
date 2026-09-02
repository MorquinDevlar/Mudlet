/***************************************************************************
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

#include "SingleLineTextEdit.h"
#include "TrailingWhitespaceMarker.h"

#include <QColor>
#include <QKeyEvent>
#include <QPalette>

namespace {
// How much of the field's own text colour a placeholder is written in
constexpr qreal scmPlaceholderStrength = 0.6;
} // namespace

SingleLineTextEdit::SingleLineTextEdit(QWidget* parent)
: QPlainTextEdit(parent)
{
    highlighter = new TriggerHighlighter(this->document());
    highlighter->setHighlightingEnabled(true);
    setWordWrapMode(QTextOption::NoWrap);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTabChangesFocus(true);
}

// strip whitespace formatting marks (middle dots) when copying,
// creating fresh mime data to avoid HTML carrying the marks
QMimeData* SingleLineTextEdit::createMimeDataFromSelection() const
{
    QString text = textCursor().selectedText();
    unmarkQString(&text);
    auto* mimeData = new QMimeData();
    mimeData->setText(text);
    return mimeData;
}

// trap some commonly used multi-line key shortcuts
void SingleLineTextEdit::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

// ensure height remains on single line
void SingleLineTextEdit::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
}

// ensure we can't paste multiple lines
void SingleLineTextEdit::insertFromMimeData(const QMimeData* source)
{
    if (source->hasText()) {
        QString text = source->text();
        QString firstLine = text.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts).first();
        QPlainTextEdit::insertPlainText(firstLine);
    }
}

// Like QLineEdit: deselect when focus moves to another widget, but not when a
// popup or another window merely borrows it - the context menu's own Copy runs
// after this FocusOut
void SingleLineTextEdit::focusOutEvent(QFocusEvent* event)
{
    QPlainTextEdit::focusOutEvent(event);
    const Qt::FocusReason reason = event->reason();
    if (reason == Qt::ActiveWindowFocusReason || reason == Qt::PopupFocusReason) {
        return;
    }
    QTextCursor cursor = textCursor();
    cursor.clearSelection();
    setTextCursor(cursor);
}

void SingleLineTextEdit::setHighlightingEnabled(bool enabled)
{
    highlighter->setHighlightingEnabled(enabled);
    rehighlight();
}

void SingleLineTextEdit::setTheme(const QString& themeName)
{
    // Only the syntax colouring: the theme used to be painted onto the widget
    // as well, which put a code pane's dark background behind a pattern on a
    // light form. What the field is drawn in comes from setFieldColors().
    highlighter->setTheme(themeName);
}

void SingleLineTextEdit::setFieldColors(const QColor& background, const QColor& text)
{
    if (!background.isValid() || !text.isValid()) {
        return;
    }

    QPalette fieldPalette = palette();
    fieldPalette.setColor(QPalette::Base, background);
    fieldPalette.setColor(QPalette::Text, text);
    // Read against the inside of the control it stands in, not against the form
    QColor placeholderColor = text;
    placeholderColor.setAlphaF(scmPlaceholderStrength);
    fieldPalette.setColor(QPalette::PlaceholderText, placeholderColor);
    setPalette(fieldPalette);
    viewport()->setPalette(fieldPalette);
    viewport()->setAutoFillBackground(true);

    // ...and the token colours have to be readable on it
    highlighter->setFieldColors(background, text);
}

void SingleLineTextEdit::rehighlight()
{
    if (toPlainText().isEmpty()) {
        return;
    }

    highlighter->rehighlight();
}
