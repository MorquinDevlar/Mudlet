/***************************************************************************
 *   Copyright (C) 2008-2013 by Heiko Koehn - KoehnHeiko@googlemail.com    *
 *   Copyright (C) 2014 by Ahmed Charles - acharles@outlook.com            *
 *   Copyright (C) 2014-2024, 2026 by Stephen Lyons                        *
 *                                               - slysven@virginmedia.com *
 *   Copyright (C) 2016 by Owen Davison - odavison@cs.dal.ca               *
 *   Copyright (C) 2016-2020 by Ian Adkins - ieadkins@gmail.com            *
 *   Copyright (C) 2017 by Tom Scheper - scheper@gmail.com                 *
 *   Copyright (C) 2023-2025 by Lecker Kebap - Leris@mudlet.org            *
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


#include "dlgTriggerEditor.h"

#include "Host.h"
#include "LuaInterface.h"
#include "TConsole.h"
#include "TDebug.h"
#include "TEasyButtonBar.h"
#include "TTextEdit.h"
#include "TToolBar.h"
#include "VarUnit.h"
#include "XMLimport.h"
#include "XMLexport.h"
#include "dlgActionMainArea.h"
#include "dlgAliasMainArea.h"
#include "dlgColorTrigger.h"
#include "dlgKeysMainArea.h"
#include "dlgPackageExporter.h"
#include "dlgProfilePreferences.h"
#include "dlgScriptsMainArea.h"
#include "dlgTriggerPatternEdit.h"
#include "SidebarItemDelegate.h"
#include "SingleLineTextEdit.h"
#include "TrailingWhitespaceMarker.h"
#include "EditorAddItemCommand.h"
#include "EditorDeleteItemCommand.h"
#include "EditorItemXMLHelpers.h"
#include "EditorModifyPropertyCommand.h"
#include "EditorMoveItemCommand.h"
#include "EditorPlaceholderButton.h"
#include "EditorToggleActiveCommand.h"
#include "EditorTreeDelegate.h"
#include "GripSplitter.h"
#include "SearchResultDelegate.h"
#include "mudlet.h"
#include "uiDesign.h"
#include "utils.h"
#include "edbee/models/textdocumentscopes.h"

#include <QApplication>
#include <QCheckBox>
#include <QAbstractButton>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QFontMetrics>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMetaEnum>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <QTime>
#include <QShowEvent>
#include <QRegularExpression>
#include <QToolButton>
#include <QToolBar>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <pugixml.hpp>
#include <QVBoxLayout>


// Forward declaration for per-property undo helper (defined later in this file)
static void pushKeyPropertyCommand(EditorUndoStack* undoStack, Host* host, int keyID, const QString& keyName, const QString& propertyName, const QString& oldStateXML, const QString& newStateXML);

using namespace std::chrono_literals;

// Used as a QObject::property so that we can keep track of the color for the
// trigger colorizer buttons loaded from a trigger even if the user disables
// and then reenables the colorizer function (and we "grey out" the color while
// it is disabled):
static const char* cButtonBaseColor = "baseColor";

// Separates the XML packages of individually copied items on the clipboard so
// that pasting can import and place each one
static const QString cMultiItemPasteSeparator = qsl("\n<!--MUDLET_MULTI_ITEM_SEPARATOR-->\n");

// The column an item is edited in - the form, the strip that heads the code
// pane and the pane itself - is one surface, held away from the window's edges
// by this and by nothing else. Set on the frame that carries all three rather
// than on each of them, so that everything down the column lines up by
// construction.
static constexpr int scmEditorColumnPaddingHorizontal = 14;
// Where the first thing in every one of the window's three columns starts: the
// sidebar's first row, the search field heading the panel of items, and
// whatever leads the column an item is edited in - the notice when one is
// showing, the name row when it is not. One number rather than three that agree
// today, because three drift apart the next time one column's margins are
// touched. The settings dialog's columns start further down (16px, 24px inside
// a page), so this is the editor's own measurement rather than the design
// language's.
static constexpr int scmEditorColumnTopInset = 12;
// ...and what one piece of that column is held away from the next by
static constexpr int scmEditorColumnSpacing = 12;

// The sidebar down the left of the editor, drawn by uiDesign::sidebarStyleSheet()
// from the settings dialog's rules and these measurements of its own
static constexpr int scmEditorSidebarPadding = 12;
// Above and below its rows, which is where the first of them starts - the same
// line the other two columns start on
static constexpr int scmEditorSidebarVerticalPadding = scmEditorColumnTopInset;
static constexpr int scmEditorSidebarMaximumWidth = 180;
static constexpr int scmEditorSidebarRailWidth = 46;
static constexpr int scmEditorSidebarRailPadding = 6;
static constexpr int scmEditorSidebarSeparatorInset = 12;
static constexpr int scmEditorSidebarRowHeight = 36;
static constexpr int scmEditorSidebarIconSize = 18;
// What a row costs beside its name: the pill's accent bar and padding, the icon
// and the gap the view leaves after it
static constexpr int scmEditorSidebarRowChrome = 40;
// The narrowest the editor is worth showing its names beside - a floor for the
// breakpoint, not a width anything is held to
static constexpr int scmEditorContentColumnWidth = 640;

// The ID beside an item's name, drawn as a pill: a monospace word a size down
// from the form around it, in a box whose corner is half its height
static constexpr qreal scmEditorIdChipFontScale = 0.85;
static constexpr int scmEditorIdChipPaddingVertical = 3;

// What is left round the word on the button the trigger's options are opened
// from. Its height is not left to these: a min-height in a stylesheet is what a
// tool button's contents are given, and the style then adds its own margins on
// top - so the button is set to the fields' height outright instead.
static constexpr int scmEditorRowButtonPaddingVertical = 5;
static constexpr int scmEditorRowButtonPaddingHorizontal = 12;

// How far a control the reader can press is taken while the pointer is on it:
// a raised one lifts a shade further off what carries it, while an outlined one
// draws its hairline a shade nearer the words instead
static constexpr qreal scmEditorRaisedHoverWeight = 0.08;
static constexpr qreal scmEditorHoveredBorderWeight = 0.35;

// The one measurement of that sidebar which is not a constant is what it is
// drawn at with the names showing, since that is the longest of the names -
// see editorSidebarWidths()
static uiDesign::SidebarMetrics editorSidebarMetrics(const int expandedWidth)
{
    return {.expandedWidth = expandedWidth,
            .railWidth = scmEditorSidebarRailWidth,
            .padding = scmEditorSidebarPadding,
            .railPadding = scmEditorSidebarRailPadding,
            .verticalPadding = scmEditorSidebarVerticalPadding,
            .separatorInset = scmEditorSidebarSeparatorInset};
}

// A match row is stepped in from the heading naming the item it was found in -
// far enough to read as belonging to it, and no further, because the panel is
// narrow and the line the match is on is what the row is there to show
static constexpr int scmEditorSearchResultIndent = 14;

// The heading strip the Lua editor sits under, which is the splitter's handle
static constexpr int scmEditorCodeHeaderGlyphSize = 13;
// What the heading is set in from the code pane's own left edge
static constexpr int scmEditorCodeHeaderInset = 4;
// Kept clear in the middle of the strip for the grip the handle draws there
static constexpr int scmEditorCodeHeaderGripGap = 56;
static constexpr int scmEditorCompileDotDiameter = 8;
// A failure is named on the strip and spelled out in the tooltip: a compiler's
// idea of a sentence does not fit next to a heading
static constexpr int scmEditorCompileMessageWidth = 260;

// The trigger form's options, as a column of cards beside its patterns. Wide
// enough for the longest of the four card's rows without the cards having to
// wrap, and narrow enough to leave the patterns the rest of the form.
static constexpr int scmEditorTriggerOptionsWidth = 280;
// A colour button says what it is by the colour it is filled with, so it is
// sized as a well rather than as a word
static constexpr int scmEditorColorWellHeight = 26;
// The boxes the two matching modes are named in, and the gap after them, which
// is also what the rows of the panel are spaced by
static constexpr int scmEditorModeChipPadding = 7;
static constexpr int scmEditorCardRowGap = 8;
static constexpr int scmEditorModeChipGap = scmEditorCardRowGap;
// Three digits and a pair of arrows; the rest of the row is the words around it
static constexpr int scmEditorOptionsSpinBoxWidth = 72;
// How many lines the AND mode can be asked to match within. Nothing in TTrigger
// bounds mConditionLineDelta, so the number is only a question of what is worth
// typing - but the visible spin box and the hidden spinBox_lineMargin it writes
// into have to agree on it, or the hidden one clamps what was typed and echoes
// the clamped value straight back, eating keystrokes.
static constexpr int scmEditorMatchWithinLinesMax = 999;
// What a card leaves round what it holds - tighter than the settings dialog's
// 16, as the options column is a third of the width a settings page is - and,
// since the title is the first line inside the frame rather than a heading
// above it, how far in from the frame the title starts as well
static constexpr int scmEditorCardPadding = 12;

// ...which, with the property the rules select on, is the whole of what the
// editor's cards differ from the settings dialog's by; everything else about
// them is drawn by uiDesign::cardStyleSheet(). This column has no card without
// a title, and none holding a group box of the .ui file's own. The height of
// the title's own line is measured under the font the window is running at, so
// it is the one runtime number here.
static uiDesign::CardMetrics cardMetrics(const int titleHeight)
{
    return {.cardProperty = uiDesign::scmProp_editorCard, .padding = scmEditorCardPadding, .titleHeight = titleHeight};
}
// The least the code pane is left with when the options panel borrows height
// from it: below this the editor stops being one anything can be typed into
static constexpr int scmEditorSourcePaneFloor = 120;
// A banner's picture, beside a line of text rather than the 64px block the
// .ui file sizes it as
static constexpr int scmEditorBannerGlyphSize = 20;

// A trigger's pattern rows. A row is as tall as the profile's display font asks
// for, since that is the font the pattern itself is read in - but never shorter
// than a field elsewhere on the form plus what the row's layout insets its
// controls by, or the pattern and the type beside it would be the two controls
// in the window drawn a size down from the rest. This is what the row's layout
// leaves above and below its controls together, and has to stay the sum of
// trigger_pattern_edit.ui's top and bottom margins.
static constexpr int scmEditorPatternRowMargins = 8;
static constexpr int scmEditorPatternRowMinimumHeight = uiDesign::scmInputHeight + scmEditorPatternRowMargins;
static constexpr int scmEditorPatternRowPadding = scmEditorPatternRowMargins;
// How far the row is taken towards the text on it while the mouse is there -
// the same wash every other hovered row in the two windows gets
static constexpr qreal scmEditorPatternHoverStrength = 0.07;
// What the type combo box costs beside the longest of the names it offers: the
// swatch it draws each name against, its arrow, and the style's own padding
static constexpr int scmEditorPatternTypeChrome = 60;
// Room for the two digits of the highest row number there can be
static constexpr int scmEditorPatternNumberDigits = 2;
static constexpr int scmEditorPatternDeleteButtonSize = 22;
static constexpr int scmEditorPatternDeleteGlyphSize = 13;
// The line drawn across the row a dragged one would land on
static constexpr int scmEditorPatternDropIndicatorHeight = 2;
// Past this much movement the press on a row's grip is a drag rather than a click
static constexpr int scmEditorPatternDragThreshold = 4;
// As many patterns as one trigger is allowed to hold
static constexpr int scmEditorPatternRowLimit = 50;
// The swatch each pattern type is named beside: a small rounded square, drawn at
// the weight the rest of the row is drawn at
static constexpr int scmEditorPatternSwatchSize = 12;
static constexpr qreal scmEditorPatternSwatchRadius = 3.0;
static constexpr qreal scmEditorPatternSwatchSaturation = 0.55;
// Off the page the swatch lies on, the way every other coloured mark in the two
// windows is: a hue says which type, the page says how light it is drawn
static constexpr qreal scmEditorPatternSwatchLightnessOnDark = 0.58;
static constexpr qreal scmEditorPatternSwatchLightnessOnLight = 0.45;
// The grip at the left of a pattern row: two columns of three dots
static constexpr qreal scmEditorPatternGripDotDiameter = 2.0;
static constexpr qreal scmEditorPatternGripPitch = 3.0;
static constexpr int scmEditorPatternGripColumns = 2;
static constexpr int scmEditorPatternGripRows = 3;
// The dots are a target of five pixels across; what the pointer has to hit is
// the column they are centred in
static constexpr int scmEditorPatternGripWidth = 12;
// What the dashed frame of the button adding a pattern is held away from the row
// above it and the edge below by. Named rather than written into the sheet
// alone, because the button paints its own frame and has to inset by the same.
static constexpr int scmEditorAddPatternMarginTop = 4;
static constexpr int scmEditorAddPatternMarginBottom = 2;
// Carrying a row up or down without the mouse. Not Ctrl+Shift+Up/Down, which is
// already how the first and the last pattern are jumped to.
static constexpr auto scmEditorPatternMoveUpKeys = Qt::CTRL | Qt::ALT | Qt::Key_Up;
static constexpr auto scmEditorPatternMoveDownKeys = Qt::CTRL | Qt::ALT | Qt::Key_Down;

// What the sidebar keeps on each of its rows: the action the row stands for,
// and the view that action leaves the editor showing (cmUnknownView for the
// rows that run a one-off action instead of changing the view)
static constexpr int scmRole_editorSidebarAction = Qt::UserRole;
static constexpr int scmRole_editorSidebarView = Qt::UserRole + 1;

// One swatch per entry of mPatternList, in that order, and the hues are the ones
// the eight types have always been told apart by. A hue below zero is a type
// with no colour of its own - the plain substring and the colour trigger, whose
// own two wells say what it matches - and those are mixed off the page instead,
// at the weight that keeps the pair apart from each other.
struct EditorPatternSwatch
{
    qreal hue;
    qreal neutralWeight;
};

static constexpr EditorPatternSwatch scmEditorPatternSwatches[]{
        {-1.0, 0.72}, // substring
        {0.58, 0.0},  // perl regex
        {0.02, 0.0},  // start of line
        {0.34, 0.0},  // exact match
        {0.50, 0.0},  // lua function
        {0.83, 0.0},  // line spacer
        {-1.0, 0.40}, // color trigger
        {0.13, 0.0},  // prompt
};

// Drawn at the screen's own ratio, so the rounded corners stay corners rather
// than becoming steps on a retina display
static QIcon editorPatternSwatchIcon(const QColor& fill, const QColor& outline, const qreal ratio)
{
    QPixmap swatch(qRound(scmEditorPatternSwatchSize * ratio), qRound(scmEditorPatternSwatchSize * ratio));
    swatch.setDevicePixelRatio(ratio);
    swatch.fill(Qt::transparent);

    QPainter painter(&swatch);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Half a pen in on every side: a rectangle stroked on its own path would
    // otherwise lose half the line off the edge of the pixmap
    const QRectF body(0.5, 0.5, scmEditorPatternSwatchSize - 1.0, scmEditorPatternSwatchSize - 1.0);
    painter.setPen(QPen(outline, 1.0));
    painter.setBrush(fill);
    painter.drawRoundedRect(body, scmEditorPatternSwatchRadius, scmEditorPatternSwatchRadius);
    painter.end();
    return QIcon(swatch);
}

// The grip a pattern row is dragged by. Painted rather than written as U+22EE
// twice over: a grid of dots is what the rest of the editor's chrome is drawn
// at, and no interface font has to be asked whether it has the character.
static QPixmap editorPatternGripGlyph(const QColor& color, const qreal ratio)
{
    const qreal width = (scmEditorPatternGripColumns - 1) * scmEditorPatternGripPitch + scmEditorPatternGripDotDiameter;
    const qreal height = (scmEditorPatternGripRows - 1) * scmEditorPatternGripPitch + scmEditorPatternGripDotDiameter;
    QPixmap grip(qRound(width * ratio), qRound(height * ratio));
    grip.setDevicePixelRatio(ratio);
    grip.fill(Qt::transparent);

    QPainter painter(&grip);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    for (int column = 0; column < scmEditorPatternGripColumns; ++column) {
        for (int row = 0; row < scmEditorPatternGripRows; ++row) {
            painter.drawEllipse(QRectF(column * scmEditorPatternGripPitch, row * scmEditorPatternGripPitch, scmEditorPatternGripDotDiameter, scmEditorPatternGripDotDiameter));
        }
    }
    painter.end();
    return grip;
}

// Track whether the shared auto-complete provider has been initialized
bool dlgTriggerEditor::smAutoCompleteInitialized = false;

dlgTriggerEditor::dlgTriggerEditor(Host* pH)
: mpHost(pH)
, mSearchOptions(pH->mSearchOptions)
{
    // init generated dialog
    setupUi(this);

    // clang-format off
    introAddItem.insert(EditorViewType::cmAliasView, {
        //: Headline for the Alias intro
        tr("Alias react on user input."), {
        //: Name of a selectable option for the Alias intro
        {qsl("alias1"), tr("How to add a new alias now"),
        //: Help contents of a selectable option for the Alias intro
            tr("<ol><li>Click on the 'Add Item' icon above.</li>"
               "<li>Define an input <strong>pattern</strong> either literally or with a Perl regular expression.</li>"
               "<li>Define a 'substitution' <strong>command</strong> to send to the game in clear text <strong>instead of the alias pattern</strong>, or write a script for more complicated needs.</li>"
               "<li><strong>Activate</strong> the alias.</li></ol>")},
        //: Name of a selectable option for the Alias intro
        {qsl("alias2"), tr("How to add a new alias from the input line"),
            qsl("%1%2%3%4").arg(
                //: Help contents of a selectable option for the Alias intro
                qsl("<p>%1</p>").arg(tr("There are a <a href='https://forums.mudlet.org/viewtopic.php?f=6&t=22609'>couple</a> of <a href='https://forums.mudlet.org/viewtopic.php?f=6&t=16462'>packages</a> that can help you.")),
                //: Part of the Alias intro - This introductory text will be followed by a Lua code example for a trigger.
                qsl("<p>%1</p>").arg(tr("Alias can also be defined from the input line in the main profile window like this:")),
                qsl("<p><code>%1</code></p>").arg(qsl("lua permAlias(&quot;%1&quot;, &quot;&quot;, &quot;%2&quot;, function() send(&quot;%3&quot;) echo(&quot;%4&quot;) end)").arg(
                    //: Part of the Alias intro, code example for an alias - This is the name of the alias which reacts on the player typing "hi" by saying "Greetings, traveller!" in game.
                    tr("My greetings"),
                    //: Part of the Alias intro, code example for an alias - This is the text input from the player which will be reacted on by saying "Greetings, traveller!" in game.
                    tr("hi"),
                    //: Part of the Alias intro, code example for an alias - This is the command that Mudlet will send to the game after the player typed "hi".
                    tr("say Greetings, traveller!"),
                    //: Part of the Alias intro, code example for an alias - This is the confirmation text shown to the player after they typed "hi" and we said "Greetings, traveller!" in game.
                    tr("We said hi!"))),
                //: Part of the Alias intro - This is the conclusion after the code example for an alias which reacts on the player typing "hi" by saying "Greetings, traveller!" in game.
                qsl("<p>%1</p>").arg("You can now greet by typing 'hi'"))},
        {qsl("alias3"), tr("Where to find more information"),
            qsl("<ul>%1%2%3%4</ul>").arg( // reduce clutter for translators
                qsl("<li><p>%1</p><li>").arg(tr("Watch a <a href='%1'>video demonstration</a> of the basic functionality.")
                    .arg(qsl("https://youtu.be/Uz6EDvZYNvE"))),
                qsl("<li><p>%1</p></li>").arg(tr("Read the <a href='http://wiki.mudlet.org/w/Manual:Introduction#Aliases'>Introduction to Aliases</a> for a detailed overview.")),
                qsl("<li><p>%1</p>").arg(tr("Do you maybe have any other suggestions, questions or doubts?")),
                qsl("<p>%1</p></li>").arg(tr("Join our community on <a href='https://www.mudlet.org/chat'>Discord</a> or in <a href='https://forums.mudlet.org/'>Mudlet forums</a> - See you there!")))}}});

    introAddItem.insert(EditorViewType::cmTriggerView, {
        //: Headline for the Trigger intro
        tr("Triggers react on game output."), {
        //: Name of a selectable option for the Trigger intro
        {qsl("trigger1"), tr("How to add a new trigger now"),
        //: Help contents of a selectable option for the Trigger intro
            tr("<ol><li>Click on the 'Add Item' icon above.</li>"
               "<li>Define a <strong>pattern</strong> that you want to trigger on.</li>"
               "<li>Select the appropriate pattern <strong>type</strong>.</li>"
               "<li>Define a clear text <strong>command</strong> that you want to send to the game if the trigger finds the pattern in the text from the game, or write a script for more complicated needs..</li>"
               "<li><strong>Activate</strong> the trigger.</li></ol>")},
        //: Name of a selectable option for the Trigger intro
        {qsl("trigger2"), tr("How to add a new trigger from the input line"),
            qsl("%1%2%3%4").arg(
                //: Help contents of a selectable option for the Trigger intro
                qsl("<p>%1</p>").arg(tr("There are a <a href='https://forums.mudlet.org/viewtopic.php?f=6&t=22609'>couple</a> of <a href='https://forums.mudlet.org/viewtopic.php?f=6&t=16462'>packages</a> that can help you.")),
                //: Part of the Trigger intro - This introductory text will be followed by a Lua code example for a trigger.
                qsl("<p>%1</p>").arg(tr("Triggers can also be defined from the input line in the main profile window like this:")),
                qsl("<p><code>%1</code></p>").arg(qsl("lua permSubstringTrigger(&quot;%1&quot;, &quot;&quot;, &quot;%2&quot;, function() send(&quot;%3&quot;) end)").arg(
                    //: Part of the Trigger intro, code example for a trigger - This is the name of the trigger which reacts on "You are thirsty" with "drink water".
                    tr("My drink trigger"),
                    //: Part of the Trigger intro, code example for a trigger - This is the text from game which will be triggered on, and reacted to with "drink water".
                    tr("You are thirsty."),
                    //: Part of the Trigger intro, code example for a trigger - This is the command sent to game after we triggered on text "You are thirsty." from game.
                    tr("drink water"))),
                //: Part of the Trigger intro - This is the conclusion after the code example for a trigger which reacts on "You are thirsty" with "drink water".
                qsl("<p>%1</p>").arg("This will keep you refreshed."))},
        {qsl("trigger3"), tr("Where to find more information"),
            qsl("<ul>%1%2%3%4</ul>").arg( // reduce clutter for translators
                qsl("<li><p>%1</p><li>").arg(tr("Watch a <a href='%1'>video demonstration</a> of the basic functionality.")
                    .arg(qsl("https://youtu.be/jYjop54-Y3I"))),
                qsl("<li><p>%1</p></li>").arg(tr("Read the <a href='http://wiki.mudlet.org/w/Manual:Introduction#Triggers'>Introduction to Triggers</a> for a detailed overview.")),
                qsl("<li><p>%1</p>").arg(tr("Do you maybe have any other suggestions, questions or doubts?")),
                qsl("<p>%1</p></li>").arg(tr("Join our community on <a href='https://www.mudlet.org/chat'>Discord</a> or in <a href='https://forums.mudlet.org/'>Mudlet forums</a> - See you there!")))}}});

    introAddItem.insert(EditorViewType::cmScriptView, {
        //: Headline for the Script intro
        tr("Scripts organize code and can react to events."), {
        //: Name of a selectable option for the Script intro
        {qsl("script1"), tr("How to add a new script now"),
        //: Help contents of a selectable option for the Script intro
            tr("<ol><li>Click on the 'Add Item' icon above.</li>"
               "<li>Enter a script in the box below. You can for example define <strong>functions</strong> to be called by other triggers, aliases, etc.</li>"
               "<li>If you write lua <strong>commands</strong> without defining a function, they will be run on Mudlet startup and each time you open the script for editing.</li>"
               "<li><strong>Activate</strong> the script.</li></ol>"
               "<p><strong>Note:</strong> Scripts are run automatically when viewed, even if they are deactivated.</p>")},
        //: Name of a selectable option for the Script intro
        {qsl("script2"), tr("How to have a script react to events"),
        //: Help contents of a selectable option for the Script intro
            tr("<p>You can register a list of <strong>events</strong> with the + and - symbols. If one of these events take place, the function with the same name as the script item itself will be called.</p>"
               "<p><strong>Note:</strong> Events can also be added to a script from the command line in the main profile window like this:</p>"
               "<p><code>lua registerAnonymousEventHandler(&quot;nameOfTheMudletEvent&quot;, &quot;nameOfYourFunctionToBeCalled&quot;)</code></p>")},
        {qsl("script3"), tr("Where to find more information"),
            qsl("<ul>%1%2%3%4</ul>").arg( // reduce clutter for translators
                qsl("<li><p>%1</p><li>").arg(tr("Watch a <a href='%1'>video demonstration</a> of the basic functionality.")
                    .arg(qsl("https://youtu.be/10mJUh4Hq-A"))),
                qsl("<li><p>%1</p></li>").arg(tr("Read the <a href='http://wiki.mudlet.org/w/Manual:Introduction#Scripts'>Introduction to Scripts</a> for a detailed overview.")),
                qsl("<li><p>%1</p>").arg(tr("Do you maybe have any other suggestions, questions or doubts?")),
                qsl("<p>%1</p></li>").arg(tr("Join our community on <a href='https://www.mudlet.org/chat'>Discord</a> or in <a href='https://forums.mudlet.org/'>Mudlet forums</a> - See you there!")))}}});

    introAddItem.insert(EditorViewType::cmTimerView, {
        //: Headline for the Timer intro
        tr("Timers react after a timespan once or regularly."), {
        //: Name of a selectable option for the Timer intro
        {qsl("timer1"), tr("How to add a new timer now"),
        //: Help contents of a selectable option for the Timer intro
            tr("<ol><li>Click on the 'Add Item' icon above.</li>"
               "<li>Define the <strong>timespan</strong> after which the timer should react in a this format: hours : minutes : seconds.</li>"
               "<li>Define a clear text <strong>command</strong> that you want to send to the game when the time has passed, or write a script for more complicated needs.</li>"
               "<li><strong>Activate</strong> the timer.</li></ol>"
               "<p><strong>Note:</strong> If you want the trigger to react only once and not regularly, use the Lua tempTimer() function instead.</p>")},
        //: Name of a selectable option for the Timer intro
        {qsl("timer2"), tr("How to add a new timer from the input line"),
        //: Help contents of a selectable option for the Timer intro
            tr("<p>Timers can also be defined from the input line in the main profile window like this:</p>"
               "<p><code>lua tempTimer(3, function() echo(&quot;hello!\n&quot;) end)</code></p>"
               "<p>This will greet you exactly 3 seconds after it was made.</p>")},
        {qsl("timer3"), tr("Where to find more information"),
            qsl("<ul>%1%2%3</ul>").arg( // reduce clutter for translators
                qsl("<li><p>%1</p></li>").arg(tr("Read the <a href='http://wiki.mudlet.org/w/Manual:Introduction#Timers'>Introduction to Timers</a> for a detailed overview.")),
                qsl("<li><p>%1</p>").arg(tr("Do you maybe have any other suggestions, questions or doubts?")),
                qsl("<p>%1</p></li>").arg(tr("Join our community on <a href='https://www.mudlet.org/chat'>Discord</a> or in <a href='https://forums.mudlet.org/'>Mudlet forums</a> - See you there!")))}}});

    introAddItem.insert(EditorViewType::cmActionView, {
        //: Headline for the Button intro
        tr("Buttons react on mouse clicks."), {
        //: Name of a selectable option for the Button intro
        {qsl("button1"), tr("How to add a new button now"),
        //: Help contents of a selectable option for the Button intro
            tr("<ol><li>Add a new group to create a <strong>button bar</strong>.</li>"
               "<li>Add groups as <strong>menus</strong> or sub-menus.</li>"
               "<li>Add items as <strong>buttons</strong> to a bar or menu.</li>"
               "<li>Define a <strong>command</strong> or script to execute when pressed.</li>"
               "<li><strong>Activate</strong> the item. </li></ol>"
               "<p><strong>Note:</strong> Deactivated items are hidden, including all items they contain.</p>"
               "<p><strong>Click-down buttons:</strong> Can define separate commands for press/release. Use getButtonState() to check state.</p>")},
//        {qsl("button2"), tr("How to add a new button from the input line"),
//            tr("")},
        {qsl("button3"), tr("Where to find more information"),
            qsl("<ul>%1%2%3</ul>").arg( // reduce clutter for translators
                qsl("<li><p>%1</p></li>").arg(tr("Read the <a href='http://wiki.mudlet.org/w/Manual:Introduction#Buttons'>Introduction to Buttons</a> for a detailed overview.")),
                qsl("<li><p>%1</p>").arg(tr("Do you maybe have any other suggestions, questions or doubts?")),
                qsl("<p>%1</p></li>").arg(tr("Join our community on <a href='https://www.mudlet.org/chat'>Discord</a> or in <a href='https://forums.mudlet.org/'>Mudlet forums</a> - See you there!")))}}});

    introAddItem.insert(EditorViewType::cmKeysView, {
        //: Headline for the Keys intro
        tr("Keys react on keyboard presses."), {
        //: Name of a selectable option for the Keys intro
        {qsl("key1"), tr("How to add a new keybinding now"),
        //: Help contents of a selectable option for the Keys intro
            tr("<ol><li>Click on the 'Add Item' icon above.</li>"
               "<li>Click on <strong>'grab key'</strong> and then press your key combination, e.g. including modifier keys like Control, Shift, etc.</li>"
               "<li>Define a clear text <strong>command</strong> that you want to send to the game if the button is pressed, or write a script for more complicated needs.</li>"
               "<li><strong>Activate</strong> the new key binding.</li></ol>")},
        //: Name of a selectable option for the Keys intro
        {qsl("key2"), tr("How to add a new keybinding from the input line"),
        //: Help contents of a selectable option for the Keys intro
            tr("<p>Keys can be defined from the input line in the main profile window like this:</p>"
               "<p><code>lua permKey(&quot;my jump key&quot;, &quot;&quot;, mudlet.key.F8, [[send(&quot;jump&quot;]]) end)</code></p>"
               "<p>Pressing F8 will make you jump.</p>")},
        {qsl("key3"), tr("Where to find more information"),
            qsl("<ul>%1%2%3%4</ul>").arg( // reduce clutter for translators
                qsl("<li><p>%1</p><li>").arg(tr("Watch a <a href='%1'>video demonstration</a> of the basic functionality.")
                    .arg(qsl("https://youtu.be/ZYRPZ-8fJWA"))),
                qsl("<li><p>%1</p></li>").arg(tr("Read the <a href='http://wiki.mudlet.org/w/Manual:Introduction#Keybindings'>Introduction to Keybindings</a> for a detailed overview.")),
                qsl("<li><p>%1</p>").arg(tr("Do you maybe have any other suggestions, questions or doubts?")),
                qsl("<p>%1</p></li>").arg(tr("Join our community on <a href='https://www.mudlet.org/chat'>Discord</a> or in <a href='https://forums.mudlet.org/'>Mudlet forums</a> - See you there!")))}}});

    introAddItem.insert(EditorViewType::cmVarsView, {
        //: Headline for the Variable intro
        tr("Variables store information."), {
        //: Name of a selectable option for the Variable intro
        {qsl("variable1"), tr("How to add a new variable now"),
        //: Help contents of a selectable option for the Variable intro
            tr("<ol><li>Click on the 'Add Item' icon above. To add a table instead click 'Add Group'.</li>"
               "<li>Select type of variable value (can be a string, integer, boolean)</li>"
               "<li>Enter the value you want to store in this variable.</li>"
               "<li>If you want to keep the variable in your next Mudlet sessions, check the checkbox in the list of variables to the left.</li>"
               "<li>To remove a variable manually, set it to 'nil' or click on the 'Delete' icon above.</li></ol>"
               "<p><strong>Note:</strong> Variables created here won't be saved when Mudlet shuts down unless you check their checkbox in the list of variables to the left. You could also create scripts with the variables instead.</p>")},
        //: Name of a selectable option for the Variable intro
        {qsl("variable2"), tr("How to add a new variable from the input line"),
        //: Help contents of a selectable option for the Variable intro
            tr("<p>Variables and tables can also be defined from the input line in the main profile window like this:</p>"
               "<p><code>lua foo = &quot;bar&quot;</code></p>"
               "<p>This will create a string called 'foo' with 'bar' as its value.</p>")},
        {qsl("variable3"), tr("Where to find more information"),
            qsl("<ul>%1%2%3</ul>").arg( // reduce clutter for translators
                qsl("<li><p>%1</p></li>").arg(tr("Read the <a href='http://wiki.mudlet.org/w/Manual:Introduction#Variables'>Introduction to Variables</a> for a detailed overview.")),
                qsl("<li><p>%1</p>").arg(tr("Do you maybe have any other suggestions, questions or doubts?")),
                qsl("<p>%1</p></li>").arg(tr("Join our community on <a href='https://www.mudlet.org/chat'>Discord</a> or in <a href='https://forums.mudlet.org/'>Mudlet forums</a> - See you there!")))}}});
    // clang-format on

    // Descriptions for screen readers, clarify to translators that the context of "activated" is current status and not confirmation of toggle.
    //: Item is currently on, short enough to be spoken
    descActive = tr("activated");
    //: Item is currently off, short enough to be spoken
    descInactive = tr("deactivated");
    //: Folder is currently turned on
    descActiveFolder = tr("activated folder");
    //: Folder is currently turned off
    descInactiveFolder = tr("deactivated folder");
    //: Item is currently inactive because of errors, short enough to be spoken
    descError = tr("deactivated due to error");
    //: Item is currently turned on individually, but is member of an inactive group
    descInactiveParent = tr("%1 in a deactivated group");
    //: A trigger that unlocks other triggers is currently turned on, short enough to be spoken
    descActiveFilterChain = tr("activated filter chain");
    //: A trigger that unlocks other triggers is currently turned off, short enough to be spoken
    descInactiveFilterChain = tr("deactivated filter chain");
    //: A timer that starts after another timer is currently turned on
    descActiveOffsetTimer = tr("activated offset timer");
    //: A timer that starts after another timer is currently turned off
    descInactiveOffsetTimer = tr("deactivated offset timer");
    //: Accessible description for a newly created folder, shown after the folder name
    descNewFolder = tr("new folder");
    //: Accessible description for a newly created item, shown after the item name
    descNewItem = tr("new item");
    //: Accessible description indicating an item belongs to a package, shown after the item name. Keep short, as it's appended to other descriptions like "activated, package item"
    descPackageItem = tr("package item");

    setUnifiedTitleAndToolBarOnMac(true); //MAC OSX: make window moveable
    const QString hostName{mpHost->getName()};
    setWindowTitle(tr("%1 - Editor").arg(hostName));
    setWindowIcon(QIcon(qsl(":/icons/mudlet_editor.png")));
    auto statusBar = new QStatusBar(this);
    statusBar->setObjectName(qsl("editorStatusBar"));
    statusBar->setSizeGripEnabled(true);
    setStatusBar(statusBar);
    statusBar->show();

    // On the message side, so that it sits at the leading edge; the editor's
    // own temporary messages hide it for the seconds they are shown, which is
    // what a status bar does with anything but a permanent widget
    mpLabel_statusCounts = new QLabel(statusBar);
    mpLabel_statusCounts->setObjectName(qsl("editorStatusCounts"));
    statusBar->addWidget(mpLabel_statusCounts);
    // Permanent, which is the only thing that keeps it at the trailing edge
    mpLabel_statusAutosave = new QLabel(statusBar);
    mpLabel_statusAutosave->setObjectName(qsl("editorStatusAutosave"));
    statusBar->addPermanentWidget(mpLabel_statusAutosave);

    mpTimer_statusCounts = new QTimer(this);
    mpTimer_statusCounts->setSingleShot(true);
    mpTimer_statusCounts->setInterval(200ms);
    connect(mpTimer_statusCounts, &QTimer::timeout, this, &dlgTriggerEditor::updateEditorItemCounts);

    // The column an item is edited in is inset once, on the frame that carries
    // the form, the code pane and the strip between them - so the name at the
    // top of the form, the patterns under it and the first character of the
    // Lua all start at the same place. Everything inside is flush with it.
    if (QLayout* pColumnLayout = frame_right->layout()) {
        pColumnLayout->setContentsMargins(scmEditorColumnPaddingHorizontal, scmEditorColumnTopInset, scmEditorColumnPaddingHorizontal, 0);
    }

    mpNonCodeWidgets = new QWidget(this);
    auto* layoutColumn = new QVBoxLayout(mpNonCodeWidgets);
    layoutColumn->setContentsMargins(0, 0, 0, 0);
    layoutColumn->setSpacing(scmEditorColumnSpacing);
    splitter_right->addWidget(mpNonCodeWidgets);

    // system message area
    mpSystemMessageArea = new dlgSystemMessageArea(this);
    mpSystemMessageArea->setObjectName(qsl("mpSystemMessageArea"));
    mpSystemMessageArea->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
    // The notice is the first thing in the column when it is showing, so it
    // starts on the same line as the sidebar's first row and the search field.
    // Its own .ui file leaves the style's default margin round it, which is what
    // used to push it below both; the column it is in is what holds it off the
    // window's edges, as it does for the form that leads the column otherwise.
    if (QLayout* pMessageAreaLayout = mpSystemMessageArea->layout()) {
        pMessageAreaLayout->setContentsMargins(0, 0, 0, 0);
    }
    // set the stretch factor of the message area to 0 and everything else to 1,
    // so our errors box doesn't stretch to produce a grey area
    layoutColumn->addWidget(mpSystemMessageArea, 0);
    connect(mpSystemMessageArea->messageAreaCloseButton, &QAbstractButton::clicked, this, &dlgTriggerEditor::hideSystemMessageArea);
    connect(mpSystemMessageArea->notificationAreaMessageBox, &QLabel::linkActivated, this, &dlgTriggerEditor::slot_clickedMessageBox);
    connect(mudlet::self(), &mudlet::signal_appearanceChanged, this, &dlgTriggerEditor::slot_refreshBannerLinkColors);

    // main areas
    mpTriggersMainArea = new dlgTriggersMainArea(this);
    layoutColumn->addWidget(mpTriggersMainArea, 1);
    connect(mpTriggersMainArea->pushButtonFgColor, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_colorizeTriggerSetFgColor);
    connect(mpTriggersMainArea->pushButtonBgColor, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_colorizeTriggerSetBgColor);
    connect(mpTriggersMainArea->pushButtonSound, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_soundTrigger);
    connect(mpTriggersMainArea->groupBox_triggerColorizer, &QGroupBox::clicked, this, &dlgTriggerEditor::slot_toggleGroupBoxColorizeTrigger);
    connect(mpTriggersMainArea->toolButton_clearSoundFile, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_clearSoundFile);

    mpTimersMainArea = new dlgTimersMainArea(this);
    layoutColumn->addWidget(mpTimersMainArea, 1);

    mpAliasMainArea = new dlgAliasMainArea(this);
    layoutColumn->addWidget(mpAliasMainArea, 1);

    mpActionsMainArea = new dlgActionMainArea(this);
    layoutColumn->addWidget(mpActionsMainArea, 1);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    connect(mpActionsMainArea->checkBox_action_button_isPushDown, &QCheckBox::checkStateChanged, this, &dlgTriggerEditor::slot_toggleIsPushDownButton);
#else
    connect(mpActionsMainArea->checkBox_action_button_isPushDown, &QCheckBox::stateChanged, this, &dlgTriggerEditor::slot_toggleIsPushDownButton);
#endif

    mpKeysMainArea = new dlgKeysMainArea(this);
    layoutColumn->addWidget(mpKeysMainArea, 1);
    connect(mpKeysMainArea->pushButton_key_grabKey, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_keyGrab);

    mpVarsMainArea = new dlgVarsMainArea(this);
    layoutColumn->addWidget(mpVarsMainArea, 1);

    mpScriptsMainArea = new dlgScriptsMainArea(this);
    layoutColumn->addWidget(mpScriptsMainArea, 1);

    connect(mpScriptsMainArea->lineEdit_script_event_handler_entry, &QLineEdit::returnPressed, this, &dlgTriggerEditor::slot_scriptMainAreaAddHandler);
    connect(mpScriptsMainArea->listWidget_script_registered_event_handlers, &QListWidget::itemSelectionChanged, this, &dlgTriggerEditor::slot_scriptMainAreaEditHandler);
    connect(mpScriptsMainArea->listWidget_script_registered_event_handlers, &QListWidget::itemActivated, this, &dlgTriggerEditor::slot_scriptMainAreaClearHandlerSelection);


    // source editor area
    mpSourceEditorArea = new dlgSourceEditorArea(this);
    splitter_right->addWidget(mpSourceEditorArea);

    // And the edbee widget
    mpSourceEditorEdbee = mpSourceEditorArea->edbeeEditorWidget;
    mpSourceEditorEdbee->setAutoScrollMargin(20);
    mpSourceEditorEdbee->setPlaceholderText(tr("-- add your Lua code here"));
    mpSourceEditorEdbeeDocument = mpSourceEditorEdbee->textDocument();

    // Update the status bar on changes
    connect(mpSourceEditorEdbee->controller(), &edbee::TextEditorController::updateStatusTextSignal, this, &dlgTriggerEditor::slot_updateStatusBar);
    mpSourceEditorEdbee->controller()->setAutoScrollToCaret(edbee::TextEditorController::AutoScrollWhenFocus);

    // Update the editor preferences
    connect(mudlet::self(), &mudlet::signal_editorTextOptionsChanged, this, &dlgTriggerEditor::slot_changeEditorTextOptions);

    mudlet::loadEdbeeTheme(mpHost->getEditorTheme(), mpHost->getEditorThemeFile());

    // edbee editor find area
    mpSourceEditorFindArea = new dlgSourceEditorFindArea(mpSourceEditorEdbee);
    mpSourceEditorEdbee->horizontalScrollBar()->installEventFilter(mpSourceEditorFindArea);
    mpSourceEditorEdbee->verticalScrollBar()->installEventFilter(mpSourceEditorFindArea);
    // reposition the find area when the editor itself resizes, e.g. when the
    // system message area above it appears or disappears
    mpSourceEditorEdbee->installEventFilter(mpSourceEditorFindArea);
    mpSourceEditorFindArea->hide();

    connect(mpSourceEditorFindArea->lineEdit_findText, &QLineEdit::textChanged, this, &dlgTriggerEditor::slot_sourceFindTextChanges);
    connect(mpSourceEditorFindArea, &dlgSourceEditorFindArea::signal_sourceEditorMovementNecessary, this, &dlgTriggerEditor::slot_sourceFindMove);
    connect(mpSourceEditorFindArea->pushButton_findPrevious, &QPushButton::clicked, this, &dlgTriggerEditor::slot_sourceFindPrevious);
    connect(mpSourceEditorFindArea->pushButton_findNext, &QPushButton::clicked, this, &dlgTriggerEditor::slot_sourceFindNext);
    connect(mpSourceEditorFindArea->pushButton_replace, &QPushButton::clicked, this, &dlgTriggerEditor::slot_sourceReplace);
    connect(mpSourceEditorFindArea, &dlgSourceEditorFindArea::signal_sourceEditorFindPrevious, this, &dlgTriggerEditor::slot_sourceFindPrevious);
    connect(mpSourceEditorFindArea, &dlgSourceEditorFindArea::signal_sourceEditorFindNext, this, &dlgTriggerEditor::slot_sourceFindNext);
    connect(mpSourceEditorFindArea, &dlgSourceEditorFindArea::signal_sourceEditorReplace, this, &dlgTriggerEditor::slot_sourceReplace);
    connect(mpSourceEditorFindArea->pushButton_close, &QPushButton::clicked, this, &dlgTriggerEditor::slot_closeSourceFind);

    auto openSourceFindAction = new QAction(this);
    openSourceFindAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    openSourceFindAction->setShortcut(QKeySequence(QKeySequence::Find));
    mpSourceEditorArea->addAction(openSourceFindAction);
    connect(openSourceFindAction, &QAction::triggered, this, &dlgTriggerEditor::slot_openSourceFind);

    QAction* closeSourceFindAction = new QAction(this);
    closeSourceFindAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    closeSourceFindAction->setShortcut(QKeySequence(QKeySequence::Cancel));
    mpSourceEditorArea->addAction(closeSourceFindAction);
    connect(closeSourceFindAction, &QAction::triggered, this, &dlgTriggerEditor::slot_closeSourceFind);

    QAction* sourceFindNextAction = new QAction(this);
    sourceFindNextAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    sourceFindNextAction->setShortcut(QKeySequence(QKeySequence::FindNext));
    mpSourceEditorArea->addAction(sourceFindNextAction);
    connect(sourceFindNextAction, &QAction::triggered, this, &dlgTriggerEditor::slot_sourceFindNext);

    QAction* sourceFindPreviousAction = new QAction(this);
    sourceFindPreviousAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    sourceFindPreviousAction->setShortcut(QKeySequence(QKeySequence::FindPrevious));
    mpSourceEditorArea->addAction(sourceFindPreviousAction);
    connect(sourceFindPreviousAction, &QAction::triggered, this, &dlgTriggerEditor::slot_sourceFindPrevious);
    mpUndoStack = new EditorUndoStack(this);
    mpUndoStack->setUndoLimit(50);

    // These route to either text editor or item operations based on focus
    mpUndoAction = new QAction(QIcon::fromTheme(qsl("edit-undo"), QIcon(qsl(":/icons/edit-undo.png"))), tr("Undo"), this);
    mpUndoAction->setShortcut(QKeySequence(QKeySequence::Undo)); // Ctrl+Z
    mpUndoAction->setShortcutContext(Qt::WindowShortcut);
    mpUndoAction->setEnabled(false);
    /* In this and the next addAction(...) call we want to use the
     * QMainWindow::addAction(...) method and NOT the
     * dlgTriggerEditor::addAction(...) - without specifying this the derived
     * method is used. Calling the second one causes a bogus "new Toolbar"
     * containing a "new Menu" to be created each time the profile is opened
     * - which persist with a new pair added to the pile each time.*/
    QMainWindow::addAction(mpUndoAction);
    connect(mpUndoAction, &QAction::triggered, this, &dlgTriggerEditor::slot_smartUndo);

    mpRedoAction = new QAction(QIcon::fromTheme(qsl("edit-redo"), QIcon(qsl(":/icons/edit-redo.png"))), tr("Redo"), this);
    mpRedoAction->setShortcut(QKeySequence(QKeySequence::Redo)); // Ctrl+Y or Ctrl+Shift+Z
    mpRedoAction->setShortcutContext(Qt::WindowShortcut);
    mpRedoAction->setEnabled(false);
    QMainWindow::addAction(mpRedoAction);
    connect(mpRedoAction, &QAction::triggered, this, &dlgTriggerEditor::slot_smartRedo);

    connect(mpUndoStack, &QUndoStack::canUndoChanged, this, &dlgTriggerEditor::slot_updateUndoRedoButtonStates);
    connect(mpUndoStack, &QUndoStack::canRedoChanged, this, &dlgTriggerEditor::slot_updateUndoRedoButtonStates);

    connect(mpUndoStack, &QUndoStack::undoTextChanged, this, [this](const QString& text) {
        QString shortcut = mpUndoAction->shortcut().toString(QKeySequence::NativeText);
        if (!text.isEmpty()) {
            //: Tooltip for undo action. %1 is the action being undone (e.g., "Activate trigger \"foo\""), %2 is the keyboard shortcut
            QString undoText = tr("Undo: %1 (%2)").arg(text, shortcut);
            mpUndoAction->setToolTip(utils::richText(undoText));
            mpUndoAction->setStatusTip(undoText);
        } else {
            //: Tooltip for undo action when no specific action. %1 is the keyboard shortcut
            QString undoText = tr("Undo (%1)").arg(shortcut);
            mpUndoAction->setToolTip(utils::richText(undoText));
            mpUndoAction->setStatusTip(undoText);
        }
    });
    connect(mpUndoStack, &QUndoStack::redoTextChanged, this, [this](const QString& text) {
        QString shortcut = mpRedoAction->shortcut().toString(QKeySequence::NativeText);
        if (!text.isEmpty()) {
            //: Tooltip for redo action. %1 is the action being redone (e.g., "Activate trigger \"foo\""), %2 is the keyboard shortcut
            QString redoText = tr("Redo: %1 (%2)").arg(text, shortcut);
            mpRedoAction->setToolTip(utils::richText(redoText));
            mpRedoAction->setStatusTip(redoText);
        } else {
            //: Tooltip for redo action when no specific action. %1 is the keyboard shortcut
            QString redoText = tr("Redo (%1)").arg(shortcut);
            mpRedoAction->setToolTip(utils::richText(redoText));
            mpRedoAction->setStatusTip(redoText);
        }
    });

    // Store guarded pointer to text editor's undo stack for safe signal connections
    mpTextUndoStack = mpSourceEditorEdbee->controller()->textDocument()->textUndoStack();

    connect(mpTextUndoStack, &edbee::TextUndoStack::undoExecuted, this, &dlgTriggerEditor::slot_updateUndoRedoButtonStates);
    connect(mpTextUndoStack, &edbee::TextUndoStack::redoExecuted, this, &dlgTriggerEditor::slot_updateUndoRedoButtonStates);
    connect(mpTextUndoStack, &edbee::TextUndoStack::changeAdded, this, &dlgTriggerEditor::slot_updateUndoRedoButtonStates);

    slot_updateUndoRedoButtonStates();

    connect(mpUndoStack, &EditorUndoStack::itemsChanged, this, &dlgTriggerEditor::slot_itemsChanged);

    if (!smAutoCompleteInitialized) {
        auto* provider = new edbee::StringTextAutoCompleteProvider();

        // Add lua functions and reserved lua terms to an AutoComplete provider
        for (const QString& key : mudlet::smLuaFunctionNames.keys()) {
            provider->add(key, 3, mudlet::smLuaFunctionNames.value(key).toString());
        }

        // Lua reserved keywords (highest priority for basic syntax)
        provider->add(qsl("and"), 14);
        provider->add(qsl("break"), 14);
        provider->add(qsl("else"), 14);
        provider->add(qsl("elseif"), 14);
        provider->add(qsl("end"), 14);
        provider->add(qsl("false"), 14);
        provider->add(qsl("for"), 14);
        provider->add(qsl("function"), 14);
        provider->add(qsl("goto"), 14);
        provider->add(qsl("local"), 14);
        provider->add(qsl("nil"), 14);
        provider->add(qsl("not"), 14);
        provider->add(qsl("repeat"), 14);
        provider->add(qsl("return"), 14);
        provider->add(qsl("then"), 14);
        provider->add(qsl("true"), 14);
        provider->add(qsl("until"), 14);
        provider->add(qsl("while"), 14);

        // Standard Lua library functions (priority 4 - between Mudlet functions and keywords)
        // String library
        provider->add(qsl("string.byte"), 4, qsl("string.byte(s [, i [, j]])"));
        provider->add(qsl("string.char"), 4, qsl("string.char(...)"));
        provider->add(qsl("string.dump"), 4, qsl("string.dump(function)"));
        provider->add(qsl("string.find"), 4, qsl("string.find(s, pattern [, init [, plain]])"));
        provider->add(qsl("string.format"), 4, qsl("string.format(formatstring, ...)"));
        provider->add(qsl("string.gmatch"), 4, qsl("string.gmatch(s, pattern)"));
        provider->add(qsl("string.gsub"), 4, qsl("string.gsub(s, pattern, repl [, n])"));
        provider->add(qsl("string.len"), 4, qsl("string.len(s)"));
        provider->add(qsl("string.lower"), 4, qsl("string.lower(s)"));
        provider->add(qsl("string.match"), 4, qsl("string.match(s, pattern [, init])"));
        provider->add(qsl("string.rep"), 4, qsl("string.rep(s, n)"));
        provider->add(qsl("string.reverse"), 4, qsl("string.reverse(s)"));
        provider->add(qsl("string.sub"), 4, qsl("string.sub(s, i [, j])"));
        provider->add(qsl("string.upper"), 4, qsl("string.upper(s)"));

        // Table library
        provider->add(qsl("table.concat"), 4, qsl("table.concat(list [, sep [, i [, j]]])"));
        provider->add(qsl("table.insert"), 4, qsl("table.insert(list, [pos,] value)"));
        provider->add(qsl("table.pack"), 4, qsl("table.pack(...)"));
        provider->add(qsl("table.remove"), 4, qsl("table.remove(list [, pos])"));
        provider->add(qsl("table.sort"), 4, qsl("table.sort(list [, comp])"));
        provider->add(qsl("table.unpack"), 4, qsl("table.unpack(list [, i [, j]])"));

        // Math library
        provider->add(qsl("math.abs"), 4, qsl("math.abs(x)"));
        provider->add(qsl("math.acos"), 4, qsl("math.acos(x)"));
        provider->add(qsl("math.asin"), 4, qsl("math.asin(x)"));
        provider->add(qsl("math.atan"), 4, qsl("math.atan(x)"));
        provider->add(qsl("math.atan2"), 4, qsl("math.atan2(y, x)"));
        provider->add(qsl("math.ceil"), 4, qsl("math.ceil(x)"));
        provider->add(qsl("math.cos"), 4, qsl("math.cos(x)"));
        provider->add(qsl("math.cosh"), 4, qsl("math.cosh(x)"));
        provider->add(qsl("math.deg"), 4, qsl("math.deg(x)"));
        provider->add(qsl("math.exp"), 4, qsl("math.exp(x)"));
        provider->add(qsl("math.floor"), 4, qsl("math.floor(x)"));
        provider->add(qsl("math.fmod"), 4, qsl("math.fmod(x, y)"));
        provider->add(qsl("math.frexp"), 4, qsl("math.frexp(x)"));
        provider->add(qsl("math.huge"), 4, qsl("math.huge"));
        provider->add(qsl("math.ldexp"), 4, qsl("math.ldexp(m, e)"));
        provider->add(qsl("math.log"), 4, qsl("math.log(x [, base])"));
        provider->add(qsl("math.log10"), 4, qsl("math.log10(x)"));
        provider->add(qsl("math.max"), 4, qsl("math.max(x, ...)"));
        provider->add(qsl("math.min"), 4, qsl("math.min(x, ...)"));
        provider->add(qsl("math.modf"), 4, qsl("math.modf(x)"));
        provider->add(qsl("math.pi"), 4, qsl("math.pi"));
        provider->add(qsl("math.pow"), 4, qsl("math.pow(x, y)"));
        provider->add(qsl("math.rad"), 4, qsl("math.rad(x)"));
        provider->add(qsl("math.random"), 4, qsl("math.random([m [, n]])"));
        provider->add(qsl("math.randomseed"), 4, qsl("math.randomseed(x)"));
        provider->add(qsl("math.sin"), 4, qsl("math.sin(x)"));
        provider->add(qsl("math.sinh"), 4, qsl("math.sinh(x)"));
        provider->add(qsl("math.sqrt"), 4, qsl("math.sqrt(x)"));
        provider->add(qsl("math.tan"), 4, qsl("math.tan(x)"));
        provider->add(qsl("math.tanh"), 4, qsl("math.tanh(x)"));

        // IO library
        provider->add(qsl("io.close"), 4, qsl("io.close([file])"));
        provider->add(qsl("io.flush"), 4, qsl("io.flush()"));
        provider->add(qsl("io.input"), 4, qsl("io.input([file])"));
        provider->add(qsl("io.lines"), 4, qsl("io.lines([filename, ...])"));
        provider->add(qsl("io.open"), 4, qsl("io.open(filename [, mode])"));
        provider->add(qsl("io.output"), 4, qsl("io.output([file])"));
        provider->add(qsl("io.popen"), 4, qsl("io.popen(prog [, mode])"));
        provider->add(qsl("io.read"), 4, qsl("io.read(...)"));
        provider->add(qsl("io.tmpfile"), 4, qsl("io.tmpfile()"));
        provider->add(qsl("io.type"), 4, qsl("io.type(obj)"));
        provider->add(qsl("io.write"), 4, qsl("io.write(...)"));

        // OS library
        provider->add(qsl("os.clock"), 4, qsl("os.clock()"));
        provider->add(qsl("os.date"), 4, qsl("os.date([format [, time]])"));
        provider->add(qsl("os.difftime"), 4, qsl("os.difftime(t2, t1)"));
        provider->add(qsl("os.execute"), 4, qsl("os.execute([command])"));
        provider->add(qsl("os.exit"), 4, qsl("os.exit([code [, close]])"));
        provider->add(qsl("os.getenv"), 4, qsl("os.getenv(varname)"));
        provider->add(qsl("os.remove"), 4, qsl("os.remove(filename)"));
        provider->add(qsl("os.rename"), 4, qsl("os.rename(oldname, newname)"));
        provider->add(qsl("os.setlocale"), 4, qsl("os.setlocale(locale [, category])"));
        provider->add(qsl("os.time"), 4, qsl("os.time([table])"));
        provider->add(qsl("os.tmpname"), 4, qsl("os.tmpname()"));

        // Coroutine library
        provider->add(qsl("coroutine.create"), 4, qsl("coroutine.create(f)"));
        provider->add(qsl("coroutine.resume"), 4, qsl("coroutine.resume(co [, val1, ...])"));
        provider->add(qsl("coroutine.running"), 4, qsl("coroutine.running()"));
        provider->add(qsl("coroutine.status"), 4, qsl("coroutine.status(co)"));
        provider->add(qsl("coroutine.wrap"), 4, qsl("coroutine.wrap(f)"));
        provider->add(qsl("coroutine.yield"), 4, qsl("coroutine.yield(...)"));

        // Debug library
        provider->add(qsl("debug.debug"), 4, qsl("debug.debug()"));
        provider->add(qsl("debug.gethook"), 4, qsl("debug.gethook([thread])"));
        provider->add(qsl("debug.getinfo"), 4, qsl("debug.getinfo([thread,] f [, what])"));
        provider->add(qsl("debug.getlocal"), 4, qsl("debug.getlocal([thread,] f, local)"));
        provider->add(qsl("debug.getmetatable"), 4, qsl("debug.getmetatable(value)"));
        provider->add(qsl("debug.getregistry"), 4, qsl("debug.getregistry()"));
        provider->add(qsl("debug.getupvalue"), 4, qsl("debug.getupvalue(f, up)"));
        provider->add(qsl("debug.getuservalue"), 4, qsl("debug.getuservalue(u)"));
        provider->add(qsl("debug.sethook"), 4, qsl("debug.sethook([thread,] hook, mask [, count])"));
        provider->add(qsl("debug.setlocal"), 4, qsl("debug.setlocal([thread,] level, local, value)"));
        provider->add(qsl("debug.setmetatable"), 4, qsl("debug.setmetatable(value, table)"));
        provider->add(qsl("debug.setupvalue"), 4, qsl("debug.setupvalue(f, up, value)"));
        provider->add(qsl("debug.setuservalue"), 4, qsl("debug.setuservalue(udata, value)"));
        provider->add(qsl("debug.traceback"), 4, qsl("debug.traceback([thread,] [message [, level]])"));
        provider->add(qsl("debug.upvalueid"), 4, qsl("debug.upvalueid(f, n)"));
        provider->add(qsl("debug.upvaluejoin"), 4, qsl("debug.upvaluejoin(f1, n1, f2, n2)"));

        // Package library
        provider->add(qsl("package.config"), 4, qsl("package.config"));
        provider->add(qsl("package.cpath"), 4, qsl("package.cpath"));
        provider->add(qsl("package.loaded"), 4, qsl("package.loaded"));
        provider->add(qsl("package.loadlib"), 4, qsl("package.loadlib(libname, funcname)"));
        provider->add(qsl("package.path"), 4, qsl("package.path"));
        provider->add(qsl("package.preload"), 4, qsl("package.preload"));
        provider->add(qsl("package.searchers"), 4, qsl("package.searchers"));
        provider->add(qsl("package.searchpath"), 4, qsl("package.searchpath(name, path [, sep [, rep]])"));

        // Mudlet framework namespaced functions (priority 4 - same as Lua stdlib)
        // Geyser UI Framework
        provider->add(qsl("Geyser.Container:new"), 4, qsl("Geyser.Container:new(cons, container)"));
        provider->add(qsl("Geyser.Window:new"), 4, qsl("Geyser.Window:new(cons, container)"));
        provider->add(qsl("Geyser.Label:new"), 4, qsl("Geyser.Label:new(cons, container)"));
        provider->add(qsl("Geyser.MiniConsole:new"), 4, qsl("Geyser.MiniConsole:new(cons, container)"));
        provider->add(qsl("Geyser.Button:new"), 4, qsl("Geyser.Button:new(cons, container)"));
        provider->add(qsl("Geyser.Gauge:new"), 4, qsl("Geyser.Gauge:new(cons, container)"));
        provider->add(qsl("Geyser.Mapper:new"), 4, qsl("Geyser.Mapper:new(cons, container)"));
        provider->add(qsl("Geyser.UserWindow:new"), 4, qsl("Geyser.UserWindow:new(cons)"));
        provider->add(qsl("Geyser.CommandLine:new"), 4, qsl("Geyser.CommandLine:new(cons, container)"));
        provider->add(qsl("Geyser.HBox:new"), 4, qsl("Geyser.HBox:new(cons, container)"));
        provider->add(qsl("Geyser.VBox:new"), 4, qsl("Geyser.VBox:new(cons, container)"));
        provider->add(qsl("Geyser.ScrollBox:new"), 4, qsl("Geyser.ScrollBox:new(cons, container)"));
        provider->add(qsl("Geyser.ScrollBox:new2"), 4, qsl("Geyser.ScrollBox:new2()"));
        provider->add(qsl("Geyser.StyleSheet:new"), 4, qsl("Geyser.StyleSheet:new(stylesheet, parent, target)"));

        // Geyser namespace functions
        provider->add(qsl("Geyser.Color.parse"), 4, qsl("Geyser.Color.parse(color)"));
        provider->add(qsl("Geyser.Color.hex"), 4, qsl("Geyser.Color.hex(color)"));
        provider->add(qsl("Geyser.Color.hexa"), 4, qsl("Geyser.Color.hexa(color)"));
        provider->add(qsl("Geyser.Color.hhex"), 4, qsl("Geyser.Color.hhex(color)"));
        provider->add(qsl("Geyser.Color.hhexa"), 4, qsl("Geyser.Color.hhexa(color)"));
        provider->add(qsl("Geyser.Color.hdec"), 4, qsl("Geyser.Color.hdec(color)"));
        provider->add(qsl("Geyser.Color.hdeca"), 4, qsl("Geyser.Color.hdeca(color)"));

        // Adjustable Container Framework
        provider->add(qsl("Adjustable.Container:new"), 4, qsl("Adjustable.Container:new(cons, container)"));

        // Database Framework
        provider->add(qsl("db.create"), 4, qsl("db.create(db_name, schema)"));
        provider->add(qsl("db.query"), 4, qsl("db.query(db_name, query, ...)"));
        provider->add(qsl("db.insert"), 4, qsl("db.insert(db_name, sheet_name, values)"));
        provider->add(qsl("db.update"), 4, qsl("db.update(db_name, sheet_name, values, query)"));
        provider->add(qsl("db.delete"), 4, qsl("db.delete(db_name, sheet_name, query)"));
        provider->add(qsl("db.fetch"), 4, qsl("db.fetch(db_name, query, ...)"));
        provider->add(qsl("db.aggregate"), 4, qsl("db.aggregate(db_name, query, ...)"));

        // DateTime utilities
        provider->add(qsl("datetime.parse"), 4, qsl("datetime.parse(format, date_string)"));

        // Transfer ownership to Edbee - deleted automatically at app shutdown
        edbee::Edbee::instance()->autoCompleteProviderList()->giveProvider(provider);
        smAutoCompleteInitialized = true;
    }

    mpSourceEditorEdbee->textEditorComponent()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(mpSourceEditorEdbee->textEditorComponent(), &QWidget::customContextMenuRequested, this, &dlgTriggerEditor::slot_editorContextMenu);

    // option areas
    mpErrorConsole = new TConsole(mpHost, qsl("errors_%1").arg(hostName), TConsole::ErrorConsole, this);
    mpErrorConsole->setWrapAt(100);
    mpErrorConsole->slot_toggleTimeStamps(true);
    mpErrorConsole->print(qsl("%1\n").arg(tr("*** starting new session ***")));
    mpErrorConsole->setMinimumHeight(100);
    mpErrorConsole->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
    splitter_right->addWidget(mpErrorConsole);

    // The room a window has over and above what the panes need belongs to the
    // code, which is the one pane with no natural height of its own: the form
    // above it is as tall as the item's fields, and the errors box below it
    // opens at its floor and is dragged from there when it is wanted. Sharing
    // the surplus out instead left the code starting a long way down the
    // window, under a form stretched past anything it had to show. Both are
    // still dragged to any size the handles allow, and neither can collapse.
    splitter_right->setStretchFactor(0, 0); // mpNonCodeWidgets
    splitter_right->setCollapsible(0, false);
    splitter_right->setStretchFactor(1, 1); // mpSourceEditorArea
    splitter_right->setCollapsible(1, false);
    splitter_right->setStretchFactor(2, 0); // mpErrorConsole
    splitter_right->setCollapsible(2, false);

    mpErrorConsole->hide();

    // Every pane the right hand splitter stacks is in place, so the handle
    // between the first two exists to be given its heading
    setupEditorCodeHeader();

    // The trigger form's own options, and the strip that stands in for them
    buildTriggerOptionsPanel();

    // Only explicit clicks change the persisted preference - the space-driven
    // auto-collapse in slot_rightSplitterMoved must stay transient:
    connect(mpTriggersMainArea->toolButton_toggleExtraControls, &QAbstractButton::clicked, this, &dlgTriggerEditor::setTriggerOptionsShown);
    connect(mpButton_triggerOptionsSummary, &QAbstractButton::clicked, this, [this]() {
        setTriggerOptionsShown(true);
    });
    updateExtraControlsToggleIcon();

    connect(splitter_right, &QSplitter::splitterMoved, this, &dlgTriggerEditor::slot_rightSplitterMoved);
    // additional settings
    treeWidget_triggers->setColumnCount(1);
    treeWidget_triggers->setTreeType(TreeType::Trigger);
    treeWidget_triggers->setRootIsDecorated(false);
    treeWidget_triggers->setHost(mpHost);
    treeWidget_triggers->header()->hide();
    treeWidget_triggers->setContextMenuPolicy(Qt::ActionsContextMenu);

    treeWidget_aliases->hide();
    treeWidget_aliases->setHost(mpHost);
    treeWidget_aliases->setTreeType(TreeType::Alias);
    treeWidget_aliases->setColumnCount(1);
    treeWidget_aliases->header()->hide();
    treeWidget_aliases->setRootIsDecorated(false);
    treeWidget_aliases->setContextMenuPolicy(Qt::ActionsContextMenu);

    treeWidget_actions->hide();
    treeWidget_actions->setHost(mpHost);
    treeWidget_actions->setTreeType(TreeType::Action);
    treeWidget_actions->setColumnCount(1);
    treeWidget_actions->header()->hide();
    treeWidget_actions->setRootIsDecorated(false);
    treeWidget_actions->setContextMenuPolicy(Qt::ActionsContextMenu);

    treeWidget_timers->hide();
    treeWidget_timers->setHost(mpHost);
    treeWidget_timers->setTreeType(TreeType::Timer);
    treeWidget_timers->setColumnCount(1);
    treeWidget_timers->header()->hide();
    treeWidget_timers->setRootIsDecorated(false);
    treeWidget_timers->setContextMenuPolicy(Qt::ActionsContextMenu);

    treeWidget_variables->hide();
    treeWidget_variables->setHost(mpHost);
    treeWidget_variables->setTreeType(TreeType::Var);
    treeWidget_variables->setColumnCount(2);
    treeWidget_variables->hideColumn(1);
    treeWidget_variables->header()->hide();
    treeWidget_variables->setRootIsDecorated(false);
    treeWidget_variables->setContextMenuPolicy(Qt::ActionsContextMenu);

    treeWidget_keys->hide();
    treeWidget_keys->setHost(mpHost);
    treeWidget_keys->setTreeType(TreeType::Key);
    treeWidget_keys->setColumnCount(1);
    treeWidget_keys->header()->hide();
    treeWidget_keys->setRootIsDecorated(false);
    treeWidget_keys->setContextMenuPolicy(Qt::ActionsContextMenu);

    treeWidget_scripts->hide();
    treeWidget_scripts->setHost(mpHost);
    treeWidget_scripts->setTreeType(TreeType::Script);
    treeWidget_scripts->setColumnCount(1);
    treeWidget_scripts->header()->hide();
    treeWidget_scripts->setRootIsDecorated(false);
    treeWidget_scripts->setContextMenuPolicy(Qt::ActionsContextMenu);

    QAction* viewTriggerAction = new QAction(QIcon(qsl(":/icons/tools-wizard.png")), tr("Triggers"), this);
    viewTriggerAction->setStatusTip(tr("Show Triggers"));
    viewTriggerAction->setToolTip(qsl("%1 (%2)").arg(tr("Show Triggers"), QKeySequence(Qt::CTRL | Qt::Key_1).toString(QKeySequence::NativeText)));
    connect(viewTriggerAction, &QAction::triggered, this, &dlgTriggerEditor::slot_showTriggers);

    QAction* viewAliasAction = new QAction(QIcon(qsl(":/icons/system-users.png")), tr("Aliases"), this);
    viewAliasAction->setStatusTip(tr("Show Aliases"));
    viewAliasAction->setToolTip(qsl("%1 (%2)").arg(tr("Show Aliases"), QKeySequence(Qt::CTRL | Qt::Key_2).toString(QKeySequence::NativeText)));
    connect(viewAliasAction, &QAction::triggered, this, &dlgTriggerEditor::slot_showAliases);

    QAction* viewScriptsAction = new QAction(QIcon(qsl(":/icons/document-properties.png")), tr("Scripts"), this);
    viewScriptsAction->setStatusTip(tr("Show Scripts"));
    viewScriptsAction->setToolTip(qsl("%1 (%2)").arg(tr("Show Scripts"), QKeySequence(Qt::CTRL | Qt::Key_3).toString(QKeySequence::NativeText)));
    connect(viewScriptsAction, &QAction::triggered, this, &dlgTriggerEditor::slot_showScripts);

    QAction* showTimersAction = new QAction(QIcon(qsl(":/icons/chronometer.png")), tr("Timers"), this);
    showTimersAction->setStatusTip(tr("Show Timers"));
    showTimersAction->setToolTip(qsl("%1 (%2)").arg(tr("Show Timers"), QKeySequence(Qt::CTRL | Qt::Key_4).toString(QKeySequence::NativeText)));
    connect(showTimersAction, &QAction::triggered, this, &dlgTriggerEditor::slot_showTimers);

    QAction* viewKeysAction = new QAction(QIcon(qsl(":/icons/preferences-desktop-keyboard.png")), tr("Keys"), this);
    viewKeysAction->setStatusTip(tr("Show Keybindings"));
    viewKeysAction->setToolTip(qsl("%1 (%2)").arg(tr("Show Keybindings"), QKeySequence(Qt::CTRL | Qt::Key_5).toString(QKeySequence::NativeText)));
    connect(viewKeysAction, &QAction::triggered, this, &dlgTriggerEditor::slot_showKeys);

    QAction* viewVarsAction = new QAction(QIcon(qsl(":/icons/variables.png")), tr("Variables"), this);
    viewVarsAction->setStatusTip(tr("Show Variables"));
    viewVarsAction->setToolTip(qsl("%1 (%2)").arg(tr("Show Variables"), QKeySequence(Qt::CTRL | Qt::Key_6).toString(QKeySequence::NativeText)));
    connect(viewVarsAction, &QAction::triggered, this, &dlgTriggerEditor::slot_showVariables);

    QAction* viewActionAction = new QAction(QIcon(qsl(":/icons/bookmarks.png")), tr("Buttons"), this);
    viewActionAction->setStatusTip(tr("Show Buttons"));
    viewActionAction->setToolTip(qsl("%1 (%2)").arg(tr("Show Buttons"), QKeySequence(Qt::CTRL | Qt::Key_7).toString(QKeySequence::NativeText)));
    connect(viewActionAction, &QAction::triggered, this, &dlgTriggerEditor::slot_showActions);


    QAction* viewErrorsAction = new QAction(QIcon(qsl(":/icons/errors.png")), tr("Errors"), this);
    viewErrorsAction->setStatusTip(tr("Show/Hide the errors console in the bottom right of this editor."));
    viewErrorsAction->setToolTip(qsl("%1 (%2)").arg(tr("Show/Hide errors console"), QKeySequence(Qt::CTRL | Qt::Key_8).toString(QKeySequence::NativeText)));
    connect(viewErrorsAction, &QAction::triggered, this, &dlgTriggerEditor::slot_viewErrorsAction);

    QAction* viewStatsAction = new QAction(QIcon(qsl(":/icons/view-statistics.png")), tr("Statistics"), this);
    viewStatsAction->setStatusTip(tr("Generate a statistics summary display on the main profile console."));
    viewStatsAction->setToolTip(qsl("%1 (%2)").arg(tr("Generate statistics"), QKeySequence(Qt::CTRL | Qt::Key_9).toString(QKeySequence::NativeText)));
    connect(viewStatsAction, &QAction::triggered, this, &dlgTriggerEditor::slot_viewStatsAction);

    QAction* showDebugAreaAction = new QAction(QIcon(qsl(":/icons/tools-report-bug.png")), tr("Debug"), this);
    showDebugAreaAction->setStatusTip(tr("Show/Hide the separate Central Debug Console - when being displayed the system will be slower."));
    //: %1 is a keyboard shortcut, e.g. 'Ctrl+0' on Windows/Linux or '⌘0' on macOS
    showDebugAreaAction->setToolTip(
            utils::richText(tr("Show/Hide Debug Console (%1) -> system will be <b><i>slower</i></b>.").arg(QKeySequence(Qt::CTRL | Qt::Key_0).toString(QKeySequence::NativeText))));
    connect(showDebugAreaAction, &QAction::triggered, this, &dlgTriggerEditor::slot_toggleCentralDebugConsole);

    mpAction_toggleActive = new QAction(tr("Activate"), this);
    mpAction_toggleActive->setStatusTip(tr("Toggle Active or Non-Active Mode for Triggers, Scripts etc."));
    connect(mpAction_toggleActive, &QAction::triggered, this, &dlgTriggerEditor::slot_toggleItemOrGroupActiveFlag);
    connect(treeWidget_triggers, &QTreeWidget::itemActivated, this, &dlgTriggerEditor::slot_toggleItemOrGroupActiveFlag);
    connect(treeWidget_aliases, &QTreeWidget::itemActivated, this, &dlgTriggerEditor::slot_toggleItemOrGroupActiveFlag);
    connect(treeWidget_timers, &QTreeWidget::itemActivated, this, &dlgTriggerEditor::slot_toggleItemOrGroupActiveFlag);
    connect(treeWidget_scripts, &QTreeWidget::itemActivated, this, &dlgTriggerEditor::slot_toggleItemOrGroupActiveFlag);
    connect(treeWidget_actions, &QTreeWidget::itemActivated, this, &dlgTriggerEditor::slot_toggleItemOrGroupActiveFlag);
    connect(treeWidget_keys, &QTreeWidget::itemActivated, this, &dlgTriggerEditor::slot_toggleItemOrGroupActiveFlag);


    mAddItem = new QAction(QIcon(qsl(":/icons/document-new.png")), QString(), this);
    mAddItem->setToolTip(qsl("<p>%1 (%2)</p>").arg(tr("Add Item"), QKeySequence(QKeySequence::New).toString()));
    mAddItem->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    mAddItem->setShortcut(QKeySequence(QKeySequence::New));
    frame_left->addAction(mAddItem);
    connect(mAddItem, &QAction::triggered, this, &dlgTriggerEditor::slot_addNewItem);

    mDeleteItem = new QAction(QIcon::fromTheme(qsl(":/icons/edit-delete"), QIcon(qsl(":/icons/edit-delete.png"))), QString(), this);
    mDeleteItem->setToolTip(qsl("<p>%1 (%2)</p>").arg(tr("Delete Item"), QKeySequence(QKeySequence::Delete).toString()));
    mDeleteItem->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    mDeleteItem->setShortcut(QKeySequence(QKeySequence::Delete));
    frame_left->addAction(mDeleteItem);
    connect(mDeleteItem, &QAction::triggered, this, &dlgTriggerEditor::slot_deleteItemOrGroup);

    mAddGroup = new QAction(QIcon(qsl(":/icons/folder-new.png")), QString(), this);
    //: %1 is a keyboard shortcut, e.g. 'Ctrl+Shift+N' on Windows/Linux or '⌘⇧N' on macOS
    mAddGroup->setToolTip(tr("Add Group (%1)").arg(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N).toString(QKeySequence::NativeText)));
    mAddGroup->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    mAddGroup->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    frame_left->addAction(mAddGroup);
    connect(mAddGroup, &QAction::triggered, this, &dlgTriggerEditor::slot_addNewGroup);

    // 'Save Item' does not see to be translated as it is only ever used programmatically and not visible to the player
    // PLACEMARKER 1/3 save button texts need to be kept in sync
    mSaveItem = new QAction(QIcon(qsl(":/icons/document-save-as.png")), qsl("Save Item"), this);
    //: %1 is a keyboard shortcut, e.g. 'Ctrl+S' on Windows/Linux or '⌘S' on macOS
    mSaveItem->setToolTip(tr("<p>Saves the selected item. (%1)</p>"
                             "<p>Saving causes any changes to the item to take effect. It will not save to disk, "
                             "so changes will be lost in case of a computer/program crash (but Save Profile to the right will be secure.)</p>")
                                  .arg(QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText)));
    connect(mSaveItem, &QAction::triggered, this, &dlgTriggerEditor::slot_saveEdits);

    QAction* copyAction = new QAction(tr("Copy"), this);
    copyAction->setShortcut(QKeySequence(QKeySequence::Copy));
    // only take effect if the treeview is selected, otherwise it hijacks the shortcut from edbee
    copyAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    copyAction->setToolTip(utils::richText(tr("Copy the trigger/script/alias/etc")));
    copyAction->setStatusTip(tr("Copy the trigger/script/alias/etc"));
    treeWidget_triggers->addAction(copyAction);
    treeWidget_aliases->addAction(copyAction);
    treeWidget_timers->addAction(copyAction);
    treeWidget_scripts->addAction(copyAction);
    treeWidget_actions->addAction(copyAction);
    treeWidget_keys->addAction(copyAction);
    // The trees are hidden while the panel is showing search results, and a
    // shortcut on a hidden widget is not one the window can reach - so the
    // panel itself carries the pair too, the way add and delete already do
    frame_left->addAction(copyAction);
    connect(copyAction, &QAction::triggered, this, &dlgTriggerEditor::slot_copyXml);

    QAction* pasteAction = new QAction(tr("Paste"), this);
    pasteAction->setShortcut(QKeySequence(QKeySequence::Paste));
    // only take effect if the treeview is selected, otherwise it hijacks the shortcut from edbee
    pasteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    pasteAction->setToolTip(tr("Paste triggers/scripts/aliases/etc from the clipboard"));
    pasteAction->setStatusTip(tr("Paste triggers/scripts/aliases/etc from the clipboard"));
    treeWidget_triggers->addAction(pasteAction);
    treeWidget_aliases->addAction(pasteAction);
    treeWidget_timers->addAction(pasteAction);
    treeWidget_scripts->addAction(pasteAction);
    treeWidget_actions->addAction(pasteAction);
    treeWidget_keys->addAction(pasteAction);
    frame_left->addAction(pasteAction);
    connect(pasteAction, &QAction::triggered, this, &dlgTriggerEditor::slot_pasteXml);

    // Add delete action to all tree widgets for right-click context menu
    treeWidget_triggers->addAction(mDeleteItem);
    treeWidget_aliases->addAction(mDeleteItem);
    treeWidget_timers->addAction(mDeleteItem);
    treeWidget_scripts->addAction(mDeleteItem);
    treeWidget_actions->addAction(mDeleteItem);
    treeWidget_keys->addAction(mDeleteItem);
    treeWidget_variables->addAction(mDeleteItem);

    // Add separators and additional actions to context menu
    QAction* separator1 = new QAction(this);
    separator1->setSeparator(true);
    QAction* separator2 = new QAction(this);
    separator2->setSeparator(true);

    // Add context menu actions to all tree widgets
    QList<QTreeWidget*> treeWidgets = {treeWidget_triggers, treeWidget_aliases, treeWidget_timers, treeWidget_scripts, treeWidget_actions, treeWidget_keys, treeWidget_variables};

    for (QTreeWidget* widget : treeWidgets) {
        widget->addAction(mAddItem);
        widget->addAction(mAddGroup);
        widget->addAction(separator1);
        // Copy, Paste, Delete are already added above
        widget->addAction(separator2);
    }

    // Switching an item on and off is otherwise only offered by the dot at the
    // head of its row, which is a 9px target and says nothing about itself.
    // Variables are left out: there is nothing there to switch.
    const QList<QTreeWidget*> itemTreeWidgets = {treeWidget_triggers, treeWidget_aliases, treeWidget_timers, treeWidget_scripts, treeWidget_actions, treeWidget_keys};
    for (QTreeWidget* widget : itemTreeWidgets) {
        widget->addAction(mpAction_toggleActive);
    }

    QAction* importAction = new QAction(QIcon(qsl(":/icons/import.png")), tr("Import"), this);
    importAction->setEnabled(true);
    connect(importAction, &QAction::triggered, this, &dlgTriggerEditor::slot_import);

    mpExportAction = new QAction(QIcon(qsl(":/icons/export.png")), tr("Export"), this);
    mpExportAction->setEnabled(true);
    connect(mpExportAction, &QAction::triggered, this, &dlgTriggerEditor::slot_export);

    mpCreateModuleAction = new QAction(QIcon(qsl(":/icons/package-exporter.png")), tr("Create Module"), this);
    mpCreateModuleAction->setEnabled(true);
    mpCreateModuleAction->setToolTip(tr("<p>Create a module from selected items</p>"));
    connect(mpCreateModuleAction, &QAction::triggered, this, &dlgTriggerEditor::slot_createModule);

    mProfileSaveAction = new QAction(QIcon(qsl(":/icons/document-save-all.png")), tr("Save Profile"), this);
    //: %1 is a keyboard shortcut, e.g. 'Ctrl+Shift+S' on Windows/Linux or '⌘⇧S' on macOS
    mProfileSaveAction->setToolTip(tr("<p>Saves your profile. (%1)</p>"
                                      "<p>Saves your entire profile (triggers, aliases, scripts, timers, buttons and "
                                      "keys, but not the map or script-specific settings) to your computer disk, so "
                                      "in case of a computer or program crash, all changes you have done will be "
                                      "retained.</p>"
                                      "<p>It also makes a backup of your profile, you can load an older version of it "
                                      "when connecting.</p>"
                                      "<p>Should there be any modules that are marked to be \"<i>synced</i>\" this will "
                                      "also cause them to be saved and reloaded into other profiles if they too are "
                                      "active.</p>")
                                           .arg(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S).toString(QKeySequence::NativeText)));
    //: Status tip for saving profile
    mProfileSaveAction->setStatusTip(tr("Save profile (triggers, aliases, scripts, timers, buttons, keys - not the map) and synchronize modules."));

    mProfileSaveAsAction = new QAction(QIcon(qsl(":/icons/utilities-file-archiver.png")), tr("Save Profile As"), this);

    if (mpHost->mLoadedOk) {
        connect(mProfileSaveAction, &QAction::triggered, this, &dlgTriggerEditor::slot_profileSaveAction);
        connect(mProfileSaveAsAction, &QAction::triggered, this, &dlgTriggerEditor::slot_profileSaveAsAction);
    } else {
        mProfileSaveAction->setDisabled(true);
        mProfileSaveAsAction->setDisabled(true);
        auto disabledSaving = tr("Something went wrong loading your Mudlet profile and it could not be loaded. "
                                 "Try loading an older version in 'Connect - Options - Profile history'");
        mProfileSaveAction->setToolTip(disabledSaving);
        mProfileSaveAsAction->setToolTip(disabledSaving);
    }

    auto* nextSectionShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), this);
    QObject::connect(nextSectionShortcut, &QShortcut::activated, this, &dlgTriggerEditor::slot_nextSection);

    QShortcut* previousSectionShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this);
    connect(previousSectionShortcut, &QShortcut::activated, this, &dlgTriggerEditor::slot_previousSection);

    QShortcut* activateMainWindowAction = new QShortcut(QKeySequence((Qt::ALT | Qt::Key_E)), this);
    connect(activateMainWindowAction, &QShortcut::activated, this, &dlgTriggerEditor::slot_activateMainWindow);

    toolBar = new QToolBar();

    connect(mudlet::self(), &mudlet::signal_setToolBarIconSize, this, &dlgTriggerEditor::slot_setToolBarIconSize);
    connect(mudlet::self(), &mudlet::signal_setTreeIconSize, this, &dlgTriggerEditor::slot_setTreeWidgetIconSize);
    slot_setToolBarIconSize(mudlet::self()->mToolbarIconSize);
    slot_setTreeWidgetIconSize(mudlet::self()->mEditorTreeWidgetIconSize);

    toolBar->setMovable(true);
    toolBar->setObjectName(qsl("editorActionsToolbar"));
    //: This is the toolbar that is initially placed at the top of the editor.
    toolBar->setWindowTitle(tr("Editor Toolbar - %1 - Actions").arg(hostName));

    // Grouped by what the buttons do to the item being worked on, with what
    // acts on the profile as a whole pushed to the far end
    toolBar->addAction(mAddItem);
    toolBar->addAction(mAddGroup);
    toolBar->addSeparator();
    toolBar->addAction(mSaveItem);
    toolBar->addSeparator();
    toolBar->addAction(mDeleteItem);
    toolBar->addSeparator();
    // Smart undo/redo toolbar buttons (route based on focus)
    toolBar->addAction(mpUndoAction);
    toolBar->addAction(mpRedoAction);

    // A toolbar has no notion of an alignment, so an expanding blank is what
    // separates the item half from the profile half
    auto* pToolBarSpacer = new QWidget(toolBar);
    pToolBarSpacer->setObjectName(qsl("editorToolbarSpacer"));
    pToolBarSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar->addWidget(pToolBarSpacer);

    toolBar->addAction(importAction);
    toolBar->addAction(mpExportAction);
    toolBar->addAction(mpCreateModuleAction);
    toolBar->addSeparator();
    toolBar->addAction(mProfileSaveAction);

    // Every picture an action carries, on the toolbar and in the trees' context
    // menu alike, is drawn from a monochrome glyph tinted to the palette, which
    // restyleEditorIcons() redoes whenever the theme changes
    mEditorActionGlyphs = {{mAddItem, qsl(":/icons/editor-add.png")},
                           {mAddGroup, qsl(":/icons/editor-add-group.png")},
                           {mSaveItem, qsl(":/icons/editor-save-item.png")},
                           {mDeleteItem, qsl(":/icons/editor-delete.png")},
                           {mpUndoAction, qsl(":/icons/editor-undo.png")},
                           {mpRedoAction, qsl(":/icons/editor-redo.png")},
                           {importAction, qsl(":/icons/editor-import.png")},
                           {mpExportAction, qsl(":/icons/editor-export.png")},
                           {mpCreateModuleAction, qsl(":/icons/editor-module.png")},
                           {mProfileSaveAction, qsl(":/icons/editor-save-profile.png")},
                           {mProfileSaveAsAction, qsl(":/icons/editor-save-profile.png")},
                           // Menu-only, and reached from the trees rather than the toolbar
                           {mpAction_toggleActive, qsl(":/icons/editor-activate.png")},
                           {copyAction, qsl(":/icons/editor-copy.png")},
                           {pasteAction, qsl(":/icons/editor-paste.png")}};

    // Saving the profile under another name is the rarer of the pair, so it
    // hangs off the button beside it rather than taking a place of its own
    if (auto* pButton_saveProfile = qobject_cast<QToolButton*>(toolBar->widgetForAction(mProfileSaveAction))) {
        auto* pMenu_saveProfile = new QMenu(pButton_saveProfile);
        pMenu_saveProfile->addAction(mProfileSaveAsAction);
        pButton_saveProfile->setMenu(pMenu_saveProfile);
        pButton_saveProfile->setPopupMode(QToolButton::MenuButtonPopup);
    }

    applyEditorToolbarButtonStyles();

    connect(checkBox_displayAllVariables, &QAbstractButton::toggled, this, &dlgTriggerEditor::slot_toggleHiddenVariables);

    connect(mpVarsMainArea->checkBox_variable_hidden, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_hideVariable);

    // What the editor is showing is chosen from the sidebar down its left,
    // where a vertical strip of pictures used to be. The names read as one
    // list, so the seven things a profile is made of come first and the three
    // that run a one-off action are put below a rule.
    buildEditorSidebar();
    addEditorSidebarRow(viewTriggerAction, EditorViewType::cmTriggerView, qsl(":/icons/editor-triggers.png"));
    addEditorSidebarRow(viewAliasAction, EditorViewType::cmAliasView, qsl(":/icons/editor-aliases.png"));
    addEditorSidebarRow(viewScriptsAction, EditorViewType::cmScriptView, qsl(":/icons/editor-scripts.png"));
    addEditorSidebarRow(showTimersAction, EditorViewType::cmTimerView, qsl(":/icons/editor-timers.png"));
    addEditorSidebarRow(viewKeysAction, EditorViewType::cmKeysView, qsl(":/icons/editor-keys.png"));
    addEditorSidebarRow(viewActionAction, EditorViewType::cmActionView, qsl(":/icons/editor-buttons.png"));
    addEditorSidebarRow(viewVarsAction, EditorViewType::cmVarsView, qsl(":/icons/editor-variables.png"));
    addEditorSidebarSeparator();
    addEditorSidebarRow(viewErrorsAction, EditorViewType::cmUnknownView, qsl(":/icons/editor-errors.png"));
    addEditorSidebarRow(viewStatsAction, EditorViewType::cmUnknownView, qsl(":/icons/editor-statistics.png"));
    addEditorSidebarRow(showDebugAreaAction, EditorViewType::cmUnknownView, qsl(":/icons/editor-debug.png"));

    // A toolbar used to be what these actions were held by, which is what put
    // their Ctrl+1 to Ctrl+0 shortcuts within reach of the window; a sidebar
    // row is a piece of data rather than a widget an action can live on, so the
    // window holds them itself
    addActions(mEditorViewActions);

    // The sidebar is the window's leftmost column rather than a piece of the
    // .ui file's layout, so what that file makes the central widget moves in
    // beside it. Taken rather than reparented, or the main window would be left
    // holding a central widget that has gone elsewhere.
    auto* pShell = new QWidget(this);
    pShell->setObjectName(qsl("editorShell"));
    auto* pShellLayout = new QHBoxLayout(pShell);
    pShellLayout->setContentsMargins(0, 0, 0, 0);
    pShellLayout->setSpacing(0);
    pShellLayout->addWidget(mpWidget_editorSidebarPane);
    if (QWidget* pEditorBody = QMainWindow::takeCentralWidget()) {
        pShellLayout->addWidget(pEditorBody, 1);
        // Taking it made it a window of its own, which hid it
        pEditorBody->show();
    }
    QMainWindow::setCentralWidget(pShell);

    QMainWindow::addToolBar(Qt::TopToolBarArea, toolBar);

    // (Top) "Actions" toolbar - the only one left to lose:
    //: This will restore that toolbar in the editor window, after a user has hidden it or moved it to another docking location or floated it elsewhere.
    mpAction_restoreEditorActionsToolbar = new QAction(tr("Restore Actions toolbar"), this);

    connect(mpAction_restoreEditorActionsToolbar, &QAction::triggered, this, &dlgTriggerEditor::slot_restoreEditorActionsToolbar);
    connect(toolBar, &QToolBar::visibilityChanged, this, &dlgTriggerEditor::slot_visibilityChangedEditorActionsToolbar);
    connect(toolBar, &QToolBar::topLevelChanged, this, &dlgTriggerEditor::slot_floatingChangedEditorActionsToolbar);

    treeWidget_triggers->addAction(mpAction_restoreEditorActionsToolbar);
    treeWidget_aliases->addAction(mpAction_restoreEditorActionsToolbar);
    treeWidget_timers->addAction(mpAction_restoreEditorActionsToolbar);
    treeWidget_scripts->addAction(mpAction_restoreEditorActionsToolbar);
    treeWidget_actions->addAction(mpAction_restoreEditorActionsToolbar);
    treeWidget_keys->addAction(mpAction_restoreEditorActionsToolbar);

    // This only has to be shown should the toolbar get hidden, and by default
    // the starting state for that is a visible one so it needs to be hidden at
    // the start:
    mpAction_restoreEditorActionsToolbar->setVisible(false);
    setShortcuts();

    setupEditorPanel();
    applyEditorShellStyle();

    // Adding, deleting and activating all reach the tree through its model, so
    // that is the one place the counts can be kept up to date from
    for (QTreeWidget* pTreeWidget : {treeWidget_triggers, treeWidget_aliases, treeWidget_timers, treeWidget_scripts, treeWidget_actions, treeWidget_keys}) {
        connect(pTreeWidget->model(), &QAbstractItemModel::rowsInserted, this, &dlgTriggerEditor::scheduleEditorItemCountUpdate);
        connect(pTreeWidget->model(), &QAbstractItemModel::rowsRemoved, this, &dlgTriggerEditor::scheduleEditorItemCountUpdate);
        connect(pTreeWidget->model(), &QAbstractItemModel::dataChanged, this, &dlgTriggerEditor::scheduleEditorItemCountUpdate);
    }

    auto config = mpSourceEditorEdbee->config();
    config->beginChanges();
    config->setThemeName(mpHost->getEditorTheme());
    config->setFont(mpHost->getDisplayFont());
    config->setShowWhitespaceMode((mudlet::self()->mEditorTextOptions & QTextOption::ShowTabsAndSpaces) ? edbee::TextEditorConfig::ShowWhitespaces : edbee::TextEditorConfig::HideWhitespaces);
    config->setUseLineSeparator(mudlet::self()->mEditorTextOptions & QTextOption::ShowLineAndParagraphSeparators);
    config->setAutocompleteAutoShow(mpHost->mEditorAutoComplete);
    config->setRenderBidiContolCharacters(mpHost->getEditorShowBidi());
    config->setAutocompleteMinimalCharacters(3);
    config->endChanges();

    connect(comboBox_searchTerms, qOverload<int>(&QComboBox::activated), this, &dlgTriggerEditor::slot_searchMudletItems);

    // The per-type selection slots reload the whole form unconditionally, so a stray
    // clicked() on the row that is already loaded would throw away unsaved edits. Only
    // the first click on a row has to reach them - itemSelectionChanged() has already
    // loaded that row by the time clicked() arrives - so drop same-row emissions here,
    // which keeps a click on a control inside the current row inert by construction.
    auto connectSelectionOnClick = [this](QTreeWidget* pTreeWidget, QTreeWidgetItem* const& pCurrentItem, void (dlgTriggerEditor::*pSelectionSlot)(QTreeWidgetItem*)) {
        connect(pTreeWidget, &QTreeWidget::itemClicked, this, [this, &pCurrentItem, pSelectionSlot](QTreeWidgetItem* pItem) {
            if (pItem != pCurrentItem) {
                (this->*pSelectionSlot)(pItem);
            }
        });
    };
    connectSelectionOnClick(treeWidget_triggers, mpCurrentTriggerItem, &dlgTriggerEditor::slot_triggerSelected);
    connect(treeWidget_triggers, &QTreeWidget::itemSelectionChanged, this, &dlgTriggerEditor::slot_treeSelectionChanged);
    connect(treeWidget_triggers, &TTreeWidget::itemMoved, this, &dlgTriggerEditor::slot_itemMoved);
    connect(treeWidget_triggers, &TTreeWidget::batchMoveStarted, this, &dlgTriggerEditor::slot_batchMoveStarted);
    connect(treeWidget_triggers, &TTreeWidget::batchMoveEnded, this, &dlgTriggerEditor::slot_batchMoveEnded);
    connectSelectionOnClick(treeWidget_keys, mpCurrentKeyItem, &dlgTriggerEditor::slot_keySelected);
    connect(treeWidget_keys, &QTreeWidget::itemSelectionChanged, this, &dlgTriggerEditor::slot_treeSelectionChanged);
    connect(treeWidget_keys, &TTreeWidget::itemMoved, this, &dlgTriggerEditor::slot_itemMoved);
    connect(treeWidget_keys, &TTreeWidget::batchMoveStarted, this, &dlgTriggerEditor::slot_batchMoveStarted);
    connect(treeWidget_keys, &TTreeWidget::batchMoveEnded, this, &dlgTriggerEditor::slot_batchMoveEnded);
    connectSelectionOnClick(treeWidget_timers, mpCurrentTimerItem, &dlgTriggerEditor::slot_timerSelected);
    connect(treeWidget_timers, &QTreeWidget::itemSelectionChanged, this, &dlgTriggerEditor::slot_treeSelectionChanged);
    connect(treeWidget_timers, &TTreeWidget::itemMoved, this, &dlgTriggerEditor::slot_itemMoved);
    connect(treeWidget_timers, &TTreeWidget::batchMoveStarted, this, &dlgTriggerEditor::slot_batchMoveStarted);
    connect(treeWidget_timers, &TTreeWidget::batchMoveEnded, this, &dlgTriggerEditor::slot_batchMoveEnded);
    connectSelectionOnClick(treeWidget_scripts, mpCurrentScriptItem, &dlgTriggerEditor::slot_scriptsSelected);
    connect(treeWidget_scripts, &QTreeWidget::itemSelectionChanged, this, &dlgTriggerEditor::slot_treeSelectionChanged);
    connect(treeWidget_scripts, &TTreeWidget::itemMoved, this, &dlgTriggerEditor::slot_itemMoved);
    connect(treeWidget_scripts, &TTreeWidget::batchMoveStarted, this, &dlgTriggerEditor::slot_batchMoveStarted);
    connect(treeWidget_scripts, &TTreeWidget::batchMoveEnded, this, &dlgTriggerEditor::slot_batchMoveEnded);
    connectSelectionOnClick(treeWidget_aliases, mpCurrentAliasItem, &dlgTriggerEditor::slot_aliasSelected);
    connect(treeWidget_aliases, &QTreeWidget::itemSelectionChanged, this, &dlgTriggerEditor::slot_treeSelectionChanged);
    connect(treeWidget_aliases, &TTreeWidget::itemMoved, this, &dlgTriggerEditor::slot_itemMoved);
    connect(treeWidget_aliases, &TTreeWidget::batchMoveStarted, this, &dlgTriggerEditor::slot_batchMoveStarted);
    connect(treeWidget_aliases, &TTreeWidget::batchMoveEnded, this, &dlgTriggerEditor::slot_batchMoveEnded);
    connectSelectionOnClick(treeWidget_actions, mpCurrentActionItem, &dlgTriggerEditor::slot_actionSelected);
    connect(treeWidget_actions, &QTreeWidget::itemSelectionChanged, this, &dlgTriggerEditor::slot_treeSelectionChanged);
    connect(treeWidget_actions, &TTreeWidget::itemMoved, this, &dlgTriggerEditor::slot_itemMoved);
    connect(treeWidget_actions, &TTreeWidget::batchMoveStarted, this, &dlgTriggerEditor::slot_batchMoveStarted);
    connect(treeWidget_actions, &TTreeWidget::batchMoveEnded, this, &dlgTriggerEditor::slot_batchMoveEnded);
    connect(treeWidget_variables, &QTreeWidget::itemClicked, this, &dlgTriggerEditor::slot_variableSelected);
    connect(treeWidget_variables, &QTreeWidget::itemChanged, this, &dlgTriggerEditor::slot_variableChanged);
    connect(treeWidget_variables, &QTreeWidget::itemSelectionChanged, this, &dlgTriggerEditor::slot_treeSelectionChanged);
    connect(treeWidget_searchResults, &QTreeWidget::itemClicked, this, &dlgTriggerEditor::slot_itemSelectedInSearchResults);

    // triggers
    connect(mpTriggersMainArea->lineEdit_trigger_name, &QLineEdit::textEdited, this, &dlgTriggerEditor::slot_itemEdited);
    connect(mpTriggersMainArea->lineEdit_trigger_command, &QLineEdit::textEdited, this, &dlgTriggerEditor::slot_itemEdited);
    connect(mpTriggersMainArea->pushButtonSound, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_itemEdited);
    connect(mpSourceEditorEdbeeDocument, &edbee::TextDocument::textChanged, this, &dlgTriggerEditor::slot_itemEdited);

    // Per-property immediate saves for triggers (creates individual undo entries)
    connect(mpTriggersMainArea->lineEdit_trigger_name, &QLineEdit::editingFinished, this, &dlgTriggerEditor::slot_saveProperty_TriggerName);
    connect(mpTriggersMainArea->lineEdit_trigger_command, &QLineEdit::editingFinished, this, &dlgTriggerEditor::slot_saveProperty_TriggerCommand);
    connect(mpTriggersMainArea->spinBox_stayOpen, qOverload<int>(&QSpinBox::valueChanged), this, &dlgTriggerEditor::slot_saveProperty_TriggerStayOpen);
    connect(mpTriggersMainArea->spinBox_lineMargin, qOverload<int>(&QSpinBox::valueChanged), this, &dlgTriggerEditor::slot_saveProperty_TriggerLineMargin);
    connect(mpTriggersMainArea->checkBox_filterTrigger, &QCheckBox::toggled, this, &dlgTriggerEditor::slot_saveProperty_TriggerFilterTrigger);
    connect(mpTriggersMainArea->checkBox_perlSlashGOption, &QCheckBox::toggled, this, &dlgTriggerEditor::slot_saveProperty_TriggerPerlSlashG);
    connect(mpTriggersMainArea->groupBox_soundTrigger, &QGroupBox::toggled, this, &dlgTriggerEditor::slot_saveProperty_TriggerSoundEnabled);
    connect(mpTriggersMainArea->lineEdit_soundFile, &QLineEdit::editingFinished, this, &dlgTriggerEditor::slot_saveProperty_TriggerSoundFile);
    connect(mpTriggersMainArea->groupBox_triggerColorizer, &QGroupBox::toggled, this, &dlgTriggerEditor::slot_saveProperty_TriggerColorizer);

    // aliases
    connect(mpAliasMainArea->lineEdit_alias_name, &QLineEdit::textEdited, this, &dlgTriggerEditor::slot_itemEdited);
    connect(mpAliasMainArea->lineEdit_alias_pattern, &QLineEdit::textEdited, this, &dlgTriggerEditor::slot_itemEdited);
    connect(mpAliasMainArea->lineEdit_alias_command, &QLineEdit::textEdited, this, &dlgTriggerEditor::slot_itemEdited);

    // Per-property immediate saves for aliases
    connect(mpAliasMainArea->lineEdit_alias_name, &QLineEdit::editingFinished, this, &dlgTriggerEditor::slot_saveProperty_AliasName);
    connect(mpAliasMainArea->lineEdit_alias_pattern, &QLineEdit::editingFinished, this, &dlgTriggerEditor::slot_saveProperty_AliasPattern);
    connect(mpAliasMainArea->lineEdit_alias_command, &QLineEdit::editingFinished, this, &dlgTriggerEditor::slot_saveProperty_AliasCommand);

    // scripts
    connect(mpScriptsMainArea->lineEdit_script_name, &QLineEdit::textEdited, this, &dlgTriggerEditor::slot_itemEdited);
    connect(mpScriptsMainArea->lineEdit_script_event_handler_entry, &QLineEdit::textEdited, this, &dlgTriggerEditor::slot_itemEdited);

    // Per-property immediate saves for scripts
    connect(mpScriptsMainArea->lineEdit_script_name, &QLineEdit::editingFinished, this, &dlgTriggerEditor::slot_saveProperty_ScriptName);

    // timers
    connect(mpTimersMainArea->lineEdit_timer_name, &QLineEdit::textEdited, this, &dlgTriggerEditor::slot_itemEdited);
    connect(mpTimersMainArea->lineEdit_timer_command, &QLineEdit::textEdited, this, &dlgTriggerEditor::slot_itemEdited);

    // Per-property immediate saves for timers
    connect(mpTimersMainArea->lineEdit_timer_name, &QLineEdit::editingFinished, this, &dlgTriggerEditor::slot_saveProperty_TimerName);
    connect(mpTimersMainArea->lineEdit_timer_command, &QLineEdit::editingFinished, this, &dlgTriggerEditor::slot_saveProperty_TimerCommand);
    connect(mpTimersMainArea->timeEdit_timer_hours, &QTimeEdit::timeChanged, this, &dlgTriggerEditor::slot_saveProperty_TimerTime);
    connect(mpTimersMainArea->timeEdit_timer_minutes, &QTimeEdit::timeChanged, this, &dlgTriggerEditor::slot_saveProperty_TimerTime);
    connect(mpTimersMainArea->timeEdit_timer_seconds, &QTimeEdit::timeChanged, this, &dlgTriggerEditor::slot_saveProperty_TimerTime);
    connect(mpTimersMainArea->timeEdit_timer_msecs, &QTimeEdit::timeChanged, this, &dlgTriggerEditor::slot_saveProperty_TimerTime);

    // keys
    connect(mpKeysMainArea->lineEdit_key_name, &QLineEdit::textEdited, this, &dlgTriggerEditor::slot_itemEdited);
    connect(mpKeysMainArea->lineEdit_key_command, &QLineEdit::textEdited, this, &dlgTriggerEditor::slot_itemEdited);
    connect(mpKeysMainArea->pushButton_key_grabKey, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_itemEdited);

    // Per-property immediate saves for keys
    connect(mpKeysMainArea->lineEdit_key_name, &QLineEdit::editingFinished, this, &dlgTriggerEditor::slot_saveProperty_KeyName);
    connect(mpKeysMainArea->lineEdit_key_command, &QLineEdit::editingFinished, this, &dlgTriggerEditor::slot_saveProperty_KeyCommand);

    // buttons
    connect(mpActionsMainArea->lineEdit_action_name, &QLineEdit::textEdited, this, &dlgTriggerEditor::slot_itemEdited);
    connect(mpActionsMainArea->lineEdit_action_name, &QLineEdit::textEdited, this, &dlgTriggerEditor::slot_itemEdited);

    // Per-property immediate saves for actions (buttons)
    connect(mpActionsMainArea->lineEdit_action_name, &QLineEdit::editingFinished, this, &dlgTriggerEditor::slot_saveProperty_ActionName);
    connect(mpActionsMainArea->lineEdit_action_button_command_down, &QLineEdit::editingFinished, this, &dlgTriggerEditor::slot_saveProperty_ActionCommandDown);
    connect(mpActionsMainArea->lineEdit_action_button_command_up, &QLineEdit::editingFinished, this, &dlgTriggerEditor::slot_saveProperty_ActionCommandUp);
    connect(mpActionsMainArea->checkBox_action_button_isPushDown, &QCheckBox::toggled, this, &dlgTriggerEditor::slot_saveProperty_ActionIsPushDown);
    connect(mpActionsMainArea->spinBox_action_bar_columns, qOverload<int>(&QSpinBox::valueChanged), this, &dlgTriggerEditor::slot_saveProperty_ActionBarColumns);
    connect(mpActionsMainArea->spinBox_action_bar_offsetToFirstButton, qOverload<int>(&QSpinBox::valueChanged), this, &dlgTriggerEditor::slot_saveProperty_ActionBarFillerOffset);
    connect(mpActionsMainArea->comboBox_action_bar_orientation, qOverload<int>(&QComboBox::currentIndexChanged), this, &dlgTriggerEditor::slot_saveProperty_ActionBarOrientation);
    connect(mpActionsMainArea->comboBox_action_bar_location, qOverload<int>(&QComboBox::currentIndexChanged), this, &dlgTriggerEditor::slot_saveProperty_ActionBarLocation);
    connect(mpActionsMainArea->comboBox_action_button_rotation, qOverload<int>(&QComboBox::currentIndexChanged), this, &dlgTriggerEditor::slot_saveProperty_ActionButtonRotation);
    connect(mpActionsMainArea->plainTextEdit_action_css, &QPlainTextEdit::textChanged, this, &dlgTriggerEditor::slot_saveProperty_ActionCSS);

    // The clear button needs no handler of its own: it empties the field, and an
    // empty field is what puts the results away - see the textChanged() below
    comboBox_searchTerms->lineEdit()->setClearButtonEnabled(true);
    auto pLineEdit_searchTerm = comboBox_searchTerms->lineEdit();
    //: Placeholder in the editor's search field, which searches the profile's own triggers, aliases, scripts and the rest
    pLineEdit_searchTerm->setPlaceholderText(tr("Search items..."));

    mpAction_searchOptions = new QAction(tr("Search Options"), this);
    mpAction_searchOptions->setObjectName(qsl("mpAction_searchOptions"));

    QMenu* pMenu_searchOptions = new QMenu(tr("Search Options"), this);
    pMenu_searchOptions->setObjectName(qsl("pMenu_searchOptions"));
    pMenu_searchOptions->setToolTipsVisible(true);

    mpAction_searchCaseSensitive = new QAction(tr("Case sensitive"), this);
    mpAction_searchCaseSensitive->setObjectName(qsl("mpAction_searchCaseSensitive"));
    mpAction_searchCaseSensitive->setToolTip(utils::richText(tr("Match case precisely")));
    mpAction_searchCaseSensitive->setCheckable(true);
    pMenu_searchOptions->insertAction(nullptr, mpAction_searchCaseSensitive);

    mpAction_searchIncludeVariables = new QAction(tr("Include variables"), this);
    mpAction_searchIncludeVariables->setObjectName(qsl("mpAction_searchIncludeVariables"));
    mpAction_searchIncludeVariables->setToolTip(utils::richText(tr("Search variables (slower)")));
    mpAction_searchIncludeVariables->setCheckable(true);
    pMenu_searchOptions->insertAction(nullptr, mpAction_searchIncludeVariables);

    mpAction_searchWholeWord = new QAction(tr("Whole word"), this);
    mpAction_searchWholeWord->setObjectName(qsl("mpAction_searchWholeWord"));
    mpAction_searchWholeWord->setToolTip(utils::richText(tr("Only match whole words")));
    mpAction_searchWholeWord->setCheckable(true);
    pMenu_searchOptions->insertAction(nullptr, mpAction_searchWholeWord);

    // This will set the icon and the Search Options menu items - and needs to
    // be done BEFORE the menu items are connect()ed:
    setSearchOptions(mSearchOptions);

    connect(mpAction_searchCaseSensitive, &QAction::triggered, this, &dlgTriggerEditor::slot_toggleSearchCaseSensitivity);
    connect(mpAction_searchIncludeVariables, &QAction::triggered, this, &dlgTriggerEditor::slot_toggleSearchIncludeVariables);
    connect(mpAction_searchWholeWord, &QAction::triggered, this, &dlgTriggerEditor::slot_toggleSearchWholeWord);


    mpAction_searchOptions->setMenu(pMenu_searchOptions);

    pLineEdit_searchTerm->addAction(mpAction_searchOptions, QLineEdit::LeadingPosition);

    // The box's own drop-down is given no width by the shell stylesheet, so that
    // the row reads as a field rather than as a list to pick from. The searches
    // already run are still in it, and Alt+Down still opens them - this is the
    // same door for the mouse, and it is only there once there is a search to go
    // back to.
    mpAction_searchHistory = pLineEdit_searchTerm->addAction(QIcon(), QLineEdit::TrailingPosition);
    mpAction_searchHistory->setObjectName(qsl("mpAction_searchHistory"));
    //: Tooltip on the small chevron at the right of the editor's search field, which reopens what was searched for before
    mpAction_searchHistory->setToolTip(utils::richText(tr("Recent searches")));
    connect(mpAction_searchHistory, &QAction::triggered, this, [this]() {
        comboBox_searchTerms->showPopup();
    });
    connect(comboBox_searchTerms->model(), &QAbstractItemModel::rowsInserted, this, &dlgTriggerEditor::updateSearchHistoryAction);
    connect(comboBox_searchTerms->model(), &QAbstractItemModel::rowsRemoved, this, &dlgTriggerEditor::updateSearchHistoryAction);
    updateSearchHistoryAction();

    connect(mpScriptsMainArea->toolButton_script_add_event_handler, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_scriptMainAreaAddHandler);
    connect(mpScriptsMainArea->toolButton_script_remove_event_handler, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_scriptMainAreaDeleteHandler);

    mpTriggersMainArea->hide();
    mpTimersMainArea->hide();
    mpScriptsMainArea->hide();
    mpAliasMainArea->hide();
    mpActionsMainArea->hide();
    mpKeysMainArea->hide();
    mpVarsMainArea->hide();

    mpSourceEditorArea->hide();

    clearEditorNotification();

    treeWidget_triggers->show();
    treeWidget_aliases->hide();
    treeWidget_actions->hide();
    treeWidget_timers->hide();
    treeWidget_scripts->hide();
    treeWidget_keys->hide();
    treeWidget_variables->hide();

    // Hiding a widget invalidates the layout measurement Qt caches for it, but
    // that invalidation only travels up to its containers while the widget is
    // still visible. Everything hidden just above therefore leaves the
    // containers still holding a minimum height measured while all seven main
    // areas were on screen at once - their heights stacked, around 1600px. No
    // event loop runs between here and readSettings(), so that stale figure is
    // the one the first layout pass hands the window as its minimum height, and
    // the editor opens at screen height however small the geometry the user
    // left behind. Re-measuring the containers makes the minimum honest.
    const QList<QWidget*> justHidden{mpTriggersMainArea,
                                     mpTimersMainArea,
                                     mpScriptsMainArea,
                                     mpAliasMainArea,
                                     mpActionsMainArea,
                                     mpKeysMainArea,
                                     mpVarsMainArea,
                                     mpSourceEditorArea,
                                     treeWidget_aliases,
                                     treeWidget_actions,
                                     treeWidget_timers,
                                     treeWidget_scripts,
                                     treeWidget_keys,
                                     treeWidget_variables};
    for (const QWidget* pWidget : justHidden) {
        for (QWidget* pContainer = pWidget->parentWidget(); pContainer; pContainer = pContainer->parentWidget()) {
            pContainer->updateGeometry();
            if (pContainer == this) {
                break;
            }
        }
    }

    readSettings();
    slot_showAllTriggerControls(mShowAllTriggerControls);

    comboBox_searchTerms->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QFrame* searchContainer = new QFrame();
    mpWidget_searchResultsPane = searchContainer;
    QVBoxLayout* searchLayout = new QVBoxLayout(searchContainer);
    searchLayout->addWidget(treeWidget_searchResults);

    // What is typed leads the panel rather than trailing it: the field belongs
    // with the trees it narrows down, not underneath the results it last
    // produced. The combo box keeps the row the .ui file gives it - it is the
    // row that moves, so no widget changes hands and the search behaviour is
    // untouched.
    verticalLayout_frame_left->removeWidget(widget_searchTerm);
    verticalLayout_frame_left->insertWidget(0, widget_searchTerm);
    // ...and being the first thing in its column, it starts on the line the
    // other two columns start on. The row's own layout is what holds it down,
    // since the pane behind it is painted to the top of the window.
    if (QLayout* pSearchRowLayout = widget_searchTerm->layout()) {
        const QMargins searchRowMargins = pSearchRowLayout->contentsMargins();
        pSearchRowLayout->setContentsMargins(searchRowMargins.left(), scmEditorColumnTopInset, searchRowMargins.right(), searchRowMargins.bottom());
    }

    // Which variables are listed narrows what a search of them turns up, so it
    // stays reachable while the results are the thing on show: it sits with the
    // search row in the part of the panel that does not take turns, rather than
    // inside the pane of trees that goes away. What shows it is unchanged - it
    // is still the Variables view alone.
    verticalLayout_frame_left->removeWidget(checkBox_displayAllVariables);
    verticalLayout_frame_left->insertWidget(1, checkBox_displayAllVariables);

    // The two panes take turns rather than sharing the height, so there is no
    // split for the reader to place and none to remember
    searchSplitter = new QSplitter(Qt::Vertical);

    QFrame* itemContainer = new QFrame();
    mpWidget_itemTreesPane = itemContainer;
    QVBoxLayout* itemLayout = new QVBoxLayout(itemContainer);

    itemLayout->addWidget(treeWidget_triggers);
    itemLayout->addWidget(treeWidget_aliases);
    itemLayout->addWidget(treeWidget_actions);
    itemLayout->addWidget(treeWidget_timers);
    itemLayout->addWidget(treeWidget_scripts);
    itemLayout->addWidget(treeWidget_keys);
    itemLayout->addWidget(treeWidget_variables);

    // No stretch factors and no handle to place: a splitter hands everything it
    // has to its one visible pane, and the other is always hidden
    searchSplitter->addWidget(itemContainer);
    searchSplitter->addWidget(searchContainer);

    verticalLayout_frame_left->addWidget(searchSplitter);
    // The search row keeps its own height and the splitter takes the rest
    verticalLayout_frame_left->setStretchFactor(searchSplitter, 1);

    // Results and trees take turns in the panel: nothing has been searched for
    // yet, so the trees have it
    setSearchResultsShown(false);

    // Escape gives the panel back to the trees. The filter is on the field
    // itself rather than on the combo box, so that an open history popup gets
    // the key first and closes without the search being cleared out from under
    // it.
    comboBox_searchTerms->lineEdit()->installEventFilter(this);
    connect(comboBox_searchTerms->lineEdit(), &QLineEdit::textChanged, this, [this](const QString& text) {
        if (text.isEmpty()) {
            slot_clearSearchResults();
        }
    });

    mpScrollArea = mpTriggersMainArea->scrollArea;
    // The pattern rows are part of the form, not a well sunk into it. A
    // QAbstractScrollArea fills its viewport with QPalette::Base - the colour a
    // field is filled with - which drew the whole edit column as a darker box
    // laid over the page. Nothing here is typed into, so it carries the surface
    // it sits on instead, and loses the frame that outlined it as separate.
    mpScrollArea->setObjectName(qsl("editorPatternScroll"));
    mpScrollArea->setFrameShape(QFrame::NoFrame);
    mpScrollArea->viewport()->setAutoFillBackground(false);
    mpWidget_triggerItems = new QWidget;
    mpWidget_triggerItems->setObjectName(qsl("editorPatternList"));
    auto lay1 = new QVBoxLayout(mpWidget_triggerItems);
    lay1->setContentsMargins(0, 0, 0, 0);
    lay1->setSpacing(0);
    mpScrollArea->setWidget(mpWidget_triggerItems);
    // After setWidget(), which turns the fill back on for whatever it is handed.
    // The widget takes QPalette::Base as its background role off the viewport it
    // is reparented into, so left filled it would carry the field colour. The
    // named rule below holds it off as things stand - a styled background is
    // painted in place of the palette brush, not over it - but that rule is
    // written by applyEditorShellStyle(), which the first paint can beat and a
    // guarded early return can skip. The one line the options column already
    // does this with is at the foot of buildTriggerOptionsPanel().
    mpWidget_triggerItems->setAutoFillBackground(false);

    lay1->addStretch();

    mPatternList << tr("substring") << tr("perl regex") << tr("start of line") << tr("exact match") << tr("lua function") << tr("line spacer") << tr("color trigger") << tr("prompt");

    // No row exists yet, so this only cuts the eight swatches; restyleEditorIcons()
    // is what re-cuts them and hands them round once there are rows
    restylePatternTypeIcons();

    setupAddPatternButton();

    // One empty row, and the Add pattern button under it for the next one
    showPatternItems(1);
    setupPatternNavigationShortcuts();
    updatePatternTabOrder();

    connect(mpHost, &Host::signal_editorThemeChanged, this, &dlgTriggerEditor::slot_editorThemeChanged);
    // fire this now as the theme has already been set and we need the syntax highlighter to pick it up
    mpHost->editorThemeChanged();

    // force the minimum size of the scroll area for the trigger items to be one
    // and a half trigger item widgets:
    const int triggerWidgetItemMinHeight = qRound(mTriggerPatternEdit.at(0)->minimumSizeHint().height() * 1.5);
    mpScrollArea->setMinimumHeight(triggerWidgetItemMinHeight);

    widget_searchTerm->updateGeometry();

    showIDLabels(mpHost->showIdsInEditor());
    if (mAutosaveInterval > 0) {
        startTimer(mAutosaveInterval * 1min);
    }
}

dlgTriggerEditor::~dlgTriggerEditor()
{
    // ~QWidget closes the editor once this destructor is done, and whichever
    // of the item fields has the keyboard focus then emits editingFinished()
    // into one of the slot_saveProperty_...() slots when this object is no
    // longer a valid receiver (#9574)
    utils::disconnectChildSignals(this);
    // The undo stacks are not in this widget's child tree - the edbee one hangs
    // off a parentless CharTextDocument - so disconnect them by hand:
    if (mpTextUndoStack) {
        disconnect(mpTextUndoStack, nullptr, this, nullptr);
    }
    if (mpUndoStack) {
        disconnect(mpUndoStack, nullptr, this, nullptr);
    }
}

void dlgTriggerEditor::slot_clickedMessageBox(const QString& URL)
{
    if (URL.startsWith("http")) {
        QDesktopServices::openUrl(URL);
    } else { // internal links used by expanding info text navigation
        showIntro(URL);
    }
}

void dlgTriggerEditor::slot_editorThemeChanged()
{
    for (auto* patternEdit : std::as_const(mTriggerPatternEdit)) {
        applyPatternWidgetStyle(patternEdit);
    }
}

void dlgTriggerEditor::slot_smartUndo()
{
    // Stack-based undo: prioritize text editor changes, then fall back to item operations
    // This provides intuitive behavior - most recent change undoes first, regardless of focus

    bool canUndoText = mpTextUndoStack && mpTextUndoStack->canUndo();
    bool canUndoItems = mpUndoStack && mpUndoStack->canUndo();

#if defined(DEBUG_UNDO_REDO)
    qDebug() << "dlgTriggerEditor::slot_smartUndo() - canUndoText:" << canUndoText << "canUndoItems:" << canUndoItems;
#endif

    if (canUndoText) {
#if defined(DEBUG_UNDO_REDO)
        qDebug() << "dlgTriggerEditor::slot_smartUndo() - Performing text undo via edbee";
#endif
        mpSourceEditorEdbee->controller()->undo();
    } else if (canUndoItems) {
#if defined(DEBUG_UNDO_REDO)
        qDebug() << "dlgTriggerEditor::slot_smartUndo() - Performing item undo";
#endif
        // Loop to skip commands invalidated by Lua API changes
        const int maxAttempts = 100; // Safety limit to prevent infinite loops
        int attempts = 0;
        while (mpUndoStack->canUndo() && attempts < maxAttempts) {
            mpUndoStack->undo();

            if (mpUndoStack->wasLastCommandValid()) {
#if defined(DEBUG_UNDO_REDO)
                qDebug() << "dlgTriggerEditor::slot_smartUndo() - Valid command undone after" << (attempts + 1) << "attempts";
#endif
                break;
            }

            // Command was invalid (Lua changed the item), silently skip and try next
#if defined(DEBUG_UNDO_REDO)
            qDebug() << "dlgTriggerEditor::slot_smartUndo() - Invalid command, trying next (attempt" << (attempts + 1) << ")";
#endif
            attempts++;
        }
    }

    slot_updateUndoRedoButtonStates();
}

void dlgTriggerEditor::slot_smartRedo()
{
    // Stack-based redo: prioritize text editor changes, then fall back to item operations
    // This provides intuitive behavior - most recently undone change redoes first, regardless of focus

    bool canRedoText = mpTextUndoStack && mpTextUndoStack->canRedo();
    bool canRedoItems = mpUndoStack && mpUndoStack->canRedo();

#if defined(DEBUG_UNDO_REDO)
    qDebug() << "dlgTriggerEditor::slot_smartRedo() - canRedoText:" << canRedoText << "canRedoItems:" << canRedoItems;
#endif

    if (canRedoText) {
#if defined(DEBUG_UNDO_REDO)
        qDebug() << "dlgTriggerEditor::slot_smartRedo() - Performing text redo via edbee";
#endif
        mpSourceEditorEdbee->controller()->redo();
    } else if (canRedoItems) {
#if defined(DEBUG_UNDO_REDO)
        qDebug() << "dlgTriggerEditor::slot_smartRedo() - Performing item redo";
#endif
        // Loop to skip commands invalidated by Lua API changes
        const int maxAttempts = 100; // Safety limit to prevent infinite loops
        int attempts = 0;
        while (mpUndoStack->canRedo() && attempts < maxAttempts) {
            mpUndoStack->redo();

            if (mpUndoStack->wasLastCommandValid()) {
#if defined(DEBUG_UNDO_REDO)
                qDebug() << "dlgTriggerEditor::slot_smartRedo() - Valid command redone after" << (attempts + 1) << "attempts";
#endif
                break;
            }

            // Command was invalid (Lua changed the item), silently skip and try next
#if defined(DEBUG_UNDO_REDO)
            qDebug() << "dlgTriggerEditor::slot_smartRedo() - Invalid command, trying next (attempt" << (attempts + 1) << ")";
#endif
            attempts++;
        }
    }

    slot_updateUndoRedoButtonStates();
}

void dlgTriggerEditor::slot_updateUndoRedoButtonStates()
{
    // Early exit during shutdown - guards against accessing destroyed objects
    if (!mpSourceEditorEdbee || !mpUndoAction || !mpRedoAction || !mpTextUndoStack) {
        return;
    }

    bool canUndoText = mpTextUndoStack->canUndo();
    bool canUndoItems = mpUndoStack && mpUndoStack->canUndo();

    bool canRedoText = mpTextUndoStack->canRedo();
    bool canRedoItems = mpUndoStack && mpUndoStack->canRedo();

    // Disable undo/redo in variables view since variables can be modified via Lua API
    bool inVariablesView = (mCurrentView == EditorViewType::cmVarsView);

    if (inVariablesView) {
        // In variables view, disable buttons and clear tooltips
        mpUndoAction->setEnabled(false);
        mpRedoAction->setEnabled(false);
        mpUndoAction->setToolTip(QString());
        mpUndoAction->setStatusTip(QString());
        mpRedoAction->setToolTip(QString());
        mpRedoAction->setStatusTip(QString());
    } else {
        // In other views, enable/disable based on undo/redo availability
        mpUndoAction->setEnabled(canUndoText || canUndoItems);
        mpRedoAction->setEnabled(canRedoText || canRedoItems);

        // Restore normal tooltips with keyboard shortcuts
        QString undoShortcut = mpUndoAction->shortcut().toString(QKeySequence::NativeText);
        QString redoShortcut = mpRedoAction->shortcut().toString(QKeySequence::NativeText);

        // Get undo/redo text from stack if available
        QString undoText;
        QString redoText;

        if (mpUndoStack) {
            QString stackUndoText = mpUndoStack->undoText();
            if (!stackUndoText.isEmpty()) {
                undoText = tr("Undo: %1 (%2)").arg(stackUndoText, undoShortcut);
            }

            QString stackRedoText = mpUndoStack->redoText();
            if (!stackRedoText.isEmpty()) {
                redoText = tr("Redo: %1 (%2)").arg(stackRedoText, redoShortcut);
            }
        }

        // Fall back to generic tooltips if no specific action text
        if (undoText.isEmpty()) {
            undoText = tr("Undo (%1)").arg(undoShortcut);
        }
        if (redoText.isEmpty()) {
            redoText = tr("Redo (%1)").arg(redoShortcut);
        }

        mpUndoAction->setToolTip(utils::richText(undoText));
        mpUndoAction->setStatusTip(undoText);
        mpRedoAction->setToolTip(utils::richText(redoText));
        mpRedoAction->setStatusTip(redoText);
    }
}

void dlgTriggerEditor::applyPatternWidgetStyle(dlgTriggerPatternEdit* patternWidget)
{
    if (!patternWidget || !mpHost) {
        return;
    }

    QPalette referencePalette;

    if (mpTriggersMainArea && mpTriggersMainArea->lineEdit_trigger_name) {
        referencePalette = mpTriggersMainArea->lineEdit_trigger_name->palette();
    } else {
        referencePalette = patternWidget->singleLineTextEdit_pattern->palette();
    }

    patternWidget->singleLineTextEdit_pattern->setTheme(mpHost->getEditorTheme());
    patternWidget->applyThemePalette(referencePalette);
    // After the row's palette pass, which hands every control the one the form
    // is drawn with: a pattern is typed into a field like the name above it, so
    // it is filled and written in the window's own colours whatever the syntax
    // theme says. The theme is left to say what a group or a quantifier is
    // coloured, and only once those colours read on this field.
    const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
    patternWidget->singleLineTextEdit_pattern->setFieldColors(tokens.field, tokens.text);

    // A pattern is game text rather than interface text, so it is read in the
    // font the game and the script beside it are read in: a space is then as
    // wide as a letter, and what would match is what is on the row
    const QFont patternFont = mpHost->getDisplayFont();
    patternWidget->singleLineTextEdit_pattern->setFont(patternFont);
    patternWidget->label_patternNumber->setFont(patternFont);

    const QFontMetrics patternMetrics(patternFont);
    patternWidget->label_patternNumber->setFixedWidth(patternMetrics.horizontalAdvance(QString(scmEditorPatternNumberDigits, QLatin1Char('0'))));
    // The .ui file's heights are for a row of interface text; what the row holds
    // now is a line of the display font, which is whatever the profile set it to
    const int rowHeight = std::max(scmEditorPatternRowMinimumHeight, patternMetrics.height() + scmEditorPatternRowPadding);
    patternWidget->setFixedHeight(rowHeight);
    const QMargins rowMargins = patternWidget->layout()->contentsMargins();
    const int contentHeight = rowHeight - rowMargins.top() - rowMargins.bottom();
    for (QWidget* pControl : {static_cast<QWidget*>(patternWidget->singleLineTextEdit_pattern),
                              static_cast<QWidget*>(patternWidget->comboBox_patternType),
                              static_cast<QWidget*>(patternWidget->spinBox_lineSpacer),
                              static_cast<QWidget*>(patternWidget->pushButton_fgColor),
                              static_cast<QWidget*>(patternWidget->pushButton_bgColor),
                              static_cast<QWidget*>(patternWidget->label_prompt)}) {
        pControl->setMaximumHeight(contentHeight);
    }

    patternWidget->comboBox_patternType->setFixedWidth(patternTypeColumnWidth(patternWidget->comboBox_patternType->font()));

    // Mixed once and handed to every row: fifty rows are fifty tints and fifty
    // tinted pictures otherwise
    if (mPatternDeleteIcon.isNull()) {
        mPatternDeleteIcon = patternDeleteIcon();
    }
    if (!mPatternHoverTint.isValid()) {
        mPatternHoverTint = patternHoverTint();
    }
    patternWidget->setDeleteGlyph(mPatternDeleteIcon);
    patternWidget->setHoverTint(mPatternHoverTint);
}

// Wide enough for the longest type name in whatever language the editor is in,
// so that the pattern beside it starts at the same place on every row. The names
// and the font they are drawn in are the same on every row, so the answer is
// taken once and thrown away by whatever moves either of them.
int dlgTriggerEditor::patternTypeColumnWidth(const QFont& typeFont) const
{
    if (mPatternTypeColumnWidth > 0) {
        return mPatternTypeColumnWidth;
    }
    const QFontMetrics typeMetrics(typeFont);
    int typeWidth = 0;
    for (const QString& typeName : std::as_const(mPatternList)) {
        typeWidth = std::max(typeWidth, typeMetrics.horizontalAdvance(typeName));
    }
    mPatternTypeColumnWidth = typeWidth + scmEditorPatternTypeChrome;
    return mPatternTypeColumnWidth;
}

// Tinted from the palette the way the toolbar's pictures are. The row only
// draws it while the mouse is over the row, so the button is chrome the column
// of patterns is not made to carry fifty times over.
QIcon dlgTriggerEditor::patternDeleteIcon() const
{
    const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
    const QPixmap deleteSource(qsl(":/icons/editor-delete.png"));
    QIcon deleteGlyph(uiDesign::tintedGlyph(deleteSource, tokens.mutedText));
    deleteGlyph.addPixmap(uiDesign::tintedGlyph(deleteSource, tokens.text), QIcon::Active);
    return deleteGlyph;
}

// The eight swatches, and the grip every row draws, cut once for the theme in
// force and handed to every row - a trigger can hold fifty of them, and mixing
// either per row is fifty times the same answer
void dlgTriggerEditor::restylePatternTypeIcons()
{
    const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
    const qreal ratio = devicePixelRatioF();
    // Enough of a hairline to hold a pale swatch against a light page without
    // reading as a second colour
    const QColor outline = uiDesign::blend(tokens.page, tokens.text, 0.35);
    const qreal lightness = tokens.darkPage ? scmEditorPatternSwatchLightnessOnDark : scmEditorPatternSwatchLightnessOnLight;

    mPatternIcons.clear();
    mPatternIcons.reserve(static_cast<int>(std::size(scmEditorPatternSwatches)));
    for (const auto& recipe : scmEditorPatternSwatches) {
        const QColor fill = (recipe.hue < 0.0) ? uiDesign::blend(tokens.page, tokens.text, recipe.neutralWeight) : QColor::fromHslF(recipe.hue, scmEditorPatternSwatchSaturation, lightness);
        mPatternIcons.append(editorPatternSwatchIcon(fill, outline, ratio));
    }

    mPatternGripGlyph = editorPatternGripGlyph(tokens.mutedText, ratio);

    for (auto* patternEdit : std::as_const(mTriggerPatternEdit)) {
        if (!patternEdit) {
            continue;
        }
        applyPatternTypeIcons(patternEdit->comboBox_patternType);
        applyPatternGripGlyph(patternEdit->label_dragHandle);
    }
}

// At the size they were cut at rather than blown up to whatever a platform
// style's default icon size happens to be
void dlgTriggerEditor::applyPatternTypeIcons(QComboBox* pBox) const
{
    if (!pBox) {
        return;
    }
    pBox->setIconSize(QSize(scmEditorPatternSwatchSize, scmEditorPatternSwatchSize));
    for (int i = 0; i < mPatternIcons.size() && i < pBox->count(); ++i) {
        pBox->setItemIcon(i, mPatternIcons.at(i));
    }
}

void dlgTriggerEditor::applyPatternGripGlyph(QLabel* pHandle) const
{
    if (!pHandle) {
        return;
    }
    // The dots are the whole of what the label shows: the .ui file's text is
    // cleared rather than drawn under them
    pHandle->setText(QString());
    pHandle->setPixmap(mPatternGripGlyph);
    pHandle->setFixedWidth(scmEditorPatternGripWidth);
}

void dlgTriggerEditor::createPatternItem(int index)
{
    auto* pItem = new dlgTriggerPatternEdit(mpWidget_triggerItems);
    QComboBox* pBox = pItem->comboBox_patternType;
    pBox->addItems(mPatternList);
    pBox->setItemData(0, QVariant(index));
    applyPatternTypeIcons(pBox);
    applyPatternGripGlyph(pItem->label_dragHandle);
    connect(pBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &dlgTriggerEditor::slot_setupPatternControls);
    connect(pItem->pushButton_fgColor, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_colorTriggerFg);
    connect(pItem->pushButton_bgColor, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_colorTriggerBg);
    connect(pItem->singleLineTextEdit_pattern, &QPlainTextEdit::textChanged, this, &dlgTriggerEditor::slot_changedPattern);
    connect(pItem->singleLineTextEdit_pattern, &QPlainTextEdit::textChanged, this, &dlgTriggerEditor::slot_itemEdited);
    connect(pItem->spinBox_lineSpacer, qOverload<int>(&QSpinBox::valueChanged), this, &dlgTriggerEditor::slot_lineSpacerChanged);
    connect(pItem->spinBox_lineSpacer, qOverload<int>(&QSpinBox::valueChanged), this, &dlgTriggerEditor::slot_itemEdited);

    connect(pItem->toolButton_deletePattern, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_deletePatternRow);

    auto* pLayout = static_cast<QVBoxLayout*>(mpWidget_triggerItems->layout());
    // Under the rows already there and over the button that adds one more, which
    // is what keeps that button beneath the last row on show
    const int insertIndex = mpButton_addPattern ? pLayout->indexOf(mpButton_addPattern) : pLayout->count() - 1;
    pLayout->insertWidget(insertIndex, pItem);

    mTriggerPatternEdit.push_back(pItem);
    pItem->mRow = index;
    pItem->pushButton_fgColor->hide();
    pItem->pushButton_bgColor->hide();
    pItem->label_prompt->hide();
    pItem->spinBox_lineSpacer->hide();
    pItem->label_patternNumber->setText(QString::number(index + 1));
    pItem->label_patternNumber->show();

    //: Tooltip on the grip at the left of one of a trigger's pattern rows. %1 and %2 are the keyboard shortcuts that do the same thing without the mouse.
    pItem->label_dragHandle->setToolTip(
            utils::richText(tr("Drag to reorder this pattern, or move it with %1 and %2")
                                    .arg(QKeySequence(scmEditorPatternMoveUpKeys).toString(QKeySequence::NativeText), QKeySequence(scmEditorPatternMoveDownKeys).toString(QKeySequence::NativeText))));
    //: Name of a pattern row's grip as a screen reader speaks it. %1 is the number of the row it belongs to.
    pItem->label_dragHandle->setAccessibleName(tr("Reorder pattern %1").arg(index + 1));
    //: Tooltip on the button that takes one of a trigger's patterns away
    pItem->toolButton_deletePattern->setToolTip(utils::richText(tr("Delete this pattern")));
    //: Name of a pattern row's delete button as a screen reader speaks it. %1 is the number of the row it belongs to.
    pItem->toolButton_deletePattern->setAccessibleName(tr("Delete pattern %1").arg(index + 1));
    // The glyph is only drawn under the mouse, so the keyboard needs a way in of
    // its own - and the row shows the glyph while the button holds focus
    pItem->toolButton_deletePattern->setFocusPolicy(Qt::TabFocus);
    pItem->toolButton_deletePattern->setFixedSize(scmEditorPatternDeleteButtonSize, scmEditorPatternDeleteButtonSize);
    pItem->toolButton_deletePattern->setIconSize(QSize(scmEditorPatternDeleteGlyphSize, scmEditorPatternDeleteGlyphSize));

    lineEditShouldMarkSpaces[pItem->singleLineTextEdit_pattern] = false;

    pItem->singleLineTextEdit_pattern->installEventFilter(this);
    // The grip has no press of its own: what a drag from it means is a matter of
    // where the other rows are, which only the editor knows
    pItem->label_dragHandle->installEventFilter(this);
    applyPatternWidgetStyle(pItem);
    pItem->spinBox_lineSpacer->installEventFilter(this);
}

void dlgTriggerEditor::showPatternItems(int count)
{
    count = qBound(0, count, scmEditorPatternRowLimit);
    while (mTriggerPatternEdit.size() < count) {
        createPatternItem(mTriggerPatternEdit.size());
    }

    for (int i = 0; i < mTriggerPatternEdit.size(); ++i) {
        auto* pItem = mTriggerPatternEdit[i];
        if (!pItem) {
            continue;
        }

        if (i < count) {
            pItem->show();
        } else {
            auto* edit = pItem->singleLineTextEdit_pattern;
            edit->blockSignals(true);
            edit->clear();
            edit->blockSignals(false);
            lineEditShouldMarkSpaces[edit] = false;

            auto* combo = pItem->comboBox_patternType;
            combo->blockSignals(true);
            combo->setCurrentIndex(REGEX_SUBSTRING);
            combo->blockSignals(false);

            pItem->pushButton_fgColor->hide();
            pItem->pushButton_bgColor->hide();
            pItem->label_prompt->hide();
            pItem->spinBox_lineSpacer->hide();
            pItem->hide();
        }
    }

    mVisiblePatternCount = count;
    updatePatternPlaceholders();
    updateAddPatternButton();
    updatePatternTabOrder();
}

void dlgTriggerEditor::updatePatternPlaceholders()
{
    for (int i = 0; i < mVisiblePatternCount; ++i) {
        auto* patternItem = mTriggerPatternEdit.value(i, nullptr);
        if (!patternItem) {
            continue;
        }

        auto* edit = patternItem->singleLineTextEdit_pattern;
        if (!edit) {
            continue;
        }

        if (!edit->isVisible() || !edit->toPlainText().isEmpty()) {
            edit->setPlaceholderText(QString());
            continue;
        }

        const QString placeholder = patternPlaceholderText(patternItem->comboBox_patternType->currentIndex());
        edit->setPlaceholderText(placeholder);
    }
}

QString dlgTriggerEditor::patternPlaceholderText(const int patternType) const
{
    switch (patternType) {
    case REGEX_SUBSTRING:
        return tr("Text to find (anywhere in the game output)");
    case REGEX_PERL:
        return tr("Text to find (as a regular expression pattern)");
    case REGEX_BEGIN_OF_LINE_SUBSTRING:
        return tr("Text to find (from beginning of the line)");
    case REGEX_EXACT_MATCH:
        return tr("Exact line to match");
    case REGEX_LUA_CODE:
        return tr("Lua code to run (return true to match)");
    default:
        return QString();
    }
}

void dlgTriggerEditor::setupPatternNavigationShortcuts()
{
    if (mFirstPatternShortcut) {
        mFirstPatternShortcut->deleteLater();
        mFirstPatternShortcut = nullptr;
    }

    if (mLastPatternShortcut) {
        mLastPatternShortcut->deleteLater();
        mLastPatternShortcut = nullptr;
    }

    for (auto* shortcut : std::as_const(mPatternNavigationShortcuts)) {
        if (shortcut) {
            shortcut->deleteLater();
        }
    }
    mPatternNavigationShortcuts.clear();

    if (!mpTriggersMainArea) {
        return;
    }

    mFirstPatternShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Up), mpTriggersMainArea);
    mFirstPatternShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(mFirstPatternShortcut, &QShortcut::activated, this, [this]() {
        if (mVisiblePatternCount < 1) {
            return;
        }
        focusPatternItem(0, Qt::ShortcutFocusReason);
    });


    mLastPatternShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Down), mpTriggersMainArea);
    mLastPatternShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(mLastPatternShortcut, &QShortcut::activated, this, [this]() {
        if (mVisiblePatternCount < 1) {
            return;
        }
        focusPatternItem(mVisiblePatternCount - 1, Qt::ShortcutFocusReason);
    });

    // Dragging a row's grip is the only way to reorder patterns with a mouse, so
    // these two are the only way to reorder them without one. Kept in the list
    // changeView() switches on and off with the trigger view.
    auto addReorderShortcut = [this](const QKeySequence& keys, const int offset) {
        auto* pShortcut = new QShortcut(keys, mpTriggersMainArea);
        pShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(pShortcut, &QShortcut::activated, this, [this, offset]() {
            movePatternRowByKeyboard(offset);
        });
        mPatternNavigationShortcuts.append(pShortcut);
    };
    addReorderShortcut(QKeySequence(scmEditorPatternMoveUpKeys), -1);
    addReorderShortcut(QKeySequence(scmEditorPatternMoveDownKeys), 1);

    const bool enableShortcuts = mCurrentView == EditorViewType::cmTriggerView;
    if (mFirstPatternShortcut) {
        mFirstPatternShortcut->setEnabled(enableShortcuts);
    }

    if (mLastPatternShortcut) {
        mLastPatternShortcut->setEnabled(enableShortcuts);
    }

    for (auto* pShortcut : std::as_const(mPatternNavigationShortcuts)) {
        pShortcut->setEnabled(enableShortcuts);
    }
}

// Which of the pattern rows the keyboard is in. The focus can be on any of a
// row's controls, or on the row itself, so the answer is whichever row is an
// ancestor of what holds it.
int dlgTriggerEditor::focusedPatternRow() const
{
    for (const QWidget* pFocused = QApplication::focusWidget(); pFocused; pFocused = pFocused->parentWidget()) {
        if (const auto* pRow = qobject_cast<const dlgTriggerPatternEdit*>(pFocused)) {
            return pRow->mRow;
        }
    }
    return -1;
}

void dlgTriggerEditor::movePatternRowByKeyboard(const int offset)
{
    const int from = focusedPatternRow();
    if (from < 0) {
        return;
    }

    const int to = from + offset;
    if (to < 0 || to > lastVisiblePatternRow()) {
        return;
    }

    movePatternRowContent(from, to);
    // The rows are a pool that is never reordered, so the focus stayed where it
    // was while what it was on moved - it follows the contents rather than the
    // widget, which is what makes a second press carry the same pattern further
    focusPatternItem(to, Qt::ShortcutFocusReason);
}

// The row after the last one is always empty and typing in it adds another, so
// this is the same thing said out loud - and the one way to reach a row from the
// keyboard without having typed in the one before it
void dlgTriggerEditor::setupAddPatternButton()
{
    mpButton_addPattern = new uiDesign::PlaceholderButton(mpWidget_triggerItems);
    mpButton_addPattern->setObjectName(qsl("editorAddPattern"));
    mpButton_addPattern->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    //: Button under a trigger's patterns that gives it one more
    mpButton_addPattern->setText(tr("Add pattern"));
    mpButton_addPattern->setCursor(Qt::PointingHandCursor);
    // The button paints the dashed frame it is read as a placeholder by, and
    // auto-raise would have the style draw a second one over it under the mouse
    mpButton_addPattern->setAutoRaise(false);
    mpButton_addPattern->setFrameMargins(QMargins(0, scmEditorAddPatternMarginTop, 0, scmEditorAddPatternMarginBottom));
    mpButton_addPattern->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    connect(mpButton_addPattern, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_addPattern);

    auto* pLayout = static_cast<QVBoxLayout*>(mpWidget_triggerItems->layout());
    // Over the stretch that holds the rows up against the top of the area, and
    // under every row: createPatternItem() puts each new one above this button
    pLayout->insertWidget(pLayout->count() - 1, mpButton_addPattern, 0, Qt::AlignLeft);

    // Out of the layout on purpose: it is put over whichever row a dragged one
    // would land on, which is a place no layout has a slot for
    mpFrame_patternDropIndicator = new QFrame(mpWidget_triggerItems);
    mpFrame_patternDropIndicator->setObjectName(qsl("editorPatternDropIndicator"));
    mpFrame_patternDropIndicator->setFixedHeight(scmEditorPatternDropIndicatorHeight);
    mpFrame_patternDropIndicator->setAttribute(Qt::WA_TransparentForMouseEvents);
    mpFrame_patternDropIndicator->hide();

    // The button comes into being after the shell has been styled, so it asks
    // for its own picture rather than waiting for the next appearance change
    restyleAddPatternIcon();
    updateAddPatternButton();
}

void dlgTriggerEditor::restyleAddPatternIcon()
{
    if (!mpButton_addPattern) {
        return;
    }

    const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
    const QPixmap addSource(qsl(":/icons/editor-add.png"));
    QIcon addGlyph(uiDesign::tintedGlyph(addSource, tokens.mutedText));
    addGlyph.addPixmap(uiDesign::tintedGlyph(addSource, tokens.accentText), QIcon::Active);
    mpButton_addPattern->setIcon(addGlyph);
    const int addGlyphSize = qRound(mpButton_addPattern->fontMetrics().height() * 0.9);
    mpButton_addPattern->setIconSize(QSize(addGlyphSize, addGlyphSize));

    // The dashed frame is the only thing saying that one more pattern goes
    // there, so it is held to what a control needs rather than to the hairline a
    // card border gets away with; the card border it used to borrow is 1.3:1.
    const QColor restingFrame = uiDesign::blend(tokens.card, tokens.text, 0.45);
    // ...and once the trigger holds as many patterns as it can, the frame drops
    // back to the hairline a card is edged with: there is nothing to click
    const QColor disabledFrame = uiDesign::blend(tokens.card, tokens.text, 0.22);
    mpButton_addPattern->setFrameColors(restingFrame, tokens.accent, disabledFrame);
}

void dlgTriggerEditor::updateAddPatternButton()
{
    if (!mpButton_addPattern) {
        return;
    }

    const bool roomLeft = mVisiblePatternCount < scmEditorPatternRowLimit;
    mpButton_addPattern->setEnabled(roomLeft);
    if (roomLeft) {
        //: Tooltip on the button that gives a trigger one more pattern
        mpButton_addPattern->setToolTip(utils::richText(tr("Add another pattern to this trigger")));
    } else {
        //: Tooltip on that button once the trigger holds as many patterns as it can. %n is that number.
        mpButton_addPattern->setToolTip(utils::richText(tr("A trigger can hold %n pattern(s) at most", nullptr, scmEditorPatternRowLimit)));
    }
}

void dlgTriggerEditor::slot_addPattern()
{
    if (mVisiblePatternCount >= scmEditorPatternRowLimit) {
        return;
    }

    showPatternItems(mVisiblePatternCount + 1);
    focusPatternItem(mVisiblePatternCount - 1);
}

void dlgTriggerEditor::slot_deletePatternRow()
{
    auto* pButton = qobject_cast<QToolButton*>(sender());
    if (!pButton) {
        return;
    }

    auto* patternItem = qobject_cast<dlgTriggerPatternEdit*>(pButton->parentWidget());
    if (!patternItem) {
        return;
    }

    deletePatternRow(patternItem->mRow);
}

dlgTriggerEditor::PatternRowContent dlgTriggerEditor::patternRowContent(const dlgTriggerPatternEdit* patternItem) const
{
    PatternRowContent content;
    if (!patternItem) {
        return content;
    }

    content.type = patternItem->comboBox_patternType->currentIndex();
    content.pattern = patternItem->singleLineTextEdit_pattern->toPlainText();
    content.lineSpacerValue = patternItem->spinBox_lineSpacer->value();
    content.foregroundText = patternItem->pushButton_fgColor->text();
    content.foregroundStyleSheet = patternItem->pushButton_fgColor->styleSheet();
    content.backgroundText = patternItem->pushButton_bgColor->text();
    content.backgroundStyleSheet = patternItem->pushButton_bgColor->styleSheet();
    const auto marked = lineEditShouldMarkSpaces.find(patternItem->singleLineTextEdit_pattern);
    content.markSpaces = marked != lineEditShouldMarkSpaces.cend() && marked->second;
    return content;
}

void dlgTriggerEditor::setPatternRowContent(dlgTriggerPatternEdit* patternItem, const PatternRowContent& content)
{
    if (!patternItem) {
        return;
    }

    // Held quiet for the whole of this, setupPatternControls() included: a row
    // told what it now holds is one row of a shift that is still under way, and
    // the count of what is filled in is only worth redoing once it has finished
    const bool patternWasBlocked = patternItem->singleLineTextEdit_pattern->blockSignals(true);
    {
        const QSignalBlocker typeBlocker(patternItem->comboBox_patternType);
        const QSignalBlocker spacerBlocker(patternItem->spinBox_lineSpacer);
        // The text goes in before the type does: a colour pattern's text is
        // written by the type change when the row looks empty to it
        patternItem->singleLineTextEdit_pattern->setPlainText(content.pattern);
        patternItem->comboBox_patternType->setCurrentIndex(content.type);
        patternItem->spinBox_lineSpacer->setValue(content.lineSpacerValue);
    }
    // What the two colour buttons say and are filled with is read off the
    // pattern text when a trigger is loaded, so it travels with the row rather
    // than being worked out again here
    patternItem->pushButton_fgColor->setStyleSheet(content.foregroundStyleSheet);
    patternItem->pushButton_fgColor->setText(content.foregroundText);
    patternItem->pushButton_bgColor->setStyleSheet(content.backgroundStyleSheet);
    patternItem->pushButton_bgColor->setText(content.backgroundText);

    setupPatternControls(content.type, patternItem);
    lineEditShouldMarkSpaces[patternItem->singleLineTextEdit_pattern] = content.markSpaces;
    patternItem->singleLineTextEdit_pattern->blockSignals(patternWasBlocked);
}

// The last row a move or a delete may write to. mTriggerPatternEdit is a pool of
// up to fifty rows of which only the first mVisiblePatternCount are on show, so
// bounding against the pool would let content land in a row nobody can see.
int dlgTriggerEditor::lastVisiblePatternRow() const
{
    return std::min(mVisiblePatternCount, static_cast<int>(mTriggerPatternEdit.size())) - 1;
}

void dlgTriggerEditor::movePatternRowContent(const int from, const int to)
{
    const int lastRow = lastVisiblePatternRow();
    if (from == to || from < 0 || to < 0 || from > lastRow || to > lastRow) {
        return;
    }

    const PatternRowContent moved = patternRowContent(mTriggerPatternEdit.at(from));
    // Each row the shift passes over is one step of one edit, so the chrome that
    // depends on all of them is left until the shift has finished
    mPatternBulkEdit = true;
    if (from < to) {
        for (int i = from; i < to; ++i) {
            setPatternRowContent(mTriggerPatternEdit.at(i), patternRowContent(mTriggerPatternEdit.at(i + 1)));
        }
    } else {
        for (int i = from; i > to; --i) {
            setPatternRowContent(mTriggerPatternEdit.at(i), patternRowContent(mTriggerPatternEdit.at(i - 1)));
        }
    }
    setPatternRowContent(mTriggerPatternEdit.at(to), moved);
    mPatternBulkEdit = false;

    slot_itemEdited();
    compactPatternRows();
    updatePatternTabOrder();
}

// The row widgets are a pool that is never reordered, so a row is taken away by
// pulling what every row below it holds up one place and emptying the last
void dlgTriggerEditor::deletePatternRow(const int row)
{
    const int lastRow = lastVisiblePatternRow();
    if (row < 0 || row > lastRow) {
        return;
    }

    mPatternBulkEdit = true;
    for (int i = row; i < lastRow; ++i) {
        setPatternRowContent(mTriggerPatternEdit.at(i), patternRowContent(mTriggerPatternEdit.at(i + 1)));
    }
    setPatternRowContent(mTriggerPatternEdit.at(lastRow), PatternRowContent());
    mPatternBulkEdit = false;

    slot_itemEdited();
    // Which is what collapses the rows the delete left empty at the bottom
    compactPatternRows();
    updatePatternTabOrder();
}

int dlgTriggerEditor::patternRowAt(const QPoint& itemsPos) const
{
    int lastVisible = -1;
    for (int i = 0; i < mVisiblePatternCount && i < mTriggerPatternEdit.size(); ++i) {
        const auto* patternItem = mTriggerPatternEdit.at(i);
        if (!patternItem || patternItem->isHidden()) {
            continue;
        }

        lastVisible = i;
        if (itemsPos.y() <= patternItem->geometry().bottom()) {
            return i;
        }
    }

    return lastVisible;
}

void dlgTriggerEditor::showPatternDropIndicator(const int targetRow)
{
    if (!mpFrame_patternDropIndicator) {
        return;
    }

    auto* patternItem = mTriggerPatternEdit.value(targetRow, nullptr);
    if (!patternItem || targetRow == mPatternDragSourceRow) {
        mpFrame_patternDropIndicator->hide();
        return;
    }

    // The edge the dragged row would come to rest against: over the target when
    // it is being carried up the list, under it when it is going down
    const QRect rowRect = patternItem->geometry();
    const int indicatorTop = targetRow < mPatternDragSourceRow ? rowRect.top() : rowRect.bottom() - scmEditorPatternDropIndicatorHeight;
    mpFrame_patternDropIndicator->setGeometry(rowRect.left(), indicatorTop, rowRect.width(), scmEditorPatternDropIndicatorHeight);
    mpFrame_patternDropIndicator->raise();
    mpFrame_patternDropIndicator->show();
}

void dlgTriggerEditor::endPatternRowDrag(const bool dropped)
{
    if (mpFrame_patternDropIndicator) {
        mpFrame_patternDropIndicator->hide();
    }

    const int fromRow = mPatternDragSourceRow;
    const int toRow = mPatternDragTargetRow;
    mPatternDragSourceRow = -1;
    mPatternDragTargetRow = -1;
    mPatternDragActive = false;

    if (dropped) {
        movePatternRowContent(fromRow, toRow);
    }
}

// A press on a row's grip is followed until it is let go: the rows are a pool
// that is never reordered, so there is no widget for QDrag to carry - what moves
// between two places is what the rows hold.
bool dlgTriggerEditor::handlePatternHandleEvent(QObject* watched, QEvent* event)
{
    auto* pHandle = qobject_cast<QLabel*>(watched);
    if (!pHandle || pHandle->objectName() != qsl("label_dragHandle")) {
        return false;
    }

    auto* patternItem = qobject_cast<dlgTriggerPatternEdit*>(pHandle->parentWidget());
    if (!patternItem || !mpWidget_triggerItems) {
        return false;
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* pMouseEvent = static_cast<QMouseEvent*>(event);
        if (pMouseEvent->button() != Qt::LeftButton) {
            return false;
        }
        mPatternDragSourceRow = patternItem->mRow;
        mPatternDragTargetRow = patternItem->mRow;
        mPatternDragPressPos = pMouseEvent->globalPosition().toPoint();
        mPatternDragActive = false;
        return true;
    }
    case QEvent::MouseMove: {
        if (mPatternDragSourceRow < 0) {
            return false;
        }
        auto* pMouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint globalPos = pMouseEvent->globalPosition().toPoint();
        if (!mPatternDragActive) {
            if ((globalPos - mPatternDragPressPos).manhattanLength() < scmEditorPatternDragThreshold) {
                return true;
            }
            mPatternDragActive = true;
            pHandle->setCursor(Qt::ClosedHandCursor);
        }
        mPatternDragTargetRow = patternRowAt(mpWidget_triggerItems->mapFromGlobal(globalPos));
        showPatternDropIndicator(mPatternDragTargetRow);
        return true;
    }
    case QEvent::MouseButtonRelease: {
        if (mPatternDragSourceRow < 0) {
            return false;
        }
        pHandle->setCursor(Qt::OpenHandCursor);
        endPatternRowDrag(mPatternDragActive);
        return true;
    }
    default:
        break;
    }

    return false;
}

void dlgTriggerEditor::slot_hideVariable(bool status)
{
    LuaInterface* lI = mpHost->getLuaInterface();
    VarUnit* vu = lI->getVarUnit();
    TVar* var = vu->getWVar(mpCurrentVarItem);
    if (var) {
        if (status) {
            vu->addHidden(var, 1);
        } else {
            vu->removeHidden(var);
        }
    }
}

void dlgTriggerEditor::slot_toggleHiddenVariables(bool state)
{
    if (showHiddenVars != state) {
        showHiddenVars = state;
        repopulateVars();
    }
}

void dlgTriggerEditor::slot_viewStatsAction()
{
    mpHost->mpConsole->showStatistics();
    mudlet::self()->raise();
    mudlet::self()->activateWindow();
    mudlet::self()->raise();
}

void dlgTriggerEditor::slot_viewErrorsAction()
{
    mpErrorConsole->setVisible(!mpErrorConsole->isVisible());
}


void dlgTriggerEditor::slot_setToolBarIconSize(const int s)
{
    if (s <= 0) {
        return;
    }

    // The actions toolbar keeps its own arrangement whatever the icon size is:
    // a row of names beside pictures is what its grouping is read from
    applyEditorToolbarButtonStyles();

    toolBar->setIconSize(QSize(s * 8, s * 8));
}

void dlgTriggerEditor::slot_setTreeWidgetIconSize(const int s)
{
    if (s <= 0) {
        return;
    }

    const QSize newSize(s * 8, s * 8);
    treeWidget_triggers->setIconSize(newSize);
    treeWidget_aliases->setIconSize(newSize);
    treeWidget_timers->setIconSize(newSize);
    treeWidget_scripts->setIconSize(newSize);
    treeWidget_keys->setIconSize(newSize);
    treeWidget_actions->setIconSize(newSize);
    treeWidget_variables->setIconSize(newSize);
}

void dlgTriggerEditor::closeEvent(QCloseEvent* event)
{
    emit editorClosing();
    writeSettings();
    event->accept();
}

// The strip along the top of a window that can be grabbed with the mouse: as
// long as enough of it lands on a screen that is attached now, the window can
// be reached and moved, whatever the rest of it hangs over
static bool windowPlacementReachable(const QPoint& topLeft, const QSize& windowSize)
{
    constexpr int titleBarHeight = 30;
    constexpr int minimumGrabWidth = 120;

    const QRect grabStrip(topLeft, QSize(windowSize.width(), titleBarHeight));
    for (const QScreen* pScreen : QGuiApplication::screens()) {
        const QRect reachable = pScreen->availableGeometry().intersected(grabStrip);
        if (reachable.width() >= qMin(minimumGrabWidth, windowSize.width()) && reachable.height() >= titleBarHeight / 2) {
            return true;
        }
    }
    return false;
}

QSize dlgTriggerEditor::defaultEditorSize(const QRect& availableArea) const
{
    // With nothing stored, the editor opens as a companion to the profile
    // window rather than at a size fixed long before the screens it runs on
    // now: the profile window, a step inside it so both are visible at once
    QSize base;
    if (mpHost && mpHost->mpConsole && mpHost->mpConsole->window()) {
        base = mpHost->mpConsole->window()->size();
    }
    if (base.isEmpty()) {
        base = availableArea.size();
    }
    return QSize(qRound(base.width() * 0.9), qRound(base.height() * 0.9));
}

void dlgTriggerEditor::repositionOnProfileScreen()
{
    mRepositioningEditorWindow = true;
    if (mpHost && mpHost->mpConsole) {
        utils::positionDialogOnActiveProfileScreen(this, nullptr, mpHost->mpConsole);
    } else {
        utils::centerDialogOnScreen(this, QGuiApplication::primaryScreen());
    }
    mRepositioningEditorWindow = false;
}

bool dlgTriggerEditor::onSameScreenAsProfile() const
{
    if (!mpHost || !mpHost->mpConsole) {
        return true;
    }

    const QScreen* pProfileScreen = QApplication::screenAt(mpHost->mpConsole->mapToGlobal(mpHost->mpConsole->rect().center()));
    if (!pProfileScreen) {
        pProfileScreen = mpHost->mpConsole->screen();
    }
    const QScreen* pEditorScreen = QApplication::screenAt(mapToGlobal(rect().center()));
    if (!pProfileScreen || !pEditorScreen) {
        return true;
    }
    return pProfileScreen == pEditorScreen;
}

void dlgTriggerEditor::restoreWindowGeometry()
{
    QSettings& settings = *mudlet::getQSettings();

    const QScreen* pScreen = (mpHost && mpHost->mpConsole) ? mpHost->mpConsole->screen() : nullptr;
    if (!pScreen) {
        pScreen = QGuiApplication::primaryScreen();
    }
    const QRect availableArea = pScreen ? pScreen->availableGeometry() : QRect(0, 0, 1024, 768);

    const QSize storedSize = settings.value(qsl("script_editor_size")).toSize();
    QSize targetSize = (storedSize.isValid() && !storedSize.isEmpty()) ? storedSize : defaultEditorSize(availableArea);
    // A size stored before this layout existed, or on a screen that is no
    // longer attached, is bigger than the desktop it is about to open on -
    // bring it back inside that desktop rather than open off the bottom of it
    targetSize = targetSize.boundedTo(availableArea.size()).expandedTo(minimumSizeHint().boundedTo(availableArea.size()));
    resize(targetSize);

    if (settings.contains(qsl("script_editor_pos"))) {
        const QPoint storedPos = settings.value(qsl("script_editor_pos")).toPoint();
        if (windowPlacementReachable(storedPos, targetSize)) {
            mRepositioningEditorWindow = true;
            move(storedPos);
            mRepositioningEditorWindow = false;
            mEditorPlacementChosen = true;
            return;
        }
    }

    // Nothing stored, or what was stored points at a screen that has gone:
    // open with the profile the editor belongs to instead
    repositionOnProfileScreen();
}

void dlgTriggerEditor::readSettings()
{
    QSettings& settings = *mudlet::getQSettings();

    restoreWindowGeometry();

    mAutosaveInterval = settings.value("autosaveIntervalMinutes", 2).toInt();

    mTriggerEditorSplitterState = settings.value("mTriggerEditorSplitterState", QByteArray()).toByteArray();
    mAliasEditorSplitterState = settings.value("mAliasEditorSplitterState", QByteArray()).toByteArray();
    mScriptEditorSplitterState = settings.value("mScriptEditorSplitterState", QByteArray()).toByteArray();
    mActionEditorSplitterState = settings.value("mActionEditorSplitterState", QByteArray()).toByteArray();
    mKeyEditorSplitterState = settings.value("mKeyEditorSplitterState", QByteArray()).toByteArray();
    mTimerEditorSplitterState = settings.value("mTimerEditorSplitterState", QByteArray()).toByteArray();
    mVarEditorSplitterState = settings.value("mVarEditorSplitterState", QByteArray()).toByteArray();

    mShowAllTriggerControls = settings.value("showAllTriggerControls", false).toBool();
}

void dlgTriggerEditor::writeSettings()
{
    QSettings& settings = *mudlet::getQSettings();
    // A maximized or full-screen window would otherwise store the size of the
    // whole screen and reopen at it for good, so what is stored is the geometry
    // it would return to
    const QRect normalArea = normalGeometry();
    const QRect storedArea = ((isMaximized() || isFullScreen()) && normalArea.isValid()) ? normalArea : QRect(pos(), size());
    settings.setValue(qsl("script_editor_pos"), storedArea.topLeft());
    settings.setValue(qsl("script_editor_size"), storedArea.size());
    settings.setValue("autosaveIntervalMinutes", mAutosaveInterval);

    settings.setValue("mTriggerEditorSplitterState", mTriggerEditorSplitterState);
    settings.setValue("mAliasEditorSplitterState", mAliasEditorSplitterState);
    settings.setValue("mScriptEditorSplitterState", mScriptEditorSplitterState);
    settings.setValue("mActionEditorSplitterState", mActionEditorSplitterState);
    settings.setValue("mKeyEditorSplitterState", mKeyEditorSplitterState);
    settings.setValue("mTimerEditorSplitterState", mTimerEditorSplitterState);
    settings.setValue("mVarEditorSplitterState", mVarEditorSplitterState);

    settings.setValue("showAllTriggerControls", mShowAllTriggerControls);
}

void dlgTriggerEditor::slot_itemSelectedInSearchResults(QTreeWidgetItem* pItem)
{
    if (!pItem) {
        return;
    }

    // For changing views from one type to another (e.g. script->triggers), we have to show
    // the new view first before changing the TreeWidgetItem. Because we save changes to
    // the current item when it is left, if we change the TreeWidgetItem and then swap
    // views the contents of the previous item will be overwritten.
    QList<QTreeWidgetItem*> foundItemsList;
    switch (static_cast<EditorViewType>(pItem->data(0, ItemRole).toInt())) {
    case EditorViewType::cmTriggerView: { // DONE
        // These searches are to be case sensitive and recursive and find an
        // exact match - we are trying to find the "Name" of the item and then,
        // in case of duplicates we do a match on exact ID number
        foundItemsList = treeWidget_triggers->findItems(pItem->data(0, NameRole).toString(), Qt::MatchCaseSensitive | Qt::MatchFixedString | Qt::MatchRecursive, 0);

        // This was inside the loop but it is a constant value for the duration
        // of this method!
        const int idSearch = pItem->data(0, IdRole).toInt();

        for (auto treeWidgetItem : std::as_const(foundItemsList)) {
            if (treeWidgetItem->data(0, IdRole).toInt() == idSearch) {
                slot_showTriggers();
                slot_triggerSelected(treeWidgetItem);
                treeWidget_triggers->setCurrentItem(treeWidgetItem, 0);
                treeWidget_triggers->scrollToItem(treeWidgetItem);

                highlightSearchMatches();

                switch (pItem->data(0, TypeRole).toInt()) {
                case SearchResultIsScript: {
                    // Defer moveCaretTo so it fires after restoreEditorState's QTimer::singleShot(0)
                    // callback, ensuring the search result position takes precedence over the
                    // previously-saved editor state.
                    const auto line = static_cast<size_t>(pItem->data(0, PatternOrLineRole).toInt());
                    const auto column = static_cast<size_t>(pItem->data(0, PositionRole).toInt());
                    mpSourceEditorEdbee->setFocus();
                    QTimer::singleShot(0ms, this, [this, line, column]() {
                        if (mpSourceEditorEdbee) {
                            mpSourceEditorEdbee->controller()->moveCaretTo(line, column, false);
                        }
                    });
                    break;
                }
                case SearchResultIsName:
                    mpTriggersMainArea->lineEdit_trigger_name->setFocus(Qt::OtherFocusReason);
                    mpTriggersMainArea->lineEdit_trigger_name->setCursorPosition(pItem->data(0, PositionRole).toInt());
                    break;
                case SearchResultIsPattern: {
                    dlgTriggerPatternEdit* pTriggerPattern = mTriggerPatternEdit.at(pItem->data(0, PatternOrLineRole).toInt());
                    mpScrollArea->ensureWidgetVisible(pTriggerPattern);
                    if (pTriggerPattern->singleLineTextEdit_pattern->isVisible()) {
                        // If is a colour trigger the singleLineTextEdit_pattern is not shown
                        pTriggerPattern->singleLineTextEdit_pattern->setFocus();
                        pTriggerPattern->singleLineTextEdit_pattern->textCursor().setPosition(pItem->data(0, PositionRole).toInt());
                    }
                    break;
                }
                case SearchResultIsCommand:
                    mpTriggersMainArea->lineEdit_trigger_command->setFocus(Qt::OtherFocusReason);
                    mpTriggersMainArea->lineEdit_trigger_command->setCursorPosition(pItem->data(0, PositionRole).toInt());
                    break;
                default:
                    qDebug() << "dlgTriggerEditor::slot_item_selected_list(...) Called for a TRIGGER type item but handler for element of type:" << treeWidgetItem->data(0, TypeRole).toInt()
                             << "not yet done/applicable...!";
                }
                return;
            }
        }
        break;
    }

    case EditorViewType::cmAliasView: {
        foundItemsList = treeWidget_aliases->findItems(pItem->data(0, NameRole).toString(), Qt::MatchCaseSensitive | Qt::MatchFixedString | Qt::MatchRecursive, 0);

        const int idSearch = pItem->data(0, IdRole).toInt();

        for (auto treeWidgetItem : std::as_const(foundItemsList)) {
            if (treeWidgetItem->data(0, IdRole).toInt() == idSearch) {
                slot_showAliases();
                slot_aliasSelected(treeWidgetItem);
                treeWidget_aliases->setCurrentItem(treeWidgetItem, 0);
                treeWidget_aliases->scrollToItem(treeWidgetItem);

                highlightSearchMatches();

                switch (pItem->data(0, TypeRole).toInt()) {
                case SearchResultIsScript: {
                    // Defer moveCaretTo so it fires after restoreEditorState's QTimer::singleShot(0)
                    // callback, ensuring the search result position takes precedence over the
                    // previously-saved editor state.
                    const auto line = static_cast<size_t>(pItem->data(0, PatternOrLineRole).toInt());
                    const auto column = static_cast<size_t>(pItem->data(0, PositionRole).toInt());
                    mpSourceEditorEdbee->setFocus();
                    QTimer::singleShot(0ms, this, [this, line, column]() {
                        if (mpSourceEditorEdbee) {
                            auto controller = mpSourceEditorEdbee->controller();
                            controller->moveCaretTo(line, column, false);
                            controller->setAutoScrollToCaret(edbee::TextEditorController::AutoScrollWhenFocus);
                        }
                    });
                    break;
                }
                case SearchResultIsName:
                    mpAliasMainArea->lineEdit_alias_name->setFocus(Qt::OtherFocusReason);
                    mpAliasMainArea->lineEdit_alias_name->setCursorPosition(pItem->data(0, PositionRole).toInt());
                    break;
                case SearchResultIsPattern:
                    mpAliasMainArea->lineEdit_alias_pattern->setFocus(Qt::OtherFocusReason);
                    mpAliasMainArea->lineEdit_alias_pattern->setCursorPosition(pItem->data(0, PositionRole).toInt());
                    break;
                case SearchResultIsCommand:
                    mpAliasMainArea->lineEdit_alias_command->setFocus(Qt::OtherFocusReason);
                    mpAliasMainArea->lineEdit_alias_command->setCursorPosition(pItem->data(0, PositionRole).toInt());
                    break;
                default:
                    qDebug() << "dlgTriggerEditor::slot_item_selected_list(...) Called for a ALIAS type item but handler for element of type:" << treeWidgetItem->data(0, TypeRole).toInt()
                             << "not yet done/applicable...!";
                }
                return;
            }
        }
        break;
    }

    case EditorViewType::cmScriptView: {
        foundItemsList = treeWidget_scripts->findItems(pItem->data(0, NameRole).toString(), Qt::MatchCaseSensitive | Qt::MatchFixedString | Qt::MatchRecursive, 0);

        const int idSearch = pItem->data(0, IdRole).toInt();

        for (auto treeWidgetItem : std::as_const(foundItemsList)) {
            if (treeWidgetItem->data(0, IdRole).toInt() == idSearch) {
                slot_showScripts();
                slot_scriptsSelected(treeWidgetItem);
                treeWidget_scripts->setCurrentItem(treeWidgetItem, 0);
                treeWidget_scripts->scrollToItem(treeWidgetItem);

                highlightSearchMatches();

                switch (pItem->data(0, TypeRole).toInt()) {
                case SearchResultIsScript: {
                    // Defer moveCaretTo so it fires after restoreEditorState's QTimer::singleShot(0)
                    // callback, ensuring the search result position takes precedence over the
                    // previously-saved editor state.
                    const auto line = static_cast<size_t>(pItem->data(0, PatternOrLineRole).toInt());
                    const auto column = static_cast<size_t>(pItem->data(0, PositionRole).toInt());
                    mpSourceEditorEdbee->setFocus();
                    QTimer::singleShot(0ms, this, [this, line, column]() {
                        if (mpSourceEditorEdbee) {
                            mpSourceEditorEdbee->controller()->moveCaretTo(line, column, false);
                        }
                    });
                    break;
                }
                case SearchResultIsName:
                    mpScriptsMainArea->lineEdit_script_name->setFocus(Qt::OtherFocusReason);
                    mpScriptsMainArea->lineEdit_script_name->setCursorPosition(pItem->data(0, PositionRole).toInt());
                    break;
                case SearchResultIsEventHandler:
                    mpScriptsMainArea->listWidget_script_registered_event_handlers->setCurrentRow(pItem->data(0, PatternOrLineRole).toInt(), QItemSelectionModel::Clear);
                    mpScriptsMainArea->listWidget_script_registered_event_handlers->scrollTo(mpScriptsMainArea->listWidget_script_registered_event_handlers->currentIndex());
                    // Taken from slot_scriptMainAreaEditHandler():
                    // Note the handler item being edited:
                    mpScriptsMainAreaEditHandlerItem = mpScriptsMainArea->listWidget_script_registered_event_handlers->currentItem();
                    if (!mpScriptsMainAreaEditHandlerItem) {
                        break;
                    }
                    // Copy the event name to the entry widget:
                    mpScriptsMainArea->lineEdit_script_event_handler_entry->setText(mpScriptsMainAreaEditHandlerItem->text());
                    // Activate editing flag:
                    mIsScriptsMainAreaEditHandler = true;
                    break;
                default:
                    qDebug() << "dlgTriggerEditor::slot_item_selected_list(...) Called for a SCRIPT type item but handler for element of type:" << treeWidgetItem->data(0, TypeRole).toInt()
                             << "not yet done/applicable...!";
                }

                return;
            }
        }
        break;
    }

    case EditorViewType::cmActionView: {
        foundItemsList = treeWidget_actions->findItems(pItem->data(0, NameRole).toString(), Qt::MatchCaseSensitive | Qt::MatchFixedString | Qt::MatchRecursive, 0);

        const int idSearch = pItem->data(0, IdRole).toInt();

        for (auto treeWidgetitem : std::as_const(foundItemsList)) {
            if (treeWidgetitem->data(0, IdRole).toInt() == idSearch) {
                slot_showActions();
                slot_actionSelected(treeWidgetitem);
                treeWidget_actions->setCurrentItem(treeWidgetitem, 0);
                treeWidget_actions->scrollToItem(treeWidgetitem);

                highlightSearchMatches();

                switch (pItem->data(0, TypeRole).toInt()) {
                case SearchResultIsScript: {
                    // Defer moveCaretTo so it fires after restoreEditorState's QTimer::singleShot(0)
                    // callback, ensuring the search result position takes precedence over the
                    // previously-saved editor state.
                    const auto line = static_cast<size_t>(pItem->data(0, PatternOrLineRole).toInt());
                    const auto column = static_cast<size_t>(pItem->data(0, PositionRole).toInt());
                    mpSourceEditorEdbee->setFocus();
                    QTimer::singleShot(0ms, this, [this, line, column]() {
                        if (mpSourceEditorEdbee) {
                            mpSourceEditorEdbee->controller()->moveCaretTo(line, column, false);
                        }
                    });
                    break;
                }
                case SearchResultIsName:
                    mpActionsMainArea->lineEdit_action_name->setFocus(Qt::OtherFocusReason);
                    mpActionsMainArea->lineEdit_action_name->setCursorPosition(pItem->data(0, PositionRole).toInt());
                    break;
                case SearchResultIsCommand:
                    mpActionsMainArea->lineEdit_action_button_command_down->setFocus(Qt::OtherFocusReason);
                    mpActionsMainArea->lineEdit_action_button_command_down->setCursorPosition(pItem->data(0, PositionRole).toInt());
                    break;
                case SearchResultIsExtraCommand:
                    if (mpActionsMainArea->checkBox_action_button_isPushDown->isChecked()) {
                        mpActionsMainArea->lineEdit_action_button_command_up->setFocus(Qt::OtherFocusReason);
                        mpActionsMainArea->lineEdit_action_button_command_up->setCursorPosition(pItem->data(0, PositionRole).toInt());
                    }
                    break;
                case SearchResultsIsCss: {
                    mpActionsMainArea->plainTextEdit_action_css->setFocus(Qt::OtherFocusReason);
                    QTextCursor cssCursor(mpActionsMainArea->plainTextEdit_action_css->textCursor());
                    cssCursor.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);
                    if (pItem->data(0, PatternOrLineRole).toInt()) {
                        // Are we not on the first line - so move down that many lines?
                        cssCursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, pItem->data(0, PatternOrLineRole).toInt());
                    }
                    if (pItem->data(0, PositionRole).toInt()) {
                        // Are we not on the first character - if so move right that many QChars...
                        cssCursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, pItem->data(0, PositionRole).toInt());
                    }
                    mpActionsMainArea->plainTextEdit_action_css->setTextCursor(cssCursor);
                } // End case SearchResultsIsCss
                break;
                default:
                    qDebug() << "dlgTriggerEditor::slot_item_selected_list(...) Called for a BUTTON type item but handler for element of type:" << treeWidgetitem->data(0, TypeRole).toInt()
                             << "not yet done/applicable...!";
                } // End or switch()
                return;
            } // End of if()
        } // End of for()
        break;
    } // End of case EditorViewType::cmActionView

    case EditorViewType::cmTimerView: {
        foundItemsList = treeWidget_timers->findItems(pItem->data(0, NameRole).toString(), Qt::MatchCaseSensitive | Qt::MatchFixedString | Qt::MatchRecursive, 0);

        const int idSearch = pItem->data(0, IdRole).toInt();

        for (auto treeWidgetItem : std::as_const(foundItemsList)) {
            if (treeWidgetItem->data(0, IdRole).toInt() == idSearch) {
                slot_showTimers();
                slot_timerSelected(treeWidgetItem);
                treeWidget_timers->setCurrentItem(treeWidgetItem, 0);
                treeWidget_timers->scrollToItem(treeWidgetItem);

                highlightSearchMatches();

                switch (pItem->data(0, TypeRole).toInt()) {
                case SearchResultIsScript: {
                    // Defer moveCaretTo so it fires after restoreEditorState's QTimer::singleShot(0)
                    // callback, ensuring the search result position takes precedence over the
                    // previously-saved editor state.
                    const auto line = static_cast<size_t>(pItem->data(0, PatternOrLineRole).toInt());
                    const auto column = static_cast<size_t>(pItem->data(0, PositionRole).toInt());
                    mpSourceEditorEdbee->setFocus();
                    QTimer::singleShot(0ms, this, [this, line, column]() {
                        if (mpSourceEditorEdbee) {
                            mpSourceEditorEdbee->controller()->moveCaretTo(line, column, false);
                        }
                    });
                    break;
                }
                case SearchResultIsName:
                    mpTimersMainArea->lineEdit_timer_name->setFocus(Qt::OtherFocusReason);
                    mpTimersMainArea->lineEdit_timer_name->setCursorPosition(pItem->data(0, PositionRole).toInt());
                    break;
                case SearchResultIsCommand:
                    mpTimersMainArea->lineEdit_timer_command->setFocus(Qt::OtherFocusReason);
                    mpTimersMainArea->lineEdit_timer_command->setCursorPosition(pItem->data(0, PositionRole).toInt());
                    break;
                default:
                    qDebug() << "dlgTriggerEditor::slot_item_selected_list(...) Called for a TIMER type item but handler for element of type:" << treeWidgetItem->data(0, TypeRole).toInt()
                             << "not yet done/applicable...!";
                } // End of switch()
                return;
            } // End of if()
        } // End of for()
        break;
    } // End of case EditorViewType::cmTimerView

    case EditorViewType::cmKeysView: {
        foundItemsList = treeWidget_keys->findItems(pItem->data(0, NameRole).toString(), Qt::MatchCaseSensitive | Qt::MatchFixedString | Qt::MatchRecursive, 0);

        for (auto treeWidgetItem : std::as_const(foundItemsList)) {
            const int idTree = treeWidgetItem->data(0, IdRole).toInt();
            const int idSearch = pItem->data(0, IdRole).toInt();
            if (idTree == idSearch) {
                slot_showKeys();
                slot_keySelected(treeWidgetItem);
                treeWidget_keys->setCurrentItem(treeWidgetItem, 0);
                treeWidget_keys->scrollToItem(treeWidgetItem);

                highlightSearchMatches();

                switch (pItem->data(0, TypeRole).toInt()) {
                case SearchResultIsScript: {
                    // Defer moveCaretTo so it fires after restoreEditorState's QTimer::singleShot(0)
                    // callback, ensuring the search result position takes precedence over the
                    // previously-saved editor state.
                    const auto line = static_cast<size_t>(pItem->data(0, PatternOrLineRole).toInt());
                    const auto column = static_cast<size_t>(pItem->data(0, PositionRole).toInt());
                    mpSourceEditorEdbee->setFocus();
                    QTimer::singleShot(0ms, this, [this, line, column]() {
                        if (mpSourceEditorEdbee) {
                            mpSourceEditorEdbee->controller()->moveCaretTo(line, column, false);
                        }
                    });
                    break;
                }
                case SearchResultIsName:
                    mpKeysMainArea->lineEdit_key_name->setFocus(Qt::OtherFocusReason);
                    mpKeysMainArea->lineEdit_key_name->setCursorPosition(pItem->data(0, PositionRole).toInt());
                    break;
                case SearchResultIsPattern: {
                    dlgTriggerPatternEdit* pTriggerPattern = mTriggerPatternEdit.at(pItem->data(0, PatternOrLineRole).toInt());
                    mpScrollArea->ensureWidgetVisible(pTriggerPattern);
                    if (pTriggerPattern->singleLineTextEdit_pattern->isVisible()) {
                        // If is a colour trigger the singleLineTextEdit_pattern is not shown
                        pTriggerPattern->singleLineTextEdit_pattern->setFocus();
                        pTriggerPattern->singleLineTextEdit_pattern->textCursor().setPosition(pItem->data(0, PositionRole).toInt());
                    }
                    break;
                }
                case SearchResultIsCommand:
                    mpKeysMainArea->lineEdit_key_command->setFocus(Qt::OtherFocusReason);
                    mpKeysMainArea->lineEdit_key_command->setCursorPosition(pItem->data(0, PositionRole).toInt());
                    break;
                default:
                    qDebug() << "dlgTriggerEditor::slot_item_selected_list(...) Called for a KEY type item but handler for element of type:" << treeWidgetItem->data(0, TypeRole).toInt()
                             << "not yet done/applicable...!";
                } // End of switch()
                return;
            } // End of if
        } // End of for
        break;
    } // End of case EditorViewType::cmKeysView

    case EditorViewType::cmVarsView: {
        LuaInterface* lI = mpHost->getLuaInterface();
        VarUnit* vu = lI->getVarUnit();
        const QStringList varShort = pItem->data(0, IdRole).toStringList();
        QList<QTreeWidgetItem*> list;
        recurseVariablesDown(mpVarBaseItem, list);
        QListIterator<QTreeWidgetItem*> it(list);
        while (it.hasNext()) {
            QTreeWidgetItem* treeWidgetItem = it.next();
            TVar* var = vu->getWVar(treeWidgetItem);
            if (vu->shortVarName(var) == varShort) {
                show_vars();
                treeWidget_variables->setCurrentItem(treeWidgetItem, 0);
                treeWidget_variables->scrollToItem(treeWidgetItem);

                highlightSearchMatches();

                switch (pItem->data(0, TypeRole).toInt()) {
                case SearchResultIsName:
                    mpVarsMainArea->lineEdit_var_name->setFocus(Qt::OtherFocusReason);
                    mpVarsMainArea->lineEdit_var_name->setCursorPosition(pItem->data(0, PositionRole).toInt());
                    break;
                case SearchResultIsValue: {
                    // Defer moveCaretTo so it fires after restoreEditorState's QTimer::singleShot(0)
                    // callback, ensuring the search result position takes precedence over the
                    // previously-saved editor state.
                    const auto line = static_cast<size_t>(pItem->data(0, PatternOrLineRole).toInt());
                    const auto column = static_cast<size_t>(pItem->data(0, PositionRole).toInt());
                    mpSourceEditorEdbee->setFocus();
                    QTimer::singleShot(0ms, this, [this, line, column]() {
                        if (mpSourceEditorEdbee) {
                            mpSourceEditorEdbee->controller()->moveCaretTo(line, column, false);
                        }
                    });
                    break;
                }
                default:
                    qDebug() << "dlgTriggerEditor::slot_item_selected_list(...) Called for a VAR type item but handler for element of type:" << treeWidgetItem->data(0, TypeRole).toInt()
                             << "not yet done/applicable...!";
                }
                return;
            }
        }
    } // End of case static_cast<int>(EditorViewType::cmVarsView)
    break;
    default: {
    } // No-op
    } // End of switch()
}

// The line a match is on, as one row of it can be shown: whitespace that a row
// has no room for taken out of it, and the match moved along by however much
// that shifted it. A tab is a problem in both directions - it is advanced to the
// next tab stop when the line is drawn but measured as a single glyph when the
// marker under the match is sized - and a line break would put letters outside
// the row altogether. Returns where the match starts in the flattened line,
// which is no longer where it starts in the item itself.
static int flattenSnippet(QString& snippet, const int matchStart)
{
    // One QChar for one QChar, so nothing before the match moves...
    snippet.replace(QChar(QChar::LineFeed), QChar(QChar::Space));
    snippet.replace(QChar(QChar::CarriageReturn), QChar(QChar::Space));
    // ...whereas each tab widened into two spaces pushes the match one QChar
    // further along the line that is shown
    const int widened = snippet.left(std::max(0, matchStart)).count(QChar::Tabulation);
    snippet.replace(QChar(QChar::Tabulation), QString(QChar::Space).repeated(2));
    return matchStart + widened;
}

// Every result row goes through here, so that the navigation data the click
// handler reads and the presentation data the delegate draws from are written
// out in one place and cannot drift apart. The heading row keeps the first
// match's navigation data as well as its own: clicking a heading goes where the
// first of the matches under it goes, which is what clicking the row for that
// match used to do before the headings existed.
void dlgTriggerEditor::addSearchResult(QTreeWidgetItem*& pParent, const SearchResultRow& row)
{
    const bool variable = (row.view == EditorViewType::cmVarsView);
    QString snippet = row.snippet;
    const int snippetStart = flattenSnippet(snippet, row.matchStart);

    if (!pParent) {
        pParent = new QTreeWidgetItem(QStringList{row.title});
        if (variable) {
            setAllSearchData(pParent, row.name, row.variableId, row.what, row.matchStart, row.subInstance);
        } else {
            setAllSearchData(pParent, row.view, row.name, row.id, row.what, row.matchStart, row.instance, row.subInstance);
        }
        pParent->setData(0, uiDesign::SearchResultDelegate::RowKindRole, static_cast<int>(uiDesign::SearchResultDelegate::ItemRow));
        pParent->setData(0, uiDesign::SearchResultDelegate::TitleRole, row.title);
        pParent->setData(0, uiDesign::SearchResultDelegate::TypeLabelRole, row.typeLabel);
        //: Read out for a search result heading: %1 is the kind of thing it is (Trigger, Alias...) and %2 its name
        pParent->setData(0, Qt::AccessibleTextRole, tr("%1 %2").arg(row.typeLabel, row.title));
        // The name is cut to the width of the panel when it is drawn, so the
        // whole of it is a hover away
        pParent->setToolTip(0, row.title);
        treeWidget_searchResults->addTopLevelItem(pParent);
        pParent->setExpanded(true);
    }

    auto* pItem = new QTreeWidgetItem(QStringList{qsl("%1 %2").arg(row.where, snippet)});
    if (variable) {
        setAllSearchData(pItem, row.name, row.variableId, row.what, row.matchStart, row.subInstance);
    } else {
        setAllSearchData(pItem, row.view, row.name, row.id, row.what, row.matchStart, row.instance, row.subInstance);
    }
    pItem->setData(0, uiDesign::SearchResultDelegate::RowKindRole, static_cast<int>(uiDesign::SearchResultDelegate::MatchRow));
    pItem->setData(0, uiDesign::SearchResultDelegate::WhereRole, row.where);
    pItem->setData(0, uiDesign::SearchResultDelegate::SnippetRole, snippet);
    pItem->setData(0, uiDesign::SearchResultDelegate::MatchStartRole, snippetStart);
    pItem->setData(0, uiDesign::SearchResultDelegate::MatchLengthRole, mSearchTerm.length());
    //: Read out for one match inside a search result: %1 is where in the item it is ("Pattern 2") and %2 the line it is on
    pItem->setData(0, Qt::AccessibleTextRole, tr("%1: %2").arg(row.where, snippet));
    pItem->setToolTip(0, snippet);
    pParent->addChild(pItem);
}

void dlgTriggerEditor::slot_searchMudletItems(const int index)
{
    if (index < 0) {
        return;
    }
    const QString s{comboBox_searchTerms->itemText(index)};
    if (s.isEmpty()) {
        return;
    }

    mSearchTerm = s;
    treeWidget_searchResults->clear();
    treeWidget_searchResults->setUpdatesEnabled(false);

    searchTriggers(s);
    searchAliases(s);
    searchScripts(s);
    searchActions(s);
    searchTimers(s);
    searchKeys(s);

    if (mSearchOptions & SearchOptionIncludeVariables) {
        searchVariables(s);
    }

    if (!treeWidget_searchResults->topLevelItemCount()) {
        // A row rather than a panel of its own: an empty list says nothing about
        // whether the search ran
        //: Shown in place of search results when a search found nothing: %1 is what was typed into the search field, in the quotation marks the locale uses
        auto* pNotice = new QTreeWidgetItem(QStringList{tr("No matches for %1").arg(QLocale().quoteString(s))});
        pNotice->setData(0, uiDesign::SearchResultDelegate::RowKindRole, static_cast<int>(uiDesign::SearchResultDelegate::NoticeRow));
        pNotice->setData(0, uiDesign::SearchResultDelegate::TitleRole, pNotice->text(0));
        pNotice->setData(0, Qt::AccessibleTextRole, pNotice->text(0));
        pNotice->setFlags(Qt::ItemIsEnabled);
        treeWidget_searchResults->addTopLevelItem(pNotice);
    }

    mpSourceEditorEdbee->controller()->textSearcher()->setSearchTerm(s);
    mpSourceEditorEdbee->controller()->textSearcher()->setCaseSensitive(mSearchOptions & SearchOptionCaseSensitive);

    treeWidget_searchResults->setUpdatesEnabled(true);
    setSearchResultsShown(true);

    mpSourceEditorEdbee->controller()->update();
}

// The panel down the left shows one of two things: the profile's items, or what
// the last search found. Results stay up once they are there - clicking one
// opens the item it names in the pane to the right without putting the trees
// back - so that a list of matches can be worked through one at a time. Clearing
// the field, pressing Escape in it, or emptying it is what hands the panel back.
void dlgTriggerEditor::setSearchResultsShown(const bool shown)
{
    if (!mpWidget_searchResultsPane || !mpWidget_itemTreesPane) {
        return;
    }
    if (mpWidget_searchResultsPane->isVisibleTo(searchSplitter) == shown) {
        return;
    }

    mpWidget_searchResultsPane->setVisible(shown);
    mpWidget_itemTreesPane->setVisible(!shown);

    if (shown) {
        return;
    }
    // A tree scrolls nothing while it is off screen, so whatever was chosen from
    // the results while it was away is brought into view now that it is back
    for (QTreeWidget* pTreeWidget : {treeWidget_triggers, treeWidget_aliases, treeWidget_timers, treeWidget_scripts, treeWidget_actions, treeWidget_keys, treeWidget_variables}) {
        if (pTreeWidget->isVisibleTo(mpWidget_itemTreesPane) && pTreeWidget->currentItem()) {
            pTreeWidget->scrollToItem(pTreeWidget->currentItem());
        }
    }
}

// Switching views while the results are up leaves the trees off screen, and
// setFocus() on a widget that is not visible does nothing - which would leave
// the panel with no focus at all, and the panel's own shortcuts with nowhere to
// fire from. What the reader is looking at is the results, so that is what takes
// it instead.
void dlgTriggerEditor::focusPanelTree(QWidget* pTreeWidget)
{
    if (mpWidget_itemTreesPane && !mpWidget_itemTreesPane->isVisibleTo(searchSplitter)) {
        treeWidget_searchResults->setFocus();
        return;
    }
    pTreeWidget->setFocus();
}

void dlgTriggerEditor::searchVariables(const QString& text)
{
    if (mCurrentView != EditorViewType::cmVarsView) {
        // repopulateVars can take some time should there be a large number
        // of variables or big tables... 8-(
        repopulateVars();
    }

    LuaInterface* lI = mpHost->getLuaInterface();
    VarUnit* vu = lI->getVarUnit();
    TVar* base = vu->getBase();
    QListIterator<TVar*> itBaseVarChildren(base->getChildren(false));
    while (itBaseVarChildren.hasNext()) {
        TVar* var = itBaseVarChildren.next();
        // We do not search for hidden variables - probably because we would
        // have to unhide all of them to show the hidden ones found by
        // searching
        if (!showHiddenVars && vu->isHidden(var)) {
            continue;
        }

        //recurse down this variable
        QList<TVar*> list;
        recursiveSearchVariables(var, list, false);
        QListIterator<TVar*> itVarDecendent(list);
        while (itVarDecendent.hasNext()) {
            TVar* varDecendent = itVarDecendent.next();
            if (!showHiddenVars && vu->isHidden(varDecendent)) {
                continue;
            }

            QTreeWidgetItem* parent = nullptr;
            const QString name = varDecendent->getName();
            const QString value = varDecendent->getValue();
            const QStringList idStringList = vu->shortVarName(varDecendent);
            QString idString;
            // Take the first element - to comply with lua requirement it
            // must begin with not a digit and not contain any spaces so is
            // a string - and it is used "unquoted" as is to be the base
            // of a lua table
            if (idStringList.size() > 1) {
                QStringList midStrings = idStringList;
                idString = midStrings.takeFirst();
                QStringListIterator itSubString(midStrings);
                while (itSubString.hasNext()) {
                    const QString intermediate = itSubString.next();
                    bool isOk = false;
                    const int numberValue = intermediate.toInt(&isOk);
                    if (isOk && QString::number(numberValue) == intermediate) {
                        // This seems to be an integer
                        idString.append(qsl("[%1]").arg(intermediate));
                    } else {
                        idString.append(qsl("[\"%1\"]").arg(intermediate));
                    }
                }
            } else if (!idStringList.empty()) {
                idString = idStringList.at(0);
            }

            SearchResultRow row;
            row.view = EditorViewType::cmVarsView;
            row.typeLabel = tr("Variable");
            // A variable is reached by the expression that names it rather than
            // by the bare name its parent table knows it as
            row.title = idString;
            row.name = name;
            row.variableId = vu->shortVarName(varDecendent);

            int startPos = 0;
            if ((startPos = findSearchMatch(name, text)) != -1) {
                // We do not (yet) worry about multiple search results in the "name"
                row.where = tr("Name");
                row.snippet = name;
                row.matchStart = startPos;
                row.what = SearchResultIsName;
                addSearchResult(parent, row);
            }

            // The additional first test is needed to exclude the case when
            // the search term matches on the word "function" which will
            // appear in EVERY "value" for a lua function in the variable
            // tree widget...
            if (value != QLatin1String("function") && (startPos = findSearchMatch(value, text)) != -1) {
                // We do not (yet) worry about multiple search results in the "value"
                row.where = tr("Value");
                row.snippet = value;
                row.matchStart = startPos;
                row.what = SearchResultIsValue;
                addSearchResult(parent, row);
            }
        }
    }
}

void dlgTriggerEditor::searchKeys(const QString& text)
{
    std::list<TKey*> const nodes = mpHost->getKeyUnit()->getKeyRootNodeList();
    for (auto key : nodes) {
        searchSingleKey(key, text);
        recursiveSearchKeys(key, text);
    }
}

void dlgTriggerEditor::searchSingleTimer(TTimer* timer, const QString& text)
{
    QTreeWidgetItem* parent = nullptr;
    const QString name = timer->getName();
    int startPos = 0;

    SearchResultRow row;
    row.view = EditorViewType::cmTimerView;
    row.typeLabel = tr("Timer");
    row.title = name;
    row.name = name;
    row.id = timer->getID();

    if ((startPos = findSearchMatch(name, text)) != -1) {
        row.where = tr("Name");
        row.snippet = name;
        row.matchStart = startPos;
        row.what = SearchResultIsName;
        addSearchResult(parent, row);
    }

    if ((startPos = findSearchMatch(timer->getCommand(), text)) != -1) {
        row.where = tr("Command");
        row.snippet = timer->getCommand();
        row.matchStart = startPos;
        row.what = SearchResultIsCommand;
        addSearchResult(parent, row);
    }

    emitScriptSearchMatches(timer->getScript(), text, name, timer->getID(), tr("Timer"), EditorViewType::cmTimerView, parent);
}

void dlgTriggerEditor::searchSingleKey(TKey* key, const QString& text)
{
    QTreeWidgetItem* parent = nullptr;
    const QString name = key->getName();
    int startPos = 0;

    SearchResultRow row;
    row.view = EditorViewType::cmKeysView;
    row.typeLabel = tr("Key");
    row.title = name;
    row.name = name;
    row.id = key->getID();

    if ((startPos = findSearchMatch(name, text)) != -1) {
        row.where = tr("Name");
        row.snippet = name;
        row.matchStart = startPos;
        row.what = SearchResultIsName;
        addSearchResult(parent, row);
    }

    if ((startPos = findSearchMatch(key->getCommand(), text)) != -1) {
        row.where = tr("Command");
        row.snippet = key->getCommand();
        row.matchStart = startPos;
        row.what = SearchResultIsCommand;
        addSearchResult(parent, row);
    }

    emitScriptSearchMatches(key->getScript(), text, name, key->getID(), tr("Key"), EditorViewType::cmKeysView, parent);
}

void dlgTriggerEditor::highlightSearchMatches()
{
    auto controller = mpSourceEditorEdbee->controller();
    auto searcher = controller->textSearcher();
    searcher->markAll(controller->borderedTextRanges());
    controller->update();
}

void dlgTriggerEditor::emitScriptSearchMatches(
        const QString& scriptText, const QString& searchText, const QString& name, int objectId, const QString& parentLabel, EditorViewType viewType, QTreeWidgetItem*& parent)
{
    const QStringList textList = scriptText.split(qsl("\n"));
    const int total = textList.count();

    SearchResultRow row;
    row.view = viewType;
    row.typeLabel = parentLabel;
    row.title = name;
    row.name = name;
    row.id = objectId;
    row.what = SearchResultIsScript;

    for (int index = 0; index < total; ++index) {
        if (textList.at(index).isEmpty() || !containsSearchMatch(textList.at(index), searchText)) {
            continue;
        }

        int instance = 0;
        int startPos = 0;
        while ((startPos = findSearchMatch(textList.at(index), searchText, startPos)) != -1) {
            //: Where in an item a search match is: %1 is the line number in its Lua code and %2 the column on that line
            row.where = tr("Lua %1:%2").arg(index + 1).arg(startPos + 1);
            row.snippet = textList.at(index);
            row.matchStart = startPos;
            row.instance = index;
            row.subInstance = instance++;
            addSearchResult(parent, row);
            ++startPos;
        }
    }
}

void dlgTriggerEditor::searchTimers(const QString& text)
{
    std::list<TTimer*> const nodes = mpHost->getTimerUnit()->getTimerRootNodeList();
    for (auto timer : nodes) {
        searchSingleTimer(timer, text);
        recursiveSearchTimers(timer, text);
    }
}

void dlgTriggerEditor::searchSingleAction(TAction* action, const QString& text)
{
    QTreeWidgetItem* parent = nullptr;
    const QString name = action->getName();
    int startPos = 0;

    SearchResultRow row;
    row.view = EditorViewType::cmActionView;
    row.typeLabel = tr("Button");
    row.title = name;
    row.name = name;
    row.id = action->getID();

    if ((startPos = findSearchMatch(name, text)) != -1) {
        row.where = tr("Name");
        row.snippet = name;
        row.matchStart = startPos;
        row.what = SearchResultIsName;
        addSearchResult(parent, row);
    }

    if ((startPos = findSearchMatch(action->getCommandButtonDown(), text)) != -1) {
        //: Where in a push-down button a search match is: the command it sends when pushed down
        row.where = action->isPushDownButton() ? tr("Command (down)") : tr("Command");
        row.snippet = action->getCommandButtonDown();
        row.matchStart = startPos;
        row.what = SearchResultIsCommand;
        addSearchResult(parent, row);
    }

    if (action->isPushDownButton()) {
        if ((startPos = findSearchMatch(action->getCommandButtonUp(), text)) != -1) {
            //: Where in a push-down button a search match is: the command it sends when let back up
            row.where = tr("Command (up)");
            row.snippet = action->getCommandButtonUp();
            row.matchStart = startPos;
            row.what = SearchResultIsExtraCommand;
            addSearchResult(parent, row);
        }
    }

    QStringList textList = action->css.split("\n");
    int total = textList.count();
    for (int index = 0; index < total; ++index) {
        if (textList.at(index).isEmpty() || !containsSearchMatch(textList.at(index), text)) {
            continue;
        }

        int instance = 0;
        startPos = 0;
        while ((startPos = findSearchMatch(textList.at(index), text, startPos)) != -1) {
            //: Where in a button a search match is: %1 is the line number in its stylesheet and %2 the column on that line
            row.where = tr("Style %1:%2").arg(index + 1).arg(startPos + 1);
            row.snippet = textList.at(index);
            row.matchStart = startPos;
            row.what = SearchResultsIsCss;
            row.instance = index;
            row.subInstance = instance++;
            addSearchResult(parent, row);
            ++startPos;
        }
    }

    emitScriptSearchMatches(action->getScript(), text, name, action->getID(), tr("Button"), EditorViewType::cmActionView, parent);
}

void dlgTriggerEditor::searchActions(const QString& text)
{
    std::list<TAction*> const nodes = mpHost->getActionUnit()->getActionRootNodeList();
    for (auto action : nodes) {
        searchSingleAction(action, text);
        recursiveSearchActions(action, text);
    }
}

void dlgTriggerEditor::searchSingleScript(TScript* script, const QString& text)
{
    QTreeWidgetItem* parent = nullptr;
    const QString name = script->getName();
    int startPos = 0;

    SearchResultRow row;
    row.view = EditorViewType::cmScriptView;
    row.typeLabel = tr("Script");
    row.title = name;
    row.name = name;
    row.id = script->getID();

    if ((startPos = findSearchMatch(name, text)) != -1) {
        row.where = tr("Name");
        row.snippet = name;
        row.matchStart = startPos;
        row.what = SearchResultIsName;
        addSearchResult(parent, row);
    }

    QStringList textList = script->getEventHandlerList();
    int total = textList.count();
    for (int index = 0; index < total; ++index) {
        if (textList.at(index).isEmpty() || !containsSearchMatch(textList.at(index), text)) {
            continue;
        }

        int instance = 0;
        startPos = 0;
        while ((startPos = findSearchMatch(textList.at(index), text, startPos)) != -1) {
            //: Where in a script a search match is: one of the events it is registered for
            row.where = tr("Event");
            row.snippet = textList.at(index);
            row.matchStart = startPos;
            row.what = SearchResultIsEventHandler;
            row.instance = index;
            row.subInstance = instance++;
            addSearchResult(parent, row);
            ++startPos;
        }
    }

    emitScriptSearchMatches(script->getScript(), text, name, script->getID(), tr("Script"), EditorViewType::cmScriptView, parent);
}

void dlgTriggerEditor::searchScripts(const QString& text)
{
    std::list<TScript*> const nodes = mpHost->getScriptUnit()->getScriptRootNodeList();
    for (auto script : nodes) {
        searchSingleScript(script, text);
        recursiveSearchScripts(script, text);
    }
}

void dlgTriggerEditor::searchSingleAlias(TAlias* alias, const QString& text)
{
    QTreeWidgetItem* parent = nullptr;
    const QString name = alias->getName();
    int startPos = 0;

    SearchResultRow row;
    row.view = EditorViewType::cmAliasView;
    row.typeLabel = tr("Alias");
    row.title = name;
    row.name = name;
    row.id = alias->getID();

    if ((startPos = findSearchMatch(name, text)) != -1) {
        row.where = tr("Name");
        row.snippet = name;
        row.matchStart = startPos;
        row.what = SearchResultIsName;
        addSearchResult(parent, row);
    }

    if ((startPos = findSearchMatch(alias->getCommand(), text)) != -1) {
        row.where = tr("Command");
        row.snippet = alias->getCommand();
        row.matchStart = startPos;
        row.what = SearchResultIsCommand;
        addSearchResult(parent, row);
    }

    if ((startPos = findSearchMatch(alias->getRegexCode(), text)) != -1) {
        row.where = tr("Pattern");
        row.snippet = alias->getRegexCode();
        row.matchStart = startPos;
        row.what = SearchResultIsPattern;
        addSearchResult(parent, row);
    }

    emitScriptSearchMatches(alias->getScript(), text, name, alias->getID(), tr("Alias"), EditorViewType::cmAliasView, parent);
}

void dlgTriggerEditor::searchAliases(const QString& text)
{
    std::list<TAlias*> const nodes = mpHost->getAliasUnit()->getAliasRootNodeList();
    for (auto alias : nodes) {
        searchSingleAlias(alias, text);
        recursiveSearchAlias(alias, text);
    }
}

void dlgTriggerEditor::searchSingleTrigger(TTrigger* trigger, const QString& text)
{
    QTreeWidgetItem* parent = nullptr;
    const QString name = trigger->getName();
    int startPos = 0;

    SearchResultRow row;
    row.view = EditorViewType::cmTriggerView;
    row.typeLabel = tr("Trigger");
    row.title = name;
    row.name = name;
    row.id = trigger->getID();

    if ((startPos = findSearchMatch(name, text)) != -1) {
        row.where = tr("Name");
        row.snippet = name;
        row.matchStart = startPos;
        row.what = SearchResultIsName;
        addSearchResult(parent, row);
    }

    if ((startPos = findSearchMatch(trigger->getCommand(), text)) != -1) {
        row.where = tr("Command");
        row.snippet = trigger->getCommand();
        row.matchStart = startPos;
        row.what = SearchResultIsCommand;
        addSearchResult(parent, row);
    }

    QStringList textList = trigger->getPatternsList();
    int total = textList.count();
    for (int index = 0; index < total; ++index) {
        if (textList.at(index).isEmpty() || !containsSearchMatch(textList.at(index), text)) {
            continue;
        }

        int instance = 0;
        startPos = 0;
        while ((startPos = findSearchMatch(textList.at(index), text, startPos)) != -1) {
            //: Where in a trigger a search match is: %1 is which of its patterns
            row.where = tr("Pattern %1").arg(index + 1);
            row.snippet = textList.at(index);
            row.matchStart = startPos;
            row.what = SearchResultIsPattern;
            row.instance = index;
            row.subInstance = instance++;
            addSearchResult(parent, row);
            ++startPos;
        }
    }

    emitScriptSearchMatches(trigger->getScript(), text, name, trigger->getID(), tr("Trigger"), EditorViewType::cmTriggerView, parent);
}

void dlgTriggerEditor::searchTriggers(const QString& text)
{
    std::list<TTrigger*> const nodes = mpHost->getTriggerUnit()->getTriggerRootNodeList();
    for (auto trigger : nodes) {
        searchSingleTrigger(trigger, text);
        recursiveSearchTriggers(trigger, text);
    }
}

void dlgTriggerEditor::recursiveSearchTriggers(TTrigger* pTriggerParent, const QString& text)
{
    std::list<TTrigger*>* childrenList = pTriggerParent->getChildrenList();
    for (auto trigger : *childrenList) {
        searchSingleTrigger(trigger, text);
        if (trigger->hasChildren()) {
            recursiveSearchTriggers(trigger, text);
        }
    }
}

void dlgTriggerEditor::recursiveSearchAlias(TAlias* pTriggerParent, const QString& text)
{
    std::list<TAlias*>* childrenList = pTriggerParent->getChildrenList();
    for (auto alias : *childrenList) {
        searchSingleAlias(alias, text);
        if (alias->hasChildren()) {
            recursiveSearchAlias(alias, text);
        }
    }
}

void dlgTriggerEditor::recursiveSearchScripts(TScript* pTriggerParent, const QString& text)
{
    std::list<TScript*>* childrenList = pTriggerParent->getChildrenList();
    for (auto script : *childrenList) {
        searchSingleScript(script, text);
        if (script->hasChildren()) {
            recursiveSearchScripts(script, text);
        }
    }
}

void dlgTriggerEditor::recursiveSearchActions(TAction* pTriggerParent, const QString& text)
{
    std::list<TAction*>* childrenList = pTriggerParent->getChildrenList();
    for (auto action : *childrenList) {
        searchSingleAction(action, text);
        if (action->hasChildren()) {
            recursiveSearchActions(action, text);
        }
    }
}

void dlgTriggerEditor::recursiveSearchTimers(TTimer* pTriggerParent, const QString& text)
{
    std::list<TTimer*>* childrenList = pTriggerParent->getChildrenList();
    for (auto timer : *childrenList) {
        searchSingleTimer(timer, text);
        if (timer->hasChildren()) {
            recursiveSearchTimers(timer, text);
        }
    }
}

void dlgTriggerEditor::recursiveSearchKeys(TKey* pTriggerParent, const QString& text)
{
    std::list<TKey*>* childrenList = pTriggerParent->getChildrenList();
    for (auto key : *childrenList) {
        searchSingleKey(key, text);
        if (key->hasChildren()) {
            recursiveSearchKeys(key, text);
        }
    }
}


void dlgTriggerEditor::delete_alias()
{
    QList<QTreeWidgetItem*> selectedItems = treeWidget_aliases->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }

    QStringList itemNames;
    QList<TAlias*> aliasesToDelete;

    for (const QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        TAlias* pT = mpHost->getAliasUnit()->getAlias(pItem->data(0, Qt::UserRole).toInt());
        if (pT) {
            itemNames << pT->getName();
            aliasesToDelete << pT;
        }
    }

    if (aliasesToDelete.isEmpty()) {
        return;
    }

    // Capture state of all items BEFORE deletion for undo
    QList<EditorDeleteItemCommand::DeletedItemInfo> deletedItems;

    // Recursive lambda to capture an alias and all its descendants
    std::function<void(TAlias*, int, int)> captureAliasAndChildren = [&](TAlias* pT, int parentID, int positionInParent) {
        if (!pT) {
            return;
        }

        EditorDeleteItemCommand::DeletedItemInfo info;
        info.itemID = pT->getID();
        info.itemName = pT->getName();
        info.parentID = parentID;
        info.positionInParent = positionInParent;

        pugi::xml_document doc;
        auto root = doc.append_child("AliasSnapshot");
        XMLexport exporter(pT);
        exporter.writeAlias(pT, root);
        std::ostringstream oss;
        doc.save(oss);
        info.xmlSnapshot = QString::fromStdString(oss.str());

        deletedItems.append(info);

        if (pT->mpMyChildrenList) {
            int i = 0;
            for (auto* pChild : *pT->mpMyChildrenList) {
                captureAliasAndChildren(pChild, pT->getID(), i);
                ++i;
            }
        }
    };

    // Helper lambda to calculate position in data model (not tree widget)
    auto calculatePosition = [](TAlias* item) -> int {
        if (!item) {
            return 0;
        }
        TAlias* parent = item->getParent();
        if (!parent) {
            return 0;
        }
        auto* childrenList = parent->getChildrenList();
        if (!childrenList) {
            return 0;
        }
        int position = 0;
        for (auto* child : *childrenList) {
            if (child == item) {
                return position;
            }
            position++;
        }
        return 0;
    };

    // Capture each selected alias and all its descendants
    for (const QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        TAlias* pT = mpHost->getAliasUnit()->getAlias(pItem->data(0, Qt::UserRole).toInt());
        if (pT) {
            int parentID = -1;
            int positionInParent = 0;

            TAlias* parent = pT->getParent();
            if (parent) {
                parentID = parent->getID();
                positionInParent = calculatePosition(pT);
            } else {
                parentID = -1;
                auto rootList = mpHost->getAliasUnit()->getAliasRootNodeList();
                int pos = 0;
                for (auto* rootItem : rootList) {
                    if (rootItem == pT) {
                        positionInParent = pos;
                        break;
                    }
                    pos++;
                }
            }

            captureAliasAndChildren(pT, parentID, positionInParent);
        }
    }

    // Sort items by their position in tree (top to bottom) to delete correctly
    std::sort(selectedItems.begin(), selectedItems.end(), [this](QTreeWidgetItem* a, QTreeWidgetItem* b) {
        QModelIndex indexA = treeWidget_aliases->indexFromItem(a);
        QModelIndex indexB = treeWidget_aliases->indexFromItem(b);
        return indexA.row() < indexB.row();
    });

    // Delete in reverse order to maintain valid indices
    std::reverse(selectedItems.begin(), selectedItems.end());

    QTreeWidgetItem* newSelection = nullptr;
    QList<QTreeWidgetItem*> removedItems;
    for (QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        QTreeWidgetItem* pParentItem = pItem->parent();
        const int itemId = pItem->data(0, Qt::UserRole).toInt();
        TAlias* pT = mpHost->getAliasUnit()->getAlias(itemId);

        if (pT) {
            if (!newSelection) {
                // Try to select sibling above, then parent, then base item
                int itemIndex = pParentItem ? pParentItem->indexOfChild(pItem) : treeWidget_aliases->indexOfTopLevelItem(pItem);
                if (itemIndex > 0 && pParentItem) {
                    // Select sibling above
                    newSelection = pParentItem->child(itemIndex - 1);
                } else if (pParentItem) {
                    // No sibling above, select parent
                    newSelection = pParentItem;
                } else {
                    // Top-level item with no sibling above, select base item
                    newSelection = mpAliasBaseItem;
                }
            }
            if (pParentItem) {
                pParentItem->removeChild(pItem);
                removedItems.append(pItem);
            }
            clearEditorState(EditorViewType::cmAliasView, itemId);
            delete pT;
        }
    }

    if (!deletedItems.isEmpty()) {
        auto* qtCmd = new EditorDeleteItemCommand(EditorViewType::cmAliasView, deletedItems, mpHost);
        mpUndoStack->pushCommand(qtCmd);
    }

    // Detaching an item nulls treeWidget() on its whole subtree, which is how a
    // newSelection that sat inside another removed subtree is caught here:
    if (newSelection && !newSelection->treeWidget()) {
        newSelection = mpAliasBaseItem;
    }

    // Set new selection
    if (newSelection) {
        mpCurrentAliasItem = newSelection;
        treeWidget_aliases->setCurrentItem(newSelection);
        slot_aliasSelected(newSelection);
    } else {
        mpCurrentAliasItem = nullptr;
        clearAliasForm();
    }

    // Has to stay after the selection handling: the slots it fires still read
    // the detached items.
    qDeleteAll(removedItems);
}

void dlgTriggerEditor::delete_action()
{
    QList<QTreeWidgetItem*> initiallySelectedItems = treeWidget_actions->selectedItems();
    if (initiallySelectedItems.isEmpty()) {
        return;
    }

    // Capture each selected action and all its descendants
    // and put them into here:
    QList<QTreeWidgetItem*> selectedItems;
    for (const auto& item : std::as_const(initiallySelectedItems)) {
        treeWidget_actions->getAllChildren(item, selectedItems);
    }

    // Remove any duplicates by converting to a set and back to a list:
    QSet<QTreeWidgetItem*> selectedItemsSet{selectedItems.cbegin(), selectedItems.cend()};
    selectedItems = QList<QTreeWidgetItem*>{selectedItemsSet.cbegin(), selectedItemsSet.cend()};

    QStringList itemNames;
    QList<TAction*> actionsToDelete;
    for (const QTreeWidgetItem* pItem : std::as_const(initiallySelectedItems)) {
        TAction* pT = mpHost->getActionUnit()->getAction(pItem->data(0, Qt::UserRole).toInt());
        if (pT) {
            itemNames << pT->getName();
            actionsToDelete << pT;
        }
    }

    if (actionsToDelete.isEmpty()) {
        return;
    }

    // Capture state of all items BEFORE deletion for undo
    QList<EditorDeleteItemCommand::DeletedItemInfo> deletedItems;

    // Recursive lambda to capture an action and all its descendants
    std::function<void(TAction*, int, int)> captureActionAndChildren = [&](TAction* pT, int parentID, int positionInParent) {
        if (!pT) {
            return;
        }

        EditorDeleteItemCommand::DeletedItemInfo info;
        info.itemID = pT->getID();
        info.itemName = pT->getName();
        info.parentID = parentID;
        info.positionInParent = positionInParent;

        // Export action to XML snapshot
        pugi::xml_document doc;
        auto root = doc.append_child("ActionSnapshot");
        XMLexport exporter(pT);
        exporter.writeAction(pT, root);
        std::ostringstream oss;
        doc.save(oss);
        info.xmlSnapshot = QString::fromStdString(oss.str());

        deletedItems.append(info);

        if (pT->mpMyChildrenList) {
            int i = 0;
            for (auto* pChild : *pT->mpMyChildrenList) {
                captureActionAndChildren(pChild, pT->getID(), i);
                ++i;
            }
        }
    };

    for (QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        TAction* pT = mpHost->getActionUnit()->getAction(pItem->data(0, Qt::UserRole).toInt());
        if (pT) {
            // Determine parent ID and position
            int parentID = -1;
            int positionInParent = 0;

            QTreeWidgetItem* pParentItem = pItem->parent();
            if (pParentItem && pParentItem != mpActionBaseItem) {
                parentID = pParentItem->data(0, Qt::UserRole).toInt();
                positionInParent = pParentItem->indexOfChild(pItem);
            } else {
                parentID = -1;
                positionInParent = mpActionBaseItem->indexOfChild(pItem);
            }

            // Recursively capture this action and all its children
            captureActionAndChildren(pT, parentID, positionInParent);
        }
    }

    // Sort items by their position in tree (top to bottom) to delete correctly
    std::sort(selectedItems.begin(), selectedItems.end(), [this](QTreeWidgetItem* a, QTreeWidgetItem* b) {
        QModelIndex indexA = treeWidget_actions->indexFromItem(a);
        QModelIndex indexB = treeWidget_actions->indexFromItem(b);
        return indexA.row() < indexB.row();
    });

    // Delete in reverse order to maintain valid indices
    std::reverse(selectedItems.begin(), selectedItems.end());

    QTreeWidgetItem* newSelection = nullptr;
    QList<QTreeWidgetItem*> removedItems;
    for (QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        QTreeWidgetItem* pParentItem = pItem->parent();
        const int itemId = pItem->data(0, Qt::UserRole).toInt();
        TAction* pT = mpHost->getActionUnit()->getAction(itemId);

        if (pT) {
            if (pT->isActive()) {
                pT->deactivate();
            }
            pT->setDataChanged();

            if (!newSelection) {
                // Try to select sibling above, then parent, then base item
                int itemIndex = pParentItem ? pParentItem->indexOfChild(pItem) : treeWidget_actions->indexOfTopLevelItem(pItem);
                if (itemIndex > 0 && pParentItem) {
                    // Select sibling above
                    newSelection = pParentItem->child(itemIndex - 1);
                } else if (pParentItem) {
                    // No sibling above, select parent
                    newSelection = pParentItem;
                } else {
                    // Top-level item with no sibling above, select base item
                    newSelection = mpActionBaseItem;
                }
            }
            if (pParentItem) {
                pParentItem->removeChild(pItem);
                removedItems.append(pItem);
            }
            clearEditorState(EditorViewType::cmActionView, itemId);
            delete pT;
        }
    }

    if (!deletedItems.isEmpty()) {
        auto* qtCmd = new EditorDeleteItemCommand(EditorViewType::cmActionView, deletedItems, mpHost);
        mpUndoStack->pushCommand(qtCmd);
    }

    if (newSelection && !newSelection->treeWidget()) {
        newSelection = mpActionBaseItem;
    }

    // Set new selection
    if (newSelection) {
        mpCurrentActionItem = newSelection;
        treeWidget_actions->setCurrentItem(newSelection);
        slot_actionSelected(newSelection);
    } else {
        mpCurrentActionItem = nullptr;
        clearActionForm();
    }

    qDeleteAll(removedItems);

    mpHost->getActionUnit()->updateAllToolbars();
}

void dlgTriggerEditor::delete_variable()
{
    QList<QTreeWidgetItem*> selectedItems = treeWidget_variables->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }

    QStringList itemNames;
    QList<TVar*> varsToDelete;
    LuaInterface* lI = mpHost->getLuaInterface();
    VarUnit* vu = lI->getVarUnit();

    for (QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        TVar* var = vu->getWVar(pItem);
        if (var) {
            itemNames << var->getName();
            varsToDelete << var;
        }
    }

    if (varsToDelete.isEmpty()) {
        return;
    }

    // Sort items by their position in tree (top to bottom) to delete correctly
    std::sort(selectedItems.begin(), selectedItems.end(), [this](QTreeWidgetItem* a, QTreeWidgetItem* b) {
        QModelIndex indexA = treeWidget_variables->indexFromItem(a);
        QModelIndex indexB = treeWidget_variables->indexFromItem(b);
        return indexA.row() < indexB.row();
    });

    // Delete in reverse order to maintain valid indices
    std::reverse(selectedItems.begin(), selectedItems.end());

    QTreeWidgetItem* newSelection = nullptr;
    QList<QTreeWidgetItem*> removedItems;
    for (QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        QTreeWidgetItem* pParentItem = pItem->parent();
        TVar* var = vu->getWVar(pItem);

        if (var) {
            lI->deleteVar(var);
            TVar* parent = var->getParent();
            if (parent) {
                parent->removeChild(var);
            }
            vu->removeVariable(var);

            if (pParentItem && !newSelection) {
                newSelection = pParentItem;
            }
            if (pParentItem) {
                pParentItem->removeChild(pItem);
                removedItems.append(pItem);
                // Deleting the TVar below frees its descendants too and nothing
                // unregisters those, so drop the whole detached subtree from the
                // lookup maps now: a later pass over a selected descendant would
                // otherwise resolve a freed TVar, as would a recycled item address:
                QList<QTreeWidgetItem*> pendingPurge{pItem};
                while (!pendingPurge.isEmpty()) {
                    QTreeWidgetItem* pEntry = pendingPurge.takeLast();
                    vu->removeTreeItem(pEntry);
                    for (int i = 0; i < pEntry->childCount(); ++i) {
                        pendingPurge.append(pEntry->child(i));
                    }
                }
            }
            delete var;
        }
    }

    if (newSelection && !newSelection->treeWidget()) {
        newSelection = mpVarBaseItem;
    }

    // Set new selection
    if (newSelection) {
        mpCurrentVarItem = newSelection;
        treeWidget_variables->setCurrentItem(newSelection);
        slot_variableSelected(newSelection);
    } else {
        mpCurrentVarItem = nullptr;
        clearVarForm();
    }

    qDeleteAll(removedItems);
}

void dlgTriggerEditor::delete_script()
{
    QList<QTreeWidgetItem*> selectedItems = treeWidget_scripts->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }

    QStringList itemNames;
    QList<TScript*> scriptsToDelete;

    for (const QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        TScript* pT = mpHost->getScriptUnit()->getScript(pItem->data(0, Qt::UserRole).toInt());
        if (pT) {
            itemNames << pT->getName();
            scriptsToDelete << pT;
        }
    }

    if (scriptsToDelete.isEmpty()) {
        return;
    }

    // Capture state of all items BEFORE deletion for undo
    QList<EditorDeleteItemCommand::DeletedItemInfo> deletedItems;

    // Recursive lambda to capture a script and all its descendants
    std::function<void(TScript*, int, int)> captureScriptAndChildren = [&](TScript* pT, int parentID, int positionInParent) {
        if (!pT) {
            return;
        }

        EditorDeleteItemCommand::DeletedItemInfo info;
        info.itemID = pT->getID();
        info.itemName = pT->getName();
        info.parentID = parentID;
        info.positionInParent = positionInParent;

        // Export script to XML snapshot
        pugi::xml_document doc;
        auto root = doc.append_child("ScriptSnapshot");
        XMLexport exporter(pT);
        exporter.writeScript(pT, root);
        std::ostringstream oss;
        doc.save(oss);
        info.xmlSnapshot = QString::fromStdString(oss.str());

        deletedItems.append(info);

        if (pT->mpMyChildrenList) {
            int i = 0;
            for (auto* pChild : *pT->mpMyChildrenList) {
                captureScriptAndChildren(pChild, pT->getID(), i);
                ++i;
            }
        }
    };

    // Capture each selected script and all its descendants
    for (QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        TScript* pT = mpHost->getScriptUnit()->getScript(pItem->data(0, Qt::UserRole).toInt());
        if (pT) {
            // Determine parent ID and position
            int parentID = -1;
            int positionInParent = 0;

            QTreeWidgetItem* pParentItem = pItem->parent();
            if (pParentItem && pParentItem != mpScriptsBaseItem) {
                parentID = pParentItem->data(0, Qt::UserRole).toInt();
                positionInParent = pParentItem->indexOfChild(pItem);
            } else {
                parentID = -1;
                positionInParent = mpScriptsBaseItem->indexOfChild(pItem);
            }

            // Recursively capture this script and all its children
            captureScriptAndChildren(pT, parentID, positionInParent);
        }
    }

    // Sort items by their position in tree (top to bottom) to delete correctly
    std::sort(selectedItems.begin(), selectedItems.end(), [this](QTreeWidgetItem* a, QTreeWidgetItem* b) {
        QModelIndex indexA = treeWidget_scripts->indexFromItem(a);
        QModelIndex indexB = treeWidget_scripts->indexFromItem(b);
        return indexA.row() < indexB.row();
    });

    // Delete in reverse order to maintain valid indices
    std::reverse(selectedItems.begin(), selectedItems.end());

    QTreeWidgetItem* newSelection = nullptr;
    QList<QTreeWidgetItem*> removedItems;
    for (QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        QTreeWidgetItem* pParentItem = pItem->parent();
        const int itemId = pItem->data(0, Qt::UserRole).toInt();
        TScript* pT = mpHost->getScriptUnit()->getScript(itemId);

        if (pT) {
            if (!newSelection) {
                // Try to select sibling above, then parent, then base item
                int itemIndex = pParentItem ? pParentItem->indexOfChild(pItem) : treeWidget_scripts->indexOfTopLevelItem(pItem);
                if (itemIndex > 0 && pParentItem) {
                    // Select sibling above
                    newSelection = pParentItem->child(itemIndex - 1);
                } else if (pParentItem) {
                    // No sibling above, select parent
                    newSelection = pParentItem;
                } else {
                    // Top-level item with no sibling above, select base item
                    newSelection = mpScriptsBaseItem;
                }
            }
            if (pParentItem) {
                pParentItem->removeChild(pItem);
                removedItems.append(pItem);
            }
            clearEditorState(EditorViewType::cmScriptView, itemId);
            delete pT;
        }
    }

    if (!deletedItems.isEmpty()) {
        auto* qtCmd = new EditorDeleteItemCommand(EditorViewType::cmScriptView, deletedItems, mpHost);
        mpUndoStack->pushCommand(qtCmd);
    }

    if (newSelection && !newSelection->treeWidget()) {
        newSelection = mpScriptsBaseItem;
    }

    // Set new selection
    if (newSelection) {
        mpCurrentScriptItem = newSelection;
        treeWidget_scripts->setCurrentItem(newSelection);
        slot_scriptsSelected(newSelection);
    } else {
        mpCurrentScriptItem = nullptr;
        clearScriptForm();
    }

    qDeleteAll(removedItems);
}

void dlgTriggerEditor::delete_key()
{
    QList<QTreeWidgetItem*> selectedItems = treeWidget_keys->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }

    QStringList itemNames;
    QList<TKey*> keysToDelete;

    for (const QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        TKey* pT = mpHost->getKeyUnit()->getKey(pItem->data(0, Qt::UserRole).toInt());
        if (pT) {
            itemNames << pT->getName();
            keysToDelete << pT;
        }
    }

    if (keysToDelete.isEmpty()) {
        return;
    }

    // Capture state of all items BEFORE deletion for undo
    QList<EditorDeleteItemCommand::DeletedItemInfo> deletedItems;

    // Recursive lambda to capture a key and all its descendants
    std::function<void(TKey*, int, int)> captureKeyAndChildren = [&](TKey* pT, int parentID, int positionInParent) {
        if (!pT) {
            return;
        }

        EditorDeleteItemCommand::DeletedItemInfo info;
        info.itemID = pT->getID();
        info.itemName = pT->getName();
        info.parentID = parentID;
        info.positionInParent = positionInParent;

        // Export key to XML snapshot
        pugi::xml_document doc;
        auto root = doc.append_child("KeySnapshot");
        XMLexport exporter(pT);
        exporter.writeKey(pT, root);
        std::ostringstream oss;
        doc.save(oss);
        info.xmlSnapshot = QString::fromStdString(oss.str());

        deletedItems.append(info);

        if (pT->mpMyChildrenList) {
            int i = 0;
            for (auto* pChild : *pT->mpMyChildrenList) {
                captureKeyAndChildren(pChild, pT->getID(), i);
                ++i;
            }
        }
    };

    // Capture each selected key and all its descendants
    for (QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        TKey* pT = mpHost->getKeyUnit()->getKey(pItem->data(0, Qt::UserRole).toInt());
        if (pT) {
            // Determine parent ID and position
            int parentID = -1;
            int positionInParent = 0;

            QTreeWidgetItem* pParentItem = pItem->parent();
            if (pParentItem && pParentItem != mpKeyBaseItem) {
                parentID = pParentItem->data(0, Qt::UserRole).toInt();
                positionInParent = pParentItem->indexOfChild(pItem);
            } else {
                parentID = -1;
                positionInParent = mpKeyBaseItem->indexOfChild(pItem);
            }

            // Recursively capture this key and all its children
            captureKeyAndChildren(pT, parentID, positionInParent);
        }
    }

    // Sort items by their position in tree (top to bottom) to delete correctly
    std::sort(selectedItems.begin(), selectedItems.end(), [this](QTreeWidgetItem* a, QTreeWidgetItem* b) {
        QModelIndex indexA = treeWidget_keys->indexFromItem(a);
        QModelIndex indexB = treeWidget_keys->indexFromItem(b);
        return indexA.row() < indexB.row();
    });

    // Delete in reverse order to maintain valid indices
    std::reverse(selectedItems.begin(), selectedItems.end());

    QTreeWidgetItem* newSelection = nullptr;
    QList<QTreeWidgetItem*> removedItems;
    for (QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        QTreeWidgetItem* pParentItem = pItem->parent();
        const int itemId = pItem->data(0, Qt::UserRole).toInt();
        TKey* pT = mpHost->getKeyUnit()->getKey(itemId);

        if (pT) {
            if (!newSelection) {
                // Try to select sibling above, then parent, then base item
                int itemIndex = pParentItem ? pParentItem->indexOfChild(pItem) : treeWidget_keys->indexOfTopLevelItem(pItem);
                if (itemIndex > 0 && pParentItem) {
                    // Select sibling above
                    newSelection = pParentItem->child(itemIndex - 1);
                } else if (pParentItem) {
                    // No sibling above, select parent
                    newSelection = pParentItem;
                } else {
                    // Top-level item with no sibling above, select base item
                    newSelection = mpKeyBaseItem;
                }
            }
            if (pParentItem) {
                pParentItem->removeChild(pItem);
                removedItems.append(pItem);
            }
            clearEditorState(EditorViewType::cmKeysView, itemId);
            delete pT;
        }
    }

    if (!deletedItems.isEmpty()) {
        auto* qtCmd = new EditorDeleteItemCommand(EditorViewType::cmKeysView, deletedItems, mpHost);
        mpUndoStack->pushCommand(qtCmd);
    }

    if (newSelection && !newSelection->treeWidget()) {
        newSelection = mpKeyBaseItem;
    }

    // Set new selection
    if (newSelection) {
        mpCurrentKeyItem = newSelection;
        treeWidget_keys->setCurrentItem(newSelection);
        slot_keySelected(newSelection);
    } else {
        mpCurrentKeyItem = nullptr;
        clearKeyForm();
    }

    qDeleteAll(removedItems);
}

void dlgTriggerEditor::delete_trigger()
{
    QList<QTreeWidgetItem*> selectedItems = treeWidget_triggers->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }

    QStringList itemNames;
    QList<TTrigger*> triggersToDelete;

    for (QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(pItem->data(0, Qt::UserRole).toInt());
        if (pT) {
            itemNames << pT->getName();
            triggersToDelete << pT;
        }
    }

    if (triggersToDelete.isEmpty()) {
        return;
    }

    // Capture state of all items BEFORE deletion for undo
    QList<EditorDeleteItemCommand::DeletedItemInfo> deletedItems;

    // Recursive lambda to capture a trigger and all its descendants
    std::function<void(TTrigger*, int, int)> captureTriggerAndChildren = [&](TTrigger* pT, int parentID, int positionInParent) {
        if (!pT) {
            return;
        }

        EditorDeleteItemCommand::DeletedItemInfo info;
        info.itemID = pT->getID();
        info.itemName = pT->getName();
        info.parentID = parentID;
        info.positionInParent = positionInParent;

        // Export trigger to XML snapshot
        pugi::xml_document doc;
        auto root = doc.append_child("TriggerSnapshot");
        XMLexport exporter(pT);
        exporter.writeTrigger(pT, root);
        std::ostringstream oss;
        doc.save(oss);
        info.xmlSnapshot = QString::fromStdString(oss.str());

        deletedItems.append(info);

        if (pT->mpMyChildrenList) {
            int i = 0;
            for (auto* pChild : *pT->mpMyChildrenList) {
                captureTriggerAndChildren(pChild, pT->getID(), i);
                ++i;
            }
        }
    };

    // Capture each selected trigger and all its descendants
    for (QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(pItem->data(0, Qt::UserRole).toInt());
        if (pT) {
            // Determine parent ID and position
            int parentID = -1;
            int positionInParent = 0;

            QTreeWidgetItem* pParentItem = pItem->parent();
            if (pParentItem) {
                if (pParentItem == mpTriggerBaseItem) {
                    parentID = -1;
                    positionInParent = mpTriggerBaseItem->indexOfChild(pItem);
                } else {
                    parentID = pParentItem->data(0, Qt::UserRole).toInt();
                    positionInParent = pParentItem->indexOfChild(pItem);
                }
            } else {
                parentID = -1;
                positionInParent = treeWidget_triggers->indexOfTopLevelItem(pItem);
            }

            // Recursively capture this trigger and all its children
            captureTriggerAndChildren(pT, parentID, positionInParent);
        }
    }

    // Sort items by their position in tree (top to bottom) to delete correctly
    std::sort(selectedItems.begin(), selectedItems.end(), [this](QTreeWidgetItem* a, QTreeWidgetItem* b) {
        QModelIndex indexA = treeWidget_triggers->indexFromItem(a);
        QModelIndex indexB = treeWidget_triggers->indexFromItem(b);
        return indexA.row() < indexB.row();
    });

    // Delete in reverse order to maintain valid indices
    std::reverse(selectedItems.begin(), selectedItems.end());

    QTreeWidgetItem* newSelection = nullptr;
    QList<QTreeWidgetItem*> removedItems;
    for (QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        QTreeWidgetItem* pParentItem = pItem->parent();
        const int itemId = pItem->data(0, Qt::UserRole).toInt();
        TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(itemId);

        if (pT) {
            if (!newSelection) {
                // Try to select sibling above, then parent, then base item
                int itemIndex = pParentItem ? pParentItem->indexOfChild(pItem) : treeWidget_triggers->indexOfTopLevelItem(pItem);
                if (itemIndex > 0 && pParentItem) {
                    // Select sibling above
                    newSelection = pParentItem->child(itemIndex - 1);
                } else if (pParentItem) {
                    // No sibling above, select parent
                    newSelection = pParentItem;
                } else {
                    // Top-level item with no sibling above, select base item
                    newSelection = mpTriggerBaseItem;
                }
            }
            if (pParentItem) {
                pParentItem->removeChild(pItem);
                removedItems.append(pItem);
            }
            clearEditorState(EditorViewType::cmTriggerView, itemId);
            delete pT;
        }
    }

    if (!deletedItems.isEmpty()) {
        auto* qtCmd = new EditorDeleteItemCommand(EditorViewType::cmTriggerView, deletedItems, mpHost);
        mpUndoStack->pushCommand(qtCmd);
    }

    if (newSelection && !newSelection->treeWidget()) {
        newSelection = mpTriggerBaseItem;
    }

    // Set new selection
    if (newSelection) {
        mpCurrentTriggerItem = newSelection;
        treeWidget_triggers->setCurrentItem(newSelection);
        slot_triggerSelected(newSelection);
    } else {
        mpCurrentTriggerItem = nullptr;
        clearTriggerForm();
    }

    qDeleteAll(removedItems);
}

void dlgTriggerEditor::delete_timer()
{
    QList<QTreeWidgetItem*> selectedItems = treeWidget_timers->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }

    QStringList itemNames;
    QList<TTimer*> timersToDelete;

    for (const QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        TTimer* pT = mpHost->getTimerUnit()->getTimer(pItem->data(0, Qt::UserRole).toInt());
        if (pT) {
            itemNames << pT->getName();
            timersToDelete << pT;
        }
    }

    if (timersToDelete.isEmpty()) {
        return;
    }

    // Capture state of all items BEFORE deletion for undo
    QList<EditorDeleteItemCommand::DeletedItemInfo> deletedItems;

    // Recursive lambda to capture a timer and all its descendants
    std::function<void(TTimer*, int, int)> captureTimerAndChildren = [&](TTimer* pT, int parentID, int positionInParent) {
        if (!pT) {
            return;
        }

        EditorDeleteItemCommand::DeletedItemInfo info;
        info.itemID = pT->getID();
        info.itemName = pT->getName();
        info.parentID = parentID;
        info.positionInParent = positionInParent;

        // Export timer to XML snapshot
        pugi::xml_document doc;
        auto root = doc.append_child("TimerSnapshot");
        XMLexport exporter(pT);
        exporter.writeTimer(pT, root);
        std::ostringstream oss;
        doc.save(oss);
        info.xmlSnapshot = QString::fromStdString(oss.str());

        deletedItems.append(info);

        if (pT->mpMyChildrenList) {
            int i = 0;
            for (auto* pChild : *pT->mpMyChildrenList) {
                captureTimerAndChildren(pChild, pT->getID(), i);
                ++i;
            }
        }
    };

    // Capture each selected timer and all its descendants
    for (QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        TTimer* pT = mpHost->getTimerUnit()->getTimer(pItem->data(0, Qt::UserRole).toInt());
        if (pT) {
            // Determine parent ID and position
            int parentID = -1;
            int positionInParent = 0;

            QTreeWidgetItem* pParentItem = pItem->parent();
            if (pParentItem && pParentItem != mpTimerBaseItem) {
                parentID = pParentItem->data(0, Qt::UserRole).toInt();
                positionInParent = pParentItem->indexOfChild(pItem);
            } else {
                parentID = -1;
                positionInParent = mpTimerBaseItem->indexOfChild(pItem);
            }

            // Recursively capture this timer and all its children
            captureTimerAndChildren(pT, parentID, positionInParent);
        }
    }

    // Sort items by their position in tree (top to bottom) to delete correctly
    std::sort(selectedItems.begin(), selectedItems.end(), [this](QTreeWidgetItem* a, QTreeWidgetItem* b) {
        QModelIndex indexA = treeWidget_timers->indexFromItem(a);
        QModelIndex indexB = treeWidget_timers->indexFromItem(b);
        return indexA.row() < indexB.row();
    });

    // Delete in reverse order to maintain valid indices
    std::reverse(selectedItems.begin(), selectedItems.end());

    QTreeWidgetItem* newSelection = nullptr;
    QList<QTreeWidgetItem*> removedItems;
    for (QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        QTreeWidgetItem* pParentItem = pItem->parent();
        const int itemId = pItem->data(0, Qt::UserRole).toInt();
        TTimer* pT = mpHost->getTimerUnit()->getTimer(itemId);

        if (pT) {
            if (!newSelection) {
                // Try to select sibling above, then parent, then base item
                int itemIndex = pParentItem ? pParentItem->indexOfChild(pItem) : treeWidget_timers->indexOfTopLevelItem(pItem);
                if (itemIndex > 0 && pParentItem) {
                    // Select sibling above
                    newSelection = pParentItem->child(itemIndex - 1);
                } else if (pParentItem) {
                    // No sibling above, select parent
                    newSelection = pParentItem;
                } else {
                    // Top-level item with no sibling above, select base item
                    newSelection = mpTimerBaseItem;
                }
            }
            if (pParentItem) {
                pParentItem->removeChild(pItem);
                removedItems.append(pItem);
            }
            clearEditorState(EditorViewType::cmTimerView, itemId);
            delete pT;
        }
    }

    if (!deletedItems.isEmpty()) {
        auto* qtCmd = new EditorDeleteItemCommand(EditorViewType::cmTimerView, deletedItems, mpHost);
        mpUndoStack->pushCommand(qtCmd);
    }

    if (newSelection && !newSelection->treeWidget()) {
        newSelection = mpTimerBaseItem;
    }

    // Set new selection
    if (newSelection) {
        mpCurrentTimerItem = newSelection;
        treeWidget_timers->setCurrentItem(newSelection);
        slot_timerSelected(newSelection);
    } else {
        mpCurrentTimerItem = nullptr;
        clearTimerForm();
    }

    qDeleteAll(removedItems);
}


void dlgTriggerEditor::activeToggle_trigger()
{
    QTreeWidgetItem* pItem = treeWidget_triggers->currentItem();
    if (!pItem) {
        return;
    }
    QIcon icon;
    QString itemDescription;

    TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(pItem->data(0, Qt::UserRole).toInt());
    if (!pT) {
        return;
    }

    // Capture old state for undo
    bool oldState = pT->shouldBeActive();
    pT->setIsActive(!oldState);
    bool newState = pT->isActive();

    if (pT->isFilterChain()) {
        if (pT->isActive()) {
            itemDescription = descActiveFilterChain;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/filter.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/filter-grey.png")), QIcon::Normal, QIcon::Off);
                itemDescription = descInactiveParent.arg(itemDescription);
            }
        } else {
            itemDescription = descInactiveFilterChain;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/filter-locked.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/filter-grey-locked.png")), QIcon::Normal, QIcon::Off);
            }
        }
    } else if (pT->isFolder()) {
        if (pT->isActive()) {
            itemDescription = descActiveFolder;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-blue.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                itemDescription = descInactiveParent.arg(itemDescription);
            }
        } else {
            itemDescription = descInactiveFolder;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-blue-locked.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
            }
        }
    } else {
        if (pT->isActive()) {
            itemDescription = descActive;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                itemDescription = descInactiveParent.arg(itemDescription);
            }
        } else {
            itemDescription = descInactive;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
            }
        }
    }

    if (!pT->state()) {
        pT->setIsActive(false);
        showError(tr(R"(<p>Unable to activate "<tt>%1</tt>": %2</p>
                     <p><i>You will need to reactivate this after the problem has been corrected.</i></p>)")
                          .arg(pT->getName().toHtmlEscaped(), pT->getError()));
        icon.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
        itemDescription = descError;
    }
    pItem->setIcon(0, icon);
    pItem->setText(0, pT->getName());
    pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);

    if (pItem->childCount() > 0) {
        children_icon_triggers(pItem);
    }

    if (mpUndoStack && oldState != newState) {
        auto* qtCmd = new EditorToggleActiveCommand(EditorViewType::cmTriggerView, pT->getID(), oldState, newState, pT->getName(), mpHost);
        mpUndoStack->pushCommand(qtCmd);
    }
}

void dlgTriggerEditor::slot_itemMoved(int itemID, int oldParentID, int newParentID, int oldPosition, int newPosition)
{
    if (!mpUndoStack) {
        return;
    }

    // Determine which view this move belongs to
    EditorViewType viewType;
    QString itemName;

    // Check which tree widget has focus or which view is active
    switch (resolveCurrentView()) {
    case EditorViewType::cmTriggerView: {
        TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(itemID);
        if (pT) {
            viewType = EditorViewType::cmTriggerView;
            itemName = pT->getName();
        } else {
            return;
        }
        break;
    }
    case EditorViewType::cmAliasView: {
        TAlias* pA = mpHost->getAliasUnit()->getAlias(itemID);
        if (pA) {
            viewType = EditorViewType::cmAliasView;
            itemName = pA->getName();
        } else {
            return;
        }
        break;
    }
    case EditorViewType::cmTimerView: {
        TTimer* pT = mpHost->getTimerUnit()->getTimer(itemID);
        if (pT) {
            viewType = EditorViewType::cmTimerView;
            itemName = pT->getName();
        } else {
            return;
        }
        break;
    }
    case EditorViewType::cmScriptView: {
        TScript* pS = mpHost->getScriptUnit()->getScript(itemID);
        if (pS) {
            viewType = EditorViewType::cmScriptView;
            itemName = pS->getName();
        } else {
            return;
        }
        break;
    }
    case EditorViewType::cmKeysView: {
        TKey* pK = mpHost->getKeyUnit()->getKey(itemID);
        if (pK) {
            viewType = EditorViewType::cmKeysView;
            itemName = pK->getName();
        } else {
            return;
        }
        break;
    }
    case EditorViewType::cmActionView: {
        TAction* pA = mpHost->getActionUnit()->getAction(itemID);
        if (pA) {
            viewType = EditorViewType::cmActionView;
            itemName = pA->getName();
        } else {
            return;
        }
        break;
    }
    default:
        return;
    }

    // Push move command to undo system
    auto* qtCmd = new EditorMoveItemCommand(viewType, itemID, oldParentID, newParentID, oldPosition, newPosition, itemName, mpHost);
    mpUndoStack->pushCommand(qtCmd);
}

void dlgTriggerEditor::slot_batchMoveStarted()
{
    if (!mpUndoStack) {
        return;
    }

    mpUndoStack->beginMacro(tr("move items"));
}

void dlgTriggerEditor::slot_batchMoveEnded()
{
    if (!mpUndoStack) {
        return;
    }

    mpUndoStack->endMacro();
}

void dlgTriggerEditor::children_icon_triggers(QTreeWidgetItem* pWidgetItemParent)
{
    for (int i = 0; i < pWidgetItemParent->childCount(); i++) {
        QTreeWidgetItem* pItem = pWidgetItemParent->child(i);
        TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(pItem->data(0, Qt::UserRole).toInt());
        if (!pT) {
            return;
        }

        QIcon icon;
        QString itemDescription;
        if (pItem->childCount() > 0) {
            children_icon_triggers(pItem);
        }
        if (pT->state()) {
            if (pT->isFilterChain()) {
                if (pT->isActive()) {
                    itemDescription = descActiveFilterChain;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/filter.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/filter-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactiveFilterChain;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/filter-locked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/filter-grey-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else if (pT->isFolder()) {
                if (pT->isActive()) {
                    itemDescription = descActiveFolder;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-blue.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactiveFolder;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-blue-locked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else {
                if (pT->isActive()) {
                    itemDescription = descActive;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }

                } else {
                    itemDescription = descInactive;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            }
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(pT->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}


void dlgTriggerEditor::activeToggle_timer()
{
    QTreeWidgetItem* pItem = treeWidget_timers->currentItem();
    if (!pItem) {
        return;
    }
    QIcon icon;
    QString itemDescription;

    TTimer* pT = mpHost->getTimerUnit()->getTimer(pItem->data(0, Qt::UserRole).toInt());
    if (!pT) {
        return;
    }

    // Capture old state for undo
    bool oldState = pT->shouldBeActive();

    if (!pT->isOffsetTimer()) {
        pT->setIsActive(!pT->shouldBeActive());
    } else {
        pT->setShouldBeActive(!pT->shouldBeActive());
    }

    // Capture new state after toggle
    bool newState = pT->shouldBeActive();

    if (pT->isFolder()) {
        // disable or enable all timers in the respective branch
        // irrespective of the user defined state.
        if (pT->shouldBeActive()) {
            pT->enableTimer(pT->getID());
        } else {
            pT->disableTimer(pT->getID());
        }

        if (pT->shouldBeActive()) {
            itemDescription = descActiveFolder;
            if (pT->ancestorsActive()) {
                if (!pT->mPackageName.isEmpty()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-brown.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-green.png")), QIcon::Normal, QIcon::Off);
                }
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                itemDescription = descInactiveParent.arg(itemDescription);
            }
        } else {
            itemDescription = descInactiveFolder;
            if (pT->ancestorsActive()) {
                if (!pT->mPackageName.isEmpty()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-brown-locked.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-green-locked.png")), QIcon::Normal, QIcon::Off);
                }
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
            }
        }
    } else {
        if (pT->isOffsetTimer()) {
            // state of offset timers is managed by the trigger engine
            if (pT->shouldBeActive()) {
                pT->enableTimer(pT->getID());
                itemDescription = descActiveOffsetTimer;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-on.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-on-grey.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveParent.arg(itemDescription);
                }
            } else {
                pT->disableTimer(pT->getID());
                itemDescription = descInactiveOffsetTimer;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-off.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-off-grey.png")), QIcon::Normal, QIcon::Off);
                }
            }
        } else {
            if (pT->shouldBeActive()) {
                pT->enableTimer(pT->getID());
                itemDescription = descActive;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveParent.arg(itemDescription);
                }
            } else {
                pT->disableTimer(pT->getID());
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                itemDescription = descInactive;
            }
        }
    }

    if (!pT->state()) {
        pT->setIsActive(false);
        showError(tr(R"(<p><b>Unable to activate "<tt>%1</tt>": %2.</b></p>
                     <p><i>You will need to reactivate this after the problem has been corrected.</i></p>)")
                          .arg(pT->getName().toHtmlEscaped(), pT->getError()));
        icon.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
        itemDescription = descError;
    }
    pItem->setIcon(0, icon);
    pItem->setText(0, pT->getName());
    pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);

    if (pItem->childCount() > 0) {
        children_icon_timer(pItem);
    }

    if (mpUndoStack && oldState != newState) {
        auto* qtCmd = new EditorToggleActiveCommand(EditorViewType::cmTimerView, pT->getID(), oldState, newState, pT->getName(), mpHost);
        mpUndoStack->pushCommand(qtCmd);
    }
}

void dlgTriggerEditor::children_icon_timer(QTreeWidgetItem* pWidgetItemParent)
{
    for (int i = 0; i < pWidgetItemParent->childCount(); i++) {
        QTreeWidgetItem* pItem = pWidgetItemParent->child(i);
        TTimer* pT = mpHost->getTimerUnit()->getTimer(pItem->data(0, Qt::UserRole).toInt());
        if (!pT) {
            return;
        }

        QIcon icon;
        QString itemDescription;
        const bool itemActive = (pT->isActive() || pT->shouldBeActive());

        if (pItem->childCount() > 0) {
            children_icon_timer(pItem);
        }
        if (pT->state()) {
            if (pT->isFolder()) {
                itemDescription = (itemActive ? descActiveFolder : descInactiveFolder);
                if (itemActive) {
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-green.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-green-locked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else {
                if (pT->isOffsetTimer()) {
                    if (pT->shouldBeActive()) {
                        itemDescription = descActiveOffsetTimer;
                        if (pT->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-on.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-on-grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        itemDescription = descInactiveOffsetTimer;
                        if (pT->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-off.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-off-grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    }
                } else {
                    if (itemActive) {
                        itemDescription = descActive;
                        if (pT->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        itemDescription = descInactive;
                        if (pT->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
                        }
                    }
                }
            }
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(pT->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}

void dlgTriggerEditor::activeToggle_alias()
{
    QTreeWidgetItem* pItem = treeWidget_aliases->currentItem();
    if (!pItem) {
        return;
    }
    QIcon icon;
    QString itemDescription;

    TAlias* pT = mpHost->getAliasUnit()->getAlias(pItem->data(0, Qt::UserRole).toInt());
    if (!pT) {
        return;
    }

    // Capture old state for undo
    bool oldState = pT->shouldBeActive();
    pT->setIsActive(!pT->shouldBeActive());
    // Capture new state after toggle
    bool newState = pT->isActive();

    if (pT->isFolder()) {
        if (pT->isActive()) {
            icon.addPixmap(QPixmap(qsl(":/icons/folder-violet.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descActiveFolder;
        } else {
            icon.addPixmap(QPixmap(qsl(":/icons/folder-violet-locked.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descInactiveFolder;
        }
    } else {
        if (pT->isActive()) {
            itemDescription = descActive;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                itemDescription = descInactiveParent.arg(itemDescription);
            }
        } else {
            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descInactive;
        }
    }

    if (!pT->state()) {
        pT->setIsActive(false);
        showError(tr(R"(<p><b>Unable to activate "<tt>%1</tt>"; %2.</b></p>
                     <p><i>You will need to reactivate this after the problem has been corrected.</i></p>)")
                          .arg(pT->getName().toHtmlEscaped(), pT->getError()));
        icon.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
        itemDescription = descError;
    }
    pItem->setIcon(0, icon);
    pItem->setText(0, pT->getName());
    pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);

    if (pItem->childCount() > 0) {
        children_icon_alias(pItem);
    }

    if (mpUndoStack && oldState != newState) {
        auto* qtCmd = new EditorToggleActiveCommand(EditorViewType::cmAliasView, pT->getID(), oldState, newState, pT->getName(), mpHost);
        mpUndoStack->pushCommand(qtCmd);
    }
}

void dlgTriggerEditor::children_icon_alias(QTreeWidgetItem* pWidgetItemParent)
{
    for (int i = 0; i < pWidgetItemParent->childCount(); i++) {
        QTreeWidgetItem* pItem = pWidgetItemParent->child(i);
        TAlias* pT = mpHost->getAliasUnit()->getAlias(pItem->data(0, Qt::UserRole).toInt());
        if (!pT) {
            return;
        }

        QIcon icon;
        QString itemDescription;
        if (pItem->childCount() > 0) {
            children_icon_alias(pItem);
        }
        if (pT->state()) {
            if (pT->isFolder()) {
                if (pT->isActive()) {
                    itemDescription = descActiveFolder;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-violet.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactiveFolder;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-violet-locked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else {
                if (pT->isActive()) {
                    itemDescription = descActive;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }

                } else {
                    itemDescription = descInactive;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            }
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(pT->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}


void dlgTriggerEditor::activeToggle_script()
{
    QTreeWidgetItem* pItem = treeWidget_scripts->currentItem();
    if (!pItem) {
        return;
    }
    QIcon icon;
    QString itemDescription;

    TScript* pT = mpHost->getScriptUnit()->getScript(pItem->data(0, Qt::UserRole).toInt());
    if (!pT) {
        return;
    }

    // Capture old state for undo
    bool oldState = pT->shouldBeActive();
    pT->setIsActive(!pT->shouldBeActive());
    // Capture new state after toggle
    bool newState = pT->isActive();

    if (pT->isFolder()) {
        if (pT->isActive()) {
            icon.addPixmap(QPixmap(qsl(":/icons/folder-orange.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descActiveFolder;
        } else {
            icon.addPixmap(QPixmap(qsl(":/icons/folder-orange-locked.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descInactiveFolder;
        }
    } else {
        if (pT->isActive()) {
            itemDescription = descActive;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                itemDescription = descInactiveParent.arg(itemDescription);
            }
        } else {
            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descInactive;
        }
    }

    if (!pT->state()) {
        pT->setIsActive(false);
        showError(tr(R"(<p><b>Unable to activate "<tt>%1</tt>"; %2.</b></p>
                     <p><i>You will need to reactivate this after the problem has been corrected.</i></p>)")
                          .arg(pT->getName().toHtmlEscaped(), pT->getError()));
        icon.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
        itemDescription = descError;
    }
    pItem->setIcon(0, icon);
    pItem->setText(0, pT->getName());
    pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    if (pItem->childCount() > 0) {
        children_icon_script(pItem);
    }

    if (mpUndoStack && oldState != newState) {
        auto* qtCmd = new EditorToggleActiveCommand(EditorViewType::cmScriptView, pT->getID(), oldState, newState, pT->getName(), mpHost);
        mpUndoStack->pushCommand(qtCmd);
    }
}

void dlgTriggerEditor::children_icon_script(QTreeWidgetItem* pWidgetItemParent)
{
    for (int i = 0; i < pWidgetItemParent->childCount(); i++) {
        QTreeWidgetItem* pItem = pWidgetItemParent->child(i);
        TScript* pT = mpHost->getScriptUnit()->getScript(pItem->data(0, Qt::UserRole).toInt());
        if (!pT) {
            return;
        }

        QIcon icon;
        QString itemDescription;
        if (pItem->childCount() > 0) {
            children_icon_script(pItem);
        }
        if (pT->state()) {
            if (pT->isFolder()) {
                if (pT->isActive()) {
                    itemDescription = descActiveFolder;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-orange.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactiveFolder;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-orange-locked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else {
                if (pT->isActive()) {
                    itemDescription = descActive;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactive;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            }
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(pT->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}


void dlgTriggerEditor::activeToggle_action()
{
    QTreeWidgetItem* pItem = treeWidget_actions->currentItem();
    if (!pItem) {
        return;
    }
    QIcon icon;
    QString itemDescription;

    TAction* pT = mpHost->getActionUnit()->getAction(pItem->data(0, Qt::UserRole).toInt());
    if (!pT) {
        return;
    }

    // Capture old state for undo
    bool oldState = pT->shouldBeActive();
    pT->setIsActive(!pT->shouldBeActive());
    pT->setDataChanged();
    // Capture new state after toggle
    bool newState = pT->isActive();

    if (pT->mpToolBar) {
        if (!pT->isActive()) {
            pT->mpToolBar->hide();
        } else {
            pT->mpToolBar->show();
        }
    }

    const bool itemActive = pT->isActive();
    if (pT->isFolder()) {
        itemDescription = (itemActive ? descActiveFolder : descInactiveFolder);
        if (!pT->ancestorsActive()) {
            // It is okay to test for being inactiveed by an ancestor before testing whether
            // the item is a package/module as those are not expected to have any parents to
            // be inactive.
            if (itemActive) {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                itemDescription = descInactiveParent.arg(itemDescription);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
            }
        } else if (!pT->mPackageName.isEmpty()) {
            // Has a package name - is a module or package master folder
            if (itemActive) {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-brown.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-brown-locked.png")), QIcon::Normal, QIcon::Off);
            }
        } else if (!pT->getParent() || !pT->getParent()->mPackageName.isEmpty()) {
            // Does not have a parent or the parent has a package name - is a toolbar
            if (itemActive) {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-yellow.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-yellow-locked.png")), QIcon::Normal, QIcon::Off);
            }
        } else {
            // Must be a menu
            if (itemActive) {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-cyan.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-cyan-locked.png")), QIcon::Normal, QIcon::Off);
            }
        }
    } else {
        if (itemActive) {
            itemDescription = descActive;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                itemDescription = descInactiveParent.arg(itemDescription);
            }
        } else {
            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descInactive;
        }
    }

    if (!pT->state()) {
        pT->setIsActive(false);
        showError(tr(R"(<p><b>Unable to activate "<tt>%1</tt>"; %2.</b></p>
                     <p><i>You will need to reactivate this after the problem has been corrected.</i></p>)")
                          .arg(pT->getName().toHtmlEscaped(), pT->getError()));
        icon.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
        itemDescription = descError;
    }
    pItem->setIcon(0, icon);
    pItem->setText(0, pT->getName());
    pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);

    mpHost->getActionUnit()->updateAllToolbars();
    if (pItem->childCount() > 0) {
        children_icon_action(pItem);
    }

    if (mpUndoStack && oldState != newState) {
        auto* qtCmd = new EditorToggleActiveCommand(EditorViewType::cmActionView, pT->getID(), oldState, newState, pT->getName(), mpHost);
        mpUndoStack->pushCommand(qtCmd);
    }
}

void dlgTriggerEditor::children_icon_action(QTreeWidgetItem* pWidgetItemParent)
{
    for (int i = 0; i < pWidgetItemParent->childCount(); i++) {
        QTreeWidgetItem* pItem = pWidgetItemParent->child(i);
        TAction* pT = mpHost->getActionUnit()->getAction(pItem->data(0, Qt::UserRole).toInt());
        if (!pT) {
            return;
        }

        QIcon icon;
        QString itemDescription;
        const bool itemActive = pT->isActive();
        if (pItem->childCount() > 0) {
            children_icon_action(pItem);
        }
        if (pT->state()) {
            if (pT->isFolder()) {
                itemDescription = (itemActive ? descActiveFolder : descInactiveFolder);
                if (!pT->mPackageName.isEmpty()) {
                    // Has a package name - is a module or package master
                    // folder
                    if (pT->isActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-brown.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-brown-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                } else if (!pT->ancestorsActive()) {
                    if (pT->isActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                } else if (!pT->getParent() || !pT->getParent()->mPackageName.isEmpty()) {
                    // Does not have a parent or the parent has a package name
                    // so the parent is a module or package master folder - so
                    // this is a toolbar:
                    if (pT->isActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-yellow.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-yellow-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                } else {
                    // Must be a menu
                    if (pT->isActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-cyan.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-cyan-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else {
                if (pT->isActive()) {
                    itemDescription = descActive;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }

                } else {
                    itemDescription = descInactive;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            }
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(pT->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}


void dlgTriggerEditor::activeToggle_key()
{
    QTreeWidgetItem* pItem = treeWidget_keys->currentItem();
    if (!pItem) {
        return;
    }
    QIcon icon;
    QString itemDescription;

    TKey* pT = mpHost->getKeyUnit()->getKey(pItem->data(0, Qt::UserRole).toInt());
    if (!pT) {
        return;
    }

    // Capture old state for undo
    bool oldState = pT->shouldBeActive();
    pT->setIsActive(!pT->shouldBeActive());
    // Capture new state after toggle
    bool newState = pT->isActive();

    if (pT->isFolder()) {
        if (pT->isActive()) {
            itemDescription = descActiveFolder;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-pink.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                itemDescription = descInactiveParent.arg(itemDescription);
            }
        } else {
            itemDescription = descInactiveFolder;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-pink-locked.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
            }
        }
    } else {
        if (pT->isActive()) {
            itemDescription = descActive;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                itemDescription = descInactiveParent.arg(itemDescription);
            }
        } else {
            itemDescription = descInactive;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
            }
        }
    }

    if (pT->state()) {
        pItem->setIcon(0, icon);
        pItem->setText(0, pT->getName());
    } else {
        QIcon iconError;
        iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
        itemDescription = descError;
        pItem->setIcon(0, iconError);
    }
    pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);

    if (!pT->state()) {
        pT->setIsActive(false);
        showError(tr(R"(<p><b>Unable to activate "<tt>%1</tt>"; %2.</b></p>
                     <p><i>You will need to reactivate this after the problem has been corrected.</i></p>)")
                          .arg(pT->getName().toHtmlEscaped(), pT->getError()));
        icon.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
        itemDescription = descError;
    }

    if (pItem->childCount() > 0) {
        children_icon_key(pItem);
    }

    if (mpUndoStack && oldState != newState) {
        auto* qtCmd = new EditorToggleActiveCommand(EditorViewType::cmKeysView, pT->getID(), oldState, newState, pT->getName(), mpHost);
        mpUndoStack->pushCommand(qtCmd);
    }
}

void dlgTriggerEditor::children_icon_key(QTreeWidgetItem* pWidgetItemParent)
{
    for (int i = 0; i < pWidgetItemParent->childCount(); i++) {
        QTreeWidgetItem* pItem = pWidgetItemParent->child(i);
        TKey* pT = mpHost->getKeyUnit()->getKey(pItem->data(0, Qt::UserRole).toInt());
        if (!pT) {
            return;
        }

        QIcon icon;
        QString itemDescription;
        if (pItem->childCount() > 0) {
            children_icon_key(pItem);
        }
        if (pT->state()) {
            if (pT->isFolder()) {
                if (pT->isActive()) {
                    itemDescription = descActiveFolder;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-pink.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactiveFolder;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-pink-locked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else {
                if (pT->isActive()) {
                    itemDescription = descActive;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }

                } else {
                    itemDescription = descInactive;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            }
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(pT->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}


void dlgTriggerEditor::addTrigger(bool isFolder)
{
    saveTrigger();

    QString name = isFolder ? tr("New trigger group") : tr("New trigger");
    QStringList nameList{name};
    const QStringList patterns;
    QList<int> const patternKinds;
    const QString script = "";

    QTreeWidgetItem* pParentItem = treeWidget_triggers->currentItem();
    QTreeWidgetItem* pNewItem = nullptr;
    TTrigger* pNewTrigger = nullptr;

    if (pParentItem) {
        const int parentID = pParentItem->data(0, Qt::UserRole).toInt();
        TTrigger* pParentTrigger = mpHost->getTriggerUnit()->getTrigger(parentID);

        if (pParentTrigger) {
            // insert new items as siblings unless the parent is a folder
            if (pParentTrigger->isFolder()) {
                pNewTrigger = new TTrigger(pParentTrigger, mpHost);
                pNewItem = new QTreeWidgetItem(pParentItem, nameList);
                pParentItem->insertChild(0, pNewItem);
            } else if (pParentTrigger->getParent() && pParentItem->parent()) {
                pNewTrigger = new TTrigger(pParentTrigger->getParent(), mpHost);
                pNewItem = new QTreeWidgetItem(pParentItem->parent(), nameList);
                pParentItem->parent()->insertChild(0, pNewItem);
            }
        }
    }

    if (!pNewTrigger) {
        // Fallback to insert a new root item
        pNewTrigger = new TTrigger(name, patterns, patternKinds, false, mpHost);
        pNewItem = new QTreeWidgetItem(mpTriggerBaseItem, nameList);
        treeWidget_triggers->insertTopLevelItem(0, pNewItem);
    }


    if (!pNewTrigger) {
        return;
    }

    // Initialize logic object properties
    pNewTrigger->setName(name);
    pNewTrigger->setRegexCodeList(patterns, patternKinds, false);
    pNewTrigger->setScript(script);
    pNewTrigger->setIsFolder(isFolder);
    pNewTrigger->setIsActive(false);
    pNewTrigger->setShouldBeActive(true);
    pNewTrigger->setIsMultiline(false);
    pNewTrigger->mStayOpen = 0;
    pNewTrigger->setConditionLineDelta(0);
    pNewTrigger->registerTrigger();

    // Initialize tree item properties
    pNewItem->setData(0, Qt::UserRole, pNewTrigger->getID());
    pNewItem->setIcon(0, QIcon(QPixmap(isFolder ? qsl(":/icons/folder-red.png") : qsl(":/icons/document-save-as.png"))));
    pNewItem->setData(0, Qt::AccessibleDescriptionRole, isFolder ? descNewFolder : descNewItem);

    // Expand parent if applicable
    if (pParentItem) {
        pParentItem->setExpanded(true);
    }

    // Reset UI
    // Block property-save slots so the widget changes below don't fire write-backs
    // into the previously selected trigger (mpCurrentTriggerItem still points at
    // it). slot_triggerSelected() clears the flag once the new item is loaded.
    mBlockPropertySave = true;
    mpTriggersMainArea->lineEdit_trigger_name->clear();
    mpTriggersMainArea->label_idNumber->clear();
    mpTriggersMainArea->checkBox_perlSlashGOption->setChecked(false);
    clearDocument(mpSourceEditorEdbee); // New Trigger
    mpTriggersMainArea->lineEdit_trigger_command->clear();
    mpTriggersMainArea->checkBox_filterTrigger->setChecked(false);
    mpTriggersMainArea->spinBox_stayOpen->setValue(0);
    mpTriggersMainArea->spinBox_lineMargin->setValue(-1);
    mpTriggersMainArea->pushButtonFgColor->setChecked(false);
    mpTriggersMainArea->pushButtonBgColor->setChecked(false);
    mpTriggersMainArea->groupBox_triggerColorizer->setChecked(false);

    // Finalize selection
    mpCurrentTriggerItem = pNewItem;
    treeWidget_triggers->setCurrentItem(pNewItem);
    slot_triggerSelected(treeWidget_triggers->currentItem());

    QTreeWidgetItem* actualParent = pNewItem->parent();
    int parentID = (actualParent && actualParent != mpTriggerBaseItem) ? actualParent->data(0, Qt::UserRole).toInt() : -1;

    int positionInParent = 0;
    if (actualParent) {
        positionInParent = actualParent->indexOfChild(pNewItem);
    } else {
        positionInParent = treeWidget_triggers->indexOfTopLevelItem(pNewItem);
    }

    auto* qtCmd = new EditorAddItemCommand(EditorViewType::cmTriggerView, pNewTrigger->getID(), parentID, positionInParent, isFolder, name, mpHost);
    mpUndoStack->pushCommand(qtCmd);

    // Note: Subsequent modify commands will automatically merge with this Add command
    // via EditorAddItemCommand::mergeWith(), grouping them into one undo operation.
}


void dlgTriggerEditor::addTimer(bool isFolder)
{
    saveTimer();

    QString name = isFolder ? tr("New timer group") : tr("New timer");
    QStringList nameList = {name};
    const QString command = "";
    const QTime time;
    const QString script = "";

    QTreeWidgetItem* pParentItem = treeWidget_timers->currentItem();
    QTreeWidgetItem* pNewItem = nullptr;
    TTimer* pNewTimer = nullptr;

    if (pParentItem) {
        const int parentID = pParentItem->data(0, Qt::UserRole).toInt();
        TTimer* pParentTrigger = mpHost->getTimerUnit()->getTimer(parentID);

        if (pParentTrigger) {
            // insert new items as siblings unless the parent is a folder
            if (pParentTrigger->isFolder()) {
                pNewTimer = new TTimer(pParentTrigger, mpHost);
                pNewItem = new QTreeWidgetItem(pParentItem, nameList);
                pParentItem->insertChild(0, pNewItem);
            } else if (pParentTrigger->getParent() && pParentItem->parent()) {
                pNewTimer = new TTimer(pParentTrigger->getParent(), mpHost);
                pNewItem = new QTreeWidgetItem(pParentItem->parent(), nameList);
                pParentItem->parent()->insertChild(0, pNewItem);
            }
        }
    }

    if (!pNewTimer) {
        // Fallback to insert a new root item
        pNewTimer = new TTimer(name, time, mpHost);
        pNewItem = new QTreeWidgetItem(mpTimerBaseItem, nameList);
        treeWidget_timers->insertTopLevelItem(0, pNewItem);
    }

    if (!pNewTimer) {
        return;
    }

    // Initialize logic object properties
    pNewTimer->setName(name);
    pNewTimer->setCommand(command);
    pNewTimer->setScript(script);
    pNewTimer->setIsFolder(isFolder);
    pNewTimer->setIsActive(false);
    mpHost->getTimerUnit()->registerTimer(pNewTimer);

    // Initialize tree item properties
    pNewItem->setData(0, Qt::UserRole, pNewTimer->getID());
    pNewItem->setIcon(0, QIcon(QPixmap(isFolder ? qsl(":/icons/folder-red.png") : qsl(":/icons/document-save-as.png"))));
    pNewItem->setData(0, Qt::AccessibleDescriptionRole, isFolder ? descNewFolder : descNewItem);

    // Expand parent if applicable
    if (pParentItem) {
        pParentItem->setExpanded(true);
    }

    // Reset UI
    //FIXME
    //mpOptionsAreaTriggers->lineEdit_trigger_name->clear();
    mpTimersMainArea->lineEdit_timer_command->clear();
    clearDocument(mpSourceEditorEdbee); // New Timer

    // Finalize selection
    mpCurrentTimerItem = pNewItem;
    treeWidget_timers->setCurrentItem(pNewItem);
    slot_timerSelected(treeWidget_timers->currentItem());

    QTreeWidgetItem* actualParent = pNewItem->parent();
    int parentID = (actualParent && actualParent != mpTimerBaseItem) ? actualParent->data(0, Qt::UserRole).toInt() : -1;

    int positionInParent = 0;
    if (actualParent) {
        positionInParent = actualParent->indexOfChild(pNewItem);
    } else {
        positionInParent = treeWidget_timers->indexOfTopLevelItem(pNewItem);
    }

    auto* qtCmd = new EditorAddItemCommand(EditorViewType::cmTimerView, pNewTimer->getID(), parentID, positionInParent, isFolder, name, mpHost);
    mpUndoStack->pushCommand(qtCmd);

    // Note: Subsequent modify commands will automatically merge with this Add command
    // via EditorAddItemCommand::mergeWith(), grouping them into one undo operation.
}

void dlgTriggerEditor::addVar(bool isFolder)
{
    saveVar();
    mpVarsMainArea->comboBox_variable_key_type->setCurrentIndex(0);
    // the variable left behind may have been one whose key type is not the
    // user's to choose, which leaves the combobox disabled
    mpVarsMainArea->comboBox_variable_key_type->setEnabled(true);
    if (isFolder) {
        // in lieu of readonly
        mpSourceEditorEdbee->setEnabled(false);
        mpVarsMainArea->comboBox_variable_value_type->setDisabled(true);
        mpVarsMainArea->comboBox_variable_value_type->setCurrentIndex(4);
        clearDocument(mpSourceEditorEdbee, QLatin1String("NewTable"));
    } else {
        // in lieu of readonly
        mpSourceEditorEdbee->setEnabled(true);
        mpVarsMainArea->comboBox_variable_value_type->setDisabled(false);
        mpVarsMainArea->comboBox_variable_value_type->setCurrentIndex(0);
    }

    LuaInterface* lI = mpHost->getLuaInterface();
    VarUnit* vu = lI->getVarUnit();

    QStringList nameList = {QString(isFolder ? tr("table_variable") : tr("variable_name"))};
    mpVarsMainArea->lineEdit_var_name->setText(nameList[0]);
    QTreeWidgetItem* pParentItem = nullptr;
    QTreeWidgetItem* pNewItem;
    QTreeWidgetItem* cItem = treeWidget_variables->currentItem();
    if (cItem) {
        TVar* cVar = vu->getWVar(cItem);
        if (cVar && cVar->getValueType() == LUA_TTABLE) {
            pParentItem = cItem;
        } else {
            pParentItem = cItem->parent();
        }
    }

    auto newVar = new TVar();
    if (pParentItem) {
        //we're nested under something, or going to be.  This HAS to be a table
        TVar* parent = vu->getWVar(pParentItem);
        if (parent && parent->getValueType() == LUA_TTABLE) {
            //create it under the parent
            pNewItem = new QTreeWidgetItem(pParentItem, nameList);
            newVar->setParent(parent);
        } else {
            pNewItem = new QTreeWidgetItem(mpVarBaseItem, nameList);
            newVar->setParent(vu->getBase());
        }
    } else {
        pNewItem = new QTreeWidgetItem(mpVarBaseItem, nameList);
        newVar->setParent(vu->getBase());
    }

    if (isFolder) {
        newVar->setValue(QString(), LUA_TTABLE);
    } else {
        newVar->setValueType(LUA_TNONE);
    }
    vu->addTempVar(pNewItem, newVar);
    pNewItem->setFlags(pNewItem->flags() & ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled));

    // Finalize selection
    mpCurrentVarItem = pNewItem;
    treeWidget_variables->setCurrentItem(pNewItem);
    slot_variableSelected(treeWidget_variables->currentItem());
    saveVar();
}

void dlgTriggerEditor::addKey(bool isFolder)
{
    saveKey();

    QString name = isFolder ? tr("New key group") : tr("New key");
    QStringList nameList = {name};
    const QString script = "";

    QTreeWidgetItem* pParentItem = treeWidget_keys->currentItem();
    QTreeWidgetItem* pNewItem = nullptr;
    TKey* pNewKey = nullptr;

    if (pParentItem) {
        const int parentID = pParentItem->data(0, Qt::UserRole).toInt();
        TKey* pParentTrigger = mpHost->getKeyUnit()->getKey(parentID);

        if (pParentTrigger) {
            // insert new items as siblings unless the parent is a folder
            if (pParentTrigger->isFolder()) {
                pNewKey = new TKey(pParentTrigger, mpHost);
                pNewItem = new QTreeWidgetItem(pParentItem, nameList);
                pParentItem->insertChild(0, pNewItem);
            } else if (pParentTrigger->getParent() && pParentItem->parent()) {
                pNewKey = new TKey(pParentTrigger->getParent(), mpHost);
                pNewItem = new QTreeWidgetItem(pParentItem->parent(), nameList);
                pParentItem->parent()->insertChild(0, pNewItem);
            }
        }
    }

    if (!pNewKey) {
        // Fallback to insert a new root item
        pNewKey = new TKey(name, mpHost);
        pNewItem = new QTreeWidgetItem(mpKeyBaseItem, nameList);
        treeWidget_keys->insertTopLevelItem(0, pNewItem);
    }

    if (!pNewKey) {
        return;
    }

    // Initialize logic object properties
    pNewKey->setName(name);
    pNewKey->setKeyCode(Qt::Key_unknown);
    pNewKey->setKeyModifiers(Qt::NoModifier);
    pNewKey->setScript(script);
    pNewKey->setIsFolder(isFolder);
    pNewKey->setIsActive(false);
    pNewKey->setShouldBeActive(true);
    pNewKey->registerKey();

    // Initialize tree item properties
    pNewItem->setData(0, Qt::UserRole, pNewKey->getID());
    pNewItem->setIcon(0, QIcon(QPixmap(isFolder ? qsl(":/icons/folder-red.png") : qsl(":/icons/document-save-as.png"))));
    pNewItem->setData(0, Qt::AccessibleDescriptionRole, isFolder ? descNewFolder : descNewItem);

    // Expand parent if applicable
    if (pParentItem) {
        pParentItem->setExpanded(true);
    }

    // Reset UI
    mpKeysMainArea->lineEdit_key_command->clear();
    mpKeysMainArea->lineEdit_key_binding->setText("no key chosen");
    clearDocument(mpSourceEditorEdbee); // New Key

    // Finalize selection
    mpCurrentKeyItem = pNewItem;
    treeWidget_keys->setCurrentItem(pNewItem);
    slot_keySelected(treeWidget_keys->currentItem());

    QTreeWidgetItem* actualParent = pNewItem->parent();
    int parentID = (actualParent && actualParent != mpKeyBaseItem) ? actualParent->data(0, Qt::UserRole).toInt() : -1;

    int positionInParent = 0;
    if (actualParent) {
        positionInParent = actualParent->indexOfChild(pNewItem);
    } else {
        positionInParent = treeWidget_keys->indexOfTopLevelItem(pNewItem);
    }

    auto* qtCmd = new EditorAddItemCommand(EditorViewType::cmKeysView, pNewKey->getID(), parentID, positionInParent, isFolder, name, mpHost);
    mpUndoStack->pushCommand(qtCmd);

    // Note: Subsequent modify commands will automatically merge with this Add command
    // via EditorAddItemCommand::mergeWith(), grouping them into one undo operation.
}


void dlgTriggerEditor::addAlias(bool isFolder)
{
    saveAlias();

    QString name = isFolder ? tr("New alias group") : tr("New alias");
    QStringList nameList = {name};
    const QString regex = "";
    const QString command = "";
    const QString script = "";

    QTreeWidgetItem* pParentItem = treeWidget_aliases->currentItem();
    QTreeWidgetItem* pNewItem = nullptr;
    TAlias* pNewAlias = nullptr;

    if (pParentItem) {
        const int parentID = pParentItem->data(0, Qt::UserRole).toInt();
        TAlias* pParentTrigger = mpHost->getAliasUnit()->getAlias(parentID);

        if (pParentTrigger) {
            // insert new items as siblings unless the parent is a folder
            if (pParentTrigger->isFolder()) {
                pNewAlias = new TAlias(pParentTrigger, mpHost);
                pNewItem = new QTreeWidgetItem(pParentItem, nameList);
                pParentItem->insertChild(0, pNewItem);
            } else if (pParentTrigger->getParent() && pParentItem->parent()) {
                pNewAlias = new TAlias(pParentTrigger->getParent(), mpHost);
                pNewItem = new QTreeWidgetItem(pParentItem->parent(), nameList);
                pParentItem->parent()->insertChild(0, pNewItem);
            }
        }
    }

    if (!pNewAlias) {
        //insert a new root item
        pNewAlias = new TAlias(name, mpHost);
        pNewAlias->setRegexCode(regex); // Empty regex will always succeed to compile
        pNewItem = new QTreeWidgetItem(mpAliasBaseItem, nameList);
        treeWidget_aliases->insertTopLevelItem(0, pNewItem);
    }

    if (!pNewAlias) {
        return;
    }

    // Initialize logic object properties
    pNewAlias->setName(name);
    pNewAlias->setCommand(command);
    pNewAlias->setRegexCode(regex); // Empty regex will always succeed to compile
    pNewAlias->setScript(script);
    pNewAlias->setIsFolder(isFolder);
    pNewAlias->setIsActive(false);
    pNewAlias->setShouldBeActive(true);
    pNewAlias->registerAlias();

    // Initialize tree item properties
    pNewItem->setData(0, Qt::UserRole, pNewAlias->getID());
    pNewItem->setIcon(0, QIcon(QPixmap(isFolder ? qsl(":/icons/folder-red.png") : qsl(":/icons/document-save-as.png"))));
    pNewItem->setData(0, Qt::AccessibleDescriptionRole, isFolder ? descNewFolder : descNewItem);

    // Expand parent if applicable
    if (pParentItem) {
        pParentItem->setExpanded(true);
    }

    // Reset UI
    mpAliasMainArea->lineEdit_alias_name->clear();
    mpAliasMainArea->label_idNumber->clear();
    mpAliasMainArea->lineEdit_alias_pattern->clear();
    mpAliasMainArea->lineEdit_alias_command->clear();
    clearDocument(mpSourceEditorEdbee); // New Alias
    mpAliasMainArea->lineEdit_alias_name->setText(name);
    mpAliasMainArea->label_idNumber->setText(QString::number(pNewAlias->getID()));

    // Finalize selection
    mpCurrentAliasItem = pNewItem;
    treeWidget_aliases->setCurrentItem(pNewItem);
    slot_aliasSelected(treeWidget_aliases->currentItem());

    QTreeWidgetItem* actualParent = pNewItem->parent();
    int parentID = (actualParent && actualParent != mpAliasBaseItem) ? actualParent->data(0, Qt::UserRole).toInt() : -1;

    int positionInParent = 0;
    if (actualParent) {
        positionInParent = actualParent->indexOfChild(pNewItem);
    } else {
        positionInParent = treeWidget_aliases->indexOfTopLevelItem(pNewItem);
    }

    auto* qtCmd = new EditorAddItemCommand(EditorViewType::cmAliasView, pNewAlias->getID(), parentID, positionInParent, isFolder, name, mpHost);
    mpUndoStack->pushCommand(qtCmd);

    // Note: Subsequent modify commands will automatically merge with this Add command
    // via EditorAddItemCommand::mergeWith(), grouping them into one undo operation.
}

void dlgTriggerEditor::addAction(bool isFolder)
{
    saveAction();

    QString name = isFolder ? tr("New menu") : tr("New button");
    QStringList nameList = {name};

    QTreeWidgetItem* pParentItem = treeWidget_actions->currentItem();
    QTreeWidgetItem* pNewItem = nullptr;
    QPointer<TAction> pNewAction = nullptr;

    if (pParentItem) {
        const int parentID = pParentItem->data(0, Qt::UserRole).toInt();
        TAction* pParentAction = mpHost->getActionUnit()->getAction(parentID);

        if (pParentAction) {
            // insert new items as siblings unless the parent is a folder
            if (pParentAction->isFolder()) {
                pNewAction = new TAction(pParentAction, mpHost);
                pNewItem = new QTreeWidgetItem(pParentItem, nameList);
                pParentItem->insertChild(0, pNewItem);
            } else if (pParentAction->getParent() && pParentItem->parent()) {
                pNewAction = new TAction(pParentAction->getParent(), mpHost);
                pNewItem = new QTreeWidgetItem(pParentItem->parent(), nameList);
                pParentItem->parent()->insertChild(0, pNewItem);
            }
        }
    }
    // Otherwise: insert a new root item
    // CHECKME: doesn't this HAVE to be a toolbar - surely buttons MUST be in a container?
    if (!pNewAction) {
        name = isFolder ? tr("New toolbar") : tr("New button");
        pNewAction = new TAction(name, mpHost);
        pNewAction->setCommandButtonUp(QString());
        QStringList nl;
        nl << name;
        pNewItem = new QTreeWidgetItem(mpActionBaseItem, nl);
        treeWidget_actions->insertTopLevelItem(0, pNewItem);
    }

    // Initialize logic object properties
    pNewAction->setName(name);
    pNewAction->setCommandButtonUp(QString());
    pNewAction->setCommandButtonDown(QString());
    pNewAction->setIsPushDownButton(false);
    pNewAction->mLocation = 1;
    pNewAction->mOrientation = 1;
    pNewAction->setScript(QString());
    pNewAction->setIsFolder(isFolder);
    pNewAction->setIsActive(false);
    pNewAction->registerAction();

    // Initialize tree item properties
    pNewItem->setData(0, Qt::UserRole, pNewAction->getID());
    pNewItem->setIcon(0, QIcon(QPixmap(isFolder ? qsl(":/icons/folder-red.png") : qsl(":/icons/document-save-as.png"))));
    pNewItem->setData(0, Qt::AccessibleDescriptionRole, isFolder ? descNewFolder : descNewItem);

    // Expand parent if applicable
    if (pParentItem) {
        pParentItem->setExpanded(true);
    }

    // Reset UI
    // Block property-save slots so the widget changes below don't fire write-backs
    // into the previously selected action (mpCurrentActionItem still points at it).
    // Unchecking checkBox_action_button_isPushDown otherwise calls
    // slot_saveProperty_ActionIsPushDown on the old action. slot_actionSelected()
    // clears the flag once the new item is loaded.
    mBlockPropertySave = true;
    mpActionsMainArea->lineEdit_action_icon->clear();
    mpActionsMainArea->checkBox_action_button_isPushDown->setChecked(false);
    clearDocument(mpSourceEditorEdbee); // New Action

    // This prevents reloading a Floating toolbar when an empty action is added.
    // After the action is saved it may trigger the rebuild.
    pNewAction->setDataSaved();

    mpHost->getActionUnit()->updateAllToolbars();

    // Finalize selection
    mpCurrentActionItem = pNewItem;
    treeWidget_actions->setCurrentItem(pNewItem);
    slot_actionSelected(treeWidget_actions->currentItem());

    QTreeWidgetItem* actualParent = pNewItem->parent();
    int parentID = (actualParent && actualParent != mpActionBaseItem) ? actualParent->data(0, Qt::UserRole).toInt() : -1;

    int positionInParent = 0;
    if (actualParent) {
        positionInParent = actualParent->indexOfChild(pNewItem);
    } else {
        positionInParent = treeWidget_actions->indexOfTopLevelItem(pNewItem);
    }

    auto* qtCmd = new EditorAddItemCommand(EditorViewType::cmActionView, pNewAction->getID(), parentID, positionInParent, isFolder, name, mpHost);
    mpUndoStack->pushCommand(qtCmd);

    // Note: Subsequent modify commands will automatically merge with this Add command
    // via EditorAddItemCommand::mergeWith(), grouping them into one undo operation.
}


void dlgTriggerEditor::addScript(bool isFolder)
{
    saveScript();

    QString name = isFolder ? tr("New script group") : tr("New script");
    QStringList nameList = {name};
    const QString script;

    QTreeWidgetItem* pParentItem = treeWidget_scripts->currentItem();
    QTreeWidgetItem* pNewItem = nullptr;
    TScript* pNewScript = nullptr;

    if (pParentItem) {
        const int parentID = pParentItem->data(0, Qt::UserRole).toInt();
        TScript* pParentTrigger = mpHost->getScriptUnit()->getScript(parentID);

        if (pParentTrigger) {
            // insert new items as siblings unless the parent is a folder
            if (pParentTrigger->isFolder()) {
                pNewScript = new TScript(pParentTrigger, mpHost);
                pNewItem = new QTreeWidgetItem(pParentItem, nameList);
                pParentItem->insertChild(0, pNewItem);
            } else if (pParentTrigger->getParent() && pParentItem->parent()) {
                pNewScript = new TScript(pParentTrigger->getParent(), mpHost);
                pNewItem = new QTreeWidgetItem(pParentItem->parent(), nameList);
                pParentItem->parent()->insertChild(0, pNewItem);
            }
        }
    }

    if (!pNewScript) {
        // Fallback to insert a new root item
        pNewScript = new TScript(name, mpHost);
        pNewItem = new QTreeWidgetItem(mpScriptsBaseItem, nameList);
        treeWidget_scripts->insertTopLevelItem(0, pNewItem);
    }

    if (!pNewScript) {
        return;
    }

    // Initialize logic object properties
    pNewScript->setName(name);
    pNewScript->setScript(script);
    pNewScript->setIsFolder(isFolder);
    pNewScript->setIsActive(false);
    pNewScript->setShouldBeActive(true);
    pNewScript->registerScript();

    // Initialize tree item properties
    pNewItem->setData(0, Qt::UserRole, pNewScript->getID());
    pNewItem->setIcon(0, QIcon(QPixmap(isFolder ? qsl(":/icons/folder-red.png") : qsl(":/icons/document-save-as.png"))));
    pNewItem->setData(0, Qt::AccessibleDescriptionRole, isFolder ? descNewFolder : descNewItem);

    // Expand parent if applicable
    if (pParentItem) {
        pParentItem->setExpanded(true);
    }

    // Reset UI
    mpScriptsMainArea->lineEdit_script_name->clear();
    mpScriptsMainArea->label_idNumber->clear();
    mpScriptsMainArea->lineEdit_script_event_handler_entry->clear();
    clearDocument(mpSourceEditorEdbee, script);

    // Finalize selection
    mpCurrentScriptItem = pNewItem;
    treeWidget_scripts->setCurrentItem(pNewItem);
    slot_scriptsSelected(treeWidget_scripts->currentItem());

    QTreeWidgetItem* actualParent = pNewItem->parent();
    int parentID = (actualParent && actualParent != mpScriptsBaseItem) ? actualParent->data(0, Qt::UserRole).toInt() : -1;

    int positionInParent = 0;
    if (actualParent) {
        positionInParent = actualParent->indexOfChild(pNewItem);
    } else {
        positionInParent = treeWidget_scripts->indexOfTopLevelItem(pNewItem);
    }

    auto* qtCmd = new EditorAddItemCommand(EditorViewType::cmScriptView, pNewScript->getID(), parentID, positionInParent, isFolder, name, mpHost);
    mpUndoStack->pushCommand(qtCmd);

    // Note: Subsequent modify commands will automatically merge with this Add command
    // via EditorAddItemCommand::mergeWith(), grouping them into one undo operation.
}

void dlgTriggerEditor::selectTriggerByID(int id)
{
    slot_showTriggers();
    QTreeWidgetItemIterator it(treeWidget_triggers);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toInt() == id) {
            slot_triggerSelected((*it));
            treeWidget_triggers->clearSelection();
            treeWidget_triggers->setCurrentItem((*it), 0);
            treeWidget_triggers->scrollToItem((*it));
            mpCurrentTriggerItem = (*it);
            return;
        }
        ++it;
    }
}

void dlgTriggerEditor::selectTimerByID(int id)
{
    slot_showTimers();
    QTreeWidgetItemIterator it(treeWidget_timers);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toInt() == id) {
            slot_timerSelected((*it));
            treeWidget_timers->clearSelection();
            treeWidget_timers->setCurrentItem((*it), 0);
            treeWidget_timers->scrollToItem((*it));
            mpCurrentTimerItem = (*it);
            return;
        }
        ++it;
    }
}

void dlgTriggerEditor::selectAliasByID(int id)
{
    slot_showAliases();
    QTreeWidgetItemIterator it(treeWidget_aliases);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toInt() == id) {
            slot_aliasSelected((*it));
            treeWidget_aliases->clearSelection();
            treeWidget_aliases->setCurrentItem((*it), 0);
            treeWidget_aliases->scrollToItem((*it));
            mpCurrentAliasItem = (*it);
            return;
        }
        ++it;
    }
}

void dlgTriggerEditor::selectScriptByID(int id)
{
    slot_showScripts();
    QTreeWidgetItemIterator it(treeWidget_scripts);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toInt() == id) {
            slot_scriptsSelected((*it));
            treeWidget_scripts->clearSelection();
            treeWidget_scripts->setCurrentItem((*it), 0);
            treeWidget_scripts->scrollToItem((*it));
            mpCurrentScriptItem = (*it);
            return;
        }
        ++it;
    }
}

void dlgTriggerEditor::selectActionByID(int id)
{
    slot_showActions();
    QTreeWidgetItemIterator it(treeWidget_actions);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toInt() == id) {
            slot_actionSelected((*it));
            treeWidget_actions->clearSelection();
            treeWidget_actions->setCurrentItem((*it), 0);
            treeWidget_actions->scrollToItem((*it));
            mpCurrentActionItem = (*it);
            return;
        }
        ++it;
    }
}

void dlgTriggerEditor::selectKeyByID(int id)
{
    slot_showKeys();
    QTreeWidgetItemIterator it(treeWidget_keys);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toInt() == id) {
            slot_keySelected((*it));
            treeWidget_keys->clearSelection();
            treeWidget_keys->setCurrentItem((*it), 0);
            treeWidget_keys->scrollToItem((*it));
            mpCurrentKeyItem = (*it);
            return;
        }
        ++it;
    }
}

TTrigger* dlgTriggerEditor::getTriggerFromTreeItem(QTreeWidgetItem* item)
{
    if (!item || !item->parent()) {
        return nullptr;
    }

    const int triggerID = item->data(0, Qt::UserRole).toInt();
    return mpHost->getTriggerUnit()->getTrigger(triggerID);
}

TAlias* dlgTriggerEditor::getAliasFromTreeItem(QTreeWidgetItem* item)
{
    if (!item || !item->parent()) {
        return nullptr;
    }

    const int aliasID = item->data(0, Qt::UserRole).toInt();
    return mpHost->getAliasUnit()->getAlias(aliasID);
}

TScript* dlgTriggerEditor::getScriptFromTreeItem(QTreeWidgetItem* item)
{
    if (!item || !item->parent()) {
        return nullptr;
    }

    const int scriptID = item->data(0, Qt::UserRole).toInt();
    return mpHost->getScriptUnit()->getScript(scriptID);
}

TTimer* dlgTriggerEditor::getTimerFromTreeItem(QTreeWidgetItem* item)
{
    if (!item || !item->parent()) {
        return nullptr;
    }

    const int timerID = item->data(0, Qt::UserRole).toInt();
    return mpHost->getTimerUnit()->getTimer(timerID);
}

TKey* dlgTriggerEditor::getKeyFromTreeItem(QTreeWidgetItem* item)
{
    if (!item || !item->parent()) {
        return nullptr;
    }

    const int keyID = item->data(0, Qt::UserRole).toInt();
    return mpHost->getKeyUnit()->getKey(keyID);
}

TAction* dlgTriggerEditor::getActionFromTreeItem(QTreeWidgetItem* item)
{
    if (!item || !item->parent()) {
        return nullptr;
    }

    const int actionID = item->data(0, Qt::UserRole).toInt();
    return mpHost->getActionUnit()->getAction(actionID);
}

void dlgTriggerEditor::slot_itemEdited()
{
    QString packageName;
    switch (mCurrentView) {
    case EditorViewType::cmTriggerView: {
        if (auto trigger = getTriggerFromTreeItem(mpCurrentTriggerItem)) {
            packageName = trigger->packageName(trigger);
        }
        break;
    }
    case EditorViewType::cmAliasView: {
        if (auto alias = getAliasFromTreeItem(mpCurrentAliasItem)) {
            packageName = alias->packageName(alias);
        }
        break;
    }
    case EditorViewType::cmTimerView: {
        if (auto timer = getTimerFromTreeItem(mpCurrentTimerItem)) {
            packageName = timer->packageName(timer);
        }
        break;
    }
    case EditorViewType::cmScriptView: {
        if (auto script = getScriptFromTreeItem(mpCurrentScriptItem)) {
            packageName = script->packageName(script);
        }
        break;
    }
    case EditorViewType::cmActionView: {
        if (auto action = getActionFromTreeItem(mpCurrentActionItem)) {
            packageName = action->packageName(action);
        }
        break;
    }
    case EditorViewType::cmKeysView: {
        if (auto key = getKeyFromTreeItem(mpCurrentKeyItem)) {
            packageName = key->packageName(key);
        }
        break;
    }
    case EditorViewType::cmUnknownView:
        [[fallthrough]];
    case EditorViewType::cmVarsView:
        break;
    }

    showPackageWarning(packageName);
}

void dlgTriggerEditor::saveTrigger()
{
    QTreeWidgetItem* pItem = mpCurrentTriggerItem;
    if (!pItem) {
        return;
    }

    // Additional safety check: ensure the item's parent is still valid
    // and that the item is still part of the tree widget
    if (!pItem->parent() || pItem->treeWidget() != treeWidget_triggers) {
        return;
    }

    mpTriggersMainArea->trimName();
    const QString name = mpTriggersMainArea->lineEdit_trigger_name->text();
    const QString command = mpTriggersMainArea->lineEdit_trigger_command->text();
    QStringList patterns;
    QList<int> patternKinds;
    int validItems = 0;
    for (const auto* patternEdit : std::as_const(mTriggerPatternEdit)) {
        QString pattern = patternEdit->singleLineTextEdit_pattern->toPlainText();

        // Spaces in the pattern may be marked with middle dots, convert them back
        unmarkQString(&pattern);

        const int patternType = patternEdit->comboBox_patternType->currentIndex();
        if (pattern.isEmpty() && patternType != REGEX_PROMPT && patternType != REGEX_LINE_SPACER) {
            continue;
        }

        ++validItems;
        switch (patternType) {
        case 0:
            patternKinds << REGEX_SUBSTRING;
            break;
        case 1:
            patternKinds << REGEX_PERL;
            break;
        case 2:
            patternKinds << REGEX_BEGIN_OF_LINE_SUBSTRING;
            break;
        case 3:
            patternKinds << REGEX_EXACT_MATCH;
            break;
        case 4:
            patternKinds << REGEX_LUA_CODE;
            break;
        case 5:
            patternKinds << REGEX_LINE_SPACER;
            pattern = patternEdit->spinBox_lineSpacer->text();
            break;
        case 6:
            patternKinds << REGEX_COLOR_PATTERN;
            break;
        case 7:
            patternKinds << REGEX_PROMPT;
            break;
        }
        patterns << pattern;
    }
    const bool isMultiline = (mpTriggersMainArea->spinBox_lineMargin->value() > -1) && (validItems > 1);
    const QString script = mpSourceEditorEdbeeDocument->text();

    const int triggerID = pItem->data(0, Qt::UserRole).toInt();
    TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(triggerID);
    if (pT) {
        // Capture OLD state before modifications (for undo)
        QString oldStateXML = exportTriggerToXML(pT);

        pT->setName(name);
        pT->setCommand(command);
        pT->setRegexCodeList(patterns, patternKinds);

        pT->setScript(script);
        pT->setIsMultiline(isMultiline);
        pT->mPerlSlashGOption = mpTriggersMainArea->checkBox_perlSlashGOption->isChecked();
        pT->mFilterTrigger = mpTriggersMainArea->checkBox_filterTrigger->isChecked();
        if (mpTriggersMainArea->spinBox_lineMargin->value() >= 0) {
            pT->setConditionLineDelta(mpTriggersMainArea->spinBox_lineMargin->value());
        }
        pT->mStayOpen = mpTriggersMainArea->spinBox_stayOpen->value();
        pT->mSoundTrigger = mpTriggersMainArea->groupBox_soundTrigger->isChecked();
        pT->setSound(mpTriggersMainArea->lineEdit_soundFile->text());

        QColor fgColor(QColorConstants::Transparent);
        QColor bgColor(QColorConstants::Transparent);
        if (!mpTriggersMainArea->pushButtonFgColor->property(cButtonBaseColor).toString().isEmpty()) {
            fgColor = QColor(mpTriggersMainArea->pushButtonFgColor->property(cButtonBaseColor).toString());
        }
        pT->setColorizerFgColor(fgColor);
        if (!mpTriggersMainArea->pushButtonBgColor->property(cButtonBaseColor).toString().isEmpty()) {
            bgColor = QColor(mpTriggersMainArea->pushButtonBgColor->property(cButtonBaseColor).toString());
        }
        pT->setColorizerBgColor(bgColor);
        pT->setIsColorizerTrigger(mpTriggersMainArea->groupBox_triggerColorizer->isChecked());
        QIcon icon;
        QString itemDescription;
        if (pT->isFilterChain()) {
            if (pT->isActive()) {
                itemDescription = descActiveFilterChain;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/filter.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/filter-grey.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveParent.arg(itemDescription);
                }
            } else {
                itemDescription = descInactiveFilterChain;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/filter-locked.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/filter-grey-locked.png")), QIcon::Normal, QIcon::Off);
                }
            }
        } else if (pT->isFolder()) {
            if (!pT->mPackageName.isEmpty()) {
                if (pT->isActive()) {
                    itemDescription = descActiveFolder;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-brown.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-brown-locked.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveFolder;
                }
            } else if (pT->isActive()) {
                itemDescription = descActiveFolder;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-blue.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveParent.arg(itemDescription);
                }
            } else {
                itemDescription = descInactiveFolder;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-blue-locked.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
                }
            }
        } else {
            if (pT->isActive()) {
                itemDescription = descActive;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveParent.arg(itemDescription);
                }
            } else {
                itemDescription = descInactive;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
                }
            }
        }
        if (pT->state()) {
            clearEditorNotification();

            if (pT->checkIfNew()) {
                if (pT->isFolder()) {
                    if (pT->shouldBeActive()) {
                        itemDescription = descActiveFolder;
                        if (pT->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-blue.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        itemDescription = descInactiveFolder;
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-blue-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                } else {
                    if (pT->shouldBeActive()) {
                        itemDescription = descActive;
                        if (pT->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        itemDescription = descInactive;
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    }
                }
                pItem->setIcon(0, icon);
                pItem->setText(0, name);

                if (pT->shouldBeActive()) {
                    pT->setIsActive(true);
                }
                pT->unmarkAsNew();
            } else {
                pItem->setIcon(0, icon);
                pItem->setText(0, name);
            }
        } else {
            QIcon iconError;
            pItem->setText(0, name);
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            pT->setIsActive(false);
            showError(pT->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);

        // Capture NEW state after modifications (for redo)
        QString newStateXML = exportTriggerToXML(pT);

        // Only push undo command if something actually changed
        if (oldStateXML != newStateXML) {
            auto* qtCmd = new EditorModifyPropertyCommand(EditorViewType::cmTriggerView, triggerID, name, oldStateXML, newStateXML, mpHost);
            mpUndoStack->pushCommand(qtCmd);

            // Clear edbee undo stack after save to make Save a commit point
            if (mpTextUndoStack) {
                mpTextUndoStack->clear();
            }
        }
    }
}

void dlgTriggerEditor::saveTimer()
{
    QTreeWidgetItem* pItem = mpCurrentTimerItem;
    if (!pItem) {
        return;
    }

    // Ensure the item is still part of the tree widget
    if (pItem->treeWidget() != treeWidget_timers) {
        return;
    }

    mpTimersMainArea->trimName();
    const QString name = mpTimersMainArea->lineEdit_timer_name->text();
    const QString script = mpSourceEditorEdbeeDocument->text();


    const int timerID = pItem->data(0, Qt::UserRole).toInt();
    TTimer* pT = mpHost->getTimerUnit()->getTimer(timerID);
    if (pT) {
        // Capture OLD state before modifications (for undo)
        QString oldStateXML = exportTimerToXML(pT);

        pT->setName(name);
        const QString command = mpTimersMainArea->lineEdit_timer_command->text();
        const int hours = mpTimersMainArea->timeEdit_timer_hours->time().hour();
        const int minutes = mpTimersMainArea->timeEdit_timer_minutes->time().minute();
        const int secs = mpTimersMainArea->timeEdit_timer_seconds->time().second();
        const int msecs = mpTimersMainArea->timeEdit_timer_msecs->time().msec();
        const QTime time(hours, minutes, secs, msecs);
        pT->setTime(time);
        pT->setCommand(command);
        pT->setName(name);
        pT->setScript(script);

        QIcon icon;
        QString itemDescription;
        if (pT->isFolder()) {
            if (!pT->mPackageName.isEmpty()) {
                if (pT->isActive()) {
                    itemDescription = descActiveFolder;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-brown.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-brown-locked.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveFolder;
                }
            } else {
                if (pT->shouldBeActive()) {
                    itemDescription = descActiveFolder;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-green.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactiveFolder;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-green-locked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            }
        } else if (pT->isOffsetTimer()) {
            if (pT->shouldBeActive()) {
                itemDescription = descActiveOffsetTimer;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-on.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-on-grey.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveParent.arg(itemDescription);
                }
            } else {
                itemDescription = descInactiveOffsetTimer;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-off.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-off-grey.png")), QIcon::Normal, QIcon::Off);
                }
            }
        } else {
            if (pT->shouldBeActive()) {
                itemDescription = descActive;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveParent.arg(itemDescription);
                }
                pT->setIsActive(true);
            } else {
                itemDescription = descInactive;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
                }
            }
        }

        if (pT->state()) {
            clearEditorNotification();

            // don't activate new timers by default - might be annoying
            pItem->setIcon(0, icon);
            pItem->setText(0, name);

        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            pItem->setText(0, name);
            showError(pT->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);

        // Capture NEW state after modifications (for redo)
        QString newStateXML = exportTimerToXML(pT);

        // Only push undo command if something actually changed
        if (oldStateXML != newStateXML) {
            auto* qtCmd = new EditorModifyPropertyCommand(EditorViewType::cmTimerView, timerID, name, oldStateXML, newStateXML, mpHost);
            mpUndoStack->pushCommand(qtCmd);

            // Clear edbee undo stack after save to make Save a commit point
            if (mpTextUndoStack) {
                mpTextUndoStack->clear();
            }
        }
    }
}

void dlgTriggerEditor::saveAlias()
{
    QTreeWidgetItem* pItem = mpCurrentAliasItem;
    if (!pItem) {
        return;
    }

    // Ensure the item is still part of the tree widget
    if (pItem->treeWidget() != treeWidget_aliases) {
        return;
    }

    mpAliasMainArea->trimName();
    QString name = mpAliasMainArea->lineEdit_alias_name->text();
    QString regex = mpAliasMainArea->lineEdit_alias_pattern->text();
    unmarkQString(&regex);


    if (!regex.isEmpty() && ((name.isEmpty()) || (name == tr("New alias")))) {
        name = regex;
    }
    const QString substitution = mpAliasMainArea->lineEdit_alias_command->text();
    //check if sub will trigger regex, ignore if there's nothing in regex - could be an alias group
    if (aliasSubstitutionLoops(regex, substitution)) {
        //we have a loop
        showAliasLoopWarning(pItem, name);
        return;
    }

    const QString script = mpSourceEditorEdbeeDocument->text();


    const int triggerID = pItem->data(0, Qt::UserRole).toInt();
    TAlias* pT = mpHost->getAliasUnit()->getAlias(triggerID);
    if (pT) {
        // Capture OLD state before modifications (for undo)
        QString oldStateXML = exportAliasToXML(pT);

        pT->setName(name);
        pT->setCommand(substitution);
        pT->setRegexCode(regex); // This could generate an error state if regex does not compile
        pT->setScript(script);

        QIcon icon;
        QString itemDescription;
        computeAliasIcon(pT, icon, itemDescription);

        if (pT->state()) {
            clearEditorNotification();

            if (pT->checkIfNew()) {
                if (pT->isFolder()) {
                    if (pT->shouldBeActive()) {
                        itemDescription = descActiveFolder;
                        if (pT->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-violet.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        itemDescription = descInactiveFolder;
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-violet-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                } else {
                    if (pT->shouldBeActive()) {
                        itemDescription = descActive;
                        if (pT->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        itemDescription = descInactive;
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    }
                }
                pItem->setIcon(0, icon);
                pItem->setText(0, name);

                if (pT->shouldBeActive()) {
                    pT->setIsActive(true);
                }
                pT->unmarkAsNew();
            } else {
                pItem->setIcon(0, icon);
                pItem->setText(0, name);
            }
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            pItem->setText(0, name);
            showError(pT->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);

        // Capture NEW state after modifications (for redo)
        QString newStateXML = exportAliasToXML(pT);

        // Only push undo command if something actually changed
        if (oldStateXML != newStateXML) {
            auto* qtCmd = new EditorModifyPropertyCommand(EditorViewType::cmAliasView, triggerID, name, oldStateXML, newStateXML, mpHost);
            mpUndoStack->pushCommand(qtCmd);

            // Clear edbee undo stack after save to make Save a commit point
            if (mpTextUndoStack) {
                mpTextUndoStack->clear();
            }
        }
    }
}

// Returns true when the substitution would match its own pattern, which makes
// the alias call itself forever. An invalid pattern can't loop - the faulty-regex
// check surfaces that instead.
bool dlgTriggerEditor::aliasSubstitutionLoops(const QString& regex, const QString& substitution) const
{
    if (regex.isEmpty()) {
        return false;
    }
    const QRegularExpression rx(regex);
    if (!rx.isValid()) {
        return false;
    }
    return rx.match(substitution).capturedStart() != -1;
}

// Computes the tree-item icon and accessible description for a non-error alias,
// shared by the explicit save and the per-property autosave paths.
void dlgTriggerEditor::computeAliasIcon(TAlias* pT, QIcon& icon, QString& itemDescription) const
{
    if (pT->isFolder()) {
        if (!pT->mPackageName.isEmpty()) {
            if (pT->isActive()) {
                itemDescription = descActiveFolder;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-brown.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveParent.arg(itemDescription);
                }
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-brown-locked.png")), QIcon::Normal, QIcon::Off);
                itemDescription = descInactiveFolder;
            }
        } else if (pT->isActive()) {
            itemDescription = descActiveFolder;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-violet.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                itemDescription = descInactiveParent.arg(itemDescription);
            }
        } else {
            itemDescription = descInactiveFolder;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-violet-locked.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
            }
        }
    } else {
        if (pT->isActive()) {
            itemDescription = descActive;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                itemDescription = descInactiveParent.arg(itemDescription);
            }
        } else {
            itemDescription = descInactive;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
            }
        }
    }
}

// Restores an alias tree item to its non-error appearance and clears the editor notice.
void dlgTriggerEditor::setAliasNormalIcon(QTreeWidgetItem* pItem, TAlias* pT)
{
    clearEditorNotification();
    if (pT->checkIfNew()) {
        // A freshly added alias keeps its "unsaved" cue until an explicit Save
        // activates it - don't recompute it to an active/inactive icon here.
        pItem->setIcon(0, QIcon(QPixmap(pT->isFolder() ? qsl(":/icons/folder-red.png") : qsl(":/icons/document-save-as.png"))));
        pItem->setData(0, Qt::AccessibleDescriptionRole, pT->isFolder() ? descNewFolder : descNewItem);
        return;
    }
    QIcon icon;
    QString itemDescription;
    computeAliasIcon(pT, icon, itemDescription);
    pItem->setIcon(0, icon);
    pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
}

// Flags an alias tree item as broken and shows the given error message.
void dlgTriggerEditor::showAliasError(QTreeWidgetItem* pItem, const QString& name, const QString& error)
{
    QIcon iconError;
    iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
    pItem->setIcon(0, iconError);
    pItem->setText(0, name);
    pItem->setData(0, Qt::AccessibleDescriptionRole, descError);
    showError(error);
}

void dlgTriggerEditor::showAliasLoopWarning(QTreeWidgetItem* pItem, const QString& name)
{
    showAliasError(pItem,
                   name,
                   tr("Alias <em>%1</em> has an infinite loop - substitution matches its own pattern. Please fix it - this alias isn't good as it'll call itself forever.").arg(name.toHtmlEscaped()));
}

// Reflects the alias's compile state on its tree item: normal icon when the
// pattern compiles, faulty-regex error otherwise.
void dlgTriggerEditor::applyAliasState(QTreeWidgetItem* pItem, TAlias* pT)
{
    if (pT->state()) {
        setAliasNormalIcon(pItem, pT);
    } else {
        showAliasError(pItem, pT->getName(), pT->getError());
    }
}

void dlgTriggerEditor::saveAction()
{
    QTreeWidgetItem* pItem = mpCurrentActionItem;
    if (!pItem) {
        return;
    }

    // Ensure the item is still part of the tree widget
    if (pItem->treeWidget() != treeWidget_actions) {
        return;
    }

    mpActionsMainArea->trimName();
    const QString name = mpActionsMainArea->lineEdit_action_name->text();
    const QString icon = mpActionsMainArea->lineEdit_action_icon->text();
    const QString commandDown = mpActionsMainArea->lineEdit_action_button_command_down->text();
    const QString commandUp = mpActionsMainArea->lineEdit_action_button_command_up->text();
    const QString script = mpSourceEditorEdbeeDocument->text();
    // currentIndex() can return -1 if no setting was previously made - need to fixup:
    const int rotation = qMax(0, mpActionsMainArea->comboBox_action_button_rotation->currentIndex());
    const int columns = mpActionsMainArea->spinBox_action_bar_columns->value();
    const int offset = mpActionsMainArea->spinBox_action_bar_offsetToFirstButton->value();
    const bool isChecked = mpActionsMainArea->checkBox_action_button_isPushDown->isChecked();
    // bottom location is no longer supported i.e. location = 1 = 0 = location top
    // currentIndex() can return -1 if no setting was previously made - need to fixup:
    int location = qMax(0, mpActionsMainArea->comboBox_action_bar_location->currentIndex());
    if (location > 0) {
        // The comboBox has indexes of 0 to 4 but we don't use 1 so jump over it:
        ++location;
    }

    // currentIndex() can return -1 if no setting was previously made - need to fixup:
    const int orientation = qMax(0, mpActionsMainArea->comboBox_action_bar_orientation->currentIndex());

    // This is an unnecessary level of indentation but has been retained to
    // reduce the noise in a git commit/diff caused by the removal of a
    // redundant "if( pITem )" - can be removed next time the file is modified
    const int actionID = pItem->data(0, Qt::UserRole).toInt();
    TAction* pA = mpHost->getActionUnit()->getAction(actionID);
    if (pA) {
        // Capture OLD state before modifications (for undo)
        QString oldStateXML = exportActionToXML(pA);

        // Check if data has been changed before it gets updated.
        bool actionDataChanged = false;
        if (pA->mLocation != location || pA->mOrientation != orientation || pA->css != mpActionsMainArea->plainTextEdit_action_css->toPlainText()) {
            actionDataChanged = true;
        }

        // Do not change anything for a module master folder - it won't "take"
        if (pA->mPackageName.isEmpty()) {
            pA->setName(name);
            pA->setIcon(icon);
            pA->setScript(script);
            pA->setCommandButtonDown(commandDown);
            pA->setCommandButtonUp(commandUp);
            pA->setIsPushDownButton(isChecked);
            pA->mLocation = location;
            pA->mOrientation = orientation;
            pA->setIsActive(pA->shouldBeActive());
            pA->setButtonRotation(rotation);
            pA->setButtonColumns(columns);
            pA->setButtonFillerOffset(offset);
            pA->mUseCustomLayout = false;
            pA->css = mpActionsMainArea->plainTextEdit_action_css->toPlainText();
        }

        QIcon icon;
        QString itemDescription;
        const bool itemActive = pA->isActive();
        if (pA->isFolder()) {
            itemDescription = (itemActive ? descActiveFolder : descInactiveFolder);
            if (!pA->mPackageName.isEmpty()) {
                // Has a package name so is a module master folder
                if (itemActive) {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-brown.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-brown-locked.png")), QIcon::Normal, QIcon::Off);
                }
            } else if (!pA->getParent() || !pA->getParent()->mPackageName.isEmpty()) {
                // No parent or it has a parent with a package name so is a toolbar
                if (itemActive) {
                    if (pA->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-yellow.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-yellow-locked.png")), QIcon::Normal, QIcon::Off);
                }
            } else {
                // Else must be a menu
                if (itemActive) {
                    if (pA->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-cyan.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-cyan-locked.png")), QIcon::Normal, QIcon::Off);
                }
            }
        } else {
            // Is a button
            if (itemActive) {
                itemDescription = descActive;
                if (pA->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveParent.arg(itemDescription);
                }
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                itemDescription = descInactive;
            }
        }

        if (pA->state()) {
            clearEditorNotification();

            pItem->setIcon(0, icon);
            pItem->setText(0, name);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            pItem->setText(0, name);
            showError(pA->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);

        // If not active, don't bother raising the TToolBar for this save.
        if (!pA->shouldBeActive()) {
            pA->setDataSaved();
        }

        if (actionDataChanged) {
            pA->setDataChanged();
        }

        // if the action has a TToolBar instance with a script error, hide that toolbar.
        if (pA->mpToolBar && !pA->state()) {
            pA->mpToolBar->hide();
        }

        // if the action location is changed, make sure the old toolbar instance is hidden.
        if (pA->mLocation == 4 && pA->mpEasyButtonBar) {
            pA->mpEasyButtonBar->hide();
        }
        if (pA->mLocation != 4 && pA->mpToolBar) {
            pA->mpToolBar->hide();
        }

        // Capture NEW state after modifications (for redo)
        QString newStateXML = exportActionToXML(pA);

        // Only push undo command if something actually changed
        if (oldStateXML != newStateXML) {
            auto* qtCmd = new EditorModifyPropertyCommand(EditorViewType::cmActionView, actionID, name, oldStateXML, newStateXML, mpHost);
            mpUndoStack->pushCommand(qtCmd);

            // Clear edbee undo stack after save to make Save a commit point
            if (mpTextUndoStack) {
                mpTextUndoStack->clear();
            }
        }
    }

    mpHost->getActionUnit()->updateAllToolbars();
    mudlet::self()->processEventLoopHack();
}

void dlgTriggerEditor::writeScript(int id)
{
    QTreeWidgetItem* pItem = mpCurrentScriptItem;
    if (!pItem) {
        return;
    }
    if (mCurrentView == EditorViewType::cmUnknownView || mCurrentView != EditorViewType::cmScriptView) {
        return;
    }
    const int scriptID = pItem->data(0, Qt::UserRole).toInt();
    if (scriptID != id) {
        return;
    }

    TScript* pT = mpHost->getScriptUnit()->getScript(scriptID);
    if (!pT) {
        return;
    }

    const QString scriptCode = pT->getScript();

    disconnect(mpSourceEditorEdbeeDocument, &edbee::TextDocument::textChanged, this, &dlgTriggerEditor::slot_itemEdited);
    mpSourceEditorEdbeeDocument->setText(scriptCode);
    connect(mpSourceEditorEdbeeDocument, &edbee::TextDocument::textChanged, this, &dlgTriggerEditor::slot_itemEdited);
}

void dlgTriggerEditor::saveScript()
{
    QTreeWidgetItem* pItem = mpCurrentScriptItem;
    if (!pItem) {
        return;
    }

    // Ensure the item is still part of the tree widget
    if (pItem->treeWidget() != treeWidget_scripts) {
        return;
    }

    mpScriptsMainArea->trimName();
    const QString name = mpScriptsMainArea->lineEdit_script_name->text();
    const QString script = mpSourceEditorEdbeeDocument->text();
    mpScriptsMainAreaEditHandlerItem = nullptr;
    QList<QListWidgetItem*> itemList;
    for (int i = 0; i < mpScriptsMainArea->listWidget_script_registered_event_handlers->count(); i++) {
        QListWidgetItem* pItem = mpScriptsMainArea->listWidget_script_registered_event_handlers->item(i);
        itemList << pItem;
    }
    QStringList handlerList;
    for (auto& listWidgetItem : itemList) {
        if (listWidgetItem->text().isEmpty()) {
            continue;
        }
        handlerList << listWidgetItem->text();
    }

    const int scriptID = pItem->data(0, Qt::UserRole).toInt();
    TScript* pT = mpHost->getScriptUnit()->getScript(scriptID);
    if (!pT) {
        return;
    }

    // Capture OLD state before modifications (for undo)
    QString oldStateXML = exportScriptToXML(pT);

    pT->setName(name);
    pT->setEventHandlerList(handlerList);
    pT->setScript(script);

    pT->compileAll();
    mpHost->getTriggerUnit()->doCleanup();
    QIcon icon;
    QString itemDescription;
    const bool itemActive = pT->isActive();
    if (pT->isFolder()) {
        itemDescription = (itemActive ? descActiveFolder : descInactiveFolder);
        if (!pT->mPackageName.isEmpty()) {
            if (itemActive) {
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-brown.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveParent.arg(itemDescription);
                }
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-brown-locked.png")), QIcon::Normal, QIcon::Off);
            }
        } else {
            if (itemActive) {
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-orange.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveParent.arg(itemDescription);
                }
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/folder-orange-locked.png")), QIcon::Normal, QIcon::Off);
            }
        }
    } else {
        if (itemActive) {
            itemDescription = descActive;
            if (pT->ancestorsActive()) {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
            } else {
                icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                itemDescription = descInactiveParent.arg(itemDescription);
            }
        } else {
            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descInactive;
        }
    }

    if (pT->state()) {
        if (auto error = pT->getLoadingError(); error) {
            showWarning(tr("While loading the profile, this script had an error that has since been fixed, "
                           "possibly by another script. The error was:%2%3")
                                .arg(qsl("<br>"), error.value()));
        } else {
            clearEditorNotification();
        }

        if (pT->checkIfNew()) {
            if (pT->isFolder()) {
                itemDescription = descActiveFolder;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-orange.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveParent.arg(itemDescription);
                }
            } else {
                if (pT->shouldBeActive()) {
                    itemDescription = descActive;
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactive;
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                }
            }
            pItem->setIcon(0, icon);
            pItem->setText(0, name);

            if (pT->shouldBeActive()) {
                pT->setIsActive(true);
            }
            pT->unmarkAsNew();
        } else {
            pItem->setIcon(0, icon);
            pItem->setText(0, name);
        }

    } else {
        QIcon iconError;
        iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
        itemDescription = descError;
        pItem->setIcon(0, iconError);
        pItem->setText(0, name);
        showError(pT->getError());
    }
    pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);

    // Capture NEW state after modifications (for redo)
    QString newStateXML = exportScriptToXML(pT);

    // Only push undo command if something actually changed
    if (oldStateXML != newStateXML) {
        auto* qtCmd = new EditorModifyPropertyCommand(EditorViewType::cmScriptView, scriptID, name, oldStateXML, newStateXML, mpHost);
        mpUndoStack->pushCommand(qtCmd);

        // Clear edbee undo stack after save to make Save a commit point
        if (mpTextUndoStack) {
            mpTextUndoStack->clear();
        }
    }

    // If pT's own body uninstalled its package during the compile above, the delete
    // was deferred (see TScript::compileScript / ScriptUnit::uninstall). We are now
    // done with pT, so flush it before returning to the event loop - otherwise the
    // 0ms save uninstallPackage() queued would serialize the "uninstalled" script
    // back into the profile:
    mpHost->getScriptUnit()->doCleanup();
}

void dlgTriggerEditor::clearEditorNotification()
{
    mpSystemMessageArea->hide();
    mCurrentBannerKey.clear();
}

void dlgTriggerEditor::updatePackageItemAccessibility(QTreeWidgetItem* pItem, const QString& currentDescription)
{
    // Append package description to existing accessible description
    // Screen readers will announce: "Item Name, [current description], package item"
    QString newDescription;
    if (currentDescription.isEmpty()) {
        newDescription = descPackageItem;
    } else {
        // Combine descriptions: e.g., "activated, package item"
        newDescription = currentDescription + qsl(", ") + descPackageItem;
    }
    pItem->setData(0, Qt::AccessibleDescriptionRole, newDescription);
}

int dlgTriggerEditor::canRecast(QTreeWidgetItem* pItem, int newNameType, int newValueType)
{
    //basic checks, return 1 if we can recast, 2 if no need to recast, 0 if we can't recast
    LuaInterface* lI = mpHost->getLuaInterface();
    VarUnit* vu = lI->getVarUnit();
    TVar* var = vu->getWVar(pItem);
    if (!var) {
        return 2;
    }
    const int currentNameType = var->getKeyType();
    const int currentValueType = var->getValueType();
    //most anything is ok to do.  We just want to enforce these rules:
    //you cannot change the type of a table that has children
    //rule removed to see if anything bad happens:
    //you cannot change anything to a table that isn't a table already
    if (currentValueType == LUA_TFUNCTION || currentNameType == LUA_TTABLE) {
        return 0; //no recasting functions or table keys
    }

    if (newValueType == LUA_TTABLE && currentValueType != LUA_TTABLE) {
        //trying to change a table to something else
        if (!var->getChildren(false).empty()) {
            return 0;
        }
        //no children, we can do this without bad things happening
        return 1;
    }

    if (currentNameType == newNameType && currentValueType == newValueType) {
        return 2;
    }
    return 1;
}

// A rename the Variables view asked for and did not get: nothing else on screen
// would show that anything was refused, and the row has meanwhile been put back
// to the name the variable still answers to.
void dlgTriggerEditor::showVariableRenameRefused(TVar* variable)
{
    //: Warning shown in the editor's Variables view when a rename could not be carried out. %1 is the name the variable keeps.
    showWarning(tr("\"%1\" was not renamed: another member of the same table already has that name, or this variable's key is a table or a function, which has no name to change.")
                        .arg(variable->getName().toHtmlEscaped()));
}

void dlgTriggerEditor::saveVar()
{
    // We can enter this function if:
    // we click on a variable without having one selected ( no parent )
    // we click on a variable from another variable
    // we click on a variable from having the top-most element selected ( parent but parent is not a variable/table )
    // we click on a variable from the same variable (such as a double click)
    // we add a new variable
    // we switch away from a variable (so we are saving the old variable)

    if (!mpCurrentVarItem) {
        return;
    }
    QTreeWidgetItem* pItem = mpCurrentVarItem;
    if (!pItem->parent()) {
        return;
    }
    auto* luaInterface = mpHost->getLuaInterface();
    auto* varUnit = luaInterface->getVarUnit();
    TVar* variable = varUnit->getWVar(pItem);
    bool newVar = false;
    if (!variable) {
        newVar = true;
        variable = varUnit->getTVar(pItem);
    }
    if (!variable) {
        return;
    }
    const QString newName = mpVarsMainArea->lineEdit_var_name->text();
    QString newValue = mpSourceEditorEdbeeDocument->text();
    if (newName.isEmpty()) {
        slot_variableSelected(pItem);
        return;
    }
    // Everything below reaches the variable by the name the tree gave it, and a
    // write through a name that does not reach it lands on a key of its own,
    // leaving a second variable beside the real one (#9903). Quietly, because
    // selecting the variable already said so. A new variable is exempt: its name
    // is the key about to be created, so there is nothing for it to find yet.
    if (!newVar && !luaInterface->writableByName(variable)) {
        return;
    }
    mChangingVar = true;
    // said at the very end: the tail of this function re-selects the row, which
    // clears whatever notification is on screen
    bool renameRefused = false;
    int uiNameType = mpVarsMainArea->comboBox_variable_key_type->itemData(mpVarsMainArea->comboBox_variable_key_type->currentIndex(), Qt::UserRole).toInt();
    int uiValueType = mpVarsMainArea->comboBox_variable_value_type->itemData(mpVarsMainArea->comboBox_variable_value_type->currentIndex(), Qt::UserRole).toInt();
    if ((uiNameType == LUA_TNUMBER || uiNameType == LUA_TSTRING) && newVar) {
        uiNameType = LUA_TNONE;
    }
    //check variable recasting
    const int varRecast = canRecast(pItem, uiNameType, uiValueType);
    if ((uiNameType == -1) || (variable && uiNameType != variable->getKeyType())) {
        bool nameNumberOk = false;
        newName.toDouble(&nameNumberOk);
        // the key type combobox shows a boolean key but does not offer it as a
        // choice, so a name that still reads as a boolean keeps its boolean key
        if (variable->getKeyType() == LUA_TBOOLEAN && (newName.toLower() == QLatin1String("true") || newName.toLower() == QLatin1String("false"))) {
            uiNameType = LUA_TBOOLEAN;
        } else if (nameNumberOk) {
            uiNameType = LUA_TNUMBER;
        } else {
            uiNameType = LUA_TSTRING;
        }
    }
    if ((uiValueType != LUA_TTABLE) && (uiValueType == -1)) {
        bool valueNumberOk = false;
        newValue.toDouble(&valueNumberOk);
        if (valueNumberOk) {
            uiValueType = LUA_TNUMBER;
        } else if (newValue.toLower() == "true" || newValue.toLower() == "false") {
            uiValueType = LUA_TBOOLEAN;
        } else {
            uiValueType = LUA_TSTRING;
        }
    }
    if (varRecast == 2) {
        //we sometimes get in here from new variables
        if (newVar) {
            //we're making this var
            variable = varUnit->getTVar(pItem);
            if (!variable) {
                variable = new TVar();
            }
            variable->setName(newName, uiNameType);
            variable->setValue(newValue, uiValueType);
            luaInterface->createVar(variable);
            varUnit->addVariable(variable);
            varUnit->addTreeItem(pItem, variable);
            varUnit->removeTempVar(pItem);
            // Attach to its real parent TVar (set in addVar) so the XML writer,
            // which iterates from the base, nests it inside the parent table
            // rather than at root level.
            TVar* parentVar = variable->getParent();
            if (!parentVar) {
                parentVar = varUnit->getBase();
            }
            parentVar->addChild(variable);
            pItem->setText(0, newName);
            mpCurrentVarItem = nullptr;
        } else if (variable) {
            if (newName == variable->getName() && (variable->getValueType() == LUA_TTABLE && newValue == variable->getValue())) {
                //no change made
            } else {
                //we're trying to rename it/recast it
                int change = 0;
                if (newName != variable->getName() || uiNameType != variable->getKeyType()) {
                    //let's make sure the nametype works
                    bool nameNumberOk = false;
                    newName.toDouble(&nameNumberOk);
                    if (variable->getKeyType() == LUA_TBOOLEAN && (newName.toLower() == QLatin1String("true") || newName.toLower() == QLatin1String("false"))) {
                        uiNameType = LUA_TBOOLEAN;
                    } else if (variable->getKeyType() == LUA_TNUMBER && nameNumberOk) {
                        uiNameType = LUA_TNUMBER;
                    } else {
                        uiNameType = LUA_TSTRING;
                    }
                    change = change | 0x1;
                }
                variable->setNewName(newName, uiNameType);
                if (variable->getValueType() != LUA_TTABLE && (newValue != variable->getValue() || uiValueType != variable->getValueType())) {
                    //let's check again
                    bool valueNumberOk = false;
                    newValue.toDouble(&valueNumberOk);
                    if (variable->getValueType() == LUA_TTABLE) {
                        //HEIKO: obvious logic error used to be valueType == LUA_TABLE
                        uiValueType = LUA_TTABLE;
                    } else if (uiValueType == LUA_TNUMBER && valueNumberOk) {
                        uiValueType = LUA_TNUMBER;
                    } else if (uiValueType == LUA_TBOOLEAN && (newValue.toLower() == "true" || newValue.toLower() == "false")) {
                        uiValueType = LUA_TBOOLEAN;
                    } else {
                        uiValueType = LUA_TSTRING; //nope, you don't agree, you lose your value
                    }
                    variable->setValue(newValue, uiValueType);
                    change = change | 0x2;
                }
                if (change) {
                    bool renamed = true;
                    if (change & 0x1 || newVar) {
                        renamed = luaInterface->renameVar(variable);
                    }
                    if ((variable->getValueType() != LUA_TTABLE && change & 0x2) || newVar) {
                        luaInterface->setValue(variable);
                    }
                    if (renamed) {
                        pItem->setText(0, newName);
                        mpCurrentVarItem = nullptr;
                    } else {
                        // the variable still answers to the name it had, so
                        // that is what the row has to go back to showing
                        pItem->setText(0, variable->getName());
                        renameRefused = true;
                    }
                } else {
                    // nothing was renamed, so there is a pending new name to
                    // drop rather than to write onto the variable
                    variable->abandonNewName();
                }
            }
        }
    } else if (varRecast == 1) { //recast it
        TVar* var = varUnit->getWVar(pItem);
        if (newVar) {
            //we're making this var
            var = varUnit->getTVar(pItem);
            var->setName(newName, uiNameType);
            var->setValue(newValue, uiValueType);
            luaInterface->createVar(var);
            varUnit->addVariable(var);
            varUnit->addTreeItem(pItem, var);
            pItem->setText(0, newName);
            mpCurrentVarItem = nullptr;
        } else if (var) {
            //we're trying to rename it/recast it
            int change = 0;
            if (newName != var->getName() || uiNameType != var->getKeyType()) {
                //let's make sure the nametype works
                bool nameNumberOk = false;
                newName.toDouble(&nameNumberOk);
                if (uiNameType == LUA_TSTRING) {
                    //do nothing, we can always make key to string
                } else if (var->getKeyType() == LUA_TBOOLEAN && (newName.toLower() == QLatin1String("true") || newName.toLower() == QLatin1String("false"))) {
                    uiNameType = LUA_TBOOLEAN;
                } else if (var->getKeyType() == LUA_TNUMBER && nameNumberOk) {
                    uiNameType = LUA_TNUMBER;
                } else {
                    uiNameType = LUA_TSTRING;
                }
                var->setNewName(newName, uiNameType);
                change = change | 0x1;
            }
            // a table's contents are not shown in the value editor, so a table staying a table is never a value change
            if ((newValue != var->getValue() || uiValueType != var->getValueType()) && !(uiValueType == LUA_TTABLE && var->getValueType() == LUA_TTABLE)) {
                //let's check again
                bool valueNumberOk = false;
                newValue.toDouble(&valueNumberOk);
                if (uiValueType == LUA_TTABLE) {
                    newValue = "{}";
                } else if (uiValueType == LUA_TNUMBER && valueNumberOk) {
                    uiValueType = LUA_TNUMBER;
                } else if (uiValueType == LUA_TBOOLEAN && (newValue.toLower() == QLatin1String("true") || newValue.toLower() == QLatin1String("false"))) {
                    uiValueType = LUA_TBOOLEAN;
                } else {
                    uiValueType = LUA_TSTRING; //nope, you don't agree, you lose your value
                }
                var->setValue(newValue, uiValueType);
                change = change | 0x2;
            }
            if (change) {
                bool renamed = true;
                if (change & 0x1 || newVar) {
                    renamed = luaInterface->renameVar(var);
                }
                if (change & 0x2 || newVar) {
                    luaInterface->setValue(var);
                }
                if (renamed) {
                    pItem->setText(0, newName);
                    mpCurrentVarItem = nullptr;
                } else {
                    pItem->setText(0, var->getName());
                    renameRefused = true;
                }
            }
        }
    }
    //redo this here in case we changed type
    pItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled | Qt::ItemIsDragEnabled | Qt::ItemIsAutoTristate | Qt::ItemIsUserCheckable);
    pItem->setToolTip(0, utils::richText(tr("Checked variables will be saved and loaded with your profile.")));
    if (!varUnit->shouldSave(variable)) {
        pItem->setFlags(pItem->flags() & ~(Qt::ItemIsDropEnabled | Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable));
        pItem->setForeground(0, QBrush(QColor("grey")));
        const QString reason = varUnit->getUnsaveableReason(variable);
        pItem->setToolTip(0, reason.isEmpty() ? QString() : utils::richText(reason));
        pItem->setCheckState(0, Qt::Unchecked);
    } else if (varUnit->isSaved(variable)) {
        pItem->setCheckState(0, Qt::Checked);
    }
    pItem->setData(0, Qt::UserRole, variable->getValueType());
    QIcon icon;
    switch (variable->getValueType()) {
    case 5:
        icon.addPixmap(QPixmap(qsl(":/icons/table.png")), QIcon::Normal, QIcon::Off);
        break;
    case 6:
        icon.addPixmap(QPixmap(qsl(":/icons/function.png")), QIcon::Normal, QIcon::Off);
        break;
    default:
        icon.addPixmap(QPixmap(qsl(":/icons/variable.png")), QIcon::Normal, QIcon::Off);
        break;
    }
    pItem->setIcon(0, icon);
    mChangingVar = false;
    slot_variableSelected(pItem);
    if (renameRefused) {
        showVariableRenameRefused(variable);
    }
}

void dlgTriggerEditor::saveKey()
{
    QTreeWidgetItem* pItem = mpCurrentKeyItem;
    if (!pItem) {
        return;
    }

    // Ensure the item is still part of the tree widget
    if (pItem->treeWidget() != treeWidget_keys) {
        return;
    }

    mpKeysMainArea->trimName();
    QString name = mpKeysMainArea->lineEdit_key_name->text();
    if (name.isEmpty() || name == tr("New key")) {
        name = mpKeysMainArea->lineEdit_key_binding->text();
    }
    const QString command = mpKeysMainArea->lineEdit_key_command->text();
    const QString script = mpSourceEditorEdbeeDocument->text();


    const int triggerID = pItem->data(0, Qt::UserRole).toInt();
    TKey* pT = mpHost->getKeyUnit()->getKey(triggerID);
    if (pT) {
        // Capture OLD state before modifications (for undo)
        QString oldStateXML = exportKeyToXML(pT);

        const QString old_name = pT->getName();
        pItem->setText(0, name);
        pT->setName(name);
        pT->setCommand(command);
        pT->setScript(script);

        pT->validateKeyBinding();

        QIcon icon;
        QString itemDescription;
        const bool itemActive = pT->isActive();
        if (pT->isFolder()) {
            itemDescription = (itemActive ? descActiveFolder : descInactiveFolder);
            if (!pT->mPackageName.isEmpty()) {
                if (itemActive) {
                    if (pT->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-brown.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-brown-locked.png")), QIcon::Normal, QIcon::Off);
                }
            } else if (itemActive) {
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-pink.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveParent.arg(itemDescription);
                }
            } else {
                itemDescription = descInactiveFolder;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-pink-locked.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
                }
            }
        } else {
            if (itemActive) {
                itemDescription = descActive;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveParent.arg(itemDescription);
                }
            } else {
                itemDescription = descInactive;
                if (pT->ancestorsActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
                }
            }
        }

        if (pT->state()) {
            clearEditorNotification();
            if (old_name == tr("New key")) {
                if (pT->isFolder()) {
                    if (pT->shouldBeActive()) {
                        itemDescription = descActiveFolder;
                        if (pT->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-pink.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        itemDescription = descInactiveFolder;
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-pink-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                } else {
                    if (pT->shouldBeActive()) {
                        itemDescription = descActive;
                        if (pT->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        itemDescription = descInactive;
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    }
                }
                pItem->setIcon(0, icon);
                pItem->setText(0, name);

                if (pT->shouldBeActive()) {
                    pT->setIsActive(true);
                }
            } else {
                pItem->setIcon(0, icon);
                pItem->setText(0, name);
            }
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            pItem->setText(0, name);
            showError(pT->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);

        // Capture NEW state after modifications (for redo)
        QString newStateXML = exportKeyToXML(pT);

        // Only push undo command if something actually changed
        if (oldStateXML != newStateXML) {
            auto* qtCmd = new EditorModifyPropertyCommand(EditorViewType::cmKeysView, triggerID, name, oldStateXML, newStateXML, mpHost);
            mpUndoStack->pushCommand(qtCmd);

            // Clear edbee undo stack after save to make Save a commit point
            if (mpTextUndoStack) {
                mpTextUndoStack->clear();
            }
        }
    }
}


void dlgTriggerEditor::setupPatternControls(const int type, dlgTriggerPatternEdit* pItem)
{
    // Display middle dots for potentially unwanted spaces in perl regex
    if (type == REGEX_PERL) {
        markQTextEdit(pItem->singleLineTextEdit_pattern);
        lineEditShouldMarkSpaces[pItem->singleLineTextEdit_pattern] = true;
        pItem->singleLineTextEdit_pattern->blockSignals(true);
        pItem->singleLineTextEdit_pattern->rehighlight();
        pItem->singleLineTextEdit_pattern->blockSignals(false);
    } else {
        unmarkQTextEdit(pItem->singleLineTextEdit_pattern);
        lineEditShouldMarkSpaces[pItem->singleLineTextEdit_pattern] = false;
    }

    switch (type) {
    case REGEX_SUBSTRING:
    case REGEX_PERL:
    case REGEX_BEGIN_OF_LINE_SUBSTRING:
    case REGEX_EXACT_MATCH:
    case REGEX_LUA_CODE:
        pItem->singleLineTextEdit_pattern->setHighlightingEnabled(type == REGEX_PERL);
        pItem->singleLineTextEdit_pattern->show();
        pItem->pushButton_fgColor->hide();
        pItem->pushButton_bgColor->hide();
        pItem->label_prompt->hide();
        pItem->spinBox_lineSpacer->hide();
        break;
    case REGEX_LINE_SPACER:
        pItem->singleLineTextEdit_pattern->hide();
        pItem->pushButton_fgColor->hide();
        pItem->pushButton_bgColor->hide();
        pItem->label_prompt->hide();
        pItem->spinBox_lineSpacer->show();
        break;
    case REGEX_COLOR_PATTERN:
        // CHECKME: Do we need to regenerate (hidden patter text) and button texts/colors?
        pItem->singleLineTextEdit_pattern->hide();
        pItem->pushButton_fgColor->show();
        pItem->pushButton_bgColor->show();
        pItem->label_prompt->hide();
        pItem->spinBox_lineSpacer->hide();
        break;
    case REGEX_PROMPT:
        pItem->singleLineTextEdit_pattern->hide();
        pItem->pushButton_fgColor->hide();
        pItem->pushButton_bgColor->hide();
        if (mpHost->mTelnet.mGA_Driver) {
            pItem->label_prompt->setText(tr("match on the prompt line"));
            pItem->label_prompt->setToolTip(QString());
            pItem->label_prompt->setEnabled(true);
        } else {
            pItem->label_prompt->setText(tr("match on the prompt line (disabled)"));
            pItem->label_prompt->setToolTip(utils::richText(tr("A Go-Ahead (GA) signal from the game is required to make this feature work")));
            pItem->label_prompt->setEnabled(false);
        }
        pItem->label_prompt->show();
        pItem->spinBox_lineSpacer->hide();
        break;
    }

    // All three walk every row, so during a move or a delete - which set one row
    // after another - they would run once per row for an answer only the last
    // row makes true. The bulk operation runs them itself once it has finished.
    if (mPatternBulkEdit) {
        return;
    }

    checkForMoreThanOneTriggerItem();
    updatePatternTabOrder();
    updatePatternPlaceholders();
}

// Typing in a row changes what the form says about the patterns, not how many
// rows there are: a row is added by the Add pattern button (or by Return in the
// last row) and taken away by its own delete button, so nothing appears under
// the cursor or vanishes from under it while the user is filling one in
void dlgTriggerEditor::handlePatternChange()
{
    checkForMoreThanOneTriggerItem();
    updatePatternPlaceholders();
}

// A move or a delete shifts what every row below the one it touched holds, so
// the rows the shift emptied at the bottom are the ones to take away. One row
// always stays: an empty trigger is one waiting for its first pattern.
void dlgTriggerEditor::compactPatternRows()
{
    checkForMoreThanOneTriggerItem();

    int lastActive = -1;
    for (int i = 0; i < mVisiblePatternCount && i < mTriggerPatternEdit.size(); ++i) {
        const auto* item = mTriggerPatternEdit.at(i);
        if (!item) {
            continue;
        }

        bool itemHasContent = !item->singleLineTextEdit_pattern->toPlainText().isEmpty();
        const int type = item->comboBox_patternType->currentIndex();
        if (type == REGEX_PROMPT) {
            itemHasContent = true;
        } else if (type == REGEX_LINE_SPACER) {
            itemHasContent = item->spinBox_lineSpacer->value() > 0 || item->spinBox_lineSpacer->isVisible();
        }

        if (itemHasContent) {
            lastActive = i;
        }
    }

    const int desiredCount = qMax(lastActive + 1, 1);
    if (desiredCount != mVisiblePatternCount) {
        showPatternItems(desiredCount);
    } else {
        updatePatternPlaceholders();
    }
}

QWidget* dlgTriggerEditor::firstFocusablePatternWidget(const dlgTriggerPatternEdit* patternItem) const
{
    if (!patternItem) {
        return nullptr;
    }

    if (patternItem->singleLineTextEdit_pattern->isVisible()) {
        return patternItem->singleLineTextEdit_pattern;
    }
    if (patternItem->spinBox_lineSpacer->isVisible()) {
        return patternItem->spinBox_lineSpacer;
    }
    if (patternItem->pushButton_fgColor->isVisible()) {
        return patternItem->pushButton_fgColor;
    }
    if (patternItem->pushButton_bgColor->isVisible()) {
        return patternItem->pushButton_bgColor;
    }

    return patternItem->comboBox_patternType;
}

bool dlgTriggerEditor::focusPatternItem(const int row, const Qt::FocusReason reason)
{
    if (row < 0 || row >= mVisiblePatternCount || row >= mTriggerPatternEdit.size()) {
        return false;
    }

    auto* patternItem = mTriggerPatternEdit.value(row, nullptr);
    if (!patternItem || !patternItem->isVisible()) {
        return false;
    }

    QWidget* target = firstFocusablePatternWidget(patternItem);
    if (!target) {
        return false;
    }

    mpScrollArea->ensureWidgetVisible(patternItem);
    target->setFocus(reason);

    if (auto* edit = qobject_cast<SingleLineTextEdit*>(target)) {
        auto cursor = edit->textCursor();
        cursor.select(QTextCursor::Document);
        edit->setTextCursor(cursor);
    } else if (auto* spinBox = qobject_cast<QSpinBox*>(target)) {
        spinBox->selectAll();
    }

    return true;
}

bool dlgTriggerEditor::focusNextPatternItem(const dlgTriggerPatternEdit* currentItem)
{
    if (!currentItem) {
        return false;
    }

    int nextRow = currentItem->mRow + 1;
    while (nextRow < mVisiblePatternCount && nextRow < mTriggerPatternEdit.size()) {
        auto* nextItem = mTriggerPatternEdit.value(nextRow, nullptr);
        if (nextItem && nextItem->isVisible()) {
            return focusPatternItem(nextRow);
        }
        ++nextRow;
    }

    return false;
}


bool dlgTriggerEditor::focusPreviousPatternItem(const dlgTriggerPatternEdit* currentItem)
{
    if (!currentItem) {
        return false;
    }

    int previousRow = currentItem->mRow - 1;
    while (previousRow >= 0) {
        auto* previousItem = mTriggerPatternEdit.value(previousRow, nullptr);
        if (previousItem && previousItem->isVisible()) {
            return focusPatternItem(previousRow);
        }
        --previousRow;
    }

    return false;
}


void dlgTriggerEditor::updatePatternTabOrder()
{
    if (!mpTriggersMainArea) {
        return;
    }

    QWidget* previous = mpTriggersMainArea->lineEdit_trigger_name;
    auto addToChain = [&previous, this](QWidget* next) {
        if (!next || !previous) {
            if (next) {
                previous = next;
            }
            return;
        }

        if (!next->isVisibleTo(mpTriggersMainArea)) {
            return;
        }

        QWidget::setTabOrder(previous, next);
        previous = next;
    };

    // The order the row reads in: the Options button sits at its far end, past
    // the command and the ID
    addToChain(mpTriggersMainArea->lineEdit_trigger_command);
    addToChain(mpTriggersMainArea->toolButton_toggleExtraControls);
    // Only there while the options are away, and addToChain() drops whatever is
    // not on show
    addToChain(mpButton_triggerOptionsSummary);

    for (int i = 0; i < mVisiblePatternCount && i < mTriggerPatternEdit.size(); ++i) {
        auto* item = mTriggerPatternEdit.value(i, nullptr);
        if (!item || !item->isVisible()) {
            continue;
        }

        QWidget* first = firstFocusablePatternWidget(item);
        addToChain(first);

        if (item->spinBox_lineSpacer->isVisible() && item->spinBox_lineSpacer != first) {
            addToChain(item->spinBox_lineSpacer);
        }
        if (item->pushButton_fgColor->isVisible()) {
            addToChain(item->pushButton_fgColor);
        }
        if (item->pushButton_bgColor->isVisible()) {
            addToChain(item->pushButton_bgColor);
        }
        if (item->comboBox_patternType->isVisible()) {
            addToChain(item->comboBox_patternType);
        }
        // Drawn only under the mouse, but the row shows the glyph while this
        // holds focus, so tabbing to it is not tabbing to nothing
        addToChain(item->toolButton_deletePattern);
    }
    // The grip is the one part of the row chrome the keyboard does not reach:
    // reordering is on Ctrl+Alt+Up and Ctrl+Alt+Down instead
    addToChain(mpButton_addPattern);
    // The four cards, in the order they are read down the panel.
    // spinBox_lineMargin is not among them: it is hidden, which addToChain()
    // takes as reason enough to leave it out, and the radios below are how the
    // mode it holds is reached.
    addToChain(mpRadioButton_matchAny);
    addToChain(mpRadioButton_matchAll);
    addToChain(mpSpinBox_matchWithinLines);
    addToChain(mpTriggersMainArea->checkBox_perlSlashGOption);
    addToChain(mpTriggersMainArea->spinBox_stayOpen);
    addToChain(mpTriggersMainArea->checkBox_filterTrigger);
    addToChain(mpTriggersMainArea->groupBox_soundTrigger);
    addToChain(mpTriggersMainArea->toolButton_clearSoundFile);
    addToChain(mpTriggersMainArea->pushButtonSound);
    addToChain(mpTriggersMainArea->groupBox_triggerColorizer);
    addToChain(mpTriggersMainArea->pushButtonFgColor);
    addToChain(mpTriggersMainArea->pushButtonBgColor);
    addToChain(mpSourceEditorEdbee);
}

void dlgTriggerEditor::slot_changedPattern()
{
    SingleLineTextEdit* textEdit = qobject_cast<SingleLineTextEdit*>(sender());

    if (textEdit && lineEditShouldMarkSpaces[textEdit]) {
        markQTextEdit(textEdit);
        textEdit->blockSignals(true);
        textEdit->rehighlight();
        textEdit->blockSignals(false);
    }

    handlePatternChange();
}

void dlgTriggerEditor::slot_lineSpacerChanged(int)
{
    handlePatternChange();
}

// This can get called after the lineEdit contents has changed and it is now a
// color pattern - ought to update coloration if it has been edited by hand
// but need to source the colors
void dlgTriggerEditor::slot_setupPatternControls(int type)
{
    QComboBox* pBox = qobject_cast<QComboBox*>(sender());
    if (!pBox) {
        return;
    }

    const int row = pBox->itemData(0).toInt();
    if (row < 0 || row >= mTriggerPatternEdit.size()) {
        return;
    }

    // This is the collection of widgets that make up one of the patterns
    // in the dlgTriggerMainArea:
    dlgTriggerPatternEdit* pPatternItem = mTriggerPatternEdit[row];
    setupPatternControls(type, pPatternItem);
    if (type == REGEX_COLOR_PATTERN) {
        if (pPatternItem->singleLineTextEdit_pattern->toPlainText().isEmpty()) {
            // This COLOR trigger is a new one in that there is NO text
            // So set it to the default (ignore both) - which will generate an
            // error if saved without setting a color for at least one element:

            pPatternItem->singleLineTextEdit_pattern->setPlainText(TTrigger::createColorPatternText(TTrigger::scmIgnored, TTrigger::scmIgnored));
        }

        // Only process the text if it looks like it should:
        if ((pPatternItem->singleLineTextEdit_pattern->toPlainText().startsWith(QLatin1String("ANSI_COLORS_F{"))
             && pPatternItem->singleLineTextEdit_pattern->toPlainText().contains(QLatin1String("}_B{")) && pPatternItem->singleLineTextEdit_pattern->toPlainText().endsWith(QLatin1String("}")))) {
            // It looks as though there IS a valid color pattern string in the
            // lineEdit, so, in case it has been edited by hand, regenerate the
            // colors that are used:
            int textAnsiFg = TTrigger::scmIgnored;
            int textAnsiBg = TTrigger::scmIgnored;
            TTrigger::decodeColorPatternText(pPatternItem->singleLineTextEdit_pattern->toPlainText(), textAnsiFg, textAnsiBg);

            if (textAnsiFg == TTrigger::scmIgnored) {
                pPatternItem->pushButton_fgColor->setStyleSheet(QString());
                //: Color trigger ignored foreground color button, ensure all three instances have the same text
                pPatternItem->pushButton_fgColor->setText(tr("Foreground color ignored"));
            } else if (textAnsiFg == TTrigger::scmDefault) {
                pPatternItem->pushButton_fgColor->setStyleSheet(QString());
                //: Color trigger default foreground color button, ensure all three instances have the same text
                pPatternItem->pushButton_fgColor->setText(tr("Default foreground color"));
            } else {
                pPatternItem->pushButton_fgColor->setStyleSheet(generateButtonStyleSheet(mpHost->getAnsiColor(textAnsiFg, false)));
                //: Color trigger ANSI foreground color button, ensure all three instances have the same text
                pPatternItem->pushButton_fgColor->setText(tr("Foreground color [ANSI %1]").arg(QString::number(textAnsiFg)));
            }

            if (textAnsiBg == TTrigger::scmIgnored) {
                pPatternItem->pushButton_bgColor->setStyleSheet(QString());
                //: Color trigger ignored background color button, ensure all three instances have the same text
                pPatternItem->pushButton_bgColor->setText(tr("Background color ignored"));
            } else if (textAnsiBg == TTrigger::scmDefault) {
                pPatternItem->pushButton_bgColor->setStyleSheet(QString());
                //: Color trigger default background color button, ensure all three instances have the same text
                pPatternItem->pushButton_bgColor->setText(tr("Default background color"));
            } else {
                pPatternItem->pushButton_bgColor->setStyleSheet(generateButtonStyleSheet(mpHost->getAnsiColor(textAnsiBg, true)));
                //: Color trigger ANSI background color button, ensure all three instances have the same text
                pPatternItem->pushButton_bgColor->setText(tr("Background color [ANSI %1]").arg(QString::number(textAnsiBg)));
            }
        }
        // Commented-out debug code:
        // else {
        //     qDebug() << "dlgTriggerEditor::slot_setupPatternControls(...) ERROR: Pattern listed as item:"
        //              << row + 1
        //              << "is supposed to be a color pattern trigger but the stored text that contains the color codes:"
        //              << pPatternItem->singleLineTextEdit_pattern->toPlainText()
        //              << "does not fit the pattern!";
        // }

    } else {
        // Is NOT a REGEX_COLOR_PATTERN - if the text corresponds to the color
        // pattern text equivalent to ignore both fore and back ground then
        // clear the text - otherwise leave as is:
        if (pPatternItem->singleLineTextEdit_pattern->toPlainText().compare(QLatin1String("ANSI_COLORS_F{IGNORE}_B{IGNORE}")) == 0) {
            pPatternItem->singleLineTextEdit_pattern->clear();
        }
    }

    handlePatternChange();
}

void dlgTriggerEditor::slot_triggerSelected(QTreeWidgetItem* pItem)
{
    if (!pItem) {
        return;
    }

    // Only save previous item if switching to a different item
    if (pItem != mpCurrentTriggerItem) {
        if (mpCurrentTriggerItem) {
            saveEditorState(EditorViewType::cmTriggerView, mpCurrentTriggerItem->data(0, Qt::UserRole).toInt());
        }
        saveTrigger();
    }

    // Disable updates during document loading to prevent visual flicker
    mpSourceEditorEdbee->setUpdatesEnabled(false);

    // Block property saves while loading the new item to prevent spurious undo entries
    mBlockPropertySave = true;

    mpCurrentTriggerItem = pItem;
    mpTriggersMainArea->show();
    mpSourceEditorArea->show();
    clearEditorNotification();
    mpTriggersMainArea->lineEdit_trigger_name->clear();
    mpTriggersMainArea->label_idNumber->clear();
    clearDocument(mpSourceEditorEdbee); // Trigger Select
    mpTriggersMainArea->checkBox_perlSlashGOption->setChecked(false);
    mpTriggersMainArea->checkBox_filterTrigger->setChecked(false);
    mpTriggersMainArea->groupBox_triggerColorizer->setChecked(false);
    mpTriggersMainArea->pushButtonFgColor->setStyleSheet(QString());
    mpTriggersMainArea->pushButtonFgColor->setProperty(cButtonBaseColor, QVariant());
    mpTriggersMainArea->pushButtonBgColor->setStyleSheet(QString());
    mpTriggersMainArea->pushButtonBgColor->setProperty(cButtonBaseColor, QVariant());
    mpTriggersMainArea->spinBox_lineMargin->setValue(-1);

    const int ID = pItem->data(0, Qt::UserRole).toInt();
    TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(ID);
    if (pT) {
        const QStringList patternList = pT->getPatternsList();
        QList<int> const propertyList = pT->getRegexCodePropertyList();

        if (patternList.size() != propertyList.size()) {
            return;
        }

        // Exactly what the trigger holds, and no trailing empty row: the Add
        // pattern button is what one more is asked for with
        showPatternItems(qMax(static_cast<int>(patternList.size()), 1));
        for (int i = 0; i < patternList.size() && i < mTriggerPatternEdit.size(); i++) {
            if (i >= pT->mColorPatternList.size()) {
                break;
            }
            // Use operator[] so we have write access to the array/list member:
            dlgTriggerPatternEdit* pPatternItem = mTriggerPatternEdit[i];
            const int pType = propertyList.at(i);
            if (!pType) {
                // If the control is for the default (0) case nudge the setting
                // up and down so that it copies the colour icon for the
                // subString type across into the QLineEdit:
                pPatternItem->comboBox_patternType->setCurrentIndex(1);
                setupPatternControls(1, pPatternItem);
            }
            pPatternItem->comboBox_patternType->setCurrentIndex(pType);
            setupPatternControls(pType, pPatternItem);
            if (pType == REGEX_PROMPT) {
                pPatternItem->singleLineTextEdit_pattern->clear();

            } else if (pType == REGEX_COLOR_PATTERN) {
                pPatternItem->singleLineTextEdit_pattern->setPlainText(patternList.at(i));
                if (pT->mColorPatternList.at(i)) {
                    if (pT->mColorPatternList.at(i)->ansiFg == TTrigger::scmIgnored) {
                        pPatternItem->pushButton_fgColor->setStyleSheet(QString());
                        //: Color trigger ignored foreground color button, ensure all three instances have the same text
                        pPatternItem->pushButton_fgColor->setText(tr("Foreground color ignored"));
                    } else if (pT->mColorPatternList.at(i)->ansiFg == TTrigger::scmDefault) {
                        pPatternItem->pushButton_fgColor->setStyleSheet(QString());
                        //: Color trigger default foreground color button, ensure all three instances have the same text
                        pPatternItem->pushButton_fgColor->setText(tr("Default foreground color"));
                    } else {
                        pPatternItem->pushButton_fgColor->setStyleSheet(generateButtonStyleSheet(pT->mColorPatternList.at(i)->mFgColor));
                        //: Color trigger ANSI foreground color button, ensure all three instances have the same text
                        pPatternItem->pushButton_fgColor->setText(tr("Foreground color [ANSI %1]").arg(QString::number(pT->mColorPatternList.at(i)->ansiFg)));
                    }

                    if (pT->mColorPatternList.at(i)->ansiBg == TTrigger::scmIgnored) {
                        pPatternItem->pushButton_bgColor->setStyleSheet(QString());
                        //: Color trigger ignored background color button, ensure all three instances have the same text
                        pPatternItem->pushButton_bgColor->setText(tr("Background color ignored"));
                    } else if (pT->mColorPatternList.at(i)->ansiBg == TTrigger::scmDefault) {
                        pPatternItem->pushButton_bgColor->setStyleSheet(QString());
                        //: Color trigger default background color button, ensure all three instances have the same text
                        pPatternItem->pushButton_bgColor->setText(tr("Default background color"));
                    } else {
                        pPatternItem->pushButton_bgColor->setStyleSheet(generateButtonStyleSheet(pT->mColorPatternList.at(i)->mBgColor));
                        //: Color trigger ANSI background color button, ensure all three instances have the same text
                        pPatternItem->pushButton_bgColor->setText(tr("Background color [ANSI %1]").arg(QString::number(pT->mColorPatternList.at(i)->ansiBg)));
                    }
                } else {
                    qWarning() << "dlgTriggerEditor::slot_triggerSelected(...) ERROR: TTrigger instance has an mColorPattern of size:" << pT->mColorPatternList.size() << "but array element:" << i
                               << "is a nullptr";
                    pPatternItem->pushButton_fgColor->setStyleSheet(QString());
                    pPatternItem->pushButton_fgColor->setText(tr("fault"));
                    pPatternItem->pushButton_bgColor->setStyleSheet(QString());
                    pPatternItem->pushButton_fgColor->setText(tr("fault"));
                }
            } else if (pType == REGEX_LINE_SPACER) {
                pPatternItem->spinBox_lineSpacer->setValue(patternList.at(i).toInt());
            } else {
                // Keep track of lineEdits that should have trailing spaces marked
                if (pType == REGEX_PERL) {
                    lineEditShouldMarkSpaces[pPatternItem->singleLineTextEdit_pattern] = true;
                }
                pPatternItem->singleLineTextEdit_pattern->setPlainText(patternList.at(i));
            }
        }

        // reset the rest of the patterns that don't have any data
        for (int i = patternList.size(); i < mVisiblePatternCount; i++) {
            auto* patternItem = mTriggerPatternEdit[i];
            patternItem->singleLineTextEdit_pattern->clear();
            patternItem->pushButton_fgColor->hide();
            patternItem->pushButton_bgColor->hide();
            patternItem->label_prompt->hide();
            patternItem->spinBox_lineSpacer->hide();
            patternItem->comboBox_patternType->setCurrentIndex(0);
        }
        // Scroll to the last used pattern:
        mpScrollArea->ensureWidgetVisible(mTriggerPatternEdit.at(qBound(0, patternList.size(), mVisiblePatternCount - 1)));
        const QString command = pT->getCommand();
        mpTriggersMainArea->lineEdit_trigger_name->setText(pItem->text(0));
        mpTriggersMainArea->label_idNumber->setText(QString::number(ID));
        mpTriggersMainArea->lineEdit_trigger_command->setText(command);
        mpTriggersMainArea->checkBox_perlSlashGOption->setChecked(pT->mPerlSlashGOption);
        mpTriggersMainArea->checkBox_filterTrigger->setChecked(pT->mFilterTrigger);
        if (pT->isMultiline()) {
            mpTriggersMainArea->spinBox_lineMargin->setValue(pT->getConditionLineDelta());
        } else {
            mpTriggersMainArea->spinBox_lineMargin->setValue(-1);
        }
        mpTriggersMainArea->spinBox_stayOpen->setValue(pT->mStayOpen);
        mpTriggersMainArea->groupBox_soundTrigger->setChecked(pT->mSoundTrigger);
        if (!pT->mSoundFile.isEmpty()) {
            mpTriggersMainArea->lineEdit_soundFile->setToolTip(pT->mSoundFile);
        }
        mpTriggersMainArea->lineEdit_soundFile->setText(pT->mSoundFile);
        mpTriggersMainArea->lineEdit_soundFile->setCursorPosition(mpTriggersMainArea->lineEdit_soundFile->text().length());
        mpTriggersMainArea->toolButton_clearSoundFile->setEnabled(!mpTriggersMainArea->lineEdit_soundFile->text().isEmpty());
        mpTriggersMainArea->groupBox_triggerColorizer->setChecked(pT->isColorizerTrigger());

        const QColor fgColor(pT->getFgColor());
        const QColor bgColor(pT->getBgColor());
        const bool transparentFg = fgColor == QColorConstants::Transparent;
        const bool transparentBg = bgColor == QColorConstants::Transparent;
        mpTriggersMainArea->pushButtonFgColor->setStyleSheet(generateButtonStyleSheet(fgColor, pT->isColorizerTrigger()));
        mpTriggersMainArea->pushButtonFgColor->setProperty(cButtonBaseColor, transparentFg ? qsl("transparent") : fgColor.name());
        //: Keep the existing colour on matches to highlight. Use shortest word possible so it fits on the button
        mpTriggersMainArea->pushButtonFgColor->setText(transparentFg ? tr("keep") : QString());
        mpTriggersMainArea->pushButtonBgColor->setStyleSheet(generateButtonStyleSheet(pT->getBgColor(), pT->isColorizerTrigger()));
        mpTriggersMainArea->pushButtonBgColor->setProperty(cButtonBaseColor, transparentBg ? qsl("transparent") : bgColor.name());
        //: Keep the existing colour on matches to highlight. Use shortest word possible so it fits on the button
        mpTriggersMainArea->pushButtonBgColor->setText(transparentBg ? tr("keep") : QString());

        checkForMoreThanOneTriggerItem();
        // The controls the strip reads only signal when their value changes, so
        // a trigger loaded over one that held the same values needs telling
        updateTriggerOptionsSummary();

        clearDocument(mpSourceEditorEdbee, pT->getScript());
        restoreEditorState(EditorViewType::cmTriggerView, ID);

        if (!pT->state()) {
            showError(pT->getError());
        } else {
            showPackageWarning(pT->packageName(pT), pItem);
        }
    } else {
        clearTriggerForm();
        // Re-enable updates since restoreEditorState won't be called
        mpSourceEditorEdbee->setUpdatesEnabled(true);
    }

    // Unblock property saves now that item loading is complete
    mBlockPropertySave = false;
}

void dlgTriggerEditor::slot_aliasSelected(QTreeWidgetItem* pItem)
{
    if (!pItem) {
        // No details to show - so show the help message:
        clearAliasForm();
        return;
    }

    // Only save previous item if switching to a different item
    if (pItem != mpCurrentAliasItem) {
        if (mpCurrentAliasItem) {
            saveEditorState(EditorViewType::cmAliasView, mpCurrentAliasItem->data(0, Qt::UserRole).toInt());
        }
        saveAlias();
    }

    // Disable updates during document loading to prevent visual flicker
    mpSourceEditorEdbee->setUpdatesEnabled(false);

    // Block property saves while loading the new item to prevent spurious undo entries
    mBlockPropertySave = true;

    mpCurrentAliasItem = pItem;
    mpAliasMainArea->show();
    mpSourceEditorArea->show();
    clearEditorNotification();
    mpAliasMainArea->lineEdit_alias_name->clear();
    mpAliasMainArea->label_idNumber->clear();
    mpAliasMainArea->lineEdit_alias_pattern->clear();
    mpAliasMainArea->lineEdit_alias_command->clear();
    clearDocument(mpSourceEditorEdbee); // Alias Select

    // mpAliasMainArea->lineEdit_alias_name->setText(pItem->text(0));
    const int ID = pItem->data(0, Qt::UserRole).toInt();
    TAlias* pT = mpHost->getAliasUnit()->getAlias(ID);
    if (pT) {
        const QString pattern = pT->getRegexCode();
        const QString command = pT->getCommand();
        const QString name = pT->getName();

        mpAliasMainArea->lineEdit_alias_pattern->setText(pattern);
        mpAliasMainArea->lineEdit_alias_command->setText(command);
        mpAliasMainArea->lineEdit_alias_name->setText(name);
        mpAliasMainArea->label_idNumber->setText(QString::number(ID));

        clearDocument(mpSourceEditorEdbee, pT->getScript());
        restoreEditorState(EditorViewType::cmAliasView, ID);

        if (!pT->state()) {
            showError(pT->getError());
        } else {
            showPackageWarning(pT->packageName(pT), pItem);
        }

    } else {
        // No details to show - as will be the case if the top item (ID = 0) is
        // selected - so show the help message:
        clearAliasForm();
        // Re-enable updates since restoreEditorState won't be called
        mpSourceEditorEdbee->setUpdatesEnabled(true);
    }

    // Unblock property saves now that item loading is complete
    mBlockPropertySave = false;
}

void dlgTriggerEditor::slot_keySelected(QTreeWidgetItem* pItem)
{
    if (!pItem) {
        // No details to show - so show the help message:
        clearKeyForm();
        return;
    }

    // Only save previous item if switching to a different item
    if (pItem != mpCurrentKeyItem) {
        if (mpCurrentKeyItem) {
            saveEditorState(EditorViewType::cmKeysView, mpCurrentKeyItem->data(0, Qt::UserRole).toInt());
        }
        saveKey();
    }

    // Disable updates during document loading to prevent visual flicker
    mpSourceEditorEdbee->setUpdatesEnabled(false);

    // Block property saves while loading the new item to prevent spurious undo entries
    mBlockPropertySave = true;

    mpCurrentKeyItem = pItem;
    mpKeysMainArea->show();
    mpSourceEditorArea->show();
    clearEditorNotification();
    mpKeysMainArea->lineEdit_key_command->clear();
    mpKeysMainArea->lineEdit_key_binding->clear();
    mpKeysMainArea->lineEdit_key_name->clear();
    mpKeysMainArea->label_idNumber->clear();
    clearDocument(mpSourceEditorEdbee); // Key Select

    mpKeysMainArea->lineEdit_key_binding->setText(pItem->text(0));
    const int ID = pItem->data(0, Qt::UserRole).toInt();
    TKey* pT = mpHost->getKeyUnit()->getKey(ID);
    if (pT) {
        const QString command = pT->getCommand();
        const QString name = pT->getName();
        mpKeysMainArea->lineEdit_key_command->setText(command);
        mpKeysMainArea->lineEdit_key_name->setText(name);
        mpKeysMainArea->label_idNumber->setText(QString::number(ID));
        const QString keyName = mpHost->getKeyUnit()->getKeyName(pT->getKeyCode(), pT->getKeyModifiers());
        mpKeysMainArea->lineEdit_key_binding->setText(keyName);

        clearDocument(mpSourceEditorEdbee, pT->getScript());
        restoreEditorState(EditorViewType::cmKeysView, ID);

        if (!pT->state()) {
            showError(pT->getError());
        } else {
            showPackageWarning(pT->packageName(pT), pItem);
        }
    } else {
        clearKeyForm();
        // Re-enable updates since restoreEditorState won't be called
        mpSourceEditorEdbee->setUpdatesEnabled(true);
    }

    // Unblock property saves now that item loading is complete
    mBlockPropertySave = false;
}

// This should not modify the contents of what pItem points at:
void dlgTriggerEditor::recurseVariablesUp(QTreeWidgetItem* const pItem, QList<QTreeWidgetItem*>& list)
{
    QTreeWidgetItem* pParentItem = pItem->parent();
    if (pParentItem && pParentItem != mpVarBaseItem) {
        list.append(pParentItem);
        recurseVariablesUp(pParentItem, list);
    }
}

// This should not modify the contents of what pItem points at:
void dlgTriggerEditor::recurseVariablesDown(QTreeWidgetItem* const pItem, QList<QTreeWidgetItem*>& list)
{
    list.append(pItem);
    for (int i = 0; i < pItem->childCount(); ++i) {
        recurseVariablesDown(pItem->child(i), list);
    }
}

// This WAS called recurseVariablesDown(TVar*, QList<TVar*>&, bool) but it is
// used for searching like the other resursiveSearchXxxxx(...) are
void dlgTriggerEditor::recursiveSearchVariables(TVar* var, QList<TVar*>& list, bool isSorted)
{
    list.append(var);
    QListIterator<TVar*> it(var->getChildren(isSorted));
    while (it.hasNext()) {
        recursiveSearchVariables(it.next(), list, isSorted);
    }
}

void dlgTriggerEditor::slot_variableChanged(QTreeWidgetItem* pItem)
{
    // This handles a small case where the radio button is clicked while the item is currently selected
    // which causes the variable to not save. In places where we populate the TreeWidgetItem, we have
    // to guard it with mChangingVar or else this will be called with every change such as the variable
    // name, etc.
    if (!pItem || mChangingVar) {
        return;
    }
    const int column = 0;
    const int state = pItem->checkState(column);
    LuaInterface* lI = mpHost->getLuaInterface();
    VarUnit* vu = lI->getVarUnit();
    TVar* var = vu->getWVar(pItem);
    if (!var) {
        return;
    }
    if (state == Qt::Checked || state == Qt::PartiallyChecked) {
        if (vu->isSaved(var)) {
            return;
        }
        // A tick is not on its own a decision to save: Qt's tristate cascade
        // ticks children the user cannot tick themselves, and marks a parent
        // partially ticked from a tick on any child, so what may be saved is
        // asked again here rather than read off the check state (#9957).
        if (!vu->shouldSave(var)) {
            return;
        }
        vu->addSavedVar(var);
        QList<QTreeWidgetItem*> list;
        recurseVariablesUp(pItem, list);
        for (auto& treeWidgetItem : list) {
            TVar* v = vu->getWVar(treeWidgetItem);
            if (v && (treeWidgetItem->checkState(column) == Qt::Checked || treeWidgetItem->checkState(column) == Qt::PartiallyChecked) && vu->shouldSave(v)) {
                vu->addSavedVar(v);
            }
        }
        list.clear();
        recurseVariablesDown(pItem, list);
        for (auto& treeWidgetItem : list) {
            TVar* v = vu->getWVar(treeWidgetItem);
            if (v && (treeWidgetItem->checkState(column) == Qt::Checked || treeWidgetItem->checkState(column) == Qt::PartiallyChecked) && vu->shouldSave(v)) {
                vu->addSavedVar(v);
            }
        }
    } else {
        // we're not checked, dont save us
        if (!vu->isSaved(var)) {
            return;
        }
        vu->removeSavedVar(var);
        QList<QTreeWidgetItem*> list;
        recurseVariablesUp(pItem, list);
        for (auto& treeWidgetItem : list) {
            TVar* v = vu->getWVar(treeWidgetItem);
            if (v && (treeWidgetItem->checkState(column) == Qt::Checked || treeWidgetItem->checkState(column) == Qt::PartiallyChecked)) {
                vu->removeSavedVar(v);
            }
        }
        list.clear();
        recurseVariablesDown(pItem, list);
        for (auto& treeWidgetItem : list) {
            TVar* v = vu->getWVar(treeWidgetItem);
            if (v && (treeWidgetItem->checkState(column) == Qt::Checked || treeWidgetItem->checkState(column) == Qt::PartiallyChecked)) {
                vu->removeSavedVar(v);
            }
        }
    }
}

void dlgTriggerEditor::slot_variableSelected(QTreeWidgetItem* pItem)
{
    if (!pItem || treeWidget_variables->indexOfTopLevelItem(pItem) == 0) {
        // Null item or it is for the first row of the tree
        clearVarForm();
        return;
    }

    clearEditorNotification();

    // save the current variable before switching to the new one
    if (pItem != mpCurrentVarItem) {
        saveVar();
    }

    mChangingVar = true;
    const int column = treeWidget_variables->currentColumn();
    const int state = pItem->checkState(column);
    LuaInterface* lI = mpHost->getLuaInterface();
    VarUnit* vu = lI->getVarUnit();
    TVar* var = vu->getWVar(pItem); // This does NOT modify pItem or what it points at
    QList<QTreeWidgetItem*> list;
    if (state == Qt::Checked || state == Qt::PartiallyChecked) {
        // What may be saved is asked again rather than read off the check state,
        // for the same reason slot_variableChanged() asks: a row can be left
        // ticked from before whatever now makes it unsaveable - a table grown
        // past the size limit, say - and clicking it would enrol it again.
        if (var && vu->shouldSave(var)) {
            vu->addSavedVar(var);
        }
        recurseVariablesUp(pItem, list); // This does NOT modify pItem or what it points at
        for (auto& treeWidgetItem : list) {
            TVar* v = vu->getWVar(treeWidgetItem);
            if (v && (treeWidgetItem->checkState(column) == Qt::Checked || treeWidgetItem->checkState(column) == Qt::PartiallyChecked) && vu->shouldSave(v)) {
                vu->addSavedVar(v);
            }
        }
        list.clear();
        recurseVariablesDown(pItem, list); // This does NOT modify pItem or what it points at
        for (auto& treeWidgetItem : list) {
            TVar* v = vu->getWVar(treeWidgetItem);
            if (v && (treeWidgetItem->checkState(column) == Qt::Checked || treeWidgetItem->checkState(column) == Qt::PartiallyChecked) && vu->shouldSave(v)) {
                vu->addSavedVar(v);
            }
        }
    } else {
        if (var) {
            vu->removeSavedVar(var);
        }
        recurseVariablesUp(pItem, list); // This does NOT modify pItem or what it points at
        for (auto& treeWidgetItem : list) {
            TVar* v = vu->getWVar(treeWidgetItem);
            if (v && (treeWidgetItem->checkState(column) == Qt::Unchecked)) {
                vu->removeSavedVar(v);
            }
        }
        list.clear();
        recurseVariablesDown(pItem, list); // This does NOT modify pItem or what it points at
        for (auto& treeWidgetItem : list) {
            TVar* v = vu->getWVar(treeWidgetItem);
            if (v && (treeWidgetItem->checkState(column) == Qt::Unchecked)) {
                vu->removeSavedVar(v);
            }
        }
    }
    mpVarsMainArea->show();
    mpSourceEditorArea->show();

    mpCurrentVarItem = pItem; //remember what has been clicked to save it

    if (column) {
        mChangingVar = false;
        return;
    }

    if (!var) {
        mpVarsMainArea->checkBox_variable_hidden->setChecked(false);
        clearDocument(mpSourceEditorEdbee); // Var Select
        //check for temp item
        var = vu->getTVar(pItem);
        if (var && var->getValueType() == LUA_TTABLE) {
            mpVarsMainArea->comboBox_variable_value_type->setDisabled(true);
            // index 4 = "table"
            mpVarsMainArea->comboBox_variable_value_type->setCurrentIndex(4);
        } else {
            mpVarsMainArea->comboBox_variable_value_type->setDisabled(false);
            // index 0 = "Auto-type"
            mpVarsMainArea->comboBox_variable_value_type->setCurrentIndex(0);
        }
        mpVarsMainArea->comboBox_variable_key_type->setCurrentIndex(0);
        mpVarsMainArea->comboBox_variable_key_type->setEnabled(true);
        mChangingVar = false;
        return;
    }

    const int varType = var->getValueType();
    const int keyType = var->getKeyType();
    QIcon icon;

    switch (keyType) {
        //    case LUA_TNONE: // -1
        //    case LUA_TNIL: // 0
    case LUA_TBOOLEAN: // 1
        // index 5 = "key (boolean)". Without this the combobox would keep
        // whatever the previously selected variable put there and name a key
        // type this variable does not have (#9959).
        mpVarsMainArea->comboBox_variable_key_type->setCurrentIndex(5);
        // a boolean key can be renamed between true and false, but it cannot be
        // recast: saveVar() puts a name that still reads as a boolean back to a
        // boolean key whatever the combobox says
        mpVarsMainArea->comboBox_variable_key_type->setEnabled(false);
        break;
        //    case LUA_TLIGHTUSERDATA: // 2
    case LUA_TNUMBER: // 3
        // index 2 = "index (integer number)"
        mpVarsMainArea->comboBox_variable_key_type->setCurrentIndex(2);
        mpVarsMainArea->comboBox_variable_key_type->setEnabled(true);
        break;
    case LUA_TSTRING: // 4
        // index 1 = "key (string)"
        mpVarsMainArea->comboBox_variable_key_type->setCurrentIndex(1);
        mpVarsMainArea->comboBox_variable_key_type->setEnabled(true);
        break;
    case LUA_TTABLE: // 5
        // index 3 = "table (use \"Add Group\" to create"
        mpVarsMainArea->comboBox_variable_key_type->setCurrentIndex(3);
        mpVarsMainArea->comboBox_variable_key_type->setEnabled(false);
        break;
    case LUA_TFUNCTION: // 6
        // index 4 = "function (cannot create from GUI)"
        mpVarsMainArea->comboBox_variable_key_type->setCurrentIndex(4);
        mpVarsMainArea->comboBox_variable_key_type->setEnabled(false);
        break;
        //    case LUA_TUSERDATA: // 7
        //    case LUA_TTHREAD: // 8
    }

    switch (varType) {
    case LUA_TNONE:
        [[fallthrough]];
    case LUA_TNIL:
        mpSourceEditorArea->hide();
        break;
    case LUA_TBOOLEAN:
        mpSourceEditorArea->show();
        mpSourceEditorEdbee->setEnabled(true);
        icon.addPixmap(QPixmap(qsl(":/icons/variable.png")), QIcon::Normal, QIcon::Off);
        // index 3 = "boolean"
        mpVarsMainArea->comboBox_variable_value_type->setCurrentIndex(3);
        mpVarsMainArea->comboBox_variable_value_type->setEnabled(true);
        break;
    case LUA_TNUMBER:
        mpSourceEditorArea->show();
        mpSourceEditorEdbee->setEnabled(true);
        icon.addPixmap(QPixmap(qsl(":/icons/variable.png")), QIcon::Normal, QIcon::Off);
        // index 2 = "number"
        mpVarsMainArea->comboBox_variable_value_type->setCurrentIndex(2);
        mpVarsMainArea->comboBox_variable_value_type->setEnabled(true);
        break;
    case LUA_TSTRING:
        mpSourceEditorArea->show();
        mpSourceEditorEdbee->setEnabled(true);
        icon.addPixmap(QPixmap(qsl(":/icons/variable.png")), QIcon::Normal, QIcon::Off);
        // index 1 = "string"
        mpVarsMainArea->comboBox_variable_value_type->setCurrentIndex(1);
        mpVarsMainArea->comboBox_variable_value_type->setEnabled(true);
        break;
    case LUA_TTABLE:
        mpSourceEditorArea->hide();
        mpSourceEditorEdbee->setEnabled(false);
        // Only allow the type to be changed away from a table if it is empty:
        mpVarsMainArea->comboBox_variable_value_type->setEnabled(!(pItem->childCount() > 0));
        // index 4 = "table"
        mpVarsMainArea->comboBox_variable_value_type->setCurrentIndex(4);
        icon.addPixmap(QPixmap(qsl(":/icons/table.png")), QIcon::Normal, QIcon::Off);
        break;
    case LUA_TFUNCTION:
        mpSourceEditorArea->hide();
        mpSourceEditorEdbee->setEnabled(false);
        mpVarsMainArea->comboBox_variable_value_type->setCurrentIndex(5);
        mpVarsMainArea->comboBox_variable_value_type->setEnabled(false);
        icon.addPixmap(QPixmap(qsl(":/icons/function.png")), QIcon::Normal, QIcon::Off);
        break;
    case LUA_TLIGHTUSERDATA:
        [[fallthrough]];
    case LUA_TUSERDATA:
        [[fallthrough]];
    case LUA_TTHREAD: {
    } // No-op
    }

    mpVarsMainArea->checkBox_variable_hidden->setChecked(vu->isHidden(var));
    mpVarsMainArea->lineEdit_var_name->setText(var->getName());
    clearDocument(mpSourceEditorEdbee, lI->getValue(var));
    pItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled | Qt::ItemIsDragEnabled | Qt::ItemIsAutoTristate | Qt::ItemIsUserCheckable);
    pItem->setToolTip(0, utils::richText(tr("Checked variables will be saved and loaded with your profile.")));
    pItem->setCheckState(0, Qt::Unchecked);
    if (!vu->shouldSave(var)) {
        pItem->setFlags(pItem->flags() & ~(Qt::ItemIsDropEnabled | Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable));
        pItem->setForeground(0, QBrush(QColor("grey")));
        const QString reason = vu->getUnsaveableReason(var);
        pItem->setToolTip(0, reason.isEmpty() ? QString() : utils::richText(reason));
    } else if (vu->isSaved(var)) {
        pItem->setCheckState(0, Qt::Checked);
    }
    pItem->setData(0, Qt::UserRole, var->getValueType());
    pItem->setIcon(0, icon);
    mChangingVar = false;
    // Said on selection rather than when the user tries to save: getValue() goes
    // by this same name, so for most of what is refused here the value box filled
    // in above is empty, and without this it reads as a real value.
    if (!lI->writableByName(var)) {
        //: Warning shown in the editor's Variables view for a variable it cannot write back to Lua. %1 is the name the variable is shown under.
        showWarning(tr("\"%1\" cannot be changed here: Mudlet cannot safely change it under the name it is shown with, so anything saved for it could go somewhere else. "
                       "Its value may show up blank for the same reason. A script can still change it.")
                            .arg(var->getName().toHtmlEscaped()));
    }
}

void dlgTriggerEditor::slot_actionSelected(QTreeWidgetItem* pItem)
{
    if (!pItem) {
        // No details to show - so show the help message:
        clearActionForm();
        return;
    }

    // Only save previous item if switching to a different item
    if (pItem != mpCurrentActionItem) {
        if (mpCurrentActionItem) {
            saveEditorState(EditorViewType::cmActionView, mpCurrentActionItem->data(0, Qt::UserRole).toInt());
        }
        saveAction();
    }

    // Disable updates during document loading to prevent visual flicker
    mpSourceEditorEdbee->setUpdatesEnabled(false);

    // Block property saves while loading the new item to prevent spurious undo entries
    mBlockPropertySave = true;

    mpActionsMainArea->show();
    mpSourceEditorArea->show();

    clearEditorNotification();
    clearDocument(mpSourceEditorEdbee); // Action Select

    mpActionsMainArea->lineEdit_action_icon->clear();
    mpActionsMainArea->lineEdit_action_name->clear();
    mpActionsMainArea->label_idNumber->clear();
    mpActionsMainArea->checkBox_action_button_isPushDown->setChecked(false);
    mpActionsMainArea->lineEdit_action_button_command_down->clear();
    mpActionsMainArea->lineEdit_action_button_command_up->clear();
    mpActionsMainArea->spinBox_action_bar_columns->clear();
    mpActionsMainArea->plainTextEdit_action_css->clear();
    mpActionsMainArea->comboBox_action_bar_location->setCurrentIndex(0);
    mpActionsMainArea->comboBox_action_bar_orientation->setCurrentIndex(0);
    mpActionsMainArea->comboBox_action_button_rotation->setCurrentIndex(0);
    mpActionsMainArea->spinBox_action_bar_columns->setValue(1);
    mpActionsMainArea->spinBox_action_bar_offsetToFirstButton->setMaximum(0);
    mpActionsMainArea->spinBox_action_bar_offsetToFirstButton->setEnabled(false);
    mpActionsMainArea->spinBox_action_bar_offsetToFirstButton->setValue(0);

    mpCurrentActionItem = pItem; //remember what has been clicked to save it
    // ID will be 0 for the root of the treewidget and it is not appropriate
    // to show any right hand side details - pT will also be nullptr!
    const int ID = pItem->data(0, Qt::UserRole).toInt();
    TAction* pT = mpHost->getActionUnit()->getAction(ID);
    if (pT) {
        mpActionsMainArea->lineEdit_action_name->setText(pT->getName());
        mpActionsMainArea->label_idNumber->setText(QString::number(ID));
        mpActionsMainArea->checkBox_action_button_isPushDown->setChecked(pT->isPushDownButton());
        mpActionsMainArea->label_action_button_command_up->hide();
        mpActionsMainArea->label_action_button_command_down->hide();
        mpActionsMainArea->lineEdit_action_button_command_up->hide();
        mpActionsMainArea->lineEdit_action_button_command_down->hide();
        mpActionsMainArea->label_action_button_command_down->setText(tr("Command:"));
        mpActionsMainArea->lineEdit_action_icon->setText(pT->getIcon());
        mpActionsMainArea->lineEdit_action_button_command_down->setText(pT->getCommandButtonDown());
        mpActionsMainArea->lineEdit_action_button_command_up->setText(pT->getCommandButtonUp());
        mpActionsMainArea->comboBox_action_button_rotation->setCurrentIndex(pT->getButtonRotation());

        clearDocument(mpSourceEditorEdbee, pT->getScript());
        restoreEditorState(EditorViewType::cmActionView, ID);

        // location = 1 = location = bottom is no longer supported
        int location = pT->mLocation;
        if (location > 0) {
            location--;
        }
        mpActionsMainArea->comboBox_action_bar_location->setCurrentIndex(location);
        mpActionsMainArea->comboBox_action_bar_orientation->setCurrentIndex(pT->mOrientation);
        mpActionsMainArea->comboBox_action_button_rotation->setCurrentIndex(pT->getButtonRotation());
        mpActionsMainArea->spinBox_action_bar_columns->setValue(pT->getButtonColumns());
        mpActionsMainArea->spinBox_action_bar_offsetToFirstButton->setValue(pT->getButtonFillerOffset());
        mpActionsMainArea->plainTextEdit_action_css->setPlainText(pT->css);
        if (pT->isFolder()) {
            if (!pT->mPackageName.isEmpty()) {
                // We have a non-empty package name (Tree<T>::mModuleName
                // is NEVER used but Tree<T>::mPackageName is for both!)
                // THUS: We are a module master folder

                mpActionsMainArea->groupBox_action_bar->hide();
                mpActionsMainArea->groupBox_action_button_appearance->hide();
                mpActionsMainArea->widget_top->hide();
                mpSourceEditorArea->hide();
            } else if (!pT->getParent() || (pT->getParent() && !pT->getParent()->mPackageName.isEmpty())) {
                // We are a top-level folder with no parent
                // OR: We have a parent and that IS a module master folder
                // THUS: We are a toolbar

                mpActionsMainArea->groupBox_action_bar->show();
                mpActionsMainArea->groupBox_action_button_appearance->hide();
                mpActionsMainArea->widget_top->show();
                mpSourceEditorArea->show();
            } else {
                // We must be a MENU

                mpActionsMainArea->groupBox_action_button_appearance->setTitle(tr("Menu properties"));
                mpActionsMainArea->groupBox_action_bar->hide();
                mpActionsMainArea->checkBox_action_button_isPushDown->hide();
                mpActionsMainArea->groupBox_action_button_appearance->show();
                mpActionsMainArea->widget_top->show();
                mpSourceEditorArea->show();
            }
        } else {
            // We are a BUTTON

            mpActionsMainArea->groupBox_action_button_appearance->setTitle(tr("Button properties"));
            mpActionsMainArea->groupBox_action_bar->hide();
            mpActionsMainArea->groupBox_action_button_appearance->show();
            mpActionsMainArea->label_action_button_command_down->show();
            mpActionsMainArea->lineEdit_action_button_command_down->show();
            mpActionsMainArea->checkBox_action_button_isPushDown->show();
            mpSourceEditorArea->show();
            if (pT->isPushDownButton()) {
                mpActionsMainArea->label_action_button_command_down->setText(tr("Command (down);"));
                mpActionsMainArea->lineEdit_action_button_command_up->show();
                mpActionsMainArea->label_action_button_command_up->show();
            }

            mpActionsMainArea->widget_top->show();
        }

        if (!pT->state()) {
            showError(pT->getError());
        } else {
            showPackageWarning(pT->packageName(pT), pItem);
        }
    } else {
        // On root of treewidget_actions: - show help message instead
        clearActionForm();
        // Re-enable updates since restoreEditorState won't be called
        mpSourceEditorEdbee->setUpdatesEnabled(true);
    }

    mBlockPropertySave = false;
}

void dlgTriggerEditor::slot_treeSelectionChanged()
{
    auto* sender = qobject_cast<TTreeWidget*>(QObject::sender());
    if (sender) {
        QTreeWidgetItem* item = sender->currentItem();
        if (!item) {
            QList<QTreeWidgetItem*> items = sender->selectedItems();
            if (items.empty()) {
                return;
            }
            item = items.first();
        }

        if (item) {
            if (sender == treeWidget_scripts) {
                slot_scriptsSelected(item);
            } else if (sender == treeWidget_keys) {
                slot_keySelected(item);
            } else if (sender == treeWidget_timers) {
                slot_timerSelected(item);
            } else if (sender == treeWidget_aliases) {
                slot_aliasSelected(item);
            } else if (sender == treeWidget_actions) {
                slot_actionSelected(item);
            } else if (sender == treeWidget_variables) {
                slot_variableSelected(item);
            } else if (sender == treeWidget_triggers) {
                slot_triggerSelected(item);
            }
        }
    }
}


void dlgTriggerEditor::slot_scriptsSelected(QTreeWidgetItem* pItem)
{
    if (!pItem) {
        // No details to show - so show the help message:
        clearScriptForm();
        return;
    }

    const int ID = pItem->data(0, Qt::UserRole).toInt();
    TScript* pT = mpHost->getScriptUnit()->getScript(ID);

    // Only save previous item if switching to a different item
    if (pItem != mpCurrentScriptItem) {
        if (mpCurrentScriptItem) {
            saveEditorState(EditorViewType::cmScriptView, mpCurrentScriptItem->data(0, Qt::UserRole).toInt());
        }
        saveScript();
    }

    // Disable updates during document loading to prevent visual flicker
    mpSourceEditorEdbee->setUpdatesEnabled(false);

    // Block property saves while loading the new item to prevent spurious undo entries
    mBlockPropertySave = true;

    mpCurrentScriptItem = pItem;
    mpScriptsMainArea->show();
    mpSourceEditorArea->show();
    clearEditorNotification();
    clearDocument(mpSourceEditorEdbee); // Script Select
    mpScriptsMainArea->lineEdit_script_name->clear();
    mpScriptsMainArea->label_idNumber->clear();
    mpScriptsMainArea->listWidget_script_registered_event_handlers->clear();
    // Has to stay after that clear(): it drops the selection before deleting the items,
    // and that selection change runs slot_scriptMainAreaEditHandler(), which notes an
    // item about to be freed. saveScript()'s nulling of the note runs too early to help,
    // and is skipped entirely when the same script is re-selected (#9835)
    slot_scriptMainAreaClearHandlerSelection(nullptr);

    if (pT) {
        const QString name = pT->getName();
        QStringList eventHandlerList = pT->getEventHandlerList();
        for (const QString& handler : std::as_const(eventHandlerList)) {
            auto pHandlerItem = new QListWidgetItem(mpScriptsMainArea->listWidget_script_registered_event_handlers);
            pHandlerItem->setText(handler);
            mpScriptsMainArea->listWidget_script_registered_event_handlers->addItem(pHandlerItem);
        }
        const QString script = pT->getScript();
        clearDocument(mpSourceEditorEdbee, script);
        restoreEditorState(EditorViewType::cmScriptView, ID);

        mpScriptsMainArea->lineEdit_script_name->setText(name);
        mpScriptsMainArea->label_idNumber->setText(QString::number(ID));
        if (auto error = pT->getLoadingError(); error) {
            showWarning(tr("While loading the profile, this script had an error that has since been fixed, "
                           "possibly by another script. The error was:%2%3")
                                .arg(qsl("<br>"), error.value()));
        } else if (!pT->state()) {
            showError(pT->getError());
        } else {
            showPackageWarning(pT->packageName(pT), pItem);
        }

    } else {
        // No details to show - as will be the case if the top item (ID = 0) is
        // selected - so show the help message:
        clearScriptForm();
        // Re-enable updates since restoreEditorState won't be called
        mpSourceEditorEdbee->setUpdatesEnabled(true);
    }

    mBlockPropertySave = false;
}

void dlgTriggerEditor::slot_timerSelected(QTreeWidgetItem* pItem)
{
    if (!pItem) {
        // No details to show - so show the help message:
        clearTimerForm();
        return;
    }

    // Only save previous item if switching to a different item
    if (pItem != mpCurrentTimerItem) {
        if (mpCurrentTimerItem) {
            saveEditorState(EditorViewType::cmTimerView, mpCurrentTimerItem->data(0, Qt::UserRole).toInt());
        }
        saveTimer();
    }

    // Disable updates during document loading to prevent visual flicker
    mpSourceEditorEdbee->setUpdatesEnabled(false);

    // Block property saves while loading the new item to prevent spurious undo entries
    mBlockPropertySave = true;

    mpCurrentTimerItem = pItem;
    mpTimersMainArea->show();
    mpSourceEditorArea->show();
    clearEditorNotification();
    clearDocument(mpSourceEditorEdbee); // Timer Select

    mpTimersMainArea->lineEdit_timer_command->clear();
    mpTimersMainArea->timeEdit_timer_hours->setTime(QTime(0, 0, 0, 0));
    mpTimersMainArea->timeEdit_timer_minutes->setTime(QTime(0, 0, 0, 0));
    mpTimersMainArea->timeEdit_timer_seconds->setTime(QTime(0, 0, 0, 0));
    mpTimersMainArea->timeEdit_timer_msecs->setTime(QTime(0, 0, 0, 0));
    mpTimersMainArea->label_idNumber->clear();
    // mpTimersMainArea->lineEdit_timer_name->setText(pItem->text(0));

    const int ID = pItem->data(0, Qt::UserRole).toInt();
    TTimer* pT = mpHost->getTimerUnit()->getTimer(ID);
    if (pT) {
        const QString command = pT->getCommand();
        const QString name = pT->getName();
        mpTimersMainArea->lineEdit_timer_command->setText(command);
        mpTimersMainArea->lineEdit_timer_name->setText(name);
        mpTimersMainArea->label_idNumber->setText(QString::number(ID));
        const QTime time = pT->getTime();
        mpTimersMainArea->timeEdit_timer_hours->setTime(QTime(time.hour(), 0, 0, 0));
        mpTimersMainArea->timeEdit_timer_minutes->setTime(QTime(0, time.minute(), 0, 0));
        mpTimersMainArea->timeEdit_timer_seconds->setTime(QTime(0, 0, time.second(), 0));
        mpTimersMainArea->timeEdit_timer_msecs->setTime(QTime(0, 0, 0, time.msec()));

        clearDocument(mpSourceEditorEdbee, pT->getScript());
        restoreEditorState(EditorViewType::cmTimerView, ID);

        if (!pT->state()) {
            showError(pT->getError());
        } else {
            showPackageWarning(pT->packageName(pT), pItem);
        }
    } else {
        clearTimerForm();
        // Re-enable updates since restoreEditorState won't be called
        mpSourceEditorEdbee->setUpdatesEnabled(true);
    }

    mBlockPropertySave = false;
}

void dlgTriggerEditor::fillout_form()
{
    mCurrentView = EditorViewType::cmUnknownView;
    mpCurrentTriggerItem = nullptr;
    mpCurrentAliasItem = nullptr;
    mpCurrentKeyItem = nullptr;
    mpCurrentActionItem = nullptr;
    mpCurrentScriptItem = nullptr;
    mpCurrentTimerItem = nullptr;
    mpCurrentVarItem = nullptr;

    mNeedUpdateData = false;
    mpTriggerBaseItem = new QTreeWidgetItem(static_cast<QTreeWidgetItem*>(nullptr), QStringList(tr("Triggers")));
    mpTriggerBaseItem->setIcon(0, QPixmap(qsl(":/icons/tools-wizard.png")));
    treeWidget_triggers->insertTopLevelItem(0, mpTriggerBaseItem);
    populateTriggers();
    mpTriggerBaseItem->setExpanded(true);
    treeWidget_triggers->setCurrentItem(mpTriggerBaseItem);

    mpTimerBaseItem = new QTreeWidgetItem(static_cast<QTreeWidgetItem*>(nullptr), QStringList(tr("Timers")));
    mpTimerBaseItem->setIcon(0, QPixmap(qsl(":/icons/chronometer.png")));
    treeWidget_timers->insertTopLevelItem(0, mpTimerBaseItem);
    populateTimers();
    mpTimerBaseItem->setExpanded(true);
    treeWidget_timers->setCurrentItem(mpTimerBaseItem);

    mpScriptsBaseItem = new QTreeWidgetItem(static_cast<QTreeWidgetItem*>(nullptr), QStringList(tr("Scripts")));
    mpScriptsBaseItem->setIcon(0, QPixmap(qsl(":/icons/accessories-text-editor.png")));
    treeWidget_scripts->insertTopLevelItem(0, mpScriptsBaseItem);
    populateScripts();
    mpScriptsBaseItem->setExpanded(true);
    treeWidget_scripts->setCurrentItem(mpScriptsBaseItem);

    mpAliasBaseItem = new QTreeWidgetItem(static_cast<QTreeWidgetItem*>(nullptr), QStringList(tr("Aliases - Input Triggers")));
    mpAliasBaseItem->setIcon(0, QPixmap(qsl(":/icons/system-users.png")));
    treeWidget_aliases->insertTopLevelItem(0, mpAliasBaseItem);
    populateAliases();
    mpAliasBaseItem->setExpanded(true);
    treeWidget_aliases->setCurrentItem(mpAliasBaseItem);

    mpActionBaseItem = new QTreeWidgetItem(static_cast<QTreeWidgetItem*>(nullptr), QStringList(tr("Buttons")));
    mpActionBaseItem->setIcon(0, QPixmap(qsl(":/icons/bookmarks.png")));
    treeWidget_actions->insertTopLevelItem(0, mpActionBaseItem);
    populateActions();
    mpActionBaseItem->setExpanded(true);
    treeWidget_actions->setCurrentItem(mpActionBaseItem);

    mpKeyBaseItem = new QTreeWidgetItem(static_cast<QTreeWidgetItem*>(nullptr), QStringList(tr("Key Bindings")));
    mpKeyBaseItem->setIcon(0, QPixmap(qsl(":/icons/preferences-desktop-keyboard.png")));
    treeWidget_keys->insertTopLevelItem(0, mpKeyBaseItem);
    populateKeys();
    mpKeyBaseItem->setExpanded(true);
    treeWidget_keys->setCurrentItem(mpKeyBaseItem);

    // Clear undo stack after initial profile loading (only on first call)
    // Only user actions after this point should be undo-able
    if (mpUndoStack && !mInitialLoadDone) {
        mpUndoStack->clear();

        mInitialLoadDone = true;
    }
}

void dlgTriggerEditor::populateKeys()
{
    std::list<TKey*> const baseNodeList_key = mpHost->getKeyUnit()->getKeyRootNodeList();
    for (auto key : baseNodeList_key) {
        if (key->isTemporary()) {
            continue;
        }

        const QString s = key->getName();
        QStringList sList;
        sList << s;
        auto pItem = new QTreeWidgetItem(mpKeyBaseItem, sList);
        pItem->setData(0, Qt::UserRole, QVariant(key->getID()));
        mpKeyBaseItem->addChild(pItem);
        QIcon icon;
        QString itemDescription;
        const bool itemActive = key->isActive();
        if (key->hasChildren()) {
            expand_child_key(key, pItem);
        }
        if (key->state()) {
            clearEditorNotification();

            if (key->isFolder()) {
                itemDescription = (itemActive ? descActiveFolder : descInactiveFolder);
                if (!key->mPackageName.isEmpty()) {
                    if (key->isActive()) {
                        if (key->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-brown.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-brown-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                } else if (key->isActive()) {
                    if (key->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-pink.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    if (key->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-pink-locked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else {
                if (key->isActive()) {
                    itemDescription = descActive;
                    if (key->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactive;
                    if (key->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            }
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(key->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}
void dlgTriggerEditor::populateActions()
{
    std::list<TAction*> const baseNodeList_action = mpHost->getActionUnit()->getActionRootNodeList();
    for (auto action : baseNodeList_action) {
        if (action->isTemporary()) {
            continue;
        }

        const QString s = action->getName();
        QStringList sList;
        sList << s;
        auto pItem = new QTreeWidgetItem(mpActionBaseItem, sList);
        pItem->setData(0, Qt::UserRole, QVariant(action->getID()));
        mpActionBaseItem->addChild(pItem);
        QIcon icon;
        QString itemDescription;
        if (action->hasChildren()) {
            expand_child_action(action, pItem);
        }
        if (action->state()) {
            clearEditorNotification();
            const bool itemActive = action->isActive();
            if (action->isFolder()) {
                itemDescription = (itemActive ? descActiveFolder : descInactiveFolder);
                if (!action->mPackageName.isEmpty()) {
                    if (itemActive) {
                        if (action->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-brown.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-brown-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                } else if (!action->getParent() || !action->getParent()->mPackageName.isEmpty()) {
                    if (itemActive) {
                        if (action->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-yellow.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-yellow-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                } else {
                    if (itemActive) {
                        if (action->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-cyan.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-cyan-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else {
                if (itemActive) {
                    itemDescription = descActive;
                    if (action->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactive;
                }
            }
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(action->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}
void dlgTriggerEditor::populateAliases()
{
    std::list<TAlias*> const baseNodeList_alias = mpHost->getAliasUnit()->getAliasRootNodeList();
    for (auto alias : baseNodeList_alias) {
        if (alias->isTemporary()) {
            continue;
        }

        const QString s = alias->getName();
        QStringList sList;
        sList << s;
        auto pItem = new QTreeWidgetItem(mpAliasBaseItem, sList);
        pItem->setData(0, Qt::UserRole, QVariant(alias->getID()));
        mpAliasBaseItem->addChild(pItem);
        QIcon icon;
        QString itemDescription;
        const bool itemActive = alias->isActive();
        if (alias->hasChildren()) {
            expand_child_alias(alias, pItem);
        }
        if (alias->state()) {
            clearEditorNotification();

            if (alias->isFolder()) {
                itemDescription = (itemActive ? descActiveFolder : descInactiveFolder);
                if (!alias->mPackageName.isEmpty()) {
                    if (itemActive) {
                        if (alias->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-brown.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-brown-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                } else if (itemActive) {
                    if (alias->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-violet.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    if (alias->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-violet-locked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else {
                if (alias->isActive()) {
                    itemDescription = descActive;
                    if (alias->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactive;
                    if (alias->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            }
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(alias->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}
void dlgTriggerEditor::populateScripts()
{
    std::list<TScript*> const baseNodeList_scripts = mpHost->getScriptUnit()->getScriptRootNodeList();
    for (auto script : baseNodeList_scripts) {
        const QString s = script->getName();

        QStringList sList;
        sList << s;
        auto pItem = new QTreeWidgetItem(mpScriptsBaseItem, sList);
        pItem->setData(0, Qt::UserRole, QVariant(script->getID()));
        mpScriptsBaseItem->addChild(pItem);
        QIcon icon;
        QString itemDescription;
        const bool itemActive = script->isActive();
        if (script->hasChildren()) {
            expand_child_scripts(script, pItem);
        }
        if (script->state()) {
            clearEditorNotification();

            if (script->isFolder()) {
                itemDescription = (itemActive ? descActiveFolder : descInactiveFolder);
                if (!script->mPackageName.isEmpty()) {
                    if (itemActive) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-brown.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-brown-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                } else {
                    if (itemActive) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-orange.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-orange-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else {
                if (script->isActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descActive;
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactive;
                }
            }
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(script->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}
void dlgTriggerEditor::populateTimers()
{
    std::list<TTimer*> const baseNodeList_timers = mpHost->getTimerUnit()->getTimerRootNodeList();
    for (auto timer : baseNodeList_timers) {
        if (timer->isTemporary()) {
            continue;
        }
        const QString s = timer->getName();
        QStringList sList;
        sList << s;
        auto pItem = new QTreeWidgetItem(mpTimerBaseItem, sList);
        pItem->setData(0, Qt::UserRole, QVariant(timer->getID()));
        mpTimerBaseItem->addChild(pItem);
        QIcon icon;
        QString itemDescription;
        const bool itemActive = timer->isActive();
        if (timer->hasChildren()) {
            expand_child_timers(timer, pItem);
        }
        if (timer->state()) {
            clearEditorNotification();

            if (timer->isFolder()) {
                itemDescription = (itemActive ? descActiveFolder : descInactiveFolder);
                if (!timer->mPackageName.isEmpty()) {
                    if (itemActive) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-brown.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-brown-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                } else {
                    if (timer->shouldBeActive()) {
                        if (timer->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-green.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        if (timer->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-green-locked.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
                        }
                    }
                }
            } else {
                if (timer->isOffsetTimer()) {
                    if (timer->shouldBeActive()) {
                        itemDescription = descActiveOffsetTimer;
                        if (timer->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-on.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-on-grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        itemDescription = descInactiveOffsetTimer;
                        if (timer->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-off.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-off-grey.png")), QIcon::Normal, QIcon::Off);
                        }
                    }
                } else {
                    if (timer->shouldBeActive()) {
                        itemDescription = descActive;
                        if (timer->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactive;
                    }
                }
            }
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(timer->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}
void dlgTriggerEditor::populateTriggers()
{
    std::list<TTrigger*> const baseNodeList = mpHost->getTriggerUnit()->getTriggerRootNodeList();
    for (auto trigger : baseNodeList) {
        if (trigger->isTemporary()) {
            continue;
        }
        const QString s = trigger->getName();
        QStringList sList;
        sList << s;
        auto pItem = new QTreeWidgetItem(mpTriggerBaseItem, sList);
        pItem->setData(0, Qt::UserRole, QVariant(trigger->getID()));
        mpTriggerBaseItem->addChild(pItem);
        QIcon icon;
        QString itemDescription;
        const bool itemActive = trigger->isActive();
        if (trigger->hasChildren()) {
            expand_child_triggers(trigger, pItem);
        }
        if (trigger->state()) {
            clearEditorNotification();

            if (trigger->isFilterChain()) {
                if (itemActive) {
                    itemDescription = descActiveFilterChain;
                    if (trigger->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/filter.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/filter-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactiveFilterChain;
                    if (trigger->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/filter-locked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/filter-grey-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else if (trigger->isFolder()) {
                itemDescription = (itemActive ? descActiveFolder : descInactiveFolder);
                if (!trigger->mPackageName.isEmpty()) {
                    if (itemActive) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-brown.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-brown-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                } else if (itemActive) {
                    if (trigger->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-blue.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    if (trigger->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-blue-locked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else {
                if (itemActive) {
                    itemDescription = descActive;
                    if (trigger->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactive;
                    if (trigger->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            }
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(trigger->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}

void dlgTriggerEditor::repopulateVars()
{
    treeWidget_variables->setUpdatesEnabled(false);
    mpVarBaseItem = new QTreeWidgetItem(QStringList(tr("Variables")));
    mpVarBaseItem->setTextAlignment(0, Qt::AlignLeft | Qt::AlignVCenter);
    mpVarBaseItem->setIcon(0, QPixmap(qsl(":/icons/variables.png")));
    treeWidget_variables->clear();
    mpCurrentVarItem = nullptr;
    treeWidget_variables->insertTopLevelItem(0, mpVarBaseItem);
    mpVarBaseItem->setExpanded(true);
    LuaInterface* lI = mpHost->getLuaInterface();
    lI->getVars(false);
    VarUnit* vu = lI->getVarUnit();
    vu->buildVarTree(mpVarBaseItem, vu->getBase(), showHiddenVars);
    mpVarBaseItem->setExpanded(true);
    treeWidget_variables->setUpdatesEnabled(true);
    treeWidget_variables->setCurrentItem(mpVarBaseItem);
}

void dlgTriggerEditor::expand_child_triggers(TTrigger* pTriggerParent, QTreeWidgetItem* pWidgetItemParent)
{
    std::list<TTrigger*>* childrenList = pTriggerParent->getChildrenList();
    for (auto trigger : *childrenList) {
        const QString s = trigger->getName();
        QStringList sList;
        sList << s;
        auto pItem = new QTreeWidgetItem(pWidgetItemParent, sList);
        pItem->setData(0, Qt::UserRole, trigger->getID());

        pWidgetItemParent->insertChild(0, pItem);
        QIcon icon;
        QString itemDescription;
        if (trigger->hasChildren()) {
            expand_child_triggers(trigger, pItem);
        }
        if (trigger->state()) {
            clearEditorNotification();

            if (trigger->isFilterChain()) {
                if (trigger->isActive()) {
                    itemDescription = descActiveFilterChain;
                    if (trigger->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/filter.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/filter-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactiveFilterChain;
                    if (trigger->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/filter-locked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/filter-grey-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else if (trigger->isFolder()) {
                if (trigger->isActive()) {
                    itemDescription = descActiveFolder;
                    if (trigger->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-blue.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactiveFolder;
                    if (trigger->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-blue-locked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else {
                if (trigger->isActive()) {
                    itemDescription = descActive;
                    if (trigger->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactive;
                    if (trigger->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            }
            //pItem->setDisabled(!trigger->ancestorsActive());
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(trigger->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}

void dlgTriggerEditor::expand_child_key(TKey* pTriggerParent, QTreeWidgetItem* pWidgetItemParent)
{
    std::list<TKey*>* childrenList = pTriggerParent->getChildrenList();
    for (auto key : *childrenList) {
        const QString s = key->getName();
        QStringList sList;
        sList << s;
        auto pItem = new QTreeWidgetItem(pWidgetItemParent, sList);
        pItem->setData(0, Qt::UserRole, key->getID());

        pWidgetItemParent->insertChild(0, pItem);
        QIcon icon;
        QString itemDescription;
        if (key->hasChildren()) {
            expand_child_key(key, pItem);
        }
        if (key->state()) {
            clearEditorNotification();

            if (key->isFolder()) {
                if (key->isActive()) {
                    itemDescription = descActiveFolder;
                    if (key->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-pink.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactiveFolder;
                    if (key->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-pink-locked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else {
                if (key->isActive()) {
                    itemDescription = descActive;
                    if (key->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactive;
                    if (key->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            }
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(key->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}


void dlgTriggerEditor::expand_child_scripts(TScript* pTriggerParent, QTreeWidgetItem* pWidgetItemParent)
{
    std::list<TScript*>* childrenList = pTriggerParent->getChildrenList();
    for (auto script : *childrenList) {
        const QString s = script->getName();
        QStringList sList;
        sList << s;
        auto pItem = new QTreeWidgetItem(pWidgetItemParent, sList);
        pItem->setData(0, Qt::UserRole, script->getID());

        pWidgetItemParent->insertChild(0, pItem);
        QIcon icon;
        QString itemDescription;
        if (script->hasChildren()) {
            expand_child_scripts(script, pItem);
        }
        if (script->state()) {
            clearEditorNotification();

            if (script->isFolder()) {
                if (script->isActive()) {
                    itemDescription = descActiveFolder;
                    if (script->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-orange.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-orange-locked.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveFolder;
                }
            } else {
                if (script->isActive()) {
                    itemDescription = descActive;
                    if (script->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactive;
                }
            }
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(script->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}

void dlgTriggerEditor::expand_child_alias(TAlias* pTriggerParent, QTreeWidgetItem* pWidgetItemParent)
{
    std::list<TAlias*>* childrenList = pTriggerParent->getChildrenList();
    for (auto alias : *childrenList) {
        const QString s = alias->getName();
        QStringList sList;
        sList << s;
        auto pItem = new QTreeWidgetItem(pWidgetItemParent, sList);
        pItem->setData(0, Qt::UserRole, alias->getID());

        pWidgetItemParent->insertChild(0, pItem);
        QIcon icon;
        QString itemDescription;
        if (alias->hasChildren()) {
            expand_child_alias(alias, pItem);
        }
        if (alias->state()) {
            clearEditorNotification();

            if (alias->isFolder()) {
                if (alias->isActive()) {
                    itemDescription = descActiveFolder;
                    if (alias->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-violet.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactiveFolder;
                    if (alias->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-violet-locked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else {
                if (alias->isActive()) {
                    itemDescription = descActive;
                    if (alias->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactive;
                    if (alias->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            }
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(alias->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}

void dlgTriggerEditor::expand_child_action(TAction* pTriggerParent, QTreeWidgetItem* pWidgetItemParent)
{
    std::list<TAction*>* childrenList = pTriggerParent->getChildrenList();
    for (auto action : *childrenList) {
        const QString s = action->getName();
        QStringList sList;
        sList << s;
        auto pItem = new QTreeWidgetItem(pWidgetItemParent, sList);
        pItem->setData(0, Qt::UserRole, action->getID());

        pWidgetItemParent->insertChild(0, pItem);
        QIcon icon;
        QString itemDescription;
        if (action->hasChildren()) {
            expand_child_action(action, pItem);
        }
        if (action->state()) {
            clearEditorNotification();

            if (!action->getParent()->mPackageName.isEmpty()) {
                // Must have a parent (or would not be IN this method) and the
                // parent has a package name - this is a toolbar
                if (action->isActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-yellow.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descActiveFolder;
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-yellow-locked.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveFolder;
                }
            } else if (action->isFolder()) {
                // Is a folder and is not a toolbar - this is a menu
                if (action->isActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-cyan.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descActiveFolder;
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/folder-cyan-locked.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactiveFolder;
                }
            } else {
                // Is a button
                if (action->isActive()) {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descActive;
                } else {
                    icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                    itemDescription = descInactive;
                }
            }
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(action->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}

void dlgTriggerEditor::expand_child_timers(TTimer* pTimerParent, QTreeWidgetItem* pWidgetItemParent)
{
    std::list<TTimer*>* childrenList = pTimerParent->getChildrenList();
    for (auto timer : *childrenList) {
        const QString s = timer->getName();
        QStringList sList;
        sList << s;
        auto pItem = new QTreeWidgetItem(pWidgetItemParent, sList);
        pItem->setData(0, Qt::UserRole, timer->getID());

        pWidgetItemParent->insertChild(0, pItem);
        QIcon icon;
        QString itemDescription;
        if (timer->hasChildren()) {
            expand_child_timers(timer, pItem);
        }
        if (timer->state()) {
            clearEditorNotification();

            if (timer->isFolder()) {
                if (timer->shouldBeActive()) {
                    if (timer->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-green.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descActiveFolder;
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey.png")), QIcon::Normal, QIcon::Off);
                        itemDescription = descInactiveParent.arg(itemDescription);
                    }
                } else {
                    itemDescription = descInactiveFolder;
                    if (timer->ancestorsActive()) {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-green-locked.png")), QIcon::Normal, QIcon::Off);
                    } else {
                        icon.addPixmap(QPixmap(qsl(":/icons/folder-grey-locked.png")), QIcon::Normal, QIcon::Off);
                    }
                }
            } else {
                if (timer->isOffsetTimer()) {
                    if (timer->shouldBeActive()) {
                        itemDescription = descActiveOffsetTimer;
                        if (timer->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-on.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-on-grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        itemDescription = descInactiveOffsetTimer;
                        if (timer->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-off.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/offsettimer-off-grey.png")), QIcon::Normal, QIcon::Off);
                        }
                    }
                } else {
                    if (timer->shouldBeActive()) {
                        itemDescription = descActive;
                        if (timer->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox_checked_grey.png")), QIcon::Normal, QIcon::Off);
                            itemDescription = descInactiveParent.arg(itemDescription);
                        }
                    } else {
                        itemDescription = descInactive;
                        if (timer->ancestorsActive()) {
                            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox.png")), QIcon::Normal, QIcon::Off);
                        } else {
                            icon.addPixmap(QPixmap(qsl(":/icons/tag_checkbox-grey.png")), QIcon::Normal, QIcon::Off);
                        }
                    }
                }
            }
            pItem->setIcon(0, icon);
        } else {
            QIcon iconError;
            iconError.addPixmap(QPixmap(qsl(":/icons/tools-report-bug.png")), QIcon::Normal, QIcon::Off);
            itemDescription = descError;
            pItem->setIcon(0, iconError);
            showError(timer->getError());
        }
        pItem->setData(0, Qt::AccessibleDescriptionRole, itemDescription);
    }
}

void dlgTriggerEditor::saveOpenChanges()
{
    beginSaveErrorCapture();

    switch (mCurrentView) {
    case EditorViewType::cmTriggerView:
        if (mpCurrentTriggerItem) {
            saveEditorState(EditorViewType::cmTriggerView, mpCurrentTriggerItem->data(0, Qt::UserRole).toInt());
        }
        saveTrigger();
        break;
    case EditorViewType::cmTimerView:
        if (mpCurrentTimerItem) {
            saveEditorState(EditorViewType::cmTimerView, mpCurrentTimerItem->data(0, Qt::UserRole).toInt());
        }
        saveTimer();
        break;
    case EditorViewType::cmAliasView:
        if (mpCurrentAliasItem) {
            saveEditorState(EditorViewType::cmAliasView, mpCurrentAliasItem->data(0, Qt::UserRole).toInt());
        }
        saveAlias();
        break;
    case EditorViewType::cmScriptView:
        if (mpCurrentScriptItem) {
            saveEditorState(EditorViewType::cmScriptView, mpCurrentScriptItem->data(0, Qt::UserRole).toInt());
        }
        saveScript();
        break;
    case EditorViewType::cmActionView:
        if (mpCurrentActionItem) {
            saveEditorState(EditorViewType::cmActionView, mpCurrentActionItem->data(0, Qt::UserRole).toInt());
        }
        saveAction();
        break;
    case EditorViewType::cmKeysView:
        if (mpCurrentKeyItem) {
            saveEditorState(EditorViewType::cmKeysView, mpCurrentKeyItem->data(0, Qt::UserRole).toInt());
        }
        saveKey();
        break;
    case EditorViewType::cmVarsView:
        saveVar();
        break;
    case EditorViewType::cmUnknownView:
        break;
    }

    endSaveErrorCapture();
}

// Helper function to determine the current view from which tree widget is visible with selection.
// This is used as a fallback when mCurrentView is cmUnknownView due to initialization timing issues.
EditorViewType dlgTriggerEditor::determineViewFromVisibleTree()
{
    if (treeWidget_triggers->isVisible() && treeWidget_triggers->currentItem()) {
        return EditorViewType::cmTriggerView;
    }
    if (treeWidget_aliases->isVisible() && treeWidget_aliases->currentItem()) {
        return EditorViewType::cmAliasView;
    }
    if (treeWidget_timers->isVisible() && treeWidget_timers->currentItem()) {
        return EditorViewType::cmTimerView;
    }
    if (treeWidget_scripts->isVisible() && treeWidget_scripts->currentItem()) {
        return EditorViewType::cmScriptView;
    }
    if (treeWidget_actions->isVisible() && treeWidget_actions->currentItem()) {
        return EditorViewType::cmActionView;
    }
    if (treeWidget_keys->isVisible() && treeWidget_keys->currentItem()) {
        return EditorViewType::cmKeysView;
    }
    if (treeWidget_variables->isVisible() && treeWidget_variables->currentItem()) {
        return EditorViewType::cmVarsView;
    }
    return EditorViewType::cmUnknownView;
}

EditorViewType dlgTriggerEditor::resolveCurrentView()
{
    if (mCurrentView != EditorViewType::cmUnknownView) {
        return mCurrentView;
    }
    EditorViewType resolved = determineViewFromVisibleTree();
    if (resolved != EditorViewType::cmUnknownView) {
        mCurrentView = resolved;
    }
    return mCurrentView;
}

void dlgTriggerEditor::timerEvent(QTimerEvent* event)
{
    Q_UNUSED(event)

    if (isActiveWindow()) {
        autoSave();
    }
}

void dlgTriggerEditor::autoSave()
{
    mpHost->saveProfile(QString(), qsl("autosave"));
    if (mpLabel_statusAutosave) {
        //: Editor status bar, trailing edge. %1 is a time of day in the player's own format.
        mpLabel_statusAutosave->setText(tr("Autosaved %1").arg(QLocale::system().toString(QTime::currentTime(), QLocale::ShortFormat)));
    }
}

void dlgTriggerEditor::enterEvent(TEnterEvent* event)
{
    Q_UNUSED(event)
    if (mNeedUpdateData) {
        saveOpenChanges();
        treeWidget_triggers->clear();
        treeWidget_aliases->clear();
        treeWidget_timers->clear();
        treeWidget_scripts->clear();
        treeWidget_actions->clear();
        treeWidget_keys->clear();
        treeWidget_variables->clear();
        fillout_form();
        mNeedUpdateData = false;
        resolveCurrentView();
    }
}

void dlgTriggerEditor::focusInEvent(QFocusEvent* pE)
{
    Q_UNUSED(pE)
    if (mNeedUpdateData) {
        saveOpenChanges();
        treeWidget_triggers->clear();
        treeWidget_aliases->clear();
        treeWidget_timers->clear();
        treeWidget_scripts->clear();
        treeWidget_actions->clear();
        treeWidget_keys->clear();
        treeWidget_variables->clear();
        fillout_form();
        mNeedUpdateData = false;
        resolveCurrentView();
    }

    if (mCurrentView == EditorViewType::cmUnknownView) {
        mpCurrentTriggerItem = nullptr;
        mpCurrentAliasItem = nullptr;
        mpCurrentKeyItem = nullptr;
        mpCurrentActionItem = nullptr;
        mpCurrentScriptItem = nullptr;
        mpCurrentTimerItem = nullptr;
        return;
    }

    if (mpCurrentTriggerItem) {
        mpCurrentTriggerItem->setSelected(true);
    }
    if (mpCurrentTimerItem) {
        mpCurrentTimerItem->setSelected(true);
    }
    if (mpCurrentAliasItem) {
        mpCurrentAliasItem->setSelected(true);
    }
    if (mpCurrentScriptItem) {
        mpCurrentScriptItem->setSelected(true);
    }
    if (mpCurrentActionItem) {
        mpCurrentActionItem->setSelected(true);
    }
    if (mpCurrentKeyItem) {
        mpCurrentKeyItem->setSelected(true);
    }
}

void dlgTriggerEditor::focusOutEvent(QFocusEvent* pE)
{
    Q_UNUSED(pE)

    saveOpenChanges();
}

void dlgTriggerEditor::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);

    // The breakpoint the sidebar collapses at is measured off splitter_main's
    // minimum size hint, and a splitter that has never been shown reports the
    // hint of a layout that has never been run - a few hundred pixels short of
    // what the editor actually needs. The first measurement worth keeping is
    // therefore the first one taken with the window on screen.
    if (!mEditorFirstShown) {
        mEditorFirstShown = true;
        invalidateEditorSidebarWidths();
        updateEditorSidebarMode();
    }

    // A placement the user chose is theirs to keep, so the editor is only moved
    // when it could not be used where it is - its screen unplugged, or the
    // desktop it was left on resized away from underneath it - or when it has
    // no chosen placement yet and the profile it belongs to is on another
    // screen, which is the reattachment case this used to re-centre for on
    // every single show
    if (!windowPlacementReachable(pos(), size()) || (!mEditorPlacementChosen && !onSameScreenAsProfile())) {
        repositionOnProfileScreen();
    }
}

void dlgTriggerEditor::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);

    // Where the user drags the window to is a placement to keep; a move the
    // editor made itself is not one
    if (isVisible() && !mRepositioningEditorWindow) {
        mEditorPlacementChosen = true;
    }
}

void dlgTriggerEditor::changeView(EditorViewType view)
{
    saveOpenChanges();

    if (mNeedUpdateData) {
        treeWidget_triggers->clear();
        treeWidget_aliases->clear();
        treeWidget_timers->clear();
        treeWidget_scripts->clear();
        treeWidget_actions->clear();
        treeWidget_keys->clear();
        treeWidget_variables->clear();
        fillout_form();
        mNeedUpdateData = false;
        resolveCurrentView();
    }

    // in lieu of readonly
    mpSourceEditorEdbee->setEnabled(true);

    if (mCurrentView != view) {
        // Clear the current item pointer for the old view so that when we return,
        // the item will be properly reloaded (not considered "same")
        switch (mCurrentView) {
        case EditorViewType::cmTriggerView:
            mpCurrentTriggerItem = nullptr;
            break;
        case EditorViewType::cmTimerView:
            mpCurrentTimerItem = nullptr;
            break;
        case EditorViewType::cmAliasView:
            mpCurrentAliasItem = nullptr;
            break;
        case EditorViewType::cmScriptView:
            mpCurrentScriptItem = nullptr;
            break;
        case EditorViewType::cmActionView:
            mpCurrentActionItem = nullptr;
            break;
        case EditorViewType::cmKeysView:
            mpCurrentKeyItem = nullptr;
            break;
        case EditorViewType::cmVarsView:
        case EditorViewType::cmUnknownView:
            break;
        }
        // Disable updates during view change to prevent visual flicker
        // (selection handler's restoreEditorState will re-enable)
        mpSourceEditorEdbee->setUpdatesEnabled(false);
        clearDocument(mpSourceEditorEdbee); // Change View
    }
    mCurrentView = view;

    const bool bannerUndoToastShowing = mpBannerUndoTimer && mpBannerUndoTimer->isActive();
    cancelBannerUndoTimer();

    // A banner (or the dismissal undo toast) belongs to the view it was shown
    // in, so hide it on a view change - otherwise it lingers over the new view
    // when that view's own banner is suppressed. showIntro() will put up the
    // right banner for the new view if one is allowed. Errors and warnings
    // (which clear mCurrentBannerKey) are not hidden by this block, though the
    // pre-existing permanently-hidden check below still can hide them. Using
    // clearEditorNotification() rather than hideSystemMessageArea() as the
    // latter would also discard the current script's unacknowledged loading
    // error.
    if (bannerUndoToastShowing || !mCurrentBannerKey.isEmpty()) {
        clearEditorNotification();
    }

    if (bannerPermanentlyHidden(mCurrentView)) {
        hideSystemMessageArea();
    }

    mpActionsMainArea->setVisible(view == EditorViewType::cmActionView);
    treeWidget_actions->setVisible(view == EditorViewType::cmActionView);

    mpAliasMainArea->setVisible(view == EditorViewType::cmAliasView);
    treeWidget_aliases->setVisible(view == EditorViewType::cmAliasView);

    mpKeysMainArea->setVisible(view == EditorViewType::cmKeysView);
    treeWidget_keys->setVisible(view == EditorViewType::cmKeysView);

    mpScriptsMainArea->setVisible(view == EditorViewType::cmScriptView);
    treeWidget_scripts->setVisible(view == EditorViewType::cmScriptView);

    mpTimersMainArea->setVisible(view == EditorViewType::cmTimerView);
    treeWidget_timers->setVisible(view == EditorViewType::cmTimerView);

    mpTriggersMainArea->setVisible(view == EditorViewType::cmTriggerView);
    treeWidget_triggers->setVisible(view == EditorViewType::cmTriggerView);

    const bool enablePatternShortcuts = view == EditorViewType::cmTriggerView;

    if (mFirstPatternShortcut) {
        mFirstPatternShortcut->setEnabled(enablePatternShortcuts);
    }
    for (auto* shortcut : std::as_const(mPatternNavigationShortcuts)) {
        if (shortcut) {
            shortcut->setEnabled(enablePatternShortcuts);
        }
    }
    if (mLastPatternShortcut) {
        mLastPatternShortcut->setEnabled(enablePatternShortcuts);
    }

    mpVarsMainArea->setVisible(view == EditorViewType::cmVarsView);
    treeWidget_variables->setVisible(view == EditorViewType::cmVarsView);
    checkBox_displayAllVariables->setVisible(view == EditorViewType::cmVarsView);

    mpAction_toggleActive->setEnabled(view != EditorViewType::cmVarsView && view != EditorViewType::cmUnknownView);
    mpExportAction->setEnabled(view != EditorViewType::cmVarsView && view != EditorViewType::cmUnknownView);

    // texts are duplicated here so that translators can work with the full string
    switch (mCurrentView) {
    case EditorViewType::cmTriggerView:
        // PLACEMARKER 2/3 save button texts need to be kept in sync
        mAddItem->setText(tr("Add Trigger"));
        mAddItem->setStatusTip(tr("Add new trigger"));
        mAddGroup->setText(tr("Add Trigger Group"));
        mAddGroup->setStatusTip(tr("Add new group of triggers"));
        mDeleteItem->setText(tr("Delete Trigger"));
        mDeleteItem->setStatusTip(tr("Delete the selected trigger"));
        mSaveItem->setText(tr("Save Trigger"));
        //: Status tip for saving trigger changes
        mSaveItem->setStatusTip(tr("Apply trigger changes (does not save to disk)."));
        break;
    case EditorViewType::cmTimerView:
        mAddItem->setText(tr("Add Timer"));
        mAddItem->setStatusTip(tr("Add new timer"));
        mAddGroup->setText(tr("Add Timer Group"));
        mAddGroup->setStatusTip(tr("Add new group of timers"));
        mDeleteItem->setText(tr("Delete Timer"));
        mDeleteItem->setStatusTip(tr("Delete the selected timer"));
        mSaveItem->setText(tr("Save Timer"));
        //: Status tip for saving timer changes
        mSaveItem->setStatusTip(tr("Apply timer changes (does not save to disk)."));
        break;
    case EditorViewType::cmAliasView:
        mAddItem->setText(tr("Add Alias"));
        mAddItem->setStatusTip(tr("Add new alias"));
        mAddGroup->setText(tr("Add Alias Group"));
        mAddGroup->setStatusTip(tr("Add new group of aliases"));
        mDeleteItem->setText(tr("Delete Alias"));
        mDeleteItem->setStatusTip(tr("Delete the selected alias"));
        mSaveItem->setText(tr("Save Alias"));
        //: Status tip for saving alias changes
        mSaveItem->setStatusTip(tr("Apply alias changes (does not save to disk)."));
        break;
    case EditorViewType::cmScriptView:
        mAddItem->setText(tr("Add Script"));
        mAddItem->setStatusTip(tr("Add new script"));
        mAddGroup->setText(tr("Add Script Group"));
        mAddGroup->setStatusTip(tr("Add new group of scripts"));
        mDeleteItem->setText(tr("Delete Script"));
        mDeleteItem->setStatusTip(tr("Delete the selected script"));
        mSaveItem->setText(tr("Save Script"));
        //: Status tip for saving script changes
        mSaveItem->setStatusTip(tr("Apply script changes (does not save to disk)."));
        break;
    case EditorViewType::cmActionView:
        mAddItem->setText(tr("Add Button"));
        mAddItem->setStatusTip(tr("Add new button"));
        mAddGroup->setText(tr("Add Toolbar or Menu"));
        mAddGroup->setStatusTip(tr("Add a Toolbar (top level) or Menu (lower levels) to contain menus or buttons"));
        mDeleteItem->setText(tr("Delete Button, Menu or Toolbar"));
        mDeleteItem->setStatusTip(tr("Delete the selected button, menu or toolbar"));
        mSaveItem->setText(tr("Save item"));
        //: Status tip for saving button changes
        mSaveItem->setStatusTip(tr("Apply button/menu/toolbar changes (does not save to disk)."));
        break;
    case EditorViewType::cmKeysView:
        mAddItem->setText(tr("Add Key"));
        mAddItem->setStatusTip(tr("Add new key"));
        mAddGroup->setText(tr("Add Key Group"));
        mAddGroup->setStatusTip(tr("Add new group of keys"));
        mDeleteItem->setText(tr("Delete Key"));
        mDeleteItem->setStatusTip(tr("Delete the selected key"));
        mSaveItem->setText(tr("Save Key"));
        //: Status tip for saving key changes
        mSaveItem->setStatusTip(tr("Apply key changes (does not save to disk)."));
        break;
    case EditorViewType::cmVarsView:
        mAddItem->setText(tr("Add Variable"));
        mAddItem->setStatusTip(tr("Add new variable"));
        mAddGroup->setText(tr("Add Lua table"));
        mAddGroup->setStatusTip(tr("Add new Lua table"));
        mDeleteItem->setText(tr("Delete Variable"));
        mDeleteItem->setStatusTip(tr("Delete the selected variable"));
        mSaveItem->setText(tr("Save Variable"));
        //: Status tip for saving variable changes
        mSaveItem->setStatusTip(tr("Apply variable changes (does not save to disk)."));
        break;
    default:
        qDebug() << "ERROR: dlgTriggerEditor::changeView() undefined view";
    }

    // Update undo/redo button states when changing views
    slot_updateUndoRedoButtonStates();

    // Every way into a view comes through here - the sidebar, a shortcut, a
    // search result, a deep link - so this is the one place the chosen row can
    // be kept telling the truth
    syncEditorSidebarSelection();

    updateEditorItemCounts();

    // If we disabled updates during view change, ensure they get re-enabled
    // (selection handlers will also re-enable via restoreEditorState, but this
    // is a fallback in case no item is selected in the new view)
    if (mpSourceEditorEdbee && !mpSourceEditorEdbee->updatesEnabled()) {
        QTimer::singleShot(0ms, this, [this]() {
            if (mpSourceEditorEdbee) {
                mpSourceEditorEdbee->setUpdatesEnabled(true);
            }
        });
    }
}

// The height the form column is asking for, kept inside what the two panes have
// between them: however much the form wants, the code pane keeps its floor.
int dlgTriggerEditor::formPaneHeightForItsContents(const int paneTotal) const
{
    // The form's height changes without a view switch - the trigger options
    // panel opens inside it - and showing a widget only invalidates the layout
    // of its immediate parent, so the chain up to the column is told first or
    // this measures the form as it was before the panel appeared
    uiDesign::invalidateLayoutsUpTo(mpTriggersMainArea->widget_right, mpNonCodeWidgets);
    return std::clamp(mpNonCodeWidgets->sizeHint().height(), 0, std::max(0, paneTotal - scmEditorSourcePaneFloor));
}

// Each view keeps its own sizes for the right hand splitter and puts them back
// on the way in. That also ends any loan the trigger options panel had taken out
// of the code pane: what it borrowed was measured against the geometry this
// throws away, so there would be nothing left to hand back on closing.
void dlgTriggerEditor::restoreRightSplitterState(const QByteArray& savedState)
{
    if (!savedState.isEmpty()) {
        splitter_right->restoreState(savedState);
        // The handle width travels with the sizes, and a profile that last
        // saved this before the grips existed puts the old one back - which
        // would leave the code pane's heading, and every grip under it, drawn
        // at a thickness nothing else in the editor uses
        splitter_right->setHandleWidth(uiDesign::GripSplitter::scmHandleThickness);

        // A saved split is only the user's up to what the form can fill. One
        // saved while the form was taller - a view with more fields, or the
        // trigger options panel open - hands this view a form pane of mostly
        // nothing and starts the code editor that far down the window; a
        // profile carrying 630px of form above 220px of code is what this was
        // written for. Anything over what the form asks for now goes to the
        // code pane, and the error console keeps its own height.
        //
        // The restore is the only thing clamped. Dragging the handle afterwards
        // is the user asking for a taller form and nothing here runs then, and
        // the measurement is of the form as it stands at this moment, so a view
        // entered with the options panel open is left the room to show it -
        // which is also what keeps refitSplitterForTriggerOptions() from having
        // anything to borrow straight after.
        QList<int> sizes = splitter_right->sizes();
        const int paneTotal = sizes.size() >= 2 ? sizes.at(0) + sizes.at(1) : 0;
        if (paneTotal > 0) {
            const int wanted = formPaneHeightForItsContents(paneTotal);
            if (sizes.at(0) > wanted) {
                sizes[1] += sizes.at(0) - wanted;
                sizes[0] = wanted;
                splitter_right->setSizes(sizes);
            }
        }
    } else {
        // The user has not sized this view's panes themselves, so the form takes
        // the height its fields need and the code takes everything left. Sizes
        // that do not add up to the space there is are shared out in proportion
        // rather than met, which is how asking for 30 above 900 came out as half
        // the window each: a code pane starting a long way down the window,
        // under a form stretched past anything it had to show.
        //
        // Nothing is written back to savedState here. Until a handle is dragged
        // there is no size to remember, and re-reading the form every time is
        // what keeps the split following what the item actually holds - the
        // first of these calls comes before any item has been picked, when the
        // form has nothing to measure.
        QList<int> sizes = splitter_right->sizes();
        const int paneTotal = sizes.size() >= 2 ? sizes.at(0) + sizes.at(1) : 0;
        if (paneTotal > 0) {
            sizes[0] = formPaneHeightForItsContents(paneTotal);
            sizes[1] = paneTotal - sizes.at(0);
            splitter_right->setSizes(sizes);
        } else {
            // Asked for before there is any geometry to divide up
            splitter_right->setSizes({30, 900, 30});
        }
    }
    mTriggerOptionsBorrowedHeight = 0;
}

void dlgTriggerEditor::slot_showTimers()
{
    changeView(EditorViewType::cmTimerView);
    QTreeWidgetItem* pI = treeWidget_timers->topLevelItem(0);
    if (!pI || pI == treeWidget_timers->currentItem() || !pI->childCount()) {
        // There is no root item, we are on the root item or there are no other
        // items - so show the help message:
        clearTimerForm();
    } else {
        mpTimersMainArea->show();
        mpSourceEditorArea->show();
        slot_timerSelected(treeWidget_timers->currentItem());
    }
    restoreRightSplitterState(mTimerEditorSplitterState);
    focusPanelTree(treeWidget_timers);
}

void dlgTriggerEditor::showCurrentTriggerItem()
{
    if (mCurrentView != EditorViewType::cmUnknownView) {
        return;
    }

    changeView(EditorViewType::cmTriggerView);
    QTreeWidgetItem* pI = treeWidget_triggers->topLevelItem(0);
    if (!pI || pI == treeWidget_triggers->currentItem() || !pI->childCount()) {
        // There is no root item, we are on the root item or there are no other
        // items - so show the help message:
        clearTriggerForm();
    } else {
        mpTriggersMainArea->show();
        mpSourceEditorArea->show();
        slot_triggerSelected(treeWidget_triggers->currentItem());
    }
}

void dlgTriggerEditor::slot_showTriggers()
{
    changeView(EditorViewType::cmTriggerView);
    QTreeWidgetItem* pI = treeWidget_triggers->topLevelItem(0);
    if (!pI || pI == treeWidget_triggers->currentItem() || !pI->childCount()) {
        // There is no root item, we are on the root item or there are no other
        // items - so show the help message:
        clearTriggerForm();
    } else {
        mpTriggersMainArea->show();
        mpSourceEditorArea->show();
        slot_triggerSelected(treeWidget_triggers->currentItem());
    }
    restoreRightSplitterState(mTriggerEditorSplitterState);
    focusPanelTree(treeWidget_triggers);
}

void dlgTriggerEditor::slot_showScripts()
{
    changeView(EditorViewType::cmScriptView);
    QTreeWidgetItem* pI = treeWidget_scripts->topLevelItem(0);
    if (!pI || pI == treeWidget_scripts->currentItem() || !pI->childCount()) {
        // There is no root item, we are on the root item or there are no other
        // items - so show the help message:
        clearScriptForm();
    } else {
        mpScriptsMainArea->show();
        mpSourceEditorArea->show();
        slot_scriptsSelected(treeWidget_scripts->currentItem());
    }
    restoreRightSplitterState(mScriptEditorSplitterState);
    focusPanelTree(treeWidget_scripts);
}

void dlgTriggerEditor::slot_showKeys()
{
    changeView(EditorViewType::cmKeysView);
    QTreeWidgetItem* pI = treeWidget_keys->topLevelItem(0);
    if (!pI || pI == treeWidget_keys->currentItem() || !pI->childCount()) {
        // There is no root item, we are on the root item or there are no other
        // items - so show the help message:
        clearKeyForm();
    } else {
        mpKeysMainArea->show();
        mpSourceEditorArea->show();
        slot_keySelected(treeWidget_keys->currentItem());
    }
    restoreRightSplitterState(mKeyEditorSplitterState);
    focusPanelTree(treeWidget_keys);
}

void dlgTriggerEditor::slot_showVariables()
{
    changeView(EditorViewType::cmVarsView);
    repopulateVars();
    mpCurrentVarItem = nullptr;
    checkBox_displayAllVariables->show();
    checkBox_displayAllVariables->setChecked(showHiddenVars);
    QTreeWidgetItem* pI = treeWidget_variables->topLevelItem(0);
    if (!pI || pI == treeWidget_variables->currentItem() || !pI->childCount()) {
        // There is no root item, we are on the root item or there are no other
        // items - so show the help message:
        clearVarForm();
    } else {
        mpVarsMainArea->show();
        mpSourceEditorArea->show();
        slot_variableSelected(treeWidget_variables->currentItem());
    }
    restoreRightSplitterState(mVarEditorSplitterState);
    focusPanelTree(treeWidget_variables);
}

void dlgTriggerEditor::show_vars()
{
    //no repopulation of variables
    changeView(EditorViewType::cmVarsView);
    mpCurrentVarItem = nullptr;
    mpSourceEditorArea->show();
    checkBox_displayAllVariables->show();
    checkBox_displayAllVariables->setChecked(showHiddenVars);
    QTreeWidgetItem* pI = treeWidget_variables->topLevelItem(0);
    if (pI) {
        if (pI->childCount() > 0) {
            mpVarsMainArea->show();
            slot_variableSelected(treeWidget_variables->currentItem());
        } else {
            clearVarForm();
        }
    }
    treeWidget_variables->show();
}


void dlgTriggerEditor::slot_showAliases()
{
    changeView(EditorViewType::cmAliasView);
    QTreeWidgetItem* pI = treeWidget_aliases->topLevelItem(0);
    if (!pI || pI == treeWidget_aliases->currentItem() || !pI->childCount()) {
        // There is no root item, we are on the root item or there are no other
        // items - so show the help message:
        clearAliasForm();
    } else {
        mpAliasMainArea->show();
        mpSourceEditorArea->show();
        slot_aliasSelected(treeWidget_aliases->currentItem());
    }
    restoreRightSplitterState(mAliasEditorSplitterState);
    focusPanelTree(treeWidget_aliases);
}

void dlgTriggerEditor::showError(const QString& text)
{
    // A still-running undo-toast expiry timer would hide this message when it
    // fires, so cancel it - the toast's content is gone from the screen anyway
    cancelBannerUndoTimer();
    mpSystemMessageArea->notificationAreaIconLabelInformation->hide();
    mpSystemMessageArea->notificationAreaIconLabelError->show();
    mpSystemMessageArea->notificationAreaIconLabelWarning->hide();
    mpSystemMessageArea->notificationAreaMessageBox->setText(text);
    mpSystemMessageArea->show();
    mCurrentBannerKey.clear();

    // A failed save reports through here, but so does a profile load meeting a
    // broken item and an activation the engine refused - neither of which is
    // anything to do with what the code pane is holding. The heading over that
    // pane therefore only listens while a save of its own item is running.
    if (mEditorSaveErrorCaptureOpen) {
        mEditorSaveErrorCaptured = text;
    }

    // Reconnect close button to normal hide behavior (neither a banner dismiss
    // nor the package warning, whose close button remembers being pressed)
    disconnect(mpSystemMessageArea->messageAreaCloseButton, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_bannerDismissClicked);
    disconnect(mpSystemMessageArea->messageAreaCloseButton, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_packageWarningDismissed);
    connect(mpSystemMessageArea->messageAreaCloseButton, &QAbstractButton::clicked, this, &dlgTriggerEditor::hideSystemMessageArea);

    if (!mpHost->mIsProfileLoadingSequence) {
        mudlet::self()->announce(text);
    }
}

void dlgTriggerEditor::showWarning(const QString& text, bool announce)
{
    // A still-running undo-toast expiry timer would hide this message when it
    // fires, so cancel it - the toast's content is gone from the screen anyway
    cancelBannerUndoTimer();
    mpSystemMessageArea->notificationAreaIconLabelInformation->hide();
    mpSystemMessageArea->notificationAreaIconLabelError->hide();
    mpSystemMessageArea->notificationAreaIconLabelWarning->show();
    mpSystemMessageArea->notificationAreaMessageBox->setText(text);
    mpSystemMessageArea->show();
    mCurrentBannerKey.clear();

    // Reconnect close button to normal hide behavior (neither a banner dismiss
    // nor the package warning, whose close button remembers being pressed)
    disconnect(mpSystemMessageArea->messageAreaCloseButton, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_bannerDismissClicked);
    disconnect(mpSystemMessageArea->messageAreaCloseButton, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_packageWarningDismissed);
    connect(mpSystemMessageArea->messageAreaCloseButton, &QAbstractButton::clicked, this, &dlgTriggerEditor::hideSystemMessageArea);

    if (!mpHost->mIsProfileLoadingSequence && announce) {
        mudlet::self()->announce(text);
    }
}

// Every item of a package raises the same warning, and switching between them
// used to raise it again for each one. It is said once per package instead, and
// the close button puts it away for as long as the editor is open: the message
// is about the package, not about the item that happened to be clicked.
void dlgTriggerEditor::showPackageWarning(const QString& packageName, QTreeWidgetItem* pItem)
{
    if (packageName.isEmpty()) {
        return;
    }

    // What a screen reader reads out after the item's name is a property of the
    // item, so it is set whether or not the banner is raised over it
    if (pItem) {
        updatePackageItemAccessibility(pItem, pItem->data(0, Qt::AccessibleDescriptionRole).toString());
    }

    if (mPackageWarningsDismissed || mWarnedPackages.contains(packageName)) {
        return;
    }
    mWarnedPackages.insert(packageName);

    //: Warning banner shown in the editor when the item being looked at came from a package
    showWarning(tr("This item is part of a package. To best preserve your changes, copy this item before editing as package upgrades may overwrite modifications."), false);

    // showWarning() wires the close button to the plain hide, which forgets
    // that it was this that was closed
    disconnect(mpSystemMessageArea->messageAreaCloseButton, &QAbstractButton::clicked, this, &dlgTriggerEditor::hideSystemMessageArea);
    connect(mpSystemMessageArea->messageAreaCloseButton, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_packageWarningDismissed);

    if (!mPackageWarningAnnounced) {
        mPackageWarningAnnounced = true;
        //: First-time educational message for screen reader users about package items
        mudlet::self()->announce(tr("Package item. Copy before editing to preserve changes."));
    }
}

// Closing the package warning by hand is taken as "yes, understood": it stays
// away for the rest of this editor's life, however many packages are opened
void dlgTriggerEditor::slot_packageWarningDismissed()
{
    mPackageWarningsDismissed = true;
    hideSystemMessageArea();
}

void dlgTriggerEditor::showInfo(const QString& text)
{
    mpSystemMessageArea->notificationAreaIconLabelError->hide();
    mpSystemMessageArea->notificationAreaIconLabelWarning->hide();
    mpSystemMessageArea->notificationAreaIconLabelInformation->show();
    mpSystemMessageArea->notificationAreaMessageBox->setText(text);
    mpSystemMessageArea->show();
    mCurrentBannerKey.clear();
    if (!mpHost->mIsProfileLoadingSequence) {
        mudlet::self()->announce(text);
    }
}

// Qt's rich text does not understand 'color: inherit' and renders it as
// black, so the theme's text colour has to be spelled out explicitly
static QString themedBannerLinkColor()
{
    return mudlet::self()->inDarkMode() ? qsl("rgb(230, 230, 230)") : qsl("black");
}

void dlgTriggerEditor::showIntro(const QString& desiredOption)
{
    if (!introAddItem.contains(mCurrentView)) {
        qWarning() << "ERROR: dlgTriggerEditor::showIntro() undefined view";
        return;
    }

    static const auto bannerKey = qsl("intro");
    bool includeBasePreference = true;
    if (mCurrentView == EditorViewType::cmTriggerView) {
        // The trigger intro banner predates the global suppression toggle, so keep
        // honouring only its explicit "hide permanently" preference to ensure it
        // still shows up for profiles that never opted out directly.
        includeBasePreference = false;
    }

    if (bannerPermanentlyHidden(mCurrentView, bannerKey, includeBasePreference)) {
        return;
    }

    introTextParts introAddCurrentItem = introAddItem.value(mCurrentView);
    QString introTextOptions;
    const QString linkColor = themedBannerLinkColor();
    for (const auto& [name, headline, contents] : std::as_const(introAddCurrentItem.options)) {
        introTextOptions.append((name != desiredOption) ? qsl("<li><a href='%1' style='color: %3; text-decoration: underline;'>%2</a></li>").arg(name, headline, linkColor)
                                                        : qsl("<li><strong>%1</strong>%2</li>").arg(headline, contents));
    }

    QString content = qsl("<p>%1</p><ul>%2</ul>").arg(introAddCurrentItem.summary, introTextOptions);

    showHideableBanner(content, bannerKey);
}

void dlgTriggerEditor::showHideableBanner(const QString& content, const QString& bannerKey)
{
    if (!mpSystemMessageArea) {
        return;
    }

    const QString settingsKey = bannerSettingsKey(mCurrentView, bannerKey);
    const QString baseKey = bannerSettingsKey(mCurrentView, QString());
    if (settingsKey.isEmpty()) {
        return;
    }

    if (mTemporarilyHiddenBanners.contains(settingsKey) || (!bannerKey.isEmpty() && mTemporarilyHiddenBanners.contains(baseKey))) {
        return;
    }

    bool includeBasePreference = true;
    if (mCurrentView == EditorViewType::cmTriggerView && bannerKey == qsl("intro")) {
        // Match the behaviour in showIntro(): ignore the view-wide suppression
        // switch so the legacy trigger intro reappears unless it was hidden via
        // its own banner controls.
        includeBasePreference = false;
    }

    if (bannerPermanentlyHidden(mCurrentView, bannerKey, includeBasePreference)) {
        return;
    }

    if (mpSystemMessageArea->isVisible() && mCurrentBannerKey != bannerKey && !mpSystemMessageArea->notificationAreaMessageBox->text().isEmpty()) {
        return;
    }

    if (mpSystemMessageArea->isVisible() && mCurrentBannerKey == bannerKey && mpSystemMessageArea->notificationAreaMessageBox->text() == content) {
        return;
    }

    disconnect(mpSystemMessageArea->messageAreaCloseButton, &QAbstractButton::clicked, this, &dlgTriggerEditor::hideSystemMessageArea);
    disconnect(mpSystemMessageArea->messageAreaCloseButton, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_bannerDismissClicked);
    disconnect(mpSystemMessageArea->messageAreaCloseButton, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_packageWarningDismissed);
    connect(mpSystemMessageArea->messageAreaCloseButton, &QAbstractButton::clicked, this, &dlgTriggerEditor::slot_bannerDismissClicked);

    disconnect(mpSystemMessageArea->notificationAreaMessageBox, &QLabel::linkActivated, nullptr, nullptr);
    connect(mpSystemMessageArea->notificationAreaMessageBox, &QLabel::linkActivated, this, &dlgTriggerEditor::slot_clickedMessageBox);

    showInfo(content);
    mCurrentBannerKey = bannerKey;
}

QString dlgTriggerEditor::bannerSettingsKey(EditorViewType viewType, const QString& bannerKey) const
{
    const QString legacyKey = legacyBannerSettingsKey(viewType, bannerKey);
    if (legacyKey.isEmpty()) {
        return legacyKey;
    }

    const QString prefix = profileSettingsPrefix();
    if (prefix.isEmpty()) {
        return legacyKey;
    }

    return qsl("%1/%2").arg(prefix, legacyKey);
}

QString dlgTriggerEditor::legacyBannerSettingsKey(EditorViewType viewType, const QString& bannerKey) const
{
    const QMetaEnum metaEnum = QMetaEnum::fromType<EditorViewType>();
    const char* enumName = metaEnum.valueToKey(static_cast<int>(viewType));

    if (!enumName) {
        return QString();
    }

    QString key = QString::fromLatin1(enumName).toLower();
    if (!bannerKey.isEmpty()) {
        key += qsl("/%1").arg(bannerKey);
    }

    return key;
}

QString dlgTriggerEditor::profileSettingsPrefix() const
{
    if (!mpHost) {
        return QString();
    }

    const QString profileName = mpHost->getName();
    if (profileName.isEmpty()) {
        return QString();
    }

    const QString sanitized = utils::sanitizeForPath(profileName);
    if (sanitized.isEmpty()) {
        return QString();
    }

    return qsl("profiles/%1").arg(sanitized);
}

void dlgTriggerEditor::slot_showActions()
{
    changeView(EditorViewType::cmActionView);
    QTreeWidgetItem* pI = treeWidget_actions->topLevelItem(0);
    if (!pI || pI == treeWidget_actions->currentItem() || !pI->childCount()) {
        // There is no root item, we are on the root item or there are no other
        // items - so show the help message:
        clearActionForm();
    } else {
        mpActionsMainArea->show();
        mpSourceEditorArea->show();
        slot_actionSelected(treeWidget_actions->currentItem());
    }
    restoreRightSplitterState(mActionEditorSplitterState);
    focusPanelTree(treeWidget_actions);
}

void dlgTriggerEditor::slot_saveEdits()
{
    beginSaveErrorCapture();

    switch (resolveCurrentView()) {
    case EditorViewType::cmTriggerView:
        saveTrigger();
        break;
    case EditorViewType::cmTimerView:
        saveTimer();
        break;
    case EditorViewType::cmAliasView:
        saveAlias();
        break;
    case EditorViewType::cmScriptView:
        saveScript();
        break;
    case EditorViewType::cmActionView:
        saveAction();
        break;
    case EditorViewType::cmKeysView:
        saveKey();
        break;
    case EditorViewType::cmVarsView:
        saveVar();
        break;
    default:
        qWarning() << "ERROR: dlgTriggerEditor::slot_saveEdits() undefined view, not sure what to save";
    }

    endSaveErrorCapture();

    // There was a mpHost->serialize() call here, but that code was
    // "short-circuited" and returned without doing anything;
}

void dlgTriggerEditor::slot_addNewItem()
{
    switch (resolveCurrentView()) {
    case EditorViewType::cmTriggerView:
        addTrigger(false); //add normal trigger
        mpTriggersMainArea->lineEdit_trigger_name->setFocus();
        mpTriggersMainArea->lineEdit_trigger_name->selectAll();
        break;
    case EditorViewType::cmTimerView:
        addTimer(false); //add normal timer
        mpTimersMainArea->lineEdit_timer_name->setFocus();
        mpTimersMainArea->lineEdit_timer_name->selectAll();
        break;
    case EditorViewType::cmAliasView:
        addAlias(false); //add normal alias
        mpAliasMainArea->lineEdit_alias_name->setFocus();
        mpAliasMainArea->lineEdit_alias_name->selectAll();
        break;
    case EditorViewType::cmScriptView:
        addScript(false); //add normal script
        mpScriptsMainArea->lineEdit_script_name->setFocus();
        mpScriptsMainArea->lineEdit_script_name->selectAll();
        break;
    case EditorViewType::cmActionView:
        addAction(false); //add normal action
        mpActionsMainArea->lineEdit_action_name->setFocus();
        mpActionsMainArea->lineEdit_action_name->selectAll();
        break;
    case EditorViewType::cmKeysView:
        addKey(false); //add normal key
        mpKeysMainArea->lineEdit_key_name->setFocus();
        mpKeysMainArea->lineEdit_key_name->selectAll();
        break;
    case EditorViewType::cmVarsView:
        addVar(false); //add variable
        mpVarsMainArea->lineEdit_var_name->setFocus();
        // variables start without a default name
        break;
    default:
        qDebug() << "ERROR: dlgTriggerEditor::slot_saveEdits() undefined view";
    }
}

void dlgTriggerEditor::slot_addNewGroup()
{
    switch (resolveCurrentView()) {
    case EditorViewType::cmTriggerView:
        addTrigger(true); //add trigger group
        mpTriggersMainArea->lineEdit_trigger_name->setFocus();
        mpTriggersMainArea->lineEdit_trigger_name->selectAll();
        break;
    case EditorViewType::cmTimerView:
        addTimer(true); //add timer group
        mpTimersMainArea->lineEdit_timer_name->setFocus();
        mpTimersMainArea->lineEdit_timer_name->selectAll();
        break;
    case EditorViewType::cmAliasView:
        addAlias(true); //add alias group
        mpAliasMainArea->lineEdit_alias_name->setFocus();
        mpAliasMainArea->lineEdit_alias_name->selectAll();
        break;
    case EditorViewType::cmScriptView:
        addScript(true); //add script group
        mpScriptsMainArea->lineEdit_script_name->setFocus();
        mpScriptsMainArea->lineEdit_script_name->selectAll();
        break;
    case EditorViewType::cmActionView:
        addAction(true); //add action group
        mpActionsMainArea->lineEdit_action_name->setFocus();
        mpActionsMainArea->lineEdit_action_name->selectAll();
        break;
    case EditorViewType::cmKeysView:
        addKey(true); //add keys group
        mpKeysMainArea->lineEdit_key_name->setFocus();
        mpKeysMainArea->lineEdit_key_name->selectAll();
        break;
    case EditorViewType::cmVarsView:
        addVar(true); // add lua table
        mpVarsMainArea->lineEdit_var_name->setFocus();
        // variables start without a default name
        break;
    default:
        qDebug() << "ERROR: dlgTriggerEditor::slot_saveEdits() undefined view";
    }
}

void dlgTriggerEditor::slot_toggleItemOrGroupActiveFlag()
{
    switch (resolveCurrentView()) {
    case EditorViewType::cmTriggerView:
        activeToggle_trigger();
        break;
    case EditorViewType::cmTimerView:
        activeToggle_timer();
        break;
    case EditorViewType::cmAliasView:
        activeToggle_alias();
        break;
    case EditorViewType::cmScriptView:
        activeToggle_script();
        break;
    case EditorViewType::cmActionView:
        activeToggle_action();
        break;
    case EditorViewType::cmKeysView:
        activeToggle_key();
        break;

    default:
        qDebug() << "ERROR: dlgTriggerEditor::slot_saveEdits() undefined view";
    }
}

void dlgTriggerEditor::slot_sourceFindMove()
{
    int x = mpSourceEditorEdbee->width() - mpSourceEditorFindArea->width();
    int y = mpSourceEditorEdbee->height() - mpSourceEditorFindArea->height();
    if (mpSourceEditorEdbee->verticalScrollBar()->isVisible()) {
        x = x - mpSourceEditorEdbee->verticalScrollBar()->width();
    }
    if (mpSourceEditorEdbee->horizontalScrollBar()->isVisible()) {
        y = y - mpSourceEditorEdbee->horizontalScrollBar()->height();
    }
    mpSourceEditorFindArea->move(x, y);
    mpSourceEditorFindArea->update();
}

void dlgTriggerEditor::slot_openSourceFind()
{
    slot_sourceFindMove();
    mpSourceEditorFindArea->show();
    mpSourceEditorFindArea->lineEdit_findText->setFocus();
    mpSourceEditorFindArea->lineEdit_findText->selectAll();
}

void dlgTriggerEditor::slot_closeSourceFind()
{
    auto controller = mpSourceEditorEdbee->controller();
    controller->borderedTextRanges()->clear();
    controller->textSelection()->range(0).clearSelection();
    controller->update();
    mpSourceEditorFindArea->hide();
    mpSourceEditorEdbee->setFocus();
}

void dlgTriggerEditor::slot_sourceReplace()
{
    auto controller = mpSourceEditorEdbee->controller();
    auto replaceText = mpSourceEditorFindArea->lineEdit_replaceText->text();
    for (size_t i = 0; i < controller->textSelection()->rangeCount(); i++) {
        auto& range = controller->textSelection()->range(i);
        if (mpSourceEditorEdbee->textDocument()->text().mid(range.anchor(), range.length()) == replaceText) {
            slot_sourceFindNext();
            continue;
        }
        if (!range.hasSelection()) {
            slot_sourceFindPrevious();
            continue;
        }
        mpSourceEditorEdbee->textDocument()->replace(range.anchor(), range.length(), replaceText);
        range.setLength(mpSourceEditorFindArea->lineEdit_replaceText->text().length());
    }
}

void dlgTriggerEditor::slot_sourceFindPrevious()
{
    auto controller = mpSourceEditorEdbee->controller();
    auto searcher = controller->textSearcher();
    searcher->setSearchTerm(mpSourceEditorFindArea->lineEdit_findText->text());
    searcher->setCaseSensitive(false);
    searcher->findPrev(mpSourceEditorEdbee);
    controller->scrollCaretVisible();
    controller->update();
    slot_sourceFindMove();
}

void dlgTriggerEditor::slot_sourceFindNext()
{
    auto controller = mpSourceEditorEdbee->controller();
    auto searcher = controller->textSearcher();
    searcher->setSearchTerm(mpSourceEditorFindArea->lineEdit_findText->text());
    searcher->setCaseSensitive(false);
    searcher->findNext(mpSourceEditorEdbee);
    controller->scrollCaretVisible();
    controller->update();
    slot_sourceFindMove();
}

void dlgTriggerEditor::slot_sourceFindTextChanges()
{
    auto text = mpSourceEditorFindArea->lineEdit_findText->text();
    if (text.length() <= 2) {
        return;
    }

    auto controller = mpSourceEditorEdbee->controller();
    auto searcher = controller->textSearcher();
    controller->borderedTextRanges()->clear();
    controller->textSelection()->range(0).clearSelection();
    searcher->setSearchTerm(text);
    searcher->markAll(controller->borderedTextRanges());
    controller->update();
}

void dlgTriggerEditor::slot_deleteItemOrGroup()
{
    switch (resolveCurrentView()) {
    case EditorViewType::cmTriggerView:
        delete_trigger();
        break;
    case EditorViewType::cmTimerView:
        delete_timer();
        break;
    case EditorViewType::cmAliasView:
        delete_alias();
        break;
    case EditorViewType::cmScriptView:
        delete_script();
        break;
    case EditorViewType::cmActionView:
        delete_action();
        break;
    case EditorViewType::cmKeysView:
        delete_key();
        break;
    case EditorViewType::cmVarsView:
        delete_variable();
        break;
    default:
        qDebug() << "ERROR: dlgTriggerEditor::slot_deleteItemOrGroup() undefined view";
    }
}

void dlgTriggerEditor::slot_saveSelectedItem()
{
    switch (resolveCurrentView()) {
    case EditorViewType::cmTriggerView:
        saveTrigger();
        break;
    case EditorViewType::cmTimerView:
        saveTimer();
        break;
    case EditorViewType::cmAliasView:
        saveAlias();
        break;
    case EditorViewType::cmScriptView:
        saveScript();
        break;
    case EditorViewType::cmActionView:
        saveAction();
        break;
    case EditorViewType::cmKeysView:
        saveKey();
        break;
    case EditorViewType::cmVarsView:
        saveVar();
        break;
    case EditorViewType::cmUnknownView:
        qWarning().nospace().noquote() << "dlgTriggerEditor::slot_saveSelectedItem() WARNING - switch(EditorViewType) not expected to be called for \"EditorViewType::cmUnknownView!\"";
    }
}


// Should the functionality change in this method be sure to review the code
// for "case SearchResultIsEventHandler" for "Scripts" in:
// slot_itemSelectedInSearchResults(...), which notes the same item by hand, and where
// that note is dropped in slot_scriptsSelected(...)
void dlgTriggerEditor::slot_scriptMainAreaEditHandler()
{
    QListWidgetItem* pItem = mpScriptsMainArea->listWidget_script_registered_event_handlers->currentItem();
    if (!pItem) {
        return;
    }

    mIsScriptsMainAreaEditHandler = true;
    mpScriptsMainAreaEditHandlerItem = pItem;
    const QString regex = pItem->text();
    if (regex.isEmpty()) {
        mIsScriptsMainAreaEditHandler = false;
        return;
    }
    mpScriptsMainArea->lineEdit_script_event_handler_entry->setText(regex);
}

void dlgTriggerEditor::slot_scriptMainAreaClearHandlerSelection(QListWidgetItem* item)
{
    Q_UNUSED(item)
    mpScriptsMainArea->listWidget_script_registered_event_handlers->clearSelection();
    mpScriptsMainArea->lineEdit_script_event_handler_entry->clear();
    mIsScriptsMainAreaEditHandler = false;
    mpScriptsMainAreaEditHandlerItem = nullptr;
}

void dlgTriggerEditor::slot_scriptMainAreaDeleteHandler()
{
    // takeItem() hands ownership of the row over to us
    delete mpScriptsMainArea->listWidget_script_registered_event_handlers->takeItem(mpScriptsMainArea->listWidget_script_registered_event_handlers->currentRow());
    slot_scriptMainAreaClearHandlerSelection(nullptr);
}

void dlgTriggerEditor::slot_scriptMainAreaAddHandler()
{
    auto addEventHandler = [&]() {
        if (mpScriptsMainArea->lineEdit_script_event_handler_entry->text().isEmpty()) {
            return;
        }

        // check for duplicate handlers
        QString newHandlerText = mpScriptsMainArea->lineEdit_script_event_handler_entry->text();
        QListWidget* list = mpScriptsMainArea->listWidget_script_registered_event_handlers;
        for (int i = 0; i < list->count(); i++) {
            if (list->item(i)->text() == newHandlerText) {
                return;
            }
        }

        auto pItem = new QListWidgetItem;
        pItem->setText(newHandlerText);
        mpScriptsMainArea->listWidget_script_registered_event_handlers->addItem(pItem);
    };

    mpScriptsMainArea->trimEventHandlerName();
    if (mIsScriptsMainAreaEditHandler) {
        if (!mpScriptsMainAreaEditHandlerItem) {
            mIsScriptsMainAreaEditHandler = false;
            addEventHandler();
        } else {
            if (mpScriptsMainAreaEditHandlerItem->text() == mpScriptsMainArea->lineEdit_script_event_handler_entry->text()
                || mpScriptsMainArea->lineEdit_script_event_handler_entry->text().isEmpty()) {
                return;
            }
            mpScriptsMainAreaEditHandlerItem->setText(mpScriptsMainArea->lineEdit_script_event_handler_entry->text());
            mpScriptsMainArea->listWidget_script_registered_event_handlers->clearSelection();
        }
    } else {
        addEventHandler();
    }

    slot_scriptMainAreaClearHandlerSelection(nullptr);
}

void dlgTriggerEditor::slot_toggleCentralDebugConsole()
{
    mudlet::self()->attachDebugArea(mpHost->getName());

    mudlet::smpDebugArea->setVisible(!mudlet::smDebugMode);
    mudlet::smDebugMode = !mudlet::smDebugMode;
    mudlet::smpDebugArea->setWindowTitle(tr("Central Debug Console"));
    if (mudlet::smDebugMode) {
        // If this is the first time the window is shown we want any previously
        // enqueued messages to be painted onto the central debug console:
        TDebug::flushMessageQueue();
    }
    mudlet::self()->refreshTabBar();
}

void dlgTriggerEditor::slot_nextSection()
{
    switch (mCurrentView) {
    case EditorViewType::cmTriggerView:
        if (qsl("edbee::TextEditorComponent").compare(QApplication::focusWidget()->metaObject()->className()) == 0) {
            treeWidget_triggers->setFocus();
            return;
        }
        if (treeWidget_triggers->hasFocus()) {
            mpTriggersMainArea->lineEdit_trigger_name->setFocus();
            return;
        }
        if (mpTriggersMainArea->hasFocus()) {
            mTriggerPatternEdit[0]->singleLineTextEdit_pattern->setFocus();
            return;
        }
        for (auto child : mpTriggersMainArea->scrollArea->findChildren<QWidget*>()) {
            if (child->hasFocus()) {
                mpSourceEditorEdbee->setFocus();
                return;
            }
        }
        for (auto child : mpTriggersMainArea->findChildren<QWidget*>()) {
            if (child->hasFocus()) {
                mTriggerPatternEdit[0]->singleLineTextEdit_pattern->setFocus();
                return;
            }
        }
        break;
    case EditorViewType::cmTimerView:
        if (qsl("edbee::TextEditorComponent").compare(QApplication::focusWidget()->metaObject()->className()) == 0) {
            treeWidget_timers->setFocus();
            return;
        }
        if (treeWidget_timers->hasFocus()) {
            mpTimersMainArea->lineEdit_timer_name->setFocus();
            return;
        }
        for (auto child : mpTimersMainArea->findChildren<QWidget*>()) {
            if (child->hasFocus()) {
                mpSourceEditorEdbee->setFocus();
                return;
            }
        }
        break;
    case EditorViewType::cmAliasView:
        if (QString("edbee::TextEditorComponent").compare(QApplication::focusWidget()->metaObject()->className()) == 0) {
            treeWidget_aliases->setFocus();
            return;
        }
        if (treeWidget_aliases->hasFocus()) {
            mpAliasMainArea->lineEdit_alias_name->setFocus();
            return;
        }
        for (auto child : mpAliasMainArea->findChildren<QWidget*>()) {
            if (child->hasFocus()) {
                mpSourceEditorEdbee->setFocus();
                return;
            }
        }
        break;
    case EditorViewType::cmScriptView:
        if (qsl("edbee::TextEditorComponent").compare(QApplication::focusWidget()->metaObject()->className()) == 0) {
            treeWidget_scripts->setFocus();
            return;
        }
        if (treeWidget_scripts->hasFocus()) {
            mpScriptsMainArea->lineEdit_script_name->setFocus();
            return;
        }
        for (auto child : mpScriptsMainArea->findChildren<QWidget*>()) {
            if (child->hasFocus()) {
                mpSourceEditorEdbee->setFocus();
                return;
            }
        }
        break;
    case EditorViewType::cmActionView:
        if (qsl("edbee::TextEditorComponent").compare(QApplication::focusWidget()->metaObject()->className()) == 0) {
            treeWidget_actions->setFocus();
            return;
        }
        if (treeWidget_actions->hasFocus()) {
            mpActionsMainArea->lineEdit_action_name->setFocus();
            return;
        }
        for (auto child : mpActionsMainArea->findChildren<QWidget*>()) {
            if (child->hasFocus()) {
                mpSourceEditorEdbee->setFocus();
                return;
            }
        }
        break;
    case EditorViewType::cmKeysView:
        if (qsl("edbee::TextEditorComponent").compare(QApplication::focusWidget()->metaObject()->className()) == 0) {
            treeWidget_keys->setFocus();
            return;
        }
        if (treeWidget_keys->hasFocus()) {
            mpKeysMainArea->lineEdit_key_name->setFocus();
            return;
        }
        for (auto child : mpKeysMainArea->findChildren<QWidget*>()) {
            if (child->hasFocus()) {
                mpSourceEditorEdbee->setFocus();
                return;
            }
        }
        break;
    case EditorViewType::cmVarsView:
        if (qsl("edbee::TextEditorComponent").compare(QApplication::focusWidget()->metaObject()->className()) == 0) {
            treeWidget_variables->setFocus();
            return;
        }
        if (treeWidget_variables->hasFocus()) {
            mpVarsMainArea->lineEdit_var_name->setFocus();
            return;
        }
        for (auto child : mpVarsMainArea->findChildren<QWidget*>()) {
            if (child->hasFocus()) {
                mpSourceEditorEdbee->setFocus();
                return;
            }
        }
        break;
    case EditorViewType::cmUnknownView:
        return;
    }
}

void dlgTriggerEditor::slot_previousSection()
{
    switch (mCurrentView) {
    case EditorViewType::cmTriggerView:
        if (QString("edbee::TextEditorComponent").compare(QApplication::focusWidget()->metaObject()->className()) == 0) {
            mTriggerPatternEdit[0]->singleLineTextEdit_pattern->setFocus();
            return;
        }
        if (treeWidget_triggers->hasFocus()) {
            mpSourceEditorEdbee->setFocus();
            return;
        }
        for (auto child : mpTriggersMainArea->scrollArea->findChildren<QWidget*>()) {
            if (child->hasFocus()) {
                mpTriggersMainArea->lineEdit_trigger_name->setFocus();
                return;
            }
        }
        for (auto child : mpTriggersMainArea->findChildren<QWidget*>()) {
            if (child->hasFocus()) {
                treeWidget_triggers->setFocus();
                return;
            }
        }
        break;
    case EditorViewType::cmTimerView:
        if (QString("edbee::TextEditorComponent").compare(QApplication::focusWidget()->metaObject()->className()) == 0) {
            mpTimersMainArea->lineEdit_timer_name->setFocus();
            return;
        }
        if (treeWidget_timers->hasFocus()) {
            mpSourceEditorEdbee->setFocus();
            return;
        }
        for (auto child : mpTimersMainArea->findChildren<QWidget*>()) {
            if (child->hasFocus()) {
                treeWidget_timers->setFocus();
                return;
            }
        }
        break;
    case EditorViewType::cmAliasView:
        if (QString("edbee::TextEditorComponent").compare(QApplication::focusWidget()->metaObject()->className()) == 0) {
            mpAliasMainArea->lineEdit_alias_name->setFocus();
            return;
        }
        if (treeWidget_aliases->hasFocus()) {
            mpSourceEditorEdbee->setFocus();
            return;
        }
        for (auto child : mpAliasMainArea->findChildren<QWidget*>()) {
            if (child->hasFocus()) {
                treeWidget_aliases->setFocus();
                return;
            }
        }
        break;
    case EditorViewType::cmScriptView:
        if (QString("edbee::TextEditorComponent").compare(QApplication::focusWidget()->metaObject()->className()) == 0) {
            mpScriptsMainArea->lineEdit_script_name->setFocus();
            return;
        }
        if (treeWidget_scripts->hasFocus()) {
            mpSourceEditorEdbee->setFocus();
            return;
        }
        for (auto child : mpScriptsMainArea->findChildren<QWidget*>()) {
            if (child->hasFocus()) {
                treeWidget_scripts->setFocus();
                return;
            }
        }
        break;
    case EditorViewType::cmActionView:
        if (QString("edbee::TextEditorComponent").compare(QApplication::focusWidget()->metaObject()->className()) == 0) {
            mpActionsMainArea->lineEdit_action_name->setFocus();
            return;
        }
        if (treeWidget_actions->hasFocus()) {
            mpSourceEditorEdbee->setFocus();
            return;
        }
        for (auto child : mpActionsMainArea->findChildren<QWidget*>()) {
            if (child->hasFocus()) {
                treeWidget_actions->setFocus();
                return;
            }
        }
        break;
    case EditorViewType::cmKeysView:
        if (QString("edbee::TextEditorComponent").compare(QApplication::focusWidget()->metaObject()->className()) == 0) {
            mpKeysMainArea->lineEdit_key_name->setFocus();
            return;
        }
        if (treeWidget_keys->hasFocus()) {
            mpSourceEditorEdbee->setFocus();
            return;
        }
        for (auto child : mpKeysMainArea->findChildren<QWidget*>()) {
            if (child->hasFocus()) {
                treeWidget_keys->setFocus();
                return;
            }
        }
        break;
    case EditorViewType::cmVarsView:
        if (QString("edbee::TextEditorComponent").compare(QApplication::focusWidget()->metaObject()->className()) == 0) {
            mpVarsMainArea->lineEdit_var_name->setFocus();
            return;
        }
        if (treeWidget_variables->hasFocus()) {
            mpSourceEditorEdbee->setFocus();
            return;
        }
        for (auto child : mpVarsMainArea->findChildren<QWidget*>()) {
            if (child->hasFocus()) {
                treeWidget_variables->setFocus();
                return;
            }
        }
        break;
    case EditorViewType::cmUnknownView:
        return;
    }
}

void dlgTriggerEditor::slot_activateMainWindow()
{
    mudlet::self()->activateWindow();
    mpHost->mpConsole->setFocus();
}

void dlgTriggerEditor::exportTrigger(const QString& fileName)
{
    QString name;
    TTrigger* pT = nullptr;
    QTreeWidgetItem* pItem = treeWidget_triggers->currentItem();
    if (pItem) {
        const int triggerID = pItem->data(0, Qt::UserRole).toInt();
        pT = mpHost->getTriggerUnit()->getTrigger(triggerID);
        if (pT) {
            name = pT->getName();
        } else {
            QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
            return;
        }
    } else {
        QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
        return;
    }
    XMLexport writer(pT);
    if (writer.exportTrigger(fileName)) {
        statusBar()->showMessage(tr("Package %1 saved").arg(name.toHtmlEscaped()), 2000);
    }
}

void dlgTriggerEditor::exportTimer(const QString& fileName)
{
    QString name;
    TTimer* pT = nullptr;
    QTreeWidgetItem* pItem = treeWidget_timers->currentItem();
    if (pItem) {
        const int triggerID = pItem->data(0, Qt::UserRole).toInt();
        pT = mpHost->getTimerUnit()->getTimer(triggerID);
        if (pT) {
            name = pT->getName();
        } else {
            QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
            return;
        }
    } else {
        QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
        return;
    }
    XMLexport writer(pT);
    if (writer.exportTimer(fileName)) {
        statusBar()->showMessage(tr("Package %1 saved").arg(name.toHtmlEscaped()), 2000);
    }
}

void dlgTriggerEditor::exportAlias(const QString& fileName)
{
    QString name;
    TAlias* pT = nullptr;
    QTreeWidgetItem* pItem = treeWidget_aliases->currentItem();
    if (pItem) {
        const int triggerID = pItem->data(0, Qt::UserRole).toInt();
        pT = mpHost->getAliasUnit()->getAlias(triggerID);
        if (pT) {
            name = pT->getName();
        } else {
            QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
            return;
        }
    } else {
        QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
        return;
    }
    XMLexport writer(pT);
    if (writer.exportAlias(fileName)) {
        statusBar()->showMessage(tr("Package %1 saved").arg(name.toHtmlEscaped()), 2000);
    }
}

void dlgTriggerEditor::exportAction(const QString& fileName)
{
    QString name;
    TAction* pT = nullptr;
    QTreeWidgetItem* pItem = treeWidget_actions->currentItem();
    if (pItem) {
        const int triggerID = pItem->data(0, Qt::UserRole).toInt();
        pT = mpHost->getActionUnit()->getAction(triggerID);
        if (pT) {
            name = pT->getName();
        } else {
            QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
            return;
        }
    } else {
        QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
        return;
    }
    XMLexport writer(pT);
    if (writer.exportAction(fileName)) {
        statusBar()->showMessage(tr("Package %1 saved").arg(name.toHtmlEscaped()), 2000);
    }
}

void dlgTriggerEditor::exportScript(const QString& fileName)
{
    QString name;
    TScript* pT = nullptr;
    QTreeWidgetItem* pItem = treeWidget_scripts->currentItem();
    if (pItem) {
        const int triggerID = pItem->data(0, Qt::UserRole).toInt();
        pT = mpHost->getScriptUnit()->getScript(triggerID);
        if (pT) {
            name = pT->getName();
        } else {
            QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
            return;
        }
    } else {
        QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
        return;
    }
    XMLexport writer(pT);
    if (writer.exportScript(fileName)) {
        statusBar()->showMessage(tr("Package %1 saved").arg(name.toHtmlEscaped()), 2000);
    }
}

void dlgTriggerEditor::exportKey(const QString& fileName)
{
    QString name;
    TKey* pT = nullptr;
    QTreeWidgetItem* pItem = treeWidget_keys->currentItem();
    if (pItem) {
        const int triggerID = pItem->data(0, Qt::UserRole).toInt();
        pT = mpHost->getKeyUnit()->getKey(triggerID);
        if (pT) {
            name = pT->getName();
        } else {
            QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
            return;
        }

    } else {
        QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
        return;
    }
    XMLexport writer(pT);
    if (writer.exportKey(fileName)) {
        statusBar()->showMessage(tr("Package %1 saved").arg(name.toHtmlEscaped()), 2000);
    }
}

void dlgTriggerEditor::exportTriggerToClipboard()
{
    QList<QTreeWidgetItem*> selectedItems = treeWidget_triggers->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
        return;
    }

    QStringList triggerNames;
    QList<TTrigger*> triggersToExport;

    for (const QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        const int triggerID = pItem->data(0, Qt::UserRole).toInt();
        TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(triggerID);
        if (pT) {
            triggerNames << pT->getName();
            triggersToExport << pT;
        }
    }

    if (triggersToExport.isEmpty()) {
        QMessageBox::warning(this, tr("Export Package:"), tr("No valid triggers found to export."));
        return;
    }

    if (triggersToExport.size() == 1) {
        // Single item - use existing method
        XMLexport writer(triggersToExport.first());
        writer.exportToClipboard(triggersToExport.first());
        statusBar()->showMessage(tr("Copied %1 to clipboard").arg(triggerNames.first().toHtmlEscaped()), 2000);
    } else {
        // Multiple items - export them individually and let user paste multiple times
        exportMultipleTriggersToClipboard(triggersToExport);
        statusBar()->showMessage(tr("Copied %1 triggers to clipboard").arg(triggersToExport.size()), 2000);
    }
}

void dlgTriggerEditor::exportMultipleTriggersToClipboard(const QList<TTrigger*>& triggers)
{
    if (triggers.isEmpty()) {
        return;
    }

    // Store multiple XML packages separated by a special delimiter
    // This allows the paste function to split and import each item individually
    QStringList xmlPackages;

    for (TTrigger* trigger : triggers) {
        XMLexport writer(trigger);

        // Get the XML for this trigger by temporarily using the clipboard
        QString originalClipboard = QApplication::clipboard()->text();
        writer.exportToClipboard(trigger);
        QString triggerXml = QApplication::clipboard()->text();
        QApplication::clipboard()->setText(originalClipboard);

        xmlPackages << triggerXml;
    }

    // Combine all XML packages with a special separator that paste can recognize
    QString combinedXml = xmlPackages.join(cMultiItemPasteSeparator);
    QApplication::clipboard()->setText(combinedXml);
}

void dlgTriggerEditor::exportTimerToClipboard()
{
    QList<QTreeWidgetItem*> selectedItems = treeWidget_timers->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
        return;
    }

    QStringList timerNames;
    QList<TTimer*> timersToExport;

    for (const QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        const int timerID = pItem->data(0, Qt::UserRole).toInt();
        TTimer* pT = mpHost->getTimerUnit()->getTimer(timerID);
        if (pT) {
            timerNames << pT->getName();
            timersToExport << pT;
        }
    }

    if (timersToExport.isEmpty()) {
        QMessageBox::warning(this, tr("Export Package:"), tr("No valid timers found to export."));
        return;
    }

    if (timersToExport.size() == 1) {
        XMLexport writer(timersToExport.first());
        writer.exportToClipboard(timersToExport.first());
        statusBar()->showMessage(tr("Copied %1 to clipboard").arg(timerNames.first().toHtmlEscaped()), 2000);
    } else {
        exportMultipleTimersToClipboard(timersToExport);
        statusBar()->showMessage(tr("Copied %1 timers to clipboard").arg(timersToExport.size()), 2000);
    }
}

void dlgTriggerEditor::exportMultipleTimersToClipboard(const QList<TTimer*>& timers)
{
    if (timers.isEmpty()) {
        return;
    }

    QStringList xmlParts;

    for (TTimer* timer : timers) {
        XMLexport writer(timer);
        QString originalClipboard = QApplication::clipboard()->text();
        writer.exportToClipboard(timer);
        QString timerXml = QApplication::clipboard()->text();
        xmlParts << timerXml;
        QApplication::clipboard()->setText(originalClipboard);
    }

    QString combinedXml = xmlParts.join(cMultiItemPasteSeparator);
    QApplication::clipboard()->setText(combinedXml);
}

void dlgTriggerEditor::exportAliasToClipboard()
{
    QList<QTreeWidgetItem*> selectedItems = treeWidget_aliases->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
        return;
    }

    QStringList aliasNames;
    QList<TAlias*> aliasesToExport;

    for (const QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        const int aliasID = pItem->data(0, Qt::UserRole).toInt();
        TAlias* pT = mpHost->getAliasUnit()->getAlias(aliasID);
        if (pT) {
            aliasNames << pT->getName();
            aliasesToExport << pT;
        }
    }

    if (aliasesToExport.isEmpty()) {
        QMessageBox::warning(this, tr("Export Package:"), tr("No valid aliases found to export."));
        return;
    }

    if (aliasesToExport.size() == 1) {
        XMLexport writer(aliasesToExport.first());
        writer.exportToClipboard(aliasesToExport.first());
        statusBar()->showMessage(tr("Copied %1 to clipboard").arg(aliasNames.first().toHtmlEscaped()), 2000);
    } else {
        exportMultipleAliasesToClipboard(aliasesToExport);
        statusBar()->showMessage(tr("Copied %1 aliases to clipboard").arg(aliasesToExport.size()), 2000);
    }
}

void dlgTriggerEditor::exportMultipleAliasesToClipboard(const QList<TAlias*>& aliases)
{
    if (aliases.isEmpty()) {
        return;
    }

    QStringList xmlParts;

    for (TAlias* alias : aliases) {
        XMLexport writer(alias);
        QString originalClipboard = QApplication::clipboard()->text();
        writer.exportToClipboard(alias);
        QString aliasXml = QApplication::clipboard()->text();
        xmlParts << aliasXml;
        QApplication::clipboard()->setText(originalClipboard);
    }

    QString combinedXml = xmlParts.join(cMultiItemPasteSeparator);
    QApplication::clipboard()->setText(combinedXml);
}

void dlgTriggerEditor::exportActionToClipboard()
{
    QList<QTreeWidgetItem*> selectedItems = treeWidget_actions->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
        return;
    }

    QStringList actionNames;
    QList<TAction*> actionsToExport;

    for (const QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        const int actionID = pItem->data(0, Qt::UserRole).toInt();
        TAction* pT = mpHost->getActionUnit()->getAction(actionID);
        if (pT) {
            actionNames << pT->getName();
            actionsToExport << pT;
        }
    }

    if (actionsToExport.isEmpty()) {
        QMessageBox::warning(this, tr("Export Package:"), tr("No valid actions found to export."));
        return;
    }

    if (actionsToExport.size() == 1) {
        XMLexport writer(actionsToExport.first());
        writer.exportToClipboard(actionsToExport.first());
        statusBar()->showMessage(tr("Copied %1 to clipboard").arg(actionNames.first().toHtmlEscaped()), 2000);
    } else {
        exportMultipleActionsToClipboard(actionsToExport);
        statusBar()->showMessage(tr("Copied %1 actions to clipboard").arg(actionsToExport.size()), 2000);
    }
}

void dlgTriggerEditor::exportMultipleActionsToClipboard(const QList<TAction*>& actions)
{
    if (actions.isEmpty()) {
        return;
    }

    QStringList xmlParts;

    for (TAction* action : actions) {
        XMLexport writer(action);
        QString originalClipboard = QApplication::clipboard()->text();
        writer.exportToClipboard(action);
        QString actionXml = QApplication::clipboard()->text();
        xmlParts << actionXml;
        QApplication::clipboard()->setText(originalClipboard);
    }

    QString combinedXml = xmlParts.join(cMultiItemPasteSeparator);
    QApplication::clipboard()->setText(combinedXml);
}

void dlgTriggerEditor::exportScriptToClipboard()
{
    QList<QTreeWidgetItem*> selectedItems = treeWidget_scripts->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
        return;
    }

    QStringList scriptNames;
    QList<TScript*> scriptsToExport;

    for (const QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        const int scriptID = pItem->data(0, Qt::UserRole).toInt();
        TScript* pT = mpHost->getScriptUnit()->getScript(scriptID);
        if (pT) {
            scriptNames << pT->getName();
            scriptsToExport << pT;
        }
    }

    if (scriptsToExport.isEmpty()) {
        QMessageBox::warning(this, tr("Export Package:"), tr("No valid scripts found to export."));
        return;
    }

    if (scriptsToExport.size() == 1) {
        XMLexport writer(scriptsToExport.first());
        writer.exportToClipboard(scriptsToExport.first());
        statusBar()->showMessage(tr("Copied %1 to clipboard").arg(scriptNames.first().toHtmlEscaped()), 2000);
    } else {
        exportMultipleScriptsToClipboard(scriptsToExport);
        statusBar()->showMessage(tr("Copied %1 scripts to clipboard").arg(scriptsToExport.size()), 2000);
    }
}

void dlgTriggerEditor::exportMultipleScriptsToClipboard(const QList<TScript*>& scripts)
{
    if (scripts.isEmpty()) {
        return;
    }

    QStringList xmlParts;

    for (TScript* script : scripts) {
        XMLexport writer(script);
        QString originalClipboard = QApplication::clipboard()->text();
        writer.exportToClipboard(script);
        QString scriptXml = QApplication::clipboard()->text();
        xmlParts << scriptXml;
        QApplication::clipboard()->setText(originalClipboard);
    }

    QString combinedXml = xmlParts.join(cMultiItemPasteSeparator);
    QApplication::clipboard()->setText(combinedXml);
}

void dlgTriggerEditor::exportKeyToClipboard()
{
    QList<QTreeWidgetItem*> selectedItems = treeWidget_keys->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::warning(this, tr("Export Package:"), tr("You have to choose an item for export first. Please select a tree item and then click on export again."));
        return;
    }

    QStringList keyNames;
    QList<TKey*> keysToExport;

    for (const QTreeWidgetItem* pItem : std::as_const(selectedItems)) {
        const int keyID = pItem->data(0, Qt::UserRole).toInt();
        TKey* pT = mpHost->getKeyUnit()->getKey(keyID);
        if (pT) {
            keyNames << pT->getName();
            keysToExport << pT;
        }
    }

    if (keysToExport.isEmpty()) {
        QMessageBox::warning(this, tr("Export Package:"), tr("No valid keys found to export."));
        return;
    }

    if (keysToExport.size() == 1) {
        XMLexport writer(keysToExport.first());
        writer.exportToClipboard(keysToExport.first());
        statusBar()->showMessage(tr("Copied %1 to clipboard").arg(keyNames.first().toHtmlEscaped()), 2000);
    } else {
        exportMultipleKeysToClipboard(keysToExport);
        statusBar()->showMessage(tr("Copied %1 keys to clipboard").arg(keysToExport.size()), 2000);
    }
}

void dlgTriggerEditor::exportMultipleKeysToClipboard(const QList<TKey*>& keys)
{
    if (keys.isEmpty()) {
        return;
    }

    QStringList xmlParts;

    for (TKey* key : keys) {
        XMLexport writer(key);
        QString originalClipboard = QApplication::clipboard()->text();
        writer.exportToClipboard(key);
        QString keyXml = QApplication::clipboard()->text();
        xmlParts << keyXml;
        QApplication::clipboard()->setText(originalClipboard);
    }

    QString combinedXml = xmlParts.join(cMultiItemPasteSeparator);
    QApplication::clipboard()->setText(combinedXml);
}

void dlgTriggerEditor::slot_export()
{
    const EditorViewType currentView = resolveCurrentView();
    if (currentView == EditorViewType::cmUnknownView || currentView == EditorViewType::cmVarsView) {
        return;
    }

    QSettings& settings = *mudlet::getQSettings();
    QString lastDir = settings.value("lastFileDialogLocation", QDir::homePath()).toString();

    QString fileName = QFileDialog::getSaveFileName(this, tr("Export Item"), lastDir, tr("Mudlet packages (*.xml)"));
    if (fileName.isEmpty()) {
        return;
    }

    lastDir = QFileInfo(fileName).absolutePath();
    settings.setValue("lastFileDialogLocation", lastDir);

    // Must be case insensitive to work on MacOS platforms, possibly a cause of
    // https://bugs.launchpad.net/mudlet/+bug/1417234
    if (!fileName.endsWith(qsl(".xml"), Qt::CaseInsensitive)) {
        fileName.append(qsl(".xml"));
    }


    QFile checkWriteability(fileName);
    if (!checkWriteability.open(QFile::WriteOnly | QFile::Text)) {
        QMessageBox::warning(this, tr("export package:"), tr("Cannot write file %1:\n%2.").arg(fileName.toHtmlEscaped(), checkWriteability.errorString()));
        return;
    }
    // Should close the checkWriteability that we have confirmed can be opened:
    checkWriteability.close();

    switch (currentView) {
    case EditorViewType::cmTriggerView:
        exportTrigger(fileName);
        break;
    case EditorViewType::cmTimerView:
        exportTimer(fileName);
        break;
    case EditorViewType::cmAliasView:
        exportAlias(fileName);
        break;
    case EditorViewType::cmScriptView:
        exportScript(fileName);
        break;
    case EditorViewType::cmActionView:
        exportAction(fileName);
        break;
    case EditorViewType::cmKeysView:
        exportKey(fileName);
        break;
    case EditorViewType::cmVarsView:
        [[fallthrough]];
    case EditorViewType::cmUnknownView:
        // These two have already been handled so this place in the code should
        // indeed be:
        Q_UNREACHABLE();
    }
}

void dlgTriggerEditor::slot_createModule()
{
    const EditorViewType currentView = resolveCurrentView();
    if (currentView == EditorViewType::cmUnknownView || currentView == EditorViewType::cmVarsView) {
        return;
    }

    // Open the package exporter dialog with module creation mode
    auto* packageExporter = new dlgPackageExporter(this, mpHost);

    // Pre-select the current item for export
    switch (currentView) {
    case EditorViewType::cmTriggerView:
        if (mpCurrentTriggerItem) {
            packageExporter->preselectTrigger(mpCurrentTriggerItem);
        }
        break;
    case EditorViewType::cmTimerView:
        if (mpCurrentTimerItem) {
            packageExporter->preselectTimer(mpCurrentTimerItem);
        }
        break;
    case EditorViewType::cmAliasView:
        if (mpCurrentAliasItem) {
            packageExporter->preselectAlias(mpCurrentAliasItem);
        }
        break;
    case EditorViewType::cmScriptView:
        if (mpCurrentScriptItem) {
            packageExporter->preselectScript(mpCurrentScriptItem);
        }
        break;
    case EditorViewType::cmActionView:
        if (mpCurrentActionItem) {
            packageExporter->preselectAction(mpCurrentActionItem);
        }
        break;
    case EditorViewType::cmKeysView:
        if (mpCurrentKeyItem) {
            packageExporter->preselectKey(mpCurrentKeyItem);
        }
        break;
    default:
        break;
    }

    // Set module creation mode
    packageExporter->setModuleCreationMode(true);
    packageExporter->show();
}

void dlgTriggerEditor::slot_copyXml()
{
    switch (resolveCurrentView()) {
    case EditorViewType::cmTriggerView:
        exportTriggerToClipboard();
        break;
    case EditorViewType::cmTimerView:
        exportTimerToClipboard();
        break;
    case EditorViewType::cmAliasView:
        exportAliasToClipboard();
        break;
    case EditorViewType::cmScriptView:
        exportScriptToClipboard();
        break;
    case EditorViewType::cmActionView:
        exportActionToClipboard();
        break;
    case EditorViewType::cmKeysView:
        exportKeyToClipboard();
        break;
    case EditorViewType::cmVarsView:
        qWarning().nospace().noquote() << "dlgTriggerEditor::slot_copyXml() WARNING - switch(EditorViewType) not expected to be called for \"EditorViewType::cmVarsView!\"";
        break;
    case EditorViewType::cmUnknownView:
        qWarning().nospace().noquote() << "dlgTriggerEditor::slot_copyXml() WARNING - switch(EditorViewType) not expected to be called for \"EditorViewType::cmUnknownView!\"";
        break;
    }
}

// FIXME: The switch cases in here need to handle EditorViewType::cmVarsView but how is not clear
// Applies the same placement to every pasted item that pasting a single item
// would get: into the selected group/folder, or next to the selected item.
void dlgTriggerEditor::placePastedItems(EditorViewType itemType, const QList<int>& itemIDs)
{
    QTreeWidget* targetTree = nullptr;
    std::function<bool(int)> itemIsFolder;
    std::function<void(int, int, int, int)> reParentItem;

    switch (itemType) {
    case EditorViewType::cmTriggerView:
        targetTree = treeWidget_triggers;
        itemIsFolder = [this](int id) {
            auto* pItem = mpHost->getTriggerUnit()->getTrigger(id);
            return pItem && pItem->isFolder();
        };
        reParentItem = [this](int id, int parentId, int parentPos, int childPos) {
            mpHost->getTriggerUnit()->reParentTrigger(id, 0, parentId, parentPos, childPos);
        };
        break;
    case EditorViewType::cmTimerView:
        targetTree = treeWidget_timers;
        itemIsFolder = [this](int id) {
            auto* pItem = mpHost->getTimerUnit()->getTimer(id);
            return pItem && pItem->isFolder();
        };
        reParentItem = [this](int id, int parentId, int parentPos, int childPos) {
            mpHost->getTimerUnit()->reParentTimer(id, 0, parentId, parentPos, childPos);
        };
        break;
    case EditorViewType::cmAliasView:
        targetTree = treeWidget_aliases;
        itemIsFolder = [this](int id) {
            auto* pItem = mpHost->getAliasUnit()->getAlias(id);
            return pItem && pItem->isFolder();
        };
        reParentItem = [this](int id, int parentId, int parentPos, int childPos) {
            mpHost->getAliasUnit()->reParentAlias(id, 0, parentId, parentPos, childPos);
        };
        break;
    case EditorViewType::cmScriptView:
        targetTree = treeWidget_scripts;
        itemIsFolder = [this](int id) {
            auto* pItem = mpHost->getScriptUnit()->getScript(id);
            return pItem && pItem->isFolder();
        };
        reParentItem = [this](int id, int parentId, int parentPos, int childPos) {
            mpHost->getScriptUnit()->reParentScript(id, 0, parentId, parentPos, childPos);
        };
        break;
    case EditorViewType::cmActionView:
        targetTree = treeWidget_actions;
        itemIsFolder = [this](int id) {
            auto* pItem = mpHost->getActionUnit()->getAction(id);
            return pItem && pItem->isFolder();
        };
        reParentItem = [this](int id, int parentId, int parentPos, int childPos) {
            mpHost->getActionUnit()->reParentAction(id, 0, parentId, parentPos, childPos);
        };
        break;
    case EditorViewType::cmKeysView:
        targetTree = treeWidget_keys;
        itemIsFolder = [this](int id) {
            auto* pItem = mpHost->getKeyUnit()->getKey(id);
            return pItem && pItem->isFolder();
        };
        reParentItem = [this](int id, int parentId, int parentPos, int childPos) {
            mpHost->getKeyUnit()->reParentKey(id, 0, parentId, parentPos, childPos);
        };
        break;
    case EditorViewType::cmVarsView:
    case EditorViewType::cmUnknownView:
        return;
    }

    QModelIndex targetIndex = targetTree->currentIndex();
    if (!targetIndex.isValid()) {
        QList<QTreeWidgetItem*> selectedItems = targetTree->selectedItems();
        if (!selectedItems.isEmpty()) {
            targetIndex = targetTree->indexFromItem(selectedItems.first());
        }
    }
    if (!targetIndex.isValid()) {
        return;
    }

    QTreeWidgetItem* targetItem = targetTree->itemFromIndex(targetIndex);
    const int targetId = targetIndex.data(Qt::UserRole).toInt();
    const bool isGroup = (targetItem && targetItem->childCount() > 0) || itemIsFolder(targetId);

    if (isGroup) {
        for (const int itemID : itemIDs) {
            reParentItem(itemID, targetId, -1, -1);
        }
    } else {
        // mirror single-item paste: insert as siblings right after the selected item
        const QModelIndex parentIndex = targetIndex.parent();
        const int parentId = parentIndex.data(Qt::UserRole).toInt();
        const int parentRow = parentIndex.row();
        int childRow = targetIndex.row() + 1;
        for (const int itemID : itemIDs) {
            reParentItem(itemID, parentId, parentRow, childRow++);
        }
    }
}

void dlgTriggerEditor::slot_pasteXml()
{
    XMLimport reader(mpHost);

    switch (resolveCurrentView()) {
    case EditorViewType::cmTriggerView:
        saveTrigger();
        break;
    case EditorViewType::cmTimerView:
        saveTimer();
        break;
    case EditorViewType::cmAliasView:
        saveAlias();
        break;
    case EditorViewType::cmScriptView:
        saveScript();
        break;
    case EditorViewType::cmActionView:
        saveAction();
        break;
    case EditorViewType::cmKeysView:
        saveKey();
        break;
    case EditorViewType::cmVarsView:
        qWarning().nospace().noquote() << "dlgTriggerEditor::slot_pasteXml() WARNING - switch(EditorViewType) number 1 not expected to be called for \"EditorViewType::cmVarsView!\"";
        break;
    case EditorViewType::cmUnknownView:
        qWarning().nospace().noquote() << "dlgTriggerEditor::slot_pasteXml() WARNING - switch(EditorViewType) number 1 not expected to be called for \"EditorViewType::cmUnknownView!\"";
        break;
    }

    // Check if clipboard contains multiple items (separated by our delimiter)
    QString clipboardText = QApplication::clipboard()->text();
    QStringList xmlPackages = clipboardText.split(cMultiItemPasteSeparator);

    EditorViewType importedItemType;
    int importedItemID;
    QList<int> importedIDs;

    if (xmlPackages.size() > 1) {
        EditorViewType firstImportType = EditorViewType::cmUnknownView;

        QString originalClipboard = QApplication::clipboard()->text();

        for (const QString& xmlItem : std::as_const(xmlPackages)) {
            QString xmlItemTrimmed = xmlItem.trimmed();
            if (xmlItemTrimmed.isEmpty()) {
                continue; // Skip empty items
            }

            // Temporarily set clipboard to single item
            QApplication::clipboard()->setText(xmlItemTrimmed);

            // Import this single item
            XMLimport itemReader(mpHost);
            auto [itemType, itemID] = itemReader.importFromClipboard();

            if (itemType != EditorViewType::cmUnknownView && itemID != 0) {
                importedIDs << itemID;
                if (firstImportType == EditorViewType::cmUnknownView) {
                    firstImportType = itemType;
                }
            }
        }

        // Restore original clipboard once at the end
        QApplication::clipboard()->setText(originalClipboard);

        if (!importedIDs.isEmpty()) {
            // For multiple items, we need to handle the reparenting here instead of later
            // since the later logic only handles one item at a time
            placePastedItems(firstImportType, importedIDs);

            // Use the first imported item's type and ID for the rest of the function
            importedItemType = firstImportType;
            importedItemID = importedIDs.first();

            statusBar()->showMessage(tr("Pasted %1 items successfully").arg(importedIDs.size()), 3000);
        } else {
            // No items were imported - don't create undo action
            return;
        }
    } else {
        // Single item - use original import method
        auto [itemType, itemID] = reader.importFromClipboard();
        importedItemType = itemType;
        importedItemID = itemID;

        // don't reset the view if what we pasted wasn't a Mudlet editor item
        if (importedItemType == EditorViewType::cmUnknownView && importedItemID == 0) {
            // No valid item was imported - don't create undo action
            return;
        }
    }

    if (mpUndoStack) {
        //: Undo/redo text for pasting items
        mpUndoStack->beginMacro(tr("paste"));
    }

    mCurrentView = static_cast<EditorViewType>(importedItemType);
    // importing drops the item at the bottom of the list - move it to be a sibling
    // of the currently selected item instead
    switch (mCurrentView) {
    case EditorViewType::cmTriggerView: {
        if (xmlPackages.size() > 1) {
            // multi-item pastes were already reparented above; reparenting the
            // first trigger again would link it into its parent's children twice
            break;
        }
        // Handle multi-selection: use the first selected item as reference
        QModelIndex targetIndex = treeWidget_triggers->currentIndex();
        if (!targetIndex.isValid()) {
            // If no current index, try to get from selected items
            QList<QTreeWidgetItem*> selectedItems = treeWidget_triggers->selectedItems();
            if (!selectedItems.isEmpty()) {
                targetIndex = treeWidget_triggers->indexFromItem(selectedItems.first());
            }
        }

        if (targetIndex.isValid()) {
            // Check if the selected item is a trigger group/folder
            QTreeWidgetItem* targetItem = treeWidget_triggers->itemFromIndex(targetIndex);
            int targetId = targetIndex.data(Qt::UserRole).toInt();
            TTrigger* targetTrigger = mpHost->getTriggerUnit()->getTrigger(targetId);

            // Check if target is a group/folder (has children OR is a group trigger)
            bool isGroup = (targetItem && targetItem->childCount() > 0) || (targetTrigger && targetTrigger->isFolder());

            if (isGroup) {
                // Paste INSIDE the selected group/folder
                mpHost->getTriggerUnit()->reParentTrigger(importedItemID, 0, targetId, -1, -1);
            } else {
                // Paste as sibling next to the selected item
                auto parent = targetIndex.parent();
                auto parentRow = parent.row();
                auto parentId = parent.data(Qt::UserRole).toInt();

                const int siblingRow = targetIndex.row() + 1;
                mpHost->getTriggerUnit()->reParentTrigger(importedItemID, 0, parentId, parentRow, siblingRow);
            }
        } else {
            // If no valid target, place at the root level
            mpHost->getTriggerUnit()->reParentTrigger(importedItemID, 0, 0, -1, -1);
        }
        break;
    }
    case EditorViewType::cmTimerView: {
        if (xmlPackages.size() > 1) {
            // multi-item pastes were already reparented above
            break;
        }
        QModelIndex targetIndex = treeWidget_timers->currentIndex();
        if (!targetIndex.isValid()) {
            QList<QTreeWidgetItem*> selectedItems = treeWidget_timers->selectedItems();
            if (!selectedItems.isEmpty()) {
                targetIndex = treeWidget_timers->indexFromItem(selectedItems.first());
            }
        }

        if (targetIndex.isValid()) {
            auto parent = targetIndex.parent();
            auto parentRow = parent.row();
            auto parentId = parent.data(Qt::UserRole).toInt();

            const int siblingRow = targetIndex.row() + 1;
            mpHost->getTimerUnit()->reParentTimer(importedItemID, 0, parentId, parentRow, siblingRow);
        } else {
            mpHost->getTimerUnit()->reParentTimer(importedItemID, 0, 0, -1, -1);
        }
        break;
    }
    case EditorViewType::cmAliasView: {
        if (xmlPackages.size() > 1) {
            // multi-item pastes were already reparented above
            break;
        }
        QModelIndex targetIndex = treeWidget_aliases->currentIndex();
        if (!targetIndex.isValid()) {
            QList<QTreeWidgetItem*> selectedItems = treeWidget_aliases->selectedItems();
            if (!selectedItems.isEmpty()) {
                targetIndex = treeWidget_aliases->indexFromItem(selectedItems.first());
            }
        }

        if (targetIndex.isValid()) {
            auto parent = targetIndex.parent();
            auto parentRow = parent.row();
            auto parentId = parent.data(Qt::UserRole).toInt();

            const int siblingRow = targetIndex.row() + 1;
            mpHost->getAliasUnit()->reParentAlias(importedItemID, 0, parentId, parentRow, siblingRow);
        } else {
            mpHost->getAliasUnit()->reParentAlias(importedItemID, 0, 0, -1, -1);
        }
        break;
    }
    case EditorViewType::cmScriptView: {
        if (xmlPackages.size() > 1) {
            // multi-item pastes were already reparented above
            break;
        }
        QModelIndex targetIndex = treeWidget_scripts->currentIndex();
        if (!targetIndex.isValid()) {
            QList<QTreeWidgetItem*> selectedItems = treeWidget_scripts->selectedItems();
            if (!selectedItems.isEmpty()) {
                targetIndex = treeWidget_scripts->indexFromItem(selectedItems.first());
            }
        }

        if (targetIndex.isValid()) {
            auto parent = targetIndex.parent();
            auto parentRow = parent.row();
            auto parentId = parent.data(Qt::UserRole).toInt();

            const int siblingRow = targetIndex.row() + 1;
            mpHost->getScriptUnit()->reParentScript(importedItemID, 0, parentId, parentRow, siblingRow);
        } else {
            mpHost->getScriptUnit()->reParentScript(importedItemID, 0, 0, -1, -1);
        }
        break;
    }
    case EditorViewType::cmActionView: {
        if (xmlPackages.size() > 1) {
            // multi-item pastes were already reparented above
            break;
        }
        QModelIndex targetIndex = treeWidget_actions->currentIndex();
        if (!targetIndex.isValid()) {
            QList<QTreeWidgetItem*> selectedItems = treeWidget_actions->selectedItems();
            if (!selectedItems.isEmpty()) {
                targetIndex = treeWidget_actions->indexFromItem(selectedItems.first());
            }
        }

        if (targetIndex.isValid()) {
            auto parent = targetIndex.parent();
            auto parentRow = parent.row();
            auto parentId = parent.data(Qt::UserRole).toInt();

            const int siblingRow = targetIndex.row() + 1;
            mpHost->getActionUnit()->reParentAction(importedItemID, 0, parentId, parentRow, siblingRow);
        } else {
            mpHost->getActionUnit()->reParentAction(importedItemID, 0, 0, -1, -1);
        }
        break;
    }
    case EditorViewType::cmKeysView: {
        if (xmlPackages.size() > 1) {
            // multi-item pastes were already reparented above
            break;
        }
        QModelIndex targetIndex = treeWidget_keys->currentIndex();
        if (!targetIndex.isValid()) {
            QList<QTreeWidgetItem*> selectedItems = treeWidget_keys->selectedItems();
            if (!selectedItems.isEmpty()) {
                targetIndex = treeWidget_keys->indexFromItem(selectedItems.first());
            }
        }

        if (targetIndex.isValid()) {
            auto parent = targetIndex.parent();
            auto parentRow = parent.row();
            auto parentId = parent.data(Qt::UserRole).toInt();

            const int siblingRow = targetIndex.row() + 1;
            mpHost->getKeyUnit()->reParentKey(importedItemID, 0, parentId, parentRow, siblingRow);
        } else {
            mpHost->getKeyUnit()->reParentKey(importedItemID, 0, 0, -1, -1);
        }
        break;
    }
    case EditorViewType::cmVarsView:
        qWarning().nospace().noquote() << "dlgTriggerEditor::slot_pasteXml() WARNING - switch(EditorViewType) number 2 not expected to be called for \"EditorViewType::cmVarsView!\"";
        break;
    case EditorViewType::cmUnknownView:
        qWarning().nospace().noquote() << "dlgTriggerEditor::slot_pasteXml() WARNING - switch(EditorViewType) number 2 not expected to be called for \"EditorViewType::cmUnknownView!\"";
        break;
    }

    // Register undo commands for the pasted items
    if (mpUndoStack) {
        // Helper lambda to calculate position within parent's children
        auto calculatePositionInParent = [](auto* item) -> int {
            if (!item) {
                return 0;
            }
            auto* parent = item->getParent();
            if (!parent) {
                return 0;
            }
            auto* childrenList = parent->getChildrenList();
            if (!childrenList) {
                return 0;
            }
            int position = 0;
            for (auto* child : *childrenList) {
                if (child == item) {
                    return position;
                }
                position++;
            }
            return 0;
        };

        // Helper lambda to register an undo command for a single pasted item
        auto registerUndoCommand = [&](EditorViewType viewType, int itemID) {
            QString itemName;
            int parentID = -1;
            int positionInParent = 0;
            bool isFolder = false;

            switch (viewType) {
            case EditorViewType::cmTriggerView: {
                TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(itemID);
                if (pT) {
                    itemName = pT->getName();
                    isFolder = pT->isFolder();
                    auto* parent = pT->getParent();
                    if (parent) {
                        parentID = parent->getID();
                        positionInParent = calculatePositionInParent(pT);
                    } else {
                        parentID = -1;
                        auto rootList = mpHost->getTriggerUnit()->getTriggerRootNodeList();
                        int pos = 0;
                        for (auto* rootItem : rootList) {
                            if (rootItem == pT) {
                                positionInParent = pos;
                                break;
                            }
                            pos++;
                        }
                    }
                }
                break;
            }
            case EditorViewType::cmTimerView: {
                TTimer* pT = mpHost->getTimerUnit()->getTimer(itemID);
                if (pT) {
                    itemName = pT->getName();
                    isFolder = pT->isFolder();
                    auto* parent = pT->getParent();
                    if (parent) {
                        parentID = parent->getID();
                        positionInParent = calculatePositionInParent(pT);
                    } else {
                        parentID = -1;
                        auto rootList = mpHost->getTimerUnit()->getTimerRootNodeList();
                        int pos = 0;
                        for (auto* rootItem : rootList) {
                            if (rootItem == pT) {
                                positionInParent = pos;
                                break;
                            }
                            pos++;
                        }
                    }
                }
                break;
            }
            case EditorViewType::cmAliasView: {
                TAlias* pA = mpHost->getAliasUnit()->getAlias(itemID);
                if (pA) {
                    itemName = pA->getName();
                    isFolder = pA->isFolder();
                    auto* parent = pA->getParent();
                    if (parent) {
                        parentID = parent->getID();
                        positionInParent = calculatePositionInParent(pA);
                    } else {
                        parentID = -1;
                        auto rootList = mpHost->getAliasUnit()->getAliasRootNodeList();
                        int pos = 0;
                        for (auto* rootItem : rootList) {
                            if (rootItem == pA) {
                                positionInParent = pos;
                                break;
                            }
                            pos++;
                        }
                    }
                }
                break;
            }
            case EditorViewType::cmScriptView: {
                TScript* pS = mpHost->getScriptUnit()->getScript(itemID);
                if (pS) {
                    itemName = pS->getName();
                    isFolder = pS->isFolder();
                    auto* parent = pS->getParent();
                    if (parent) {
                        parentID = parent->getID();
                        positionInParent = calculatePositionInParent(pS);
                    } else {
                        parentID = -1;
                        auto rootList = mpHost->getScriptUnit()->getScriptRootNodeList();
                        int pos = 0;
                        for (auto* rootItem : rootList) {
                            if (rootItem == pS) {
                                positionInParent = pos;
                                break;
                            }
                            pos++;
                        }
                    }
                }
                break;
            }
            case EditorViewType::cmActionView: {
                TAction* pA = mpHost->getActionUnit()->getAction(itemID);
                if (pA) {
                    itemName = pA->getName();
                    isFolder = pA->isFolder();
                    auto* parent = pA->getParent();
                    if (parent) {
                        parentID = parent->getID();
                        positionInParent = calculatePositionInParent(pA);
                    } else {
                        parentID = -1;
                        auto rootList = mpHost->getActionUnit()->getActionRootNodeList();
                        int pos = 0;
                        for (auto* rootItem : rootList) {
                            if (rootItem == pA) {
                                positionInParent = pos;
                                break;
                            }
                            pos++;
                        }
                    }
                }
                break;
            }
            case EditorViewType::cmKeysView: {
                TKey* pK = mpHost->getKeyUnit()->getKey(itemID);
                if (pK) {
                    itemName = pK->getName();
                    isFolder = pK->isFolder();
                    auto* parent = pK->getParent();
                    if (parent) {
                        parentID = parent->getID();
                        positionInParent = calculatePositionInParent(pK);
                    } else {
                        parentID = -1;
                        auto rootList = mpHost->getKeyUnit()->getKeyRootNodeList();
                        int pos = 0;
                        for (auto* rootItem : rootList) {
                            if (rootItem == pK) {
                                positionInParent = pos;
                                break;
                            }
                            pos++;
                        }
                    }
                }
                break;
            }
            default:
                return;
            }

            if (!itemName.isEmpty()) {
                auto* qtCmd = new EditorAddItemCommand(viewType, itemID, parentID, positionInParent, isFolder, itemName, mpHost);
                mpUndoStack->pushCommand(qtCmd);
            }
        };

        // Register undo commands for all imported items
        if (xmlPackages.size() > 1) {
            // Multiple items were pasted
            if (!importedIDs.isEmpty()) {
                for (const int itemID : std::as_const(importedIDs)) {
                    registerUndoCommand(importedItemType, itemID);
                }
            }
        } else {
            // Single item was pasted
            registerUndoCommand(importedItemType, importedItemID);
        }
    }

    // flag for re-rendering so the new item shows up in the right spot
    mNeedUpdateData = true;

    switch (importedItemType) {
    case EditorViewType::cmTriggerView: {
        // the view becomes collapsed as a result of the clear & redo and then
        // animates back into the unfolding, which doesn't look nice - so turn
        // off animation temporarily
        auto animated = treeWidget_triggers->isAnimated();
        treeWidget_triggers->setAnimated(false);
        selectTriggerByID(importedItemID);
        treeWidget_triggers->setAnimated(animated);

        // set the focus because hiding checkBox_displayAllVariables in changeView
        // changes the focus to the search box for some reason. This thus breaks
        // successive pastes because you'll now be pasting into the search box
        focusPanelTree(treeWidget_triggers);
        break;
    }
    case EditorViewType::cmTimerView: {
        auto animated = treeWidget_timers->isAnimated();
        treeWidget_timers->setAnimated(false);
        selectTimerByID(importedItemID);
        treeWidget_timers->setAnimated(animated);
        focusPanelTree(treeWidget_timers);
        break;
    }
    case EditorViewType::cmAliasView: {
        auto animated = treeWidget_aliases->isAnimated();
        treeWidget_aliases->setAnimated(false);
        selectAliasByID(importedItemID);
        treeWidget_aliases->setAnimated(animated);
        focusPanelTree(treeWidget_aliases);
        break;
    }
    case EditorViewType::cmScriptView: {
        auto animated = treeWidget_scripts->isAnimated();
        treeWidget_scripts->setAnimated(false);
        selectScriptByID(importedItemID);
        treeWidget_scripts->setAnimated(animated);
        focusPanelTree(treeWidget_scripts);
        break;
    }
    case EditorViewType::cmActionView: {
        auto animated = treeWidget_actions->isAnimated();
        treeWidget_actions->setAnimated(false);
        selectActionByID(importedItemID);
        treeWidget_actions->setAnimated(animated);
        focusPanelTree(treeWidget_actions);
        break;
    }
    case EditorViewType::cmKeysView: {
        auto animated = treeWidget_keys->isAnimated();
        treeWidget_keys->setAnimated(false);
        selectKeyByID(importedItemID);
        treeWidget_keys->setAnimated(animated);
        focusPanelTree(treeWidget_keys);
        break;
    }
    case EditorViewType::cmVarsView:
        qWarning().nospace().noquote() << "dlgTriggerEditor::slot_pasteXml() WARNING - switch(EditorViewType) number 3 not expected to be called for \"EditorViewType::cmVarsView!\"";
        break;
    case EditorViewType::cmUnknownView:
        qWarning().nospace().noquote() << "dlgTriggerEditor::slot_pasteXml() WARNING - switch(EditorViewType) number 3 not expected to be called for \"EditorViewType::cmUnknownView!\"";
        break;
    }

    if (mpUndoStack) {
        mpUndoStack->endMacro();
    }
}

void dlgTriggerEditor::slot_import()
{
    switch (mCurrentView) {
    case EditorViewType::cmTriggerView:
        saveTrigger();
        break;
    case EditorViewType::cmTimerView:
        saveTimer();
        break;
    case EditorViewType::cmAliasView:
        saveAlias();
        break;
    case EditorViewType::cmScriptView:
        saveScript();
        break;
    case EditorViewType::cmActionView:
        saveAction();
        break;
    case EditorViewType::cmKeysView:
        saveKey();
        break;
    case EditorViewType::cmVarsView:
        qWarning().nospace().noquote() << "dlgTriggerEditor::slot_import() WARNING - switch(EditorViewType) not expected to be called for \"EditorViewType::cmVarsView!\"";
        break;
    case EditorViewType::cmUnknownView:
        qWarning().nospace().noquote() << "dlgTriggerEditor::slot_import() WARNING - switch(EditorViewType) not expected to be called for \"EditorViewType::cmUnknownView!\"";
    }

    QSettings& settings = *mudlet::getQSettings();
    QString lastDir = settings.value(qsl("lastFileDialogLocation"), QDir::homePath()).toString();
    //: Trigger editor - import packages from file dialog (multi-select enabled)
    //: Trigger editor - file filter for supported package types (mpackage, zip, xml)
    const QStringList fileNames = QFileDialog::getOpenFileNames(this, tr("Import Mudlet Package"), lastDir, tr("Mudlet Packages (*.mpackage *.zip *.xml)"));
    if (fileNames.isEmpty()) {
        return;
    }
    lastDir = QFileInfo(fileNames.first()).absolutePath();
    settings.setValue(qsl("lastFileDialogLocation"), lastDir);

    QStringList failedPackages;

    for (const QString& fileName : fileNames) {
        auto [success, errorMsg] = mpHost->installPackage(fileName, enums::PackageModuleType::Package);
        if (success) {
            mpHost->waitForProfileSave();
        } else {
            const QString baseName = QFileInfo(fileName).fileName();
            failedPackages << baseName;
            qWarning() << "dlgTriggerEditor::slot_import() ERROR - failed to import" << baseName << ":" << errorMsg;
        }
    }

    treeWidget_triggers->clear();
    treeWidget_aliases->clear();
    treeWidget_actions->clear();
    treeWidget_timers->clear();
    treeWidget_keys->clear();
    treeWidget_scripts->clear();

    // Nullify current item pointers before saving to prevent use-after-free
    mpCurrentTriggerItem = nullptr;
    mpCurrentTimerItem = nullptr;
    mpCurrentAliasItem = nullptr;
    mpCurrentScriptItem = nullptr;
    mpCurrentActionItem = nullptr;
    mpCurrentKeyItem = nullptr;

    slot_profileSaveAction();

    fillout_form();

    slot_showTriggers();

    if (!failedPackages.isEmpty()) {
        //: Trigger editor - status message shown when some packages failed to import. %1 is a comma-separated list of package names
        statusBar()->showMessage(tr("Failed to import: %1").arg(failedPackages.join(qsl(", "))), std::chrono::milliseconds(4s).count());
    }
}

void dlgTriggerEditor::doCleanReset()
{
    if (mCleanResetQueued) {
        return;
    }

    mCleanResetQueued = true;

    QTimer::singleShot(0ms, this, [=, this]() {
        mCleanResetQueued = false;

        runScheduledCleanReset();
    });
}

void dlgTriggerEditor::runScheduledCleanReset()
{
    if (!mpHost) {
        // The profile went away between doCleanReset() scheduling this and the timer firing,
        // which is the order a teardown destroys them in. There is nothing left to repopulate
        // from, and clearing the tree widgets below would re-enter the editor through
        // selectionChanged to read the theme and font off the Host that has just gone.
        return;
    }

    // Clear all current item pointers BEFORE attempting to save or clear tree widgets
    // to prevent heap-use-after-free when the tree widgets are cleared
    mpCurrentTriggerItem = nullptr;
    mpCurrentTimerItem = nullptr;
    mpCurrentAliasItem = nullptr;
    mpCurrentScriptItem = nullptr;
    mpCurrentActionItem = nullptr;
    mpCurrentKeyItem = nullptr;

    switch (mCurrentView) {
    case EditorViewType::cmTriggerView:
        saveTrigger();
        break;
    case EditorViewType::cmTimerView:
        saveTimer();
        break;
    case EditorViewType::cmAliasView:
        saveAlias();
        break;
    case EditorViewType::cmScriptView:
        saveScript();
        break;
    case EditorViewType::cmActionView:
        saveAction();
        break;
    case EditorViewType::cmKeysView:
        saveKey();
        break;
    case EditorViewType::cmVarsView:
        // FIXME: The switch in here need to handle (or at least treat correctly) the
        // EditorViewType:cmVarsView case but how is not clear:
        qWarning().nospace().noquote() << "dlgTriggerEditor::runScheduledCleanReset() WARNING - switch(EditorViewType) not expected to be called for \"EditorViewType::cmVarsView!\"";
        break;
    case EditorViewType::cmUnknownView:
        qWarning().nospace().noquote() << "dlgTriggerEditor::runScheduledCleanReset() WARNING - switch(EditorViewType) not expected to be called for \"EditorViewType::cmUnknownView!\"";
    }

    treeWidget_triggers->clear();
    treeWidget_aliases->clear();
    treeWidget_actions->clear();
    treeWidget_timers->clear();
    treeWidget_keys->clear();
    treeWidget_scripts->clear();
    fillout_form();
    slot_showTriggers();
}

void dlgTriggerEditor::slot_profileSaveAction()
{
    // saveProfile() calls qApp->processEvents() while a save is in progress,
    // which can re-enter here via a pending doCleanReset timer. The ongoing
    // save already covers current state, so skip the redundant call.
    if (mpHost->currentlySavingProfile()) {
        return;
    }

    slot_saveEdits();

    auto [ok, filename, error] = mpHost->saveProfile(QString(), QString(), true);

    if (!ok && !error.isEmpty()) {
        QMessageBox::critical(this, tr("Couldn't save profile"), tr("Sorry, couldn't save your profile - got the following error: %1").arg(error));
    }
}

void dlgTriggerEditor::slot_profileSaveAsAction()
{
    mSavingAs = true;

    QSettings& settings = *mudlet::getQSettings();
    QString lastDir = settings.value("lastFileDialogLocation", QDir::homePath()).toString();
    QString fileName = QFileDialog::getSaveFileName(this, tr("Backup Profile"), lastDir, tr("trigger files (*.trigger *.xml)"));

    if (fileName.isEmpty()) {
        return;
    }
    lastDir = QFileInfo(fileName).absolutePath();
    settings.setValue("lastFileDialogLocation", lastDir);

    // Must be case insensitive to work on MacOS platforms, possibly a cause of
    // https://bugs.launchpad.net/mudlet/+bug/1417234
    if (!fileName.endsWith(qsl(".xml"), Qt::CaseInsensitive) && !fileName.endsWith(qsl(".trigger"), Qt::CaseInsensitive)) {
        fileName.append(qsl(".xml"));
    }
    slot_saveEdits();

    mpHost->saveProfileAs(fileName);
    mSavingAs = false;
}

bool dlgTriggerEditor::eventFilter(QObject* watched, QEvent* event)
{
    if (mIsGrabKey) {
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            switch (keyEvent->key()) {
            case Qt::Key_Up:
            case Qt::Key_Down:
            case Qt::Key_Left:
            case Qt::Key_Right:
            case Qt::Key_Escape:
                this->event(event);
                return true;
            default:
                return false;
            }
        }
        return false;
    }

    if (handlePatternHandleEvent(watched, event)) {
        return true;
    }

    // Styling the sidebar's rows takes its native focus rectangle away with
    // them; a property puts it back, since a QSS rule cannot ask whether the
    // widget a subcontrol belongs to has the focus
    if (watched == mpListWidget_editorSidebar && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)) {
        mpListWidget_editorSidebar->setProperty(uiDesign::scmProp_focused, event->type() == QEvent::FocusIn);
        mpListWidget_editorSidebar->style()->polish(mpListWidget_editorSidebar);
    }

    // Escape in the search field gives the panel back to the trees. While the
    // field's history is open the key belongs to the popup, which takes it
    // before this is reached - but a style that lets it through anyway would
    // otherwise close the list and clear the search in one keystroke.
    if (event->type() == QEvent::KeyPress && comboBox_searchTerms && watched == comboBox_searchTerms->lineEdit() && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        if (comboBox_searchTerms->view() && comboBox_searchTerms->view()->isVisible()) {
            return false;
        }
        // Emptying the field is the whole of it: the textChanged() handler put
        // on the field is what takes the results down, however they are emptied
        comboBox_searchTerms->lineEdit()->clear();
        return true;
    }

    // Return in the last pattern gives the trigger one more and puts the cursor
    // in it: the keyboard's way to what the Add pattern button does. A pattern
    // field takes no line break of its own, so the key is free for this.
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        const bool plainReturn =
                (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) && !(keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier));
        if (plainReturn) {
            if (auto* edit = qobject_cast<SingleLineTextEdit*>(watched)) {
                auto* patternItem = qobject_cast<dlgTriggerPatternEdit*>(edit->parentWidget());
                if (patternItem && patternItem->mRow == mVisiblePatternCount - 1 && mVisiblePatternCount < scmEditorPatternRowLimit) {
                    slot_addPattern();
                    return true;
                }
            }
        }
    }

    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        const Qt::KeyboardModifiers modifiers = keyEvent->modifiers();
        const Qt::KeyboardModifiers additionalModifiers = modifiers & (Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier | Qt::GroupSwitchModifier | Qt::KeypadModifier);
        if (modifiers.testFlag(Qt::ControlModifier) && additionalModifiers == Qt::NoModifier) {
            if (auto* edit = qobject_cast<SingleLineTextEdit*>(watched)) {
                auto* patternItem = qobject_cast<dlgTriggerPatternEdit*>(edit->parentWidget());
                if (keyEvent->key() == Qt::Key_Down) {
                    if (focusNextPatternItem(patternItem)) {
                        return true;
                    }
                } else if (keyEvent->key() == Qt::Key_Up) {
                    if (focusPreviousPatternItem(patternItem)) {
                        return true;
                    }
                }
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

bool dlgTriggerEditor::event(QEvent* event)
{
    if (mIsGrabKey) {
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            switch (ke->key()) {
            case Qt::Key_Escape:
                mIsGrabKey = false;
                setShortcuts();
                QCoreApplication::instance()->removeEventFilter(this);
                ke->accept();
                return true;

            case Qt::Key_Shift:
                [[fallthrough]];
            case Qt::Key_Control:
                [[fallthrough]];
            case Qt::Key_Meta:
                [[fallthrough]];
            case Qt::Key_Alt:
                [[fallthrough]];
            case Qt::Key_AltGr:
                break;

            default:
                keyGrabCallback(static_cast<Qt::Key>(ke->key()), static_cast<Qt::KeyboardModifiers>(ke->modifiers()));
                mIsGrabKey = false;
                setShortcuts();
                QCoreApplication::instance()->removeEventFilter(this);
                ke->accept();
                return true;
            }
        }
    }

    return QMainWindow::event(event);
}

void dlgTriggerEditor::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event)
    if (mpSourceEditorArea->isVisible()) {
        slot_sourceFindMove();
    }
    updateEditorSidebarMode();
}

void dlgTriggerEditor::slot_keyGrab()
{
    mIsGrabKey = true;
    setShortcuts(false);
    QCoreApplication::instance()->installEventFilter(this);
}

// Activate shortcuts for editor menu items like Ctrl+S for "Save Item" etc.
// Deactivate instead with optional "false" - to allow these for keybindings
void dlgTriggerEditor::setShortcuts(const bool active)
{
    setShortcuts(toolBar->actions(), active);
    // The view switchers are no longer a toolbar's actions, but their shortcuts
    // are still the ones a keybinding has to be able to take away
    setShortcuts(mEditorViewActions, active);
}

void dlgTriggerEditor::setShortcuts(QList<QAction*> actionList, const bool active)
{
    QString buttonLabel;
    for (auto& action : actionList) {
        if (!active) {
            action->setShortcut(QString());
            continue;
        }
        buttonLabel = action->text();
        if (auto it = mButtonShortcuts.find(buttonLabel); it != mButtonShortcuts.end()) {
            action->setShortcut(it->second);
        }
    }
}

void dlgTriggerEditor::keyGrabCallback(const Qt::Key key, const Qt::KeyboardModifiers modifier)
{
    KeyUnit* pKeyUnit = mpHost->getKeyUnit();
    if (!pKeyUnit) {
        return;
    }
    const QString keyName = pKeyUnit->getKeyName(key, modifier);
    mpKeysMainArea->lineEdit_key_binding->setText(keyName);
    QTreeWidgetItem* pItem = treeWidget_keys->currentItem();
    if (pItem) {
        const int keyID = pItem->data(0, Qt::UserRole).toInt();
        TKey* pT = mpHost->getKeyUnit()->getKey(keyID);
        if (pT) {
            if (pT->getKeyCode() == key && pT->getKeyModifiers() == modifier) {
                return;
            }

            QString oldStateXML = exportKeyToXML(pT);
            pT->setKeyCode(key);
            pT->setKeyModifiers(modifier);
            QString newStateXML = exportKeyToXML(pT);

            pushKeyPropertyCommand(mpUndoStack, mpHost, keyID, pT->getName(), qsl("keyBinding"), oldStateXML, newStateXML);
        }
    }
}

void dlgTriggerEditor::slot_toggleIsPushDownButton(const int state)
{
    if (state == Qt::Checked) {
        mpActionsMainArea->lineEdit_action_button_command_up->show();
        mpActionsMainArea->label_action_button_command_up->show();
        mpActionsMainArea->label_action_button_command_down->setText(tr("Command (down):"));
    } else {
        mpActionsMainArea->lineEdit_action_button_command_up->hide();
        mpActionsMainArea->label_action_button_command_up->hide();
        mpActionsMainArea->label_action_button_command_down->setText(tr("Command:"));
    }
}

// Set the foreground color that will be applied to text that matches the trigger pattern(s)
void dlgTriggerEditor::slot_colorizeTriggerSetFgColor()
{
    QTreeWidgetItem* pItem = mpCurrentTriggerItem;
    if (!pItem) {
        return;
    }
    if (!pItem->parent()) {
        return;
    }

    const QColor initialColor(mpTriggersMainArea->pushButtonFgColor->property(cButtonBaseColor).toString());

    QColorDialog dialog(initialColor, this);
    dialog.setWindowTitle(tr("Select foreground color to apply to matches"));
    dialog.setOption(QColorDialog::DontUseNativeDialog);

    bool keepColorClicked = false;
    auto* buttonBox = dialog.findChild<QDialogButtonBox*>();
    if (buttonBox) {
        //: Button in the color picker that preserves the existing text color on trigger matches
        auto* keepButton = buttonBox->addButton(tr("Keep color"), QDialogButtonBox::ActionRole);
        connect(keepButton, &QPushButton::clicked, &dialog, [&keepColorClicked, &dialog]() {
            keepColorClicked = true;
            dialog.accept();
        });
    }

    dialog.exec();

    if (keepColorClicked) {
        mpTriggersMainArea->pushButtonFgColor->setStyleSheet(generateButtonStyleSheet(QColorConstants::Transparent));
        //: Keep the existing colour on matches to highlight. Use shortest word possible so it fits on the button
        mpTriggersMainArea->pushButtonFgColor->setText(tr("keep"));
        mpTriggersMainArea->pushButtonFgColor->setProperty(cButtonBaseColor, qsl("transparent"));
    } else if (dialog.selectedColor().isValid()) {
        const auto color = dialog.selectedColor();
        mpTriggersMainArea->pushButtonFgColor->setStyleSheet(generateButtonStyleSheet(color));
        mpTriggersMainArea->pushButtonFgColor->setText(QString());
        mpTriggersMainArea->pushButtonFgColor->setProperty(cButtonBaseColor, color.name());
    }
    // else: Cancel - do nothing
}

// Set the background color that will be applied to text that matches the trigger pattern(s)
void dlgTriggerEditor::slot_colorizeTriggerSetBgColor()
{
    QTreeWidgetItem* pItem = mpCurrentTriggerItem;
    if (!pItem) {
        return;
    }
    if (!pItem->parent()) {
        return;
    }

    const QColor initialColor(mpTriggersMainArea->pushButtonBgColor->property(cButtonBaseColor).toString());

    QColorDialog dialog(initialColor, this);
    dialog.setWindowTitle(tr("Select background color to apply to matches"));
    dialog.setOption(QColorDialog::DontUseNativeDialog);

    bool keepColorClicked = false;
    auto* buttonBox = dialog.findChild<QDialogButtonBox*>();
    if (buttonBox) {
        //: Button in the color picker that preserves the existing text color on trigger matches
        auto* keepButton = buttonBox->addButton(tr("Keep color"), QDialogButtonBox::ActionRole);
        connect(keepButton, &QPushButton::clicked, &dialog, [&keepColorClicked, &dialog]() {
            keepColorClicked = true;
            dialog.accept();
        });
    }

    dialog.exec();

    if (keepColorClicked) {
        mpTriggersMainArea->pushButtonBgColor->setStyleSheet(generateButtonStyleSheet(QColorConstants::Transparent));
        //: Keep the existing colour on matches to highlight. Use shortest word possible so it fits on the button
        mpTriggersMainArea->pushButtonBgColor->setText(tr("keep"));
        mpTriggersMainArea->pushButtonBgColor->setProperty(cButtonBaseColor, qsl("transparent"));
    } else if (dialog.selectedColor().isValid()) {
        const auto color = dialog.selectedColor();
        mpTriggersMainArea->pushButtonBgColor->setStyleSheet(generateButtonStyleSheet(color));
        mpTriggersMainArea->pushButtonBgColor->setText(QString());
        mpTriggersMainArea->pushButtonBgColor->setProperty(cButtonBaseColor, color.name());
    }
    // else: Cancel - do nothing
}

void dlgTriggerEditor::slot_soundTrigger()
{
    // Use the existing path/filename if it is not empty, otherwise start in last global user dir
    QSettings& settings = *mudlet::getQSettings();
    QString lastDir = settings.value("lastFileDialogLocation", QDir::homePath()).toString();

    const QString fileName = QFileDialog::getOpenFileName(
            this,
            tr("Choose sound file"),
            mpTriggersMainArea->lineEdit_soundFile->text().isEmpty() ? lastDir : mpTriggersMainArea->lineEdit_soundFile->text(),
            //: This the list of file extensions that are considered for sounds from triggers, the terms inside of the '('...')' and the ";;" are used programmatically and should not be changed.
            tr("Audio files(*.aac *.mp3 *.mp4a *.oga *.ogg *.pcm *.wav *.wma);;"
               "Advanced Audio Coding-stream(*.aac);;"
               "MPEG-2 Audio Layer 3(*.mp3);;"
               "MPEG-4 Audio(*.mp4a);;"
               "Ogg Vorbis(*.oga *.ogg);;"
               "PCM Audio(*.pcm);;"
               "Wave(*.wav);;"
               "Windows Media Audio(*.wma);;"
               "All files(*.*)"));
    if (!fileName.isEmpty()) {
        // This will only be executed if the user did not press cancel
        mpTriggersMainArea->lineEdit_soundFile->setToolTip(fileName);
        mpTriggersMainArea->lineEdit_soundFile->setText(fileName);
        mpTriggersMainArea->lineEdit_soundFile->setCursorPosition(mpTriggersMainArea->lineEdit_soundFile->text().length());
        mpTriggersMainArea->toolButton_clearSoundFile->setEnabled(!mpTriggersMainArea->lineEdit_soundFile->text().isEmpty());
        lastDir = QFileInfo(fileName).absolutePath();
        settings.setValue("lastFileDialogLocation", lastDir);
    }
}

// Get the color from the user to use as that to look for as the foreground in
// a color trigger:
void dlgTriggerEditor::slot_colorTriggerFg()
{
    QTreeWidgetItem* pItem = mpCurrentTriggerItem;
    if (!pItem) {
        return;
    }
    const int triggerID = pItem->data(0, Qt::UserRole).toInt();
    TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(triggerID);
    if (!pT) {
        return;
    }

    auto* pB = qobject_cast<QPushButton*>(sender());
    if (!pB) {
        return;
    }

    dlgTriggerPatternEdit* pPatternItem = qobject_cast<dlgTriggerPatternEdit*>(pB->parent());
    if (!pPatternItem) {
        return;
    }

    // This method parses the pattern text and extracts the ansi color values
    // from it - including the special values of DEFAULT (-2) and IGNORE (-1)
    // and assigns the values to the other arguments:
    TTrigger::decodeColorPatternText(pPatternItem->singleLineTextEdit_pattern->toPlainText(), pT->mColorTriggerFgAnsi, pT->mColorTriggerBgAnsi);

    // The following method wants to know BOTH existing fore and backgrounds
    // it will select the appropriate as a result of the third argument and it
    // uses both to determine whether the result to return is valid considering
    // the other, non used (background in this method) part:
    auto pD = new dlgColorTrigger(this, pT, false, tr("Select foreground trigger color for item %1").arg(QString::number(pPatternItem->mRow + 1)));
    pD->setModal(true);
    // This sounds a bit iffy - prevent access to other application windows
    // while we get a colour setting:
    pD->setWindowModality(Qt::ApplicationModal);
    pD->exec();
    delete pD;

    const QColor color = pT->mColorTriggerFgColor;
    // The above will be an invalid colour if the colour has been reset/ignored
    // The dialogue should have changed pT->mColorTriggerFgAnsi
    QString styleSheet;
    if (color.isValid()) {
        styleSheet = generateButtonStyleSheet(color);
    }
    pB->setStyleSheet(styleSheet);

    pPatternItem->singleLineTextEdit_pattern->setPlainText(TTrigger::createColorPatternText(pT->mColorTriggerFgAnsi, pT->mColorTriggerBgAnsi));

    if (pT->mColorTriggerFgAnsi == TTrigger::scmIgnored) {
        //: Color trigger ignored foreground color button, ensure all three instances have the same text
        pB->setText(tr("Foreground color ignored"));
    } else if (pT->mColorTriggerFgAnsi == TTrigger::scmDefault) {
        //: Color trigger default foreground color button, ensure all three instances have the same text
        pB->setText(tr("Default foreground color"));
    } else {
        //: Color trigger ANSI foreground color button, ensure all three instances have the same text
        pB->setText(tr("Foreground color [ANSI %1]").arg(QString::number(pT->mColorTriggerFgAnsi)));
    }
}

// Get the color from the user to use as that to look for as the background in
// a color trigger:
void dlgTriggerEditor::slot_colorTriggerBg()
{
    QTreeWidgetItem* pItem = mpCurrentTriggerItem;
    if (!pItem) {
        return;
    }
    const int triggerID = pItem->data(0, Qt::UserRole).toInt();
    TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(triggerID);
    if (!pT) {
        return;
    }

    auto* pB = qobject_cast<QPushButton*>(sender());
    if (!pB) {
        return;
    }

    dlgTriggerPatternEdit* pPatternItem = qobject_cast<dlgTriggerPatternEdit*>(pB->parent());
    if (!pPatternItem) {
        return;
    }

    // This method parses the pattern text and extracts the ansi color values
    // from it - including the special values of DEFAULT (-2) and IGNORE (-1)
    // and assigns the values to the other arguments:
    TTrigger::decodeColorPatternText(pPatternItem->singleLineTextEdit_pattern->toPlainText(), pT->mColorTriggerFgAnsi, pT->mColorTriggerBgAnsi);

    // The following method wants to know BOTH existing fore and backgrounds
    // it will select the appropriate as a result of the third argument and it
    // uses both to determine whether the result to return is valid considering
    // the other, non used (background in this method) part:
    auto pD = new dlgColorTrigger(this, pT, true, tr("Select background trigger color for item %1").arg(QString::number(pPatternItem->mRow + 1)));
    pD->setModal(true);
    // This sounds a bit iffy - prevent access to other application windows
    // while we get a colour setting:
    pD->setWindowModality(Qt::ApplicationModal);
    pD->exec();
    delete pD;

    const QColor color = pT->mColorTriggerBgColor;
    // The above will be an invalid colour if the colour has been reset/ignored
    QString styleSheet;
    if (color.isValid()) {
        styleSheet = generateButtonStyleSheet(color);
    }
    pB->setStyleSheet(styleSheet);

    pPatternItem->singleLineTextEdit_pattern->setPlainText(TTrigger::createColorPatternText(pT->mColorTriggerFgAnsi, pT->mColorTriggerBgAnsi));

    if (pT->mColorTriggerBgAnsi == TTrigger::scmIgnored) {
        //: Color trigger ignored background color button, ensure all three instances have the same text
        pB->setText(tr("Background color ignored"));
    } else if (pT->mColorTriggerBgAnsi == TTrigger::scmDefault) {
        //: Color trigger default background color button, ensure all three instances have the same text
        pB->setText(tr("Default background color"));
    } else {
        //: Color trigger ANSI background color button, ensure all three instances have the same text
        pB->setText(tr("Background color [ANSI %1]").arg(QString::number(pT->mColorTriggerBgAnsi)));
    }
}

void dlgTriggerEditor::slot_updateStatusBar(const QString& statusText)
{
    // edbee adds the scope and last command which is rather technical debugging information,
    // so strip it away by removing the first pipe and everything after it
    const QRegularExpressionMatch match = csmSimplifyStatusBarRegex.match(statusText, 0, QRegularExpression::PartialPreferFirstMatch);
    QString stripped;
    if (match.hasPartialMatch() || match.hasMatch()) {
        stripped = match.captured(1);
    } else {
        stripped = statusText;
    }

    QMainWindow::statusBar()->showMessage(stripped);
}

void dlgTriggerEditor::slot_profileSaveStarted()
{
    mProfileSaveAction->setDisabled(true);
    mProfileSaveAsAction->setDisabled(true);
    mProfileSaveAction->setText(tr("Saving…"));
}

void dlgTriggerEditor::slot_profileSaveFinished()
{
    mProfileSaveAction->setEnabled(true);
    mProfileSaveAsAction->setEnabled(true);
    mProfileSaveAction->setText(tr("Save Profile"));
}

void dlgTriggerEditor::slot_changeEditorTextOptions(QTextOption::Flags state)
{
    edbee::TextEditorConfig* config = mpSourceEditorEdbee->config();

    config->beginChanges();
    config->setShowWhitespaceMode((state & QTextOption::ShowTabsAndSpaces) ? edbee::TextEditorConfig::ShowWhitespaces : edbee::TextEditorConfig::HideWhitespaces);
    config->setUseLineSeparator(state & QTextOption::ShowLineAndParagraphSeparators);
    config->endChanges();
}

// clearDocument( edbee::TextEditorWidget* pEditorWidget)
//
// A temporary measure for dealing with the undo spanning over multiple documents bug,
// in place until we create a proper multi-document solution. This gets called whenever
// the editor needs to be "cleared", usually when a different alias/trigger/etc is
// made or selected.
void dlgTriggerEditor::clearDocument(edbee::TextEditorWidget* pEditorWidget, const QString& initialText)
{
    // Every item switch comes through here, and the chip over the code pane
    // speaks for the last save of whatever the pane is holding - so a failure
    // reported against the item leaving it does not follow the one arriving
    clearCompileState();

    mpSourceEditorFindArea->hide();
    mpSourceEditorEdbeeDocument = new edbee::CharTextDocument();
    connect(mpSourceEditorEdbeeDocument, &edbee::TextDocument::textChanged, this, &dlgTriggerEditor::slot_itemEdited);
    // Buck.lua is a fake filename for edbee to figure out its lexer type with. Referencing the
    // lexer directly by name previously gave problems.
    // Don't apply Lua syntax highlighting for the Variables view since it displays plain data values, not code
    if (mCurrentView != EditorViewType::cmVarsView) {
        mpSourceEditorEdbeeDocument->setLanguageGrammar(edbee::Edbee::instance()->grammarManager()->detectGrammarWithFilename(QLatin1String("Buck.lua")));
    }
    pEditorWidget->controller()->giveTextDocument(mpSourceEditorEdbeeDocument);

    // Update the text undo stack pointer since we have a new document
    // Disconnect from old undo stack if it exists
    if (mpTextUndoStack) {
        disconnect(mpTextUndoStack, nullptr, this, nullptr);
    }
    // Connect to the new document's undo stack
    mpTextUndoStack = mpSourceEditorEdbeeDocument->textUndoStack();
    connect(mpTextUndoStack, &edbee::TextUndoStack::undoExecuted, this, &dlgTriggerEditor::slot_updateUndoRedoButtonStates);
    connect(mpTextUndoStack, &edbee::TextUndoStack::redoExecuted, this, &dlgTriggerEditor::slot_updateUndoRedoButtonStates);
    connect(mpTextUndoStack, &edbee::TextUndoStack::changeAdded, this, &dlgTriggerEditor::slot_updateUndoRedoButtonStates);

    auto config = mpSourceEditorEdbee->config();
    config->beginChanges();
    config->setThemeName(mpHost->getEditorTheme());
    config->setFont(mpHost->getDisplayFont());
    config->setShowWhitespaceMode((mudlet::self()->mEditorTextOptions & QTextOption::ShowTabsAndSpaces) ? edbee::TextEditorConfig::ShowWhitespaces : edbee::TextEditorConfig::HideWhitespaces);
    config->setUseLineSeparator(mudlet::self()->mEditorTextOptions & QTextOption::ShowLineAndParagraphSeparators);
    config->setSmartTab(true);
    config->setUseTabChar(false); // when you press Enter for a newline, pad with spaces and not tabs
    config->setCaretBlinkRate(200);
    config->setIndentSize(2);
    config->setCaretWidth(1);
    config->setAutocompleteAutoShow(mpHost->mEditorAutoComplete);
    config->setRenderBidiContolCharacters(mpHost->getEditorShowBidi());
    config->setAutocompleteMinimalCharacters(3);
    config->endChanges();

    // If undo is not disabled when setting the initial text, the
    // setting of the text will be undoable.
    mpSourceEditorEdbeeDocument->setUndoCollectionEnabled(false);
    disconnect(mpSourceEditorEdbeeDocument, &edbee::TextDocument::textChanged, this, &dlgTriggerEditor::slot_itemEdited);
    mpSourceEditorEdbeeDocument->setText(initialText);
    connect(mpSourceEditorEdbeeDocument, &edbee::TextDocument::textChanged, this, &dlgTriggerEditor::slot_itemEdited);
    mpSourceEditorEdbeeDocument->setUndoCollectionEnabled(true);
}

void dlgTriggerEditor::saveEditorState(EditorViewType viewType, int itemId)
{
    if (!mpSourceEditorEdbee || !mpSourceEditorEdbeeDocument) {
        return;
    }

    EditorState state;

    if (auto* controller = mpSourceEditorEdbee->controller(); controller && controller->textSelection()) {
        const int caretOffset = controller->textSelection()->range(0).anchor();
        state.caretLine = mpSourceEditorEdbeeDocument->lineFromOffset(caretOffset);
        const int lineStart = mpSourceEditorEdbeeDocument->offsetFromLine(state.caretLine);
        state.caretColumn = caretOffset - lineStart;
    }

    if (auto* scrollBar = mpSourceEditorEdbee->verticalScrollBar()) {
        state.verticalScrollPos = scrollBar->value();
    }
    if (auto* scrollBar = mpSourceEditorEdbee->horizontalScrollBar()) {
        state.horizontalScrollPos = scrollBar->value();
    }

    mEditorStates[viewType][itemId] = state;
}

// Defers restoration using QTimer::singleShot to ensure the document and scroll bars
// are fully initialized after clearDocument(). Also re-enables widget updates that were
// disabled before document loading to prevent visual flicker.
void dlgTriggerEditor::restoreEditorState(EditorViewType viewType, int itemId)
{
    if (!mpSourceEditorEdbee || !mpSourceEditorEdbeeDocument) {
        if (mpSourceEditorEdbee) {
            mpSourceEditorEdbee->setUpdatesEnabled(true);
        }
        return;
    }

    const bool hasState = mEditorStates.contains(viewType) && mEditorStates[viewType].contains(itemId);
    const EditorState state = hasState ? mEditorStates[viewType][itemId] : EditorState{};

    QTimer::singleShot(0ms, this, [this, state, hasState]() {
        if (!mpSourceEditorEdbee) {
            return;
        }

        if (!mpSourceEditorEdbeeDocument) {
            mpSourceEditorEdbee->setUpdatesEnabled(true);
            return;
        }

        if (hasState) {
            auto* vScrollBar = mpSourceEditorEdbee->verticalScrollBar();
            auto* hScrollBar = mpSourceEditorEdbee->horizontalScrollBar();

            if (auto* controller = mpSourceEditorEdbee->controller()) {
                const auto lineCount = mpSourceEditorEdbeeDocument->lineCount();
                const auto maxLine = lineCount > 0 ? lineCount - 1 : 0;
                const auto line = std::min(static_cast<decltype(maxLine)>(state.caretLine), maxLine);
                const auto lineLength = mpSourceEditorEdbeeDocument->lineLength(line);
                const auto column = std::min(static_cast<decltype(lineLength)>(state.caretColumn), lineLength);

                controller->moveCaretTo(line, column, false);
            }

            if (vScrollBar) {
                vScrollBar->setValue(state.verticalScrollPos);
            }
            if (hScrollBar) {
                hScrollBar->setValue(state.horizontalScrollPos);
            }
        }

        // Re-enable updates now that scroll position is restored (or immediately if no state)
        mpSourceEditorEdbee->setUpdatesEnabled(true);
    });
}

void dlgTriggerEditor::clearEditorState(EditorViewType viewType, int itemId)
{
    if (mEditorStates.contains(viewType)) {
        mEditorStates[viewType].remove(itemId);
    }
}

void dlgTriggerEditor::setThemeAndOtherSettings(const QString& theme)
{
    auto localConfig = mpSourceEditorEdbee->config();
    localConfig->beginChanges();
    localConfig->setThemeName(theme);
    mpHost->editorThemeChanged();
    localConfig->setFont(mpHost->getDisplayFont());
    localConfig->setShowWhitespaceMode((mudlet::self()->mEditorTextOptions & QTextOption::ShowTabsAndSpaces) ? edbee::TextEditorConfig::ShowWhitespaces : edbee::TextEditorConfig::HideWhitespaces);
    localConfig->setUseLineSeparator(mudlet::self()->mEditorTextOptions & QTextOption::ShowLineAndParagraphSeparators);
    localConfig->setAutocompleteAutoShow(mpHost->mEditorAutoComplete);
    localConfig->setRenderBidiContolCharacters(mpHost->getEditorShowBidi());
    localConfig->setAutocompleteMinimalCharacters(3);
    localConfig->endChanges();
}

// The glyph at the leading edge of the search field, which is also the button
// the options menu drops from. One magnifier in two colours rather than the nine
// bitmaps a combination of options used to be spelled out in: those said which
// options were on by the colour of three letters, which is a reading no theme
// could be made to keep. The menu behind the glyph is where the options are
// read now - each is a checkable item - and the accent says that any of them is
// in force, so a search that is quietly narrower than it looks still shows it.
void dlgTriggerEditor::createSearchOptionIcon()
{
    // The shell is styled once before the search row is built, and a restyle is
    // what calls this outside of the options changing
    if (!mpAction_searchOptions) {
        return;
    }

    const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
    const QPixmap source(qsl(":/icons/settings-search.png"));
    const QIcon newIcon(uiDesign::tintedGlyph(source, (mSearchOptions == SearchOptionNone) ? tokens.mutedText : tokens.accentText));

    // Store the current setting icon - may need to copy it into the grandparent QComboBox items
    mIcon_searchOptions = newIcon;
    // Applied it to the QLineEdit for display purposes
    mpAction_searchOptions->setIcon(newIcon);

    QStringList activeOptions;
    if (mSearchOptions & SearchOptionCaseSensitive) {
        activeOptions << mpAction_searchCaseSensitive->text();
    }
    if (mSearchOptions & SearchOptionIncludeVariables) {
        activeOptions << mpAction_searchIncludeVariables->text();
    }
    if (mSearchOptions & SearchOptionWholeWord) {
        activeOptions << mpAction_searchWholeWord->text();
    }
    if (activeOptions.isEmpty()) {
        //: Tooltip on the magnifier at the left of the editor's search field while no search option is set
        mpAction_searchOptions->setToolTip(utils::richText(tr("Search options")));
    } else {
        //: Tooltip on that magnifier once options are set. %1 is the list of them, already in the reader's language.
        mpAction_searchOptions->setToolTip(utils::richText(tr("Search options: %1").arg(activeOptions.join(qsl(", ")))));
    }
}

// Quiet enough that an empty-looking field stays empty-looking, and gone
// altogether while there is nothing behind it
void dlgTriggerEditor::updateSearchHistoryAction()
{
    if (!mpAction_searchHistory) {
        return;
    }

    const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
    QIcon chevron(uiDesign::tintedGlyph(QPixmap(qsl(":/icons/arrow-down.png")), tokens.mutedText));
    chevron.addPixmap(uiDesign::tintedGlyph(QPixmap(qsl(":/icons/arrow-down.png")), tokens.text), QIcon::Active);
    mpAction_searchHistory->setIcon(chevron);
    mpAction_searchHistory->setVisible(comboBox_searchTerms->count() > 0);
}

int dlgTriggerEditor::findSearchMatch(const QString& haystack, const QString& needle, int from) const
{
    if (needle.isEmpty()) {
        return -1;
    }

    if (mSearchOptions & SearchOptionWholeWord) {
        QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
        if (!(mSearchOptions & SearchOptionCaseSensitive)) {
            options |= QRegularExpression::CaseInsensitiveOption;
        }
        QRegularExpression regex(qsl("\\b%1\\b").arg(QRegularExpression::escape(needle)), options);
        QRegularExpressionMatch match = regex.match(haystack, from);
        if (match.hasMatch()) {
            return match.capturedStart();
        }
        return -1;
    }

    return haystack.indexOf(needle, from, (mSearchOptions & SearchOptionCaseSensitive) ? Qt::CaseSensitive : Qt::CaseInsensitive);
}

bool dlgTriggerEditor::containsSearchMatch(const QString& haystack, const QString& needle) const
{
    return findSearchMatch(haystack, needle) != -1;
}

void dlgTriggerEditor::slot_toggleSearchCaseSensitivity(const bool state)
{
    if ((mSearchOptions & SearchOptionCaseSensitive) != state) {
        mSearchOptions = (mSearchOptions & ~(SearchOptionCaseSensitive)) | (state ? SearchOptionCaseSensitive : SearchOptionNone);
        createSearchOptionIcon();
        mpHost->mSearchOptions = mSearchOptions;
    }
}

void dlgTriggerEditor::slot_toggleSearchIncludeVariables(const bool state)
{
    if ((mSearchOptions & SearchOptionIncludeVariables) != state) {
        mSearchOptions = (mSearchOptions & ~(SearchOptionIncludeVariables)) | (state ? SearchOptionIncludeVariables : SearchOptionNone);
        createSearchOptionIcon();
        mpHost->mSearchOptions = mSearchOptions;
    }
}

void dlgTriggerEditor::slot_toggleSearchWholeWord(const bool state)
{
    if ((mSearchOptions & SearchOptionWholeWord) != state) {
        mSearchOptions = (mSearchOptions & ~(SearchOptionWholeWord)) | (state ? SearchOptionWholeWord : SearchOptionNone);
        createSearchOptionIcon();
        mpHost->mSearchOptions = mSearchOptions;
    }
}

void dlgTriggerEditor::slot_clearSearchResults()
{
    // Want the clearing of the search results to show:
    treeWidget_searchResults->clear();
    treeWidget_searchResults->update();
    mSearchTerm.clear();
    // ...and the panel goes back to the profile's own items
    setSearchResultsShown(false);

    // unhighlight all instances of the item that we've searched for.
    // edbee already remembers this from a setSearchTerm() call elsewhere
    auto controller = mpSourceEditorEdbee->controller();
    auto textRanges = controller->borderedTextRanges();
    textRanges->clear();
    controller->update();
}

// shows a custom right-click menu for the editor, including the indent action
void dlgTriggerEditor::slot_editorContextMenu()
{
    edbee::TextEditorWidget* editor = mpSourceEditorEdbee;
    if (!editor) {
        return;
    }

    edbee::TextEditorController* controller = mpSourceEditorEdbee->controller();

    auto menu = new QMenu();
    auto formatAction = new QAction(tr("Format All"), menu);
    // appropriate shortcuts are automatically supplied by edbee here
    if (qApp->testAttribute(Qt::AA_DontShowIconsInMenus)) {
        menu->addAction(controller->createAction("undo", tr("Undo"), QIcon(), menu));
        menu->addAction(controller->createAction("redo", tr("Redo"), QIcon(), menu));
        menu->addSeparator();
        menu->addAction(controller->createAction("cut", tr("Cut"), QIcon(), menu));
        menu->addAction(controller->createAction("copy", tr("Copy"), QIcon(), menu));
        menu->addAction(controller->createAction("paste", tr("Paste"), QIcon(), menu));
        menu->addSeparator();
        menu->addAction(controller->createAction("sel_all", tr("Select All"), QIcon(), menu));
    } else {
        menu->addAction(controller->createAction("undo", tr("Undo"), QIcon::fromTheme(qsl("edit-undo"), QIcon(qsl(":/icons/edit-undo.png"))), menu));
        menu->addAction(controller->createAction("redo", tr("Redo"), QIcon::fromTheme(qsl("edit-redo"), QIcon(qsl(":/icons/edit-redo.png"))), menu));
        menu->addSeparator();
        menu->addAction(controller->createAction("cut", tr("Cut"), QIcon::fromTheme(qsl("edit-cut"), QIcon(qsl(":/icons/edit-cut.png"))), menu));
        menu->addAction(controller->createAction("copy", tr("Copy"), QIcon::fromTheme(qsl("edit-copy"), QIcon(qsl(":/icons/edit-copy.png"))), menu));
        menu->addAction(controller->createAction("paste", tr("Paste"), QIcon::fromTheme(qsl("edit-paste"), QIcon(qsl(":/icons/edit-paste.png"))), menu));
        menu->addSeparator();
        menu->addAction(controller->createAction("sel_all", tr("Select All"), QIcon::fromTheme(qsl("edit-select-all"), QIcon(qsl(":/icons/edit-select-all.png"))), menu));
        formatAction->setIcon(QIcon::fromTheme(qsl("run-build-clean"), QIcon::fromTheme(qsl("run-build-clean"))));
    }

    connect(formatAction, &QAction::triggered, this, [=, this]() {
        auto formattedText = mpHost->mLuaInterpreter.formatLuaCode(mpSourceEditorEdbeeDocument->text());
        // workaround for crash if undo is used, see https://github.com/edbee/edbee-lib/issues/66
        controller->beginUndoGroup();
        disconnect(mpSourceEditorEdbeeDocument, &edbee::TextDocument::textChanged, this, &dlgTriggerEditor::slot_itemEdited);
        mpSourceEditorEdbeeDocument->setText(formattedText);
        connect(mpSourceEditorEdbeeDocument, &edbee::TextDocument::textChanged, this, &dlgTriggerEditor::slot_itemEdited);
        // don't coalesce the format text action - not that it matters for us since we we only change
        // the text once during the undo group
        controller->endUndoGroup(edbee::CoalesceId_None, false);
    });

    menu->addAction(formatAction);
    menu->exec(QCursor::pos());

    delete menu;
}

QString dlgTriggerEditor::generateButtonStyleSheet(const QColor& color, const bool isEnabled)
{
    if (color != QColorConstants::Transparent && color.isValid()) {
        if (isEnabled) {
            return mudlet::self()->mTEXT_ON_BG_STYLESHEET.arg(color.lightness() > 127 ? QLatin1String("black") : QLatin1String("white"), color.name());
        }

        const QColor disabledColor = QColor::fromHsl(color.hslHue(), color.hslSaturation() / 4, color.lightness());
        return mudlet::self()->mTEXT_ON_BG_STYLESHEET.arg(QLatin1String("darkGray"), disabledColor.name());
    }
    return QString();
}

// Retrieve the background-color or color setting from the previous method, the
// colors used can theoretically be:
// * any strings of those from http://www.w3.org/TR/SVG/types.html#ColorKeywords
// * #RGB (each of R, G, and B is a single hex digit) 3 Digits
// * #RRGGBB 6 Digits
// * #AARRGGBB (Since 5.2) 8 Digits
// * #RRRGGGBBB 9 Digits
// * #RRRRGGGGBBBB 12 Digits
// * "transparent"
QColor dlgTriggerEditor::parseButtonStyleSheetColors(const QString& styleSheetText, const bool isToGetForeground)
{
    if (styleSheetText.isEmpty()) {
        return QColor();
    }

    QRegularExpression hexColorRegex;
    QRegularExpression namedColorRegex;
    if (isToGetForeground) {
        hexColorRegex.setPattern(QLatin1String("(?:[{ ])color:\\s*(?:#)([[:xdigit:]]{3,12})\\s*;")); // Capture group 1 is a foreground color made of hex digits
        QRegularExpressionMatch match = hexColorRegex.match(styleSheetText);
        if (match.hasMatch()) {
            switch (match.capturedLength(1)) {
            case 3: // RGB
                [[fallthrough]];
            case 6: // RRGGBB
                [[fallthrough]];
            case 9: // RRRGGGBBB
                [[fallthrough]];
            case 12: // RRRRGGGGBBBB
                return QColor(match.captured(1).prepend(QLatin1Char('#')));

            default:
                // case 8: // AARRGGBB - Invalid here
                qDebug().noquote().nospace() << "dlgTriggerEditor::parseButtonStyleSheetColors(\"" << styleSheetText << "\", " << isToGetForeground
                                             << ") ERROR - Invalid hex string as foreground color!";
                return QColor();
            }
        } else {
            namedColorRegex.setPattern(QLatin1String("(?:[{ ])color:\\s*(\\w{3,})\\s*;")); // Capture group 1 is a word for a foreground color
            match = namedColorRegex.match(styleSheetText);
            if (match.hasMatch()) {
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
                if (QColor::isValidColor(match.captured(1))) {
#else
                if (QColor::isValidColorName(match.captured(1))) {
#endif
                    return QColor(match.captured(1));
                }
                qDebug().noquote().nospace() << "dlgTriggerEditor::parseButtonStyleSheetColors(\"" << styleSheetText << "\", " << isToGetForeground << ") ERROR - Invalid string \""
                                             << match.captured(1) << "\" found as name of foreground color!";
                return QColor();
            }
            qDebug().noquote().nospace() << "dlgTriggerEditor::parseButtonStyleSheetColors(\"" << styleSheetText << "\", " << isToGetForeground
                                         << ") ERROR - No string as name of foreground color found!";
            return QColor();
        }
    } else {
        hexColorRegex.setPattern(QLatin1String("(?:[{ ])background-color:\\s*(?:#)([[:xdigit:]]{3,12})\\s*;")); // Capture group 1 is a background color made of hex digits
        QRegularExpressionMatch match = hexColorRegex.match(styleSheetText);
        if (match.hasMatch()) {
            switch (match.capturedLength(1)) {
            case 3: // RGB
                [[fallthrough]];
            case 6: // RRGGBB
                [[fallthrough]];
            case 9: // RRRGGGBBB
                [[fallthrough]];
            case 12: // RRRRGGGGBBBB
                return QColor(match.captured(1).prepend(QLatin1Char('#')));

            default:
                // case 8: // AARRGGBB - Invalid here
                qDebug().noquote().nospace() << "dlgTriggerEditor::parseButtonStyleSheetColors(\"" << styleSheetText << "\", " << isToGetForeground
                                             << ") ERROR - Invalid hex string as background color!";
                return QColor();
            }
        } else {
            namedColorRegex.setPattern(QLatin1String("(?:[{ ])background-color:\\s*(\\w{3,})\\s*;")); // Capture group 1 is a word for a background color
            match = namedColorRegex.match(styleSheetText);
            if (match.hasMatch()) {
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
                if (QColor::isValidColor(match.captured(1))) {
#else
                if (QColor::isValidColorName(match.captured(1))) {
#endif
                    return QColor(match.captured(1));
                }
                qDebug().noquote().nospace() << "dlgTriggerEditor::parseButtonStyleSheetColors(\"" << styleSheetText << "\", " << isToGetForeground << ") ERROR - Invalid string \""
                                             << match.captured(1) << "\" found as name of background color!";
                return QColor();
            }
            qDebug().noquote().nospace() << "dlgTriggerEditor::parseButtonStyleSheetColors(\"" << styleSheetText << "\", " << isToGetForeground
                                         << ") ERROR - No string as name of background color found!";
            return QColor();
        }
    }
}

void dlgTriggerEditor::slot_toggleGroupBoxColorizeTrigger(const bool state)
{
    if (mpTriggersMainArea->groupBox_triggerColorizer->isChecked() != state) {
        mpTriggersMainArea->groupBox_triggerColorizer->setChecked(state);
    }

    if (state) {
        // Enabled so make buttons have full colour:
        const QString fgColor = mpTriggersMainArea->pushButtonFgColor->property(cButtonBaseColor).toString();
        const QString bgColor = mpTriggersMainArea->pushButtonBgColor->property(cButtonBaseColor).toString();
        mpTriggersMainArea->pushButtonFgColor->setStyleSheet(generateButtonStyleSheet(fgColor, true));
        mpTriggersMainArea->pushButtonBgColor->setStyleSheet(generateButtonStyleSheet(bgColor, true));
        //: Keep the existing colour on matches to highlight. Use shortest word possible so it fits on the button
        mpTriggersMainArea->pushButtonFgColor->setText(fgColor == QLatin1String("transparent") ? tr("keep") : QString());
        //: Keep the existing colour on matches to highlight. Use shortest word possible so it fits on the button
        mpTriggersMainArea->pushButtonBgColor->setText(bgColor == QLatin1String("transparent") ? tr("keep") : QString());
    } else {
        // Disabled so make buttons greyed out a bit:
        mpTriggersMainArea->pushButtonFgColor->setStyleSheet(generateButtonStyleSheet(mpTriggersMainArea->pushButtonFgColor->property(cButtonBaseColor).toString(), false));
        mpTriggersMainArea->pushButtonBgColor->setStyleSheet(generateButtonStyleSheet(mpTriggersMainArea->pushButtonBgColor->property(cButtonBaseColor).toString(), false));
        mpTriggersMainArea->pushButtonFgColor->setText(QString());
        mpTriggersMainArea->pushButtonBgColor->setText(QString());
    }
}

void dlgTriggerEditor::slot_clearSoundFile()
{
    mpTriggersMainArea->lineEdit_soundFile->clear();
    mpTriggersMainArea->toolButton_clearSoundFile->setEnabled(false);
    mpTriggersMainArea->lineEdit_soundFile->setToolTip(utils::richText(tr("Sound file to play when the trigger fires.")));
}

// The ID beside a trigger's name, drawn as a pill. The type is set on the two
// labels here rather than named in the form's stylesheet, because the radius
// that makes the box a pill is half the height these metrics come to: a size
// the sheet applied would only be measurable after it had been applied, and a
// radius named any larger than half the height is clamped into an ellipse
// rather than rounded further. Answers that height.
static int styleEditorIdChip(dlgTriggersMainArea* pForm)
{
    QFont chipFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QFont formFont = pForm->font();
    if (formFont.pointSizeF() > 0.0) {
        chipFont.setPointSizeF(formFont.pointSizeF() * scmEditorIdChipFontScale);
    } else {
        chipFont.setPixelSize(std::max(1, qRound(formFont.pixelSize() * scmEditorIdChipFontScale)));
    }

    for (QLabel* pLabel : {pForm->label_idLabel, pForm->label_idNumber}) {
        pLabel->setFont(chipFont);
    }

    const int chipHeight = QFontMetrics(chipFont).height() + 2 * (scmEditorIdChipPaddingVertical + uiDesign::scmInputBorderWidth);
    pForm->frameId->setFixedHeight(chipHeight);
    return chipHeight;
}

// A card in the settings dialog's language. What makes a group box one is the
// property the shell stylesheet selects on, so nothing else is set here.
static QGroupBox* makeEditorCard(QWidget* pParent, const QString& title)
{
    auto* pCard = new QGroupBox(title, pParent);
    pCard->setProperty(uiDesign::scmProp_editorCard, true);
    pCard->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return pCard;
}

// A QScrollArea asks for a couple of dozen lines however tall the thing inside
// it is, since the rest of it is what scrolling is for. That is the wrong thing
// to say here: the panel should be given its whole height wherever the form has
// it, and only scroll where it does not. So this one asks for the height of
// what it holds, and goes on reporting a minimum of the scroll bars alone -
// which is the half that keeps the window free to be dragged smaller.
class OptionsScrollArea : public QScrollArea
{
public:
    using QScrollArea::QScrollArea;

    QSize sizeHint() const override
    {
        const QSize hint = QScrollArea::sizeHint();
        const QWidget* pPanel = widget();
        if (!pPanel) {
            return hint;
        }
        return QSize(hint.width(), pPanel->sizeHint().height() + 2 * frameWidth());
    }
};

// The trigger form's options were a column of centre-titled group boxes, each
// drawing its own frame around one or two controls. They become four cards
// built around the .ui file's own controls, so that every object name, every
// connection and every translated string the move does not touch survives it.
void dlgTriggerEditor::buildTriggerOptionsPanel()
{
    auto* pForm = mpTriggersMainArea;

    // The strip that stands in for the panel while the panel is away. It goes
    // over the patterns rather than into the row above them: that row is a
    // grid the panel shares, and a strip the width of the form belongs under it.
    mpButton_triggerOptionsSummary = new QToolButton(pForm->widget_left);
    mpButton_triggerOptionsSummary->setObjectName(qsl("editorOptionsSummary"));
    // Led by the same sliders glyph the Options button carries, so the strip and
    // the button it stands in for are read as the one thing
    mpButton_triggerOptionsSummary->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    // As wide as what it says rather than as wide as the form: a row of readings
    // stretched across the whole width reads as a bar, which it is not
    mpButton_triggerOptionsSummary->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    mpButton_triggerOptionsSummary->setFocusPolicy(Qt::StrongFocus);
    mpButton_triggerOptionsSummary->setCursor(Qt::PointingHandCursor);
    //: Tooltip on the strip that stands in for a trigger's options while they are hidden
    mpButton_triggerOptionsSummary->setToolTip(utils::richText(tr("The trigger's options, put away. Click to show them.")));
    const int summaryGlyphSize = qRound(mpButton_triggerOptionsSummary->fontMetrics().height() * 0.9);
    mpButton_triggerOptionsSummary->setIconSize(QSize(summaryGlyphSize, summaryGlyphSize));
    pForm->verticalLayout_left->insertWidget(0, mpButton_triggerOptionsSummary, 0, Qt::AlignLeft);

    // The button beside the name says what it opens, rather than being an arrow
    // with nothing on it
    auto* pToggle = pForm->toolButton_toggleExtraControls;
    pToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    //: Button beside a trigger's name that shows or hides the trigger's options
    pToggle->setText(tr("Options"));
    const int toggleGlyphSize = qRound(pToggle->fontMetrics().height() * 0.9);
    pToggle->setIconSize(QSize(toggleGlyphSize, toggleGlyphSize));
    // The stylesheet draws the frame the checked state is read from, which
    // auto-raise would otherwise take away again
    pToggle->setAutoRaise(false);
    // ...and it stands on the row at the height of the fields beside it
    pToggle->setFixedHeight(uiDesign::scmInputHeight);

    // What the two fields on the row are, said quietly: the words are the
    // form's scaffolding and the name typed beside them is its content, so
    // only one of the two is drawn at full strength
    pForm->label_trigger_name->setProperty("editorRowLabel", true);
    pForm->label_trigger_command->setProperty("editorRowLabel", true);

    // The ID reads as a quiet label on the trigger rather than a second field.
    // showIDLabels() still decides whether it is there at all.
    pForm->frameId->setProperty("editorIdChip", true);
    // The .ui file greys the pair out to make them quiet; the chip is quiet
    // enough on its own, and a disabled label cannot be selected from
    pForm->label_idLabel->setEnabled(true);
    pForm->label_idNumber->setEnabled(true);

    // Emptying the column takes every control the .ui file put in it out of a
    // layout in one go, so only what is nested deeper needs detaching by hand
    auto* pPanelLayout = qobject_cast<QVBoxLayout*>(pForm->widget_right->layout());
    while (QLayoutItem* pItem = pPanelLayout->takeAt(0)) {
        delete pItem;
    }
    // Painted by the cards, not by the column behind them
    pForm->widget_right->setAutoFillBackground(false);
    // The cards keep this width whether or not a scroll bar has appeared beside
    // them: the scroll area is the wider of the two by exactly that bar
    pForm->widget_right->setFixedWidth(scmEditorTriggerOptionsWidth);
    // The gap between the patterns and the cards is the grid's, so the cards
    // themselves have the whole width the column was measured for
    pPanelLayout->setContentsMargins(0, 0, 0, 0);
    pPanelLayout->setSpacing(scmEditorColumnSpacing);

    // Four cards are taller than a short window, and a column that reports that
    // height as a minimum drags the whole window open to fit and will not let it
    // be dragged back down. The column scrolls instead: it keeps its natural
    // height wherever there is room for it, and where there is not, the window
    // stays the size the user put it at and the cards move under the viewport.
    auto* pGrid = qobject_cast<QGridLayout*>(pForm->layout());
    mpScrollArea_triggerOptions = new OptionsScrollArea(pForm);
    mpScrollArea_triggerOptions->setObjectName(qsl("editorTriggerOptionsScroll"));
    mpScrollArea_triggerOptions->setFrameShape(QFrame::NoFrame);
    mpScrollArea_triggerOptions->setWidgetResizable(true);
    // Nothing here moves sideways: the cards are drawn at their own width, with
    // room of its own for the bar beside them, so the only direction there is
    // to scroll in is the one they are stacked in
    mpScrollArea_triggerOptions->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mpScrollArea_triggerOptions->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    // Room for the cards and, beside them, for the bar that scrolls them: they
    // are drawn at the width they were laid out for whether or not it is there.
    // applyEditorShellStyle() sets this again once the bar has been given the
    // width the rest of the editor's are drawn at.
    mpScrollArea_triggerOptions->setFixedWidth(scmEditorTriggerOptionsWidth + mpScrollArea_triggerOptions->verticalScrollBar()->sizeHint().width());
    // The grid cell the .ui file put the column in becomes the scroll area's
    pGrid->removeWidget(pForm->widget_right);
    mpScrollArea_triggerOptions->setWidget(pForm->widget_right);
    pGrid->addWidget(mpScrollArea_triggerOptions, 1, 1);
    // setWidget() fills both from their own palette, and what shows behind the
    // cards is the page the form is drawn on
    mpScrollArea_triggerOptions->viewport()->setAutoFillBackground(false);
    pForm->widget_right->setAutoFillBackground(false);

    //: Title of the card holding how a trigger's patterns are combined
    auto* pCard_matching = makeEditorCard(pForm->widget_right, tr("Matching"));
    auto* pMatchingLayout = new QVBoxLayout(pCard_matching);
    pMatchingLayout->setContentsMargins(0, 0, 0, 0);
    pMatchingLayout->setSpacing(scmEditorCardRowGap);

    mpWidget_matchModeRows = new QWidget(pCard_matching);
    mpWidget_matchModeRows->setProperty("editorPanelSurface", true);
    auto* pModeLayout = new QVBoxLayout(mpWidget_matchModeRows);
    pModeLayout->setContentsMargins(0, 0, 0, 0);
    pModeLayout->setSpacing(scmEditorCardRowGap);

    mpLabel_matchAnyChip = new QLabel(mpWidget_matchModeRows);
    mpLabel_matchAnyChip->setObjectName(qsl("editorModeChip"));
    mpLabel_matchAnyChip->setAlignment(Qt::AlignCenter);
    //: Chip beside the "any pattern" choice for a trigger, naming the boolean operator that mode is. Kept short - it is drawn in a small box.
    mpLabel_matchAnyChip->setText(tr("OR"));
    mpLabel_matchAllChip = new QLabel(mpWidget_matchModeRows);
    mpLabel_matchAllChip->setObjectName(qsl("editorModeChip"));
    mpLabel_matchAllChip->setAlignment(Qt::AlignCenter);
    //: Chip beside the "all patterns" choice for a trigger, naming the boolean operator that mode is. Kept short - it is drawn in a small box.
    mpLabel_matchAllChip->setText(tr("AND"));

    mpRadioButton_matchAny = new QRadioButton(mpWidget_matchModeRows);
    mpRadioButton_matchAny->setObjectName(qsl("editorMatchAny"));
    //: One of a trigger's two matching modes: it fires as soon as any one of its patterns matches
    mpRadioButton_matchAny->setText(tr("Any pattern fires the trigger"));
    mpRadioButton_matchAll = new QRadioButton(mpWidget_matchModeRows);
    mpRadioButton_matchAll->setObjectName(qsl("editorMatchAll"));
    //: The other matching mode: the trigger fires only once every pattern has matched. The line under it holds how many lines they have to match within.
    mpRadioButton_matchAll->setText(tr("All patterns within"));
    // What each mode does, said in full: the chip beside a row is two or three
    // letters, and the row itself is only a little longer
    //: Tooltip on the OR chip and on the "any pattern" choice beside it, saying what that matching mode does
    const QString anyToolTip = utils::richText(tr("OR: the trigger fires as soon as any one of its patterns matches a line."));
    //: Tooltip on the AND chip and on the "all patterns" choice beside it, saying what that matching mode does
    const QString allToolTip = utils::richText(tr("AND: the trigger only fires once every one of its patterns has matched, within the number of lines set below."));
    mpRadioButton_matchAny->setToolTip(anyToolTip);
    mpLabel_matchAnyChip->setToolTip(anyToolTip);
    mpRadioButton_matchAll->setToolTip(allToolTip);
    mpLabel_matchAllChip->setToolTip(allToolTip);

    auto* pAnyRow = new QHBoxLayout();
    pAnyRow->setSpacing(scmEditorModeChipGap);
    pAnyRow->addWidget(mpLabel_matchAnyChip);
    pAnyRow->addWidget(mpRadioButton_matchAny, 1);
    pModeLayout->addLayout(pAnyRow);

    auto* pAllRow = new QHBoxLayout();
    pAllRow->setSpacing(scmEditorModeChipGap);
    pAllRow->addWidget(mpLabel_matchAllChip);
    pAllRow->addWidget(mpRadioButton_matchAll, 1);
    pModeLayout->addLayout(pAllRow);

    mpWidget_matchWithinRow = new QWidget(mpWidget_matchModeRows);
    mpWidget_matchWithinRow->setProperty("editorPanelSurface", true);
    auto* pWithinLayout = new QHBoxLayout(mpWidget_matchWithinRow);
    pWithinLayout->setContentsMargins(0, 0, 0, 0);
    pWithinLayout->setSpacing(scmEditorModeChipGap);
    mpSpinBox_matchWithinLines = new QSpinBox(mpWidget_matchWithinRow);
    mpSpinBox_matchWithinLines->setObjectName(qsl("editorMatchWithinLines"));
    mpSpinBox_matchWithinLines->setRange(0, scmEditorMatchWithinLinesMax);
    // The .ui file leaves spinBox_lineMargin on QSpinBox's default maximum of 99
    pForm->spinBox_lineMargin->setMaximum(scmEditorMatchWithinLinesMax);
    mpSpinBox_matchWithinLines->setAlignment(Qt::AlignCenter);
    mpSpinBox_matchWithinLines->setMaximumWidth(scmEditorOptionsSpinBoxWidth);
    mpSpinBox_matchWithinLines->setToolTip(pForm->spinBox_lineMargin->toolTip());
    //: Follows the number of lines all of a trigger's patterns have to match within
    auto* pLabel_withinLines = new QLabel(tr("lines"), mpWidget_matchWithinRow);
    pWithinLayout->addWidget(mpSpinBox_matchWithinLines);
    pWithinLayout->addWidget(pLabel_withinLines);
    pWithinLayout->addStretch(1);
    pModeLayout->addWidget(mpWidget_matchWithinRow);
    pMatchingLayout->addWidget(mpWidget_matchModeRows);

    // Sits outside the rows it explains, so that it stays readable while they
    // are greyed out - checkForMoreThanOneTriggerItem() is what shows it
    mpLabel_matchModeHint = new QLabel(pCard_matching);
    mpLabel_matchModeHint->setProperty("editorFieldLabel", true);
    mpLabel_matchModeHint->setWordWrap(true);
    //: Caption in a trigger's Matching card, shown while the trigger has only one pattern and the OR / AND choice is therefore greyed out
    mpLabel_matchModeHint->setText(tr("Add a second pattern to choose how they are combined."));
    pMatchingLayout->addWidget(mpLabel_matchModeHint);

    //: Trigger option, was called "match all": the script runs once for every place in the line the pattern matches, rather than once for the first
    pForm->checkBox_perlSlashGOption->setText(tr("Match every occurrence in a line"));
    pMatchingLayout->addWidget(pForm->checkBox_perlSlashGOption);

    //: Title of the card holding how long a trigger goes on firing for
    auto* pCard_firing = makeEditorCard(pForm->widget_right, tr("Firing"));
    auto* pFiringLayout = new QVBoxLayout(pCard_firing);
    pFiringLayout->setContentsMargins(0, 0, 0, 0);
    pFiringLayout->setSpacing(scmEditorCardRowGap);

    auto* pStayOpenRow = new QHBoxLayout();
    pStayOpenRow->setSpacing(scmEditorModeChipGap);
    //: Precedes the number of extra lines a trigger goes on firing for
    pStayOpenRow->addWidget(new QLabel(tr("Keep firing for"), pCard_firing));
    // The one control that is nested deeper than the column just emptied
    uiDesign::detachFromLayout(pForm->spinBox_stayOpen);
    pForm->spinBox_stayOpen->setMaximumWidth(scmEditorOptionsSpinBoxWidth);
    pStayOpenRow->addWidget(pForm->spinBox_stayOpen);
    //: Follows the number of extra lines a trigger goes on firing for
    pStayOpenRow->addWidget(new QLabel(tr("more lines"), pCard_firing));
    pStayOpenRow->addStretch(1);
    pFiringLayout->addLayout(pStayOpenRow);

    //: Trigger option, was called "only pass matches": the trigger's children see only the part of the line its pattern matched
    pForm->checkBox_filterTrigger->setText(tr("Only pass matches to children"));
    pFiringLayout->addWidget(pForm->checkBox_filterTrigger);

    // The last two cards are the group boxes themselves: their check box is the
    // switch the card is titled with, which is what a checkable card is
    auto* pCard_sound = pForm->groupBox_soundTrigger;
    pCard_sound->setProperty(uiDesign::scmProp_editorCard, true);
    pCard_sound->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    //: Title of the card that plays a sound when a trigger fires; it is also the switch that turns the sound on
    pCard_sound->setTitle(tr("Play a sound"));
    auto* pSoundGrid = pForm->gridLayout_groupBox_soundTrigger;
    while (QLayoutItem* pItem = pSoundGrid->takeAt(0)) {
        delete pItem;
    }
    pSoundGrid->setContentsMargins(0, 0, 0, 0);
    pSoundGrid->setHorizontalSpacing(scmEditorModeChipGap);
    pSoundGrid->setVerticalSpacing(scmEditorModeChipGap);
    // What is playing comes first; the button that changes it reads as the
    // answer to a file already named
    pSoundGrid->addWidget(pForm->lineEdit_soundFile, 0, 0);
    pSoundGrid->addWidget(pForm->toolButton_clearSoundFile, 0, 1);
    pSoundGrid->addWidget(pForm->pushButtonSound, 1, 0, 1, 2);
    pSoundGrid->setColumnStretch(0, 1);
    pSoundGrid->setColumnStretch(1, 0);
    pSoundGrid->setRowStretch(0, 0);
    pSoundGrid->setRowStretch(1, 0);
    pForm->lineEdit_soundFile->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto* pCard_highlight = pForm->groupBox_triggerColorizer;
    pCard_highlight->setProperty(uiDesign::scmProp_editorCard, true);
    pCard_highlight->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    //: Title of the card that recolours what a trigger matched; it is also the switch that turns the recolouring on
    pCard_highlight->setTitle(tr("Highlight matches"));
    auto* pColorGrid = pForm->gridLayout;
    pColorGrid->setContentsMargins(0, 0, 0, 0);
    pColorGrid->setHorizontalSpacing(scmEditorModeChipGap);
    pColorGrid->setVerticalSpacing(4);
    pColorGrid->setColumnStretch(0, 1);
    pColorGrid->setColumnStretch(1, 1);
    pForm->label_foregroundColor->setProperty("editorFieldLabel", true);
    pForm->label_backgroundColor->setProperty("editorFieldLabel", true);
    // A colour button says what it is by the colour it is filled with, so it is
    // sized as a well. slot_triggerSelected() still writes "keep" across one
    // that has no colour to show, which is the only thing it could say.
    for (QPushButton* pWell : {pForm->pushButtonFgColor, pForm->pushButtonBgColor}) {
        pWell->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        pWell->setFixedHeight(scmEditorColorWellHeight);
    }

    // spinBox_lineMargin is what the save and load paths read, and its special
    // first value is what says which of the two modes a trigger is in. The
    // radio pair is a view of it, so it stays here, out of sight, and neither
    // path has to know the form changed. The same for the group box that used
    // to hold the fire length spin box.
    for (QGroupBox* pRetired : {pForm->groupBox_multiLineTrigger, pForm->groupBox_stayOpen}) {
        pRetired->hide();
    }

    pPanelLayout->addWidget(pCard_matching);
    pPanelLayout->addWidget(pCard_firing);
    pPanelLayout->addWidget(pCard_sound);
    pPanelLayout->addWidget(pCard_highlight);
    // slot_rightSplitterMoved reads this spacer's height to tell whether the
    // form still has room for the panel, so it stays last in the column
    pPanelLayout->addWidget(pForm->widget_verticalSpacer_right, 1);

    // The radios write the mode into spinBox_lineMargin, which is where the
    // trigger is saved from - so slot_saveProperty_TriggerLineMargin and its
    // undo entry hear about the change exactly as they did from the old control
    connect(mpRadioButton_matchAll, &QAbstractButton::toggled, this, [this](const bool checked) {
        mpTriggersMainArea->spinBox_lineMargin->setValue(checked ? mpSpinBox_matchWithinLines->value() : -1);
    });
    connect(mpSpinBox_matchWithinLines, qOverload<int>(&QSpinBox::valueChanged), this, [this](const int value) {
        if (mpRadioButton_matchAll->isChecked()) {
            mpTriggersMainArea->spinBox_lineMargin->setValue(value);
        }
    });
    // ...and the other way round, so that loading a trigger or undoing an edit
    // shows in the radios without either path knowing they are there
    connect(mpTriggersMainArea->spinBox_lineMargin, qOverload<int>(&QSpinBox::valueChanged), this, &dlgTriggerEditor::reflectTriggerMatchMode);

    connect(mpTriggersMainArea->spinBox_stayOpen, qOverload<int>(&QSpinBox::valueChanged), this, &dlgTriggerEditor::updateTriggerOptionsSummary);
    connect(mpTriggersMainArea->groupBox_soundTrigger, &QGroupBox::toggled, this, &dlgTriggerEditor::updateTriggerOptionsSummary);
    connect(mpTriggersMainArea->groupBox_triggerColorizer, &QGroupBox::toggled, this, &dlgTriggerEditor::updateTriggerOptionsSummary);

    reflectTriggerMatchMode();
}

// spinBox_lineMargin has changed - by a load, an undo, or the radios below -
// so the view of it catches up. Blocked both ways round: the radios write back
// into that same spin box.
void dlgTriggerEditor::reflectTriggerMatchMode()
{
    if (!mpRadioButton_matchAny) {
        return;
    }

    const int lineDelta = mpTriggersMainArea->spinBox_lineMargin->value();
    const bool allMode = lineDelta >= 0;
    {
        const QSignalBlocker anyBlocker(mpRadioButton_matchAny);
        const QSignalBlocker allBlocker(mpRadioButton_matchAll);
        const QSignalBlocker withinBlocker(mpSpinBox_matchWithinLines);
        mpRadioButton_matchAny->setChecked(!allMode);
        mpRadioButton_matchAll->setChecked(allMode);
        // The OR mode carries no line count; what the AND mode starts from is
        // what the old spin box stepped up to from its special first value
        mpSpinBox_matchWithinLines->setValue(std::max(0, lineDelta));
    }
    mpWidget_matchWithinRow->setEnabled(allMode);

    restyleTriggerMatchModeChips();
    updateTriggerOptionsSummary();
}

// The chosen mode's chip carries the accent. Both are as wide as the wider of
// the two words, measured after the stylesheet rather than before it, since
// that is what says which font they are drawn in.
void dlgTriggerEditor::restyleTriggerMatchModeChips()
{
    if (!mpLabel_matchAnyChip) {
        return;
    }

    const QFontMetrics chipMetrics(mpLabel_matchAnyChip->font());
    const int chipWidth = std::max(chipMetrics.horizontalAdvance(mpLabel_matchAnyChip->text()), chipMetrics.horizontalAdvance(mpLabel_matchAllChip->text())) + 2 * scmEditorModeChipPadding;
    for (QLabel* pChip : {mpLabel_matchAnyChip, mpLabel_matchAllChip}) {
        pChip->setFixedWidth(chipWidth);
    }

    mpLabel_matchAnyChip->setProperty("editorModeChipActive", mpRadioButton_matchAny->isChecked());
    mpLabel_matchAllChip->setProperty("editorModeChipActive", mpRadioButton_matchAll->isChecked());
    for (QLabel* pChip : {mpLabel_matchAnyChip, mpLabel_matchAllChip}) {
        uiDesign::repolish(pChip);
    }

    // The line under the AND choice starts where that choice's words do: past
    // the chip, and past whatever the style leaves after a radio's indicator
    if (QLayout* pWithinLayout = mpWidget_matchWithinRow->layout()) {
        const int radioTextInset = style()->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth) + style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing);
        pWithinLayout->setContentsMargins(chipWidth + scmEditorModeChipGap + radioTextInset, 0, 0, 0);
    }
}

// What the options hold, for the strip that shows while they do not
void dlgTriggerEditor::updateTriggerOptionsSummary()
{
    if (!mpButton_triggerOptionsSummary) {
        return;
    }

    // Each reading is a whole phrase of its own rather than a fragment slotted
    // into a frame, so a translator is never handed half a sentence
    const auto* pForm = mpTriggersMainArea;
    QStringList readings;
    if (pForm->spinBox_lineMargin->value() < 0) {
        //: One reading of a trigger's matching mode on the strip that summarises its options: any one pattern is enough
        readings << tr("OR mode");
    } else {
        //: The other reading of a trigger's matching mode on that strip: every pattern has to match
        readings << tr("AND mode");
    }

    //: Part of the strip that summarises a trigger's options: how many lines past the one it matched on it goes on firing for. %n is that number, which can be zero.
    readings << tr("fires %n extra line(s)", nullptr, pForm->spinBox_stayOpen->value());

    if (pForm->groupBox_soundTrigger->isChecked()) {
        //: Part of the strip that summarises a trigger's options: it plays a sound
        readings << tr("sound on");
    } else {
        //: Part of the strip that summarises a trigger's options: it plays no sound
        readings << tr("no sound");
    }

    if (pForm->groupBox_triggerColorizer->isChecked()) {
        //: Part of the strip that summarises a trigger's options: it recolours what it matched
        readings << tr("highlight on");
    } else {
        //: Part of the strip that summarises a trigger's options: it does not recolour what it matched
        readings << tr("no highlight");
    }

    mpButton_triggerOptionsSummary->setText(readings.join(qsl(" - ")));
}

// The one way the panel is opened or closed on purpose, so that the Options
// button and the summary strip mean the same thing and persist the same way
void dlgTriggerEditor::setTriggerOptionsShown(const bool shown)
{
    mShowAllTriggerControls = shown;
    slot_showAllTriggerControls(shown);
    refitSplitterForTriggerOptions(shown);
}

// The panel is as tall as its four cards, and the form holding it is one pane
// of the right hand splitter. Opening it where that pane is too short takes the
// difference off the code pane below, down to a floor; closing it hands back
// what it took and no more. Only the deliberate open and close come through
// here - the space-driven auto-collapse in slot_rightSplitterMoved happens
// during a drag, and moving the splitter under the user would fight it.
void dlgTriggerEditor::refitSplitterForTriggerOptions(const bool shown)
{
    if (mCurrentView != EditorViewType::cmTriggerView) {
        return;
    }
    QList<int> sizes = splitter_right->sizes();
    if (sizes.size() < 2) {
        return;
    }

    if (!shown) {
        if (mTriggerOptionsBorrowedHeight <= 0) {
            return;
        }
        const int handedBack = std::min(mTriggerOptionsBorrowedHeight, sizes.at(0));
        mTriggerOptionsBorrowedHeight = 0;
        sizes[0] -= handedBack;
        sizes[1] += handedBack;
        splitter_right->setSizes(sizes);
        return;
    }

    // What the form needs with the panel on show, which by now it is - but only
    // after the chain above the panel has been told, or the first open measures
    // the form as it was without it: showing a widget invalidates the layout of
    // its immediate parent and no more
    uiDesign::invalidateLayoutsUpTo(mpTriggersMainArea->widget_right, mpNonCodeWidgets);
    const int wanted = mpNonCodeWidgets->sizeHint().height();
    if (sizes.at(0) >= wanted) {
        return;
    }
    // The sizes the user last dragged to, when the panel fits in them
    if (mTriggerRightSplitterSizes.size() == sizes.size() && mTriggerRightSplitterSizes.at(0) >= wanted) {
        mTriggerOptionsBorrowedHeight = 0;
        splitter_right->setSizes(mTriggerRightSplitterSizes);
        return;
    }

    const int spare = std::max(0, sizes.at(1) - scmEditorSourcePaneFloor);
    const int borrowed = std::min(wanted - sizes.at(0), spare);
    if (borrowed <= 0) {
        return;
    }
    mTriggerOptionsBorrowedHeight = borrowed;
    sizes[0] += borrowed;
    sizes[1] -= borrowed;
    splitter_right->setSizes(sizes);
}

void dlgTriggerEditor::slot_showAllTriggerControls(const bool isShown)
{
    if (mpTriggersMainArea->toolButton_toggleExtraControls->isChecked() != isShown) {
        mpTriggersMainArea->toolButton_toggleExtraControls->setChecked(isShown);
    }

    // Set unconditionally: isVisible() is also false while the whole triggers
    // main area is hidden (e.g. during construction), which would skip the
    // explicit hide needed to keep the extra controls hidden once it shows.
    // The scroll area is what holds the grid column open, so it is the one that
    // has to go; the panel inside it goes with it either way.
    mpTriggersMainArea->widget_right->setVisible(isShown);
    if (mpScrollArea_triggerOptions) {
        mpScrollArea_triggerOptions->setVisible(isShown);
    }

    // The strip stands in for the panel whenever the panel is away, whether
    // that was asked for or the form simply ran out of room for it
    if (mpButton_triggerOptionsSummary) {
        mpButton_triggerOptionsSummary->setVisible(!isShown);
        updateTriggerOptionsSummary();
    }

    updatePatternTabOrder();
}

void dlgTriggerEditor::slot_rightSplitterMoved(const int, const int)
{
    /*
     * With all widgets shown:              With some hidden:
     *  +--------------------------------+   +--------------------------------+
     *  | name / control toggle /command |   | name / control toggle /command |
     *--+----------------------+---------+ --+----------------------+---------+
     *  |+--------------------+|         |   |+------------------------------+|
     *w_||                    ||         |   ||                              ||
     *il||    scroll area     || widget  |   ||         scroll area          ||
     *de||                    || _right  |   ||                              ||
     *gf||                    ||         |   |+------------------------------+|
     *et||                    ||         | --+--------------------------------+
     *t ||                    ||         |
     *=>|+--------------------+|         |
     *--+----------------------+---------+
     */
    const int hysteresis = 10;
    if (mpTriggersMainArea->isVisible()) {
        mTriggerEditorSplitterState = splitter_right->saveState();
        // splitterMoved() only comes from a drag, so these are the sizes the
        // user chose - what reopening the options panel goes back to when the
        // panel fits in them. Whatever the panel borrowed from the code pane
        // stops being ours to hand back the moment the user sizes the two panes
        // themselves.
        mTriggerRightSplitterSizes = splitter_right->sizes();
        mTriggerOptionsBorrowedHeight = 0;
        // The triggersMainArea is visible
        if (mpTriggersMainArea->toolButton_toggleExtraControls->isChecked()) {
            // The extra controls are visible in the triggersMainArea
            if (mpTriggersMainArea->widget_verticalSpacer_right->height() <= hysteresis) {
                // And it is not tall enough to show the right hand side - so
                // hide them - we are using the spacer to detect if there is any
                // space:
                slot_showAllTriggerControls(false);
                // And the first time note down the required height:
                if (mTriggerMainAreaMinimumHeightToShowAll < 1) {
                    mTriggerMainAreaMinimumHeightToShowAll = mpTriggersMainArea->widget_left->height();
                }
            }

        } else {
            // And the extra controls are NOT visible. Only auto-restore them if
            // the user's preference is to show them - if they explicitly hid the
            // controls a later splitter expand must not bring them back:
            if (mShowAllTriggerControls && mTriggerMainAreaMinimumHeightToShowAll > 0 && mpTriggersMainArea->widget_left->height() > mTriggerMainAreaMinimumHeightToShowAll) {
                slot_showAllTriggerControls(true);
            }
        }
    } else if (mpActionsMainArea->isVisible()) {
        mActionEditorSplitterState = splitter_right->saveState();
    } else if (mpAliasMainArea->isVisible()) {
        mAliasEditorSplitterState = splitter_right->saveState();
    } else if (mpKeysMainArea->isVisible()) {
        mKeyEditorSplitterState = splitter_right->saveState();
    } else if (mpScriptsMainArea->isVisible()) {
        mScriptEditorSplitterState = splitter_right->saveState();
    } else if (mpTimersMainArea->isVisible()) {
        mTimerEditorSplitterState = splitter_right->saveState();
    } else if (mpVarsMainArea->isVisible()) {
        mVarEditorSplitterState = splitter_right->saveState();
    }
    if (mpSourceEditorFindArea->isVisible()) {
        slot_sourceFindMove();
    }
}

// Only for other classes to set the options - as they will not be carried from
// here to the parent Host instance, whereas the slots that change the
// individual options DO also notify that Host instance about the changes they
// make:
void dlgTriggerEditor::setSearchOptions(const SearchOptions optionsState)
{
    mSearchOptions = optionsState;
    mpAction_searchCaseSensitive->setChecked(optionsState & SearchOptionCaseSensitive);
    mpAction_searchIncludeVariables->setChecked(optionsState & SearchOptionIncludeVariables);
    mpAction_searchWholeWord->setChecked(optionsState & SearchOptionWholeWord);
    createSearchOptionIcon();
}

void dlgTriggerEditor::showOrHideRestoreEditorActionsToolbarAction()
{
    if ((!toolBar->isVisible()) || toolBar->isFloating()
        || (QMainWindow::toolBarArea(toolBar) & (Qt::ToolBarArea::LeftToolBarArea | Qt::ToolBarArea::RightToolBarArea | Qt::ToolBarArea::BottomToolBarArea))) {
        // If it is NOT visible
        // OR If the toolbar is floating
        // OR it is docked in an area other than the top one
        // then show the restore action
        mpAction_restoreEditorActionsToolbar->setVisible(true);
    } else {
        // Otherwise - i.e. it is visible AND docked AND docked to the original
        // area:
        mpAction_restoreEditorActionsToolbar->setVisible(false);
    }
}

// Shows/hides the restore option for the toolbar as the toolbar itself is
// hidden/shown:
void dlgTriggerEditor::slot_visibilityChangedEditorActionsToolbar()
{
    showOrHideRestoreEditorActionsToolbarAction();
}

// Gets triggered twice during the dragging of the toolbar from one docking
// area to another - as it briefly floats during the drag:
void dlgTriggerEditor::slot_floatingChangedEditorActionsToolbar()
{
    showOrHideRestoreEditorActionsToolbarAction();
}

// This also triggers the corresponding signal that is connected to the
// showOrHideRestoreEditorActionsToolbarAction() SLOT:
void dlgTriggerEditor::slot_restoreEditorActionsToolbar()
{
    if (!toolBar->isVisible()) {
        // Reshow it
        toolBar->show();
    }
    // Forces it to redock in the starting area:
    QMainWindow::addToolBar(Qt::TopToolBarArea, toolBar);
}

void dlgTriggerEditor::clearTriggerForm()
{
    // Clear pattern fields
    for (auto* patternEdit : std::as_const(mTriggerPatternEdit)) {
        patternEdit->singleLineTextEdit_pattern->clear();
        if (patternEdit->singleLineTextEdit_pattern->isHidden()) {
            patternEdit->singleLineTextEdit_pattern->show();
        }
        patternEdit->pushButton_fgColor->hide();
        patternEdit->pushButton_bgColor->hide();
        patternEdit->label_prompt->hide();
        patternEdit->spinBox_lineSpacer->hide();
        // Nudge the type up and down so that the appropriate (coloured) icon is copied across to the QLineEdit:
        patternEdit->comboBox_patternType->setCurrentIndex(1);
        patternEdit->comboBox_patternType->setCurrentIndex(0);
    }

    mpTriggersMainArea->lineEdit_trigger_name->clear();
    mpTriggersMainArea->label_idNumber->clear();
    clearDocument(mpSourceEditorEdbee);
    mpTriggersMainArea->lineEdit_trigger_command->clear();

    mpTriggersMainArea->hide();
    mpSourceEditorArea->hide();
    if (mCurrentView != EditorViewType::cmUnknownView) {
        showIntro();
    }
}

void dlgTriggerEditor::clearTimerForm()
{
    mpTimersMainArea->hide();
    mpTimersMainArea->hide();
    if (mCurrentView != EditorViewType::cmUnknownView) {
        showIntro();
    }
}

void dlgTriggerEditor::clearAliasForm()
{
    mpAliasMainArea->hide();
    mpSourceEditorArea->hide();
    if (mCurrentView != EditorViewType::cmUnknownView) {
        showIntro();
    }
}

void dlgTriggerEditor::clearScriptForm()
{
    mpScriptsMainArea->hide();
    mpSourceEditorArea->hide();
    if (mCurrentView != EditorViewType::cmUnknownView) {
        showIntro();
    }
}

void dlgTriggerEditor::clearActionForm()
{
    mpActionsMainArea->hide();
    mpSourceEditorArea->hide();
    if (mCurrentView != EditorViewType::cmUnknownView) {
        showIntro();
    }
}

void dlgTriggerEditor::clearKeyForm()
{
    mpKeysMainArea->hide();
    mpSourceEditorArea->hide();
    if (mCurrentView != EditorViewType::cmUnknownView) {
        showIntro();
    }
}

void dlgTriggerEditor::clearVarForm()
{
    mpVarsMainArea->hide();
    mpSourceEditorArea->hide();
    if (mCurrentView != EditorViewType::cmUnknownView) {
        showIntro();
    }
}

void dlgTriggerEditor::setEditorShowBidi(const bool state)
{
    auto config = mpSourceEditorEdbee->config();
    config->beginChanges();
    config->setRenderBidiContolCharacters(state);
    config->endChanges();
    mpSourceEditorEdbee->controller()->update();
}

void dlgTriggerEditor::hideSystemMessageArea()
{
    mpSystemMessageArea->hide();

    if (mCurrentView != EditorViewType::cmScriptView) {
        return;
    }

    QTreeWidgetItem* pItem = treeWidget_scripts->currentItem();
    if (pItem) {
        TScript* pT = mpHost->getScriptUnit()->getScript(pItem->data(0, Qt::UserRole).toInt());
        if (pT && pT->getLoadingError()) {
            pT->clearLoadingError();
        }
    }
}

// Modelled on dlgProfilePreferences::buildSidebar() - the same list, the same
// measurements and the same delegate, so that the two windows read as one
// design
void dlgTriggerEditor::buildEditorSidebar()
{
    mpWidget_editorSidebarPane = new QWidget(this);
    mpWidget_editorSidebarPane->setObjectName(qsl("editorSidebarPane"));
    mpWidget_editorSidebarPane->setFixedWidth(scmEditorSidebarRailWidth);
    auto* pSidebarLayout = new QVBoxLayout(mpWidget_editorSidebarPane);
    pSidebarLayout->setContentsMargins(scmEditorSidebarPadding, scmEditorSidebarVerticalPadding, scmEditorSidebarPadding, scmEditorSidebarVerticalPadding);
    pSidebarLayout->setSpacing(4);

    mpListWidget_editorSidebar = new QListWidget(mpWidget_editorSidebarPane);
    mpListWidget_editorSidebar->setObjectName(qsl("editorSidebar"));
    mpListWidget_editorSidebar->setFrameShape(QFrame::NoFrame);
    mpListWidget_editorSidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mpListWidget_editorSidebar->setIconSize(QSize(scmEditorSidebarIconSize, scmEditorSidebarIconSize));
    mpListWidget_editorSidebar->setItemDelegate(new uiDesign::SidebarItemDelegate(mpListWidget_editorSidebar));
    //: Accessible name of the list down the left of the editor that switches between triggers, aliases, scripts and the rest
    mpListWidget_editorSidebar->setAccessibleName(tr("Editor sections"));
    // Which of its rows carries the focus ring is a property eventFilter() puts on
    mpListWidget_editorSidebar->installEventFilter(this);
    pSidebarLayout->addWidget(mpListWidget_editorSidebar, 1);

    connect(mpListWidget_editorSidebar, &QListWidget::currentRowChanged, this, &dlgTriggerEditor::slot_editorSidebarRowChanged);
    // A row that runs a one-off action rather than changing the view asks for it
    // outright, so that arrowing past it does not toggle a console. Both signals
    // are wanted - a click, and Return on a row the keyboard is on - and on a
    // style that activates on a single click one click sends both, which
    // slot_editorSidebarItemActivated() is where the second of is dropped.
    connect(mpListWidget_editorSidebar, &QListWidget::itemClicked, this, &dlgTriggerEditor::slot_editorSidebarItemActivated);
    connect(mpListWidget_editorSidebar, &QListWidget::itemActivated, this, &dlgTriggerEditor::slot_editorSidebarItemActivated);
    invalidateEditorSidebarWidths();
}

// No icon yet: the set is single-colour, so which colour is not known until
// applyEditorShellStyle() has read the theme off the palette
void dlgTriggerEditor::addEditorSidebarRow(QAction* pAction, const EditorViewType view, const QString& iconFile)
{
    auto* pItem = new QListWidgetItem(pAction->text(), mpListWidget_editorSidebar);
    pItem->setData(scmRole_editorSidebarAction, QVariant::fromValue(pAction));
    pItem->setData(scmRole_editorSidebarView, static_cast<int>(view));
    pItem->setSizeHint(QSize(0, scmEditorSidebarRowHeight));
    // The action's own tooltip names the row and gives its shortcut, which is
    // the whole of what a collapsed sidebar has to offer on hover
    pItem->setToolTip(pAction->toolTip());
    mEditorSidebarGlyphs.append({pItem, iconFile});
    mEditorViewActions.append(pAction);
    invalidateEditorSidebarWidths();
}

void dlgTriggerEditor::addEditorSidebarSeparator()
{
    auto* pItem = new QListWidgetItem(mpListWidget_editorSidebar);
    pItem->setFlags(Qt::NoItemFlags);
    pItem->setSizeHint(QSize(0, 17));
    auto* pLine = new QFrame(mpListWidget_editorSidebar);
    pLine->setObjectName(qsl("editorSidebarSeparator"));
    pLine->setFrameShape(QFrame::HLine);
    mpListWidget_editorSidebar->setItemWidget(pItem, pLine);
}

// Called from restyleEditorIcons() alone, which is where the colours come from
void dlgTriggerEditor::restyleEditorSidebarIcons(const QColor& normal, const QColor& selected)
{
    for (const auto& glyph : std::as_const(mEditorSidebarGlyphs)) {
        const QPixmap source(glyph.second);
        QIcon icon(uiDesign::tintedGlyph(source, normal));
        // Otherwise the view makes one by washing the icon in the highlight colour
        icon.addPixmap(uiDesign::tintedGlyph(source, selected), QIcon::Selected);
        glyph.first->setIcon(icon);
    }
}

// The view can be changed from a deep link, a search result or a keyboard
// shortcut as much as from the sidebar, so the row that is drawn as chosen is
// set from the view rather than the other way round - see changeView()
void dlgTriggerEditor::syncEditorSidebarSelection()
{
    if (!mpListWidget_editorSidebar || mCurrentView == EditorViewType::cmUnknownView) {
        return;
    }
    for (int row = 0, rows = mpListWidget_editorSidebar->count(); row < rows; ++row) {
        QListWidgetItem* pItem = mpListWidget_editorSidebar->item(row);
        if (static_cast<EditorViewType>(pItem->data(scmRole_editorSidebarView).toInt()) != mCurrentView) {
            continue;
        }
        // Blocked, or choosing the row would ask for the view it came from
        const QSignalBlocker blocker(mpListWidget_editorSidebar);
        mpListWidget_editorSidebar->setCurrentItem(pItem);
        return;
    }
}

void dlgTriggerEditor::slot_editorSidebarRowChanged(const int row)
{
    QListWidgetItem* pItem = mpListWidget_editorSidebar->item(row);
    if (!pItem) {
        return;
    }
    const auto view = static_cast<EditorViewType>(pItem->data(scmRole_editorSidebarView).toInt());
    // A row that runs a one-off action is not somewhere to go, and the view
    // already on show is not one to rebuild
    if (view == EditorViewType::cmUnknownView || view == mCurrentView) {
        return;
    }
    if (auto* pAction = qvariant_cast<QAction*>(pItem->data(scmRole_editorSidebarAction))) {
        pAction->trigger();
    }
}

// Errors, Statistics and Debug are not views to leave the editor on: the first
// shows and hides the console at the bottom of this window, the second prints a
// summary onto the profile's own window and raises it, and the third opens the
// central debug console. So they run on a click or on Enter rather than on the
// keyboard passing over them, and the chosen row goes back to the view that is
// actually on show.
void dlgTriggerEditor::slot_editorSidebarItemActivated(QListWidgetItem* pItem)
{
    if (!pItem || static_cast<EditorViewType>(pItem->data(scmRole_editorSidebarView).toInt()) != EditorViewType::cmUnknownView) {
        return;
    }
    // A click and an activation both arriving is one click on a style that
    // activates on one, and the two arrive in the same pass through the event
    // loop - which is what tells them apart from a Return press, whose
    // activation arrives on a pass of its own with the guard already let go of
    if (mEditorSidebarActionInFlight) {
        return;
    }
    mEditorSidebarActionInFlight = true;
    QTimer::singleShot(0ms, this, [this]() {
        mEditorSidebarActionInFlight = false;
    });

    if (auto* pAction = qvariant_cast<QAction*>(pItem->data(scmRole_editorSidebarAction))) {
        pAction->trigger();
    }
    syncEditorSidebarSelection();
}

void dlgTriggerEditor::invalidateEditorSidebarWidths()
{
    mEditorSidebarWidthsKnown = false;
}

// Measured rather than a number in the source: an interface font or a
// translation's longer names both move it. Kept once measured, as a resize asks
// for the answer on every frame of a drag and nothing that would change it can
// happen in the middle of one.
dlgTriggerEditor::EditorSidebarWidths dlgTriggerEditor::editorSidebarWidths() const
{
    if (mEditorSidebarWidthsKnown) {
        return mEditorSidebarWidths;
    }

    EditorSidebarWidths widths;
    // Zero collapses nothing, which is the right answer for a half-built window
    // - and not an answer to keep, as the window will not stay half-built
    if (!mpListWidget_editorSidebar) {
        return widths;
    }
    // The chosen row is drawn bold, so it is the bold name that has to fit
    QFont nameFont = mpListWidget_editorSidebar->font();
    nameFont.setBold(true);
    const QFontMetrics nameMetrics(nameFont);
    int widestName = 0;
    for (int row = 0, rows = mpListWidget_editorSidebar->count(); row < rows; ++row) {
        widestName = std::max(widestName, nameMetrics.horizontalAdvance(mpListWidget_editorSidebar->item(row)->text()));
    }
    widths.expanded = std::clamp(2 * scmEditorSidebarPadding + scmEditorSidebarRowChrome + widestName, scmEditorSidebarRailWidth, scmEditorSidebarMaximumWidth);

    // Deliberately not the width above: held equal, the sidebar had its names at
    // exactly one window width and the first pixel of a drag inwards took them
    // away. It is instead the narrowest the editor itself can be drawn at,
    // capped at a comfortable reading column so that a window that has to be
    // wide for one of its forms does not cost the names.
    const int bodyMinimum = splitter_main ? splitter_main->minimumSizeHint().width() : 0;
    widths.collapseBelow = widths.expanded + std::min(scmEditorContentColumnWidth, bodyMinimum);

    mEditorSidebarWidths = widths;
    mEditorSidebarWidthsKnown = true;
    return widths;
}

// The one piece of the editor's chrome no preference decides: the sidebar is a
// rail whenever the window is too narrow to hold it, and a list of names when
// not.
void dlgTriggerEditor::updateEditorSidebarMode()
{
    if (!mpWidget_editorSidebarPane) {
        return;
    }
    const EditorSidebarWidths widths = editorSidebarWidths();
    if (!widths.expanded) {
        return;
    }
    // The window's width rather than the space left over: the threshold is what
    // the *expanded* sidebar needs, so collapsing cannot flip the test that
    // collapsed it and start it oscillating
    uiDesign::setSidebarCollapsed(mpWidget_editorSidebarPane, mpListWidget_editorSidebar, qsl("editorSidebarSeparator"), width() < widths.collapseBelow, editorSidebarMetrics(widths.expanded));
}

// Called from applyEditorShellStyle() alone, which is both where the colours
// come from and the one thing an appearance change runs again
void dlgTriggerEditor::restyleEditorIcons()
{
    const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
    const QColor quietColor = tokens.mutedText;
    const QColor accentText = tokens.accentText;

    for (const auto& glyph : std::as_const(mEditorActionGlyphs)) {
        if (!glyph.first) {
            continue;
        }
        const QPixmap source(glyph.second);
        QIcon icon(uiDesign::tintedGlyph(source, quietColor));
        // Active rather than Selected: a tool button asks for Active while the
        // pointer is on it and never for Selected, which the sidebar's chosen
        // row is the one thing in the editor that does ask for
        icon.addPixmap(uiDesign::tintedGlyph(source, accentText), QIcon::Active);
        glyph.first->setIcon(icon);
    }

    // Quieter than the name beside them, and the accent under a chosen one
    restyleEditorSidebarIcons(quietColor, accentText);

    if (mpLabel_editorCodeHeaderIcon) {
        const qreal glyphRatio = mpLabel_editorCodeHeaderIcon->devicePixelRatioF();
        QPixmap headerGlyph = uiDesign::tintedGlyph(QPixmap(qsl(":/icons/editor-scripts.png")), quietColor)
                                      .scaled(QSize(scmEditorCodeHeaderGlyphSize, scmEditorCodeHeaderGlyphSize) * glyphRatio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        headerGlyph.setDevicePixelRatio(glyphRatio);
        mpLabel_editorCodeHeaderIcon->setPixmap(headerGlyph);
    }

    // The trigger form's Options button, whose glyph is a set of sliders rather
    // than one of the toolbar's actions
    updateExtraControlsToggleIcon();

    // The magnifier on the search field, which is also the button its options
    // drop from, and the chevron that reopens what was searched for before
    createSearchOptionIcon();
    updateSearchHistoryAction();

    restyleAddPatternIcon();

    // The eight type swatches and the grip every row is dragged by, both mixed
    // from the page they lie on
    restylePatternTypeIcons();

    // ...and the same for the picture on every pattern row and the tint the row
    // itself is washed with under the mouse - both kept here rather than mixed
    // per row, since a trigger can hold fifty of them
    mPatternDeleteIcon = patternDeleteIcon();
    mPatternHoverTint = patternHoverTint();
    for (auto* patternEdit : std::as_const(mTriggerPatternEdit)) {
        patternEdit->setDeleteGlyph(mPatternDeleteIcon);
        patternEdit->setHoverTint(mPatternHoverTint);
    }

    // The banner's picture, at the size of the line of text beside it rather
    // than the 64px block the .ui file sizes it as. Which of the three it is is
    // the only thing the banner says without words, so the hue is kept and only
    // the lightness comes off the page - the way the compile chip is mixed.
    if (mpSystemMessageArea) {
        const QColor warningColor = uiDesign::stateColor(uiDesign::scmStateHue_warning, tokens.darkPage);
        const QColor errorColor = uiDesign::stateColor(uiDesign::scmStateHue_error, tokens.darkPage);
        const QList<std::tuple<QLabel*, QString, QColor>> bannerGlyphs{{mpSystemMessageArea->notificationAreaIconLabelError, qsl(":/icons/dialog-error.png"), errorColor},
                                                                       {mpSystemMessageArea->notificationAreaIconLabelWarning, qsl(":/icons/dialog-warning.png"), warningColor},
                                                                       {mpSystemMessageArea->notificationAreaIconLabelInformation, qsl(":/icons/dialog-information.png"), accentText}};
        for (const auto& [pLabel, glyphFile, glyphColor] : bannerGlyphs) {
            const qreal glyphRatio = pLabel->devicePixelRatioF();
            QPixmap glyph =
                    uiDesign::tintedGlyph(QPixmap(glyphFile), glyphColor).scaled(QSize(scmEditorBannerGlyphSize, scmEditorBannerGlyphSize) * glyphRatio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            glyph.setDevicePixelRatio(glyphRatio);
            pLabel->setPixmap(glyph);
            pLabel->setMargin(0);
            pLabel->setFixedSize(scmEditorBannerGlyphSize, scmEditorBannerGlyphSize);
        }
    }
}

// The six trees a profile's own items live in are the ones a state dot says
// anything about. The variables tree is a view of what Lua holds and the results
// tree a list of matches, and neither has anything to switch on or off - so they
// take the panel's look without the dots.
void dlgTriggerEditor::setupEditorPanel()
{
    const QList<QPair<TTreeWidget*, TreeType>> itemTrees{{treeWidget_triggers, TreeType::Trigger},
                                                         {treeWidget_aliases, TreeType::Alias},
                                                         {treeWidget_timers, TreeType::Timer},
                                                         {treeWidget_scripts, TreeType::Script},
                                                         {treeWidget_actions, TreeType::Action},
                                                         {treeWidget_keys, TreeType::Key}};
    for (const auto& itemTree : itemTrees) {
        auto* pDelegate = new uiDesign::EditorTreeDelegate(itemTree.first, itemTree.second, mpHost);
        // What the delegate goes on to keep the picture that says the item has
        // never been saved - see setNewItemDescription() for why the three types
        // that also have a checkIfNew() are not asked it
        pDelegate->setNewItemDescription(descNewItem);
        // A click on the dot is answered by the delegate that draws it, and the
        // switching itself is the same one the trees' itemActivated() reaches
        connect(pDelegate, &uiDesign::EditorTreeDelegate::toggleRequested, this, &dlgTriggerEditor::slot_toggleItemOrGroupActiveFlag);
        itemTree.first->setItemDelegate(pDelegate);
        mEditorTreeDelegates.append(pDelegate);
        // A long name is cut rather than pushed off the side: a panel this
        // narrow would otherwise spend its bottom edge on a scrollbar that
        // reaches text nobody was looking for
        itemTree.first->setTextElideMode(Qt::ElideRight);
        itemTree.first->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
    treeWidget_variables->setTextElideMode(Qt::ElideRight);

    // A match is a heading naming the item it was found in, with a row under it
    // per place inside that item - all of it drawn into one column by
    // SearchResultDelegate, so there are no column headings left to label
    treeWidget_searchResults->setObjectName(qsl("editorSearchResults"));
    treeWidget_searchResults->setColumnCount(1);
    treeWidget_searchResults->setHeaderHidden(true);
    treeWidget_searchResults->setRootIsDecorated(true);
    treeWidget_searchResults->setIndentation(scmEditorSearchResultIndent);
    treeWidget_searchResults->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // A heading is taller than the matches under it, and one height for every
    // row - which is what the .ui file asks for - would flatten the difference
    treeWidget_searchResults->setUniformRowHeights(false);
    // The order matches are found in is the order they are read in: a match
    // further down an item's Lua comes after the ones above it, and a heading
    // leads where the first row under it leads. Sorting the rows by their text
    // - which the .ui file also asks for - would undo both.
    treeWidget_searchResults->setSortingEnabled(false);
    mpSearchResultDelegate = new uiDesign::SearchResultDelegate(treeWidget_searchResults);
    treeWidget_searchResults->setItemDelegate(mpSearchResultDelegate);

    // The .ui file's name for the search row says which control it holds; the
    // shell stylesheet selects on it as one of the editor's own surfaces
    widget_searchTerm->setObjectName(qsl("editorSearchRow"));

    // The panel of items is one pane from the search row at its head to the
    // trees under it, so it is named as one thing and painted once - everything
    // it holds shows it through. The .ui file's name says where the frame is;
    // this one says what it is.
    frame_left->setObjectName(qsl("editorItemPane"));
}

// The gap above the Lua editor is the only chrome that pane has, so it is what
// says the pane is a Lua one and how the last save of it went. It stays the
// splitter's handle: the strip is told not to take the mouse, so the heading is
// also what the two panes are resized by.
void dlgTriggerEditor::setupEditorCodeHeader()
{
    mpWidget_editorCodeHeader = new QWidget(this);
    mpWidget_editorCodeHeader->setObjectName(qsl("editorCodeHeader"));
    auto* pHeaderLayout = new QHBoxLayout(mpWidget_editorCodeHeader);
    // The column the strip lies in is already held off the window's edge, so
    // this is only what the heading is set in from the code under it
    pHeaderLayout->setContentsMargins(scmEditorCodeHeaderInset, 0, scmEditorCodeHeaderInset, 0);
    pHeaderLayout->setSpacing(6);

    mpLabel_editorCodeHeaderIcon = new QLabel(mpWidget_editorCodeHeader);
    mpLabel_editorCodeHeaderIcon->setObjectName(qsl("editorCodeHeaderIcon"));
    mpLabel_editorCodeHeaderIcon->setFixedSize(scmEditorCodeHeaderGlyphSize, scmEditorCodeHeaderGlyphSize);
    pHeaderLayout->addWidget(mpLabel_editorCodeHeaderIcon);

    //: Heading over the editor's code pane, naming the language what is typed there is written in
    auto* pLabel_headerTitle = new QLabel(tr("Lua script"), mpWidget_editorCodeHeader);
    pLabel_headerTitle->setObjectName(qsl("editorCodeHeaderTitle"));
    pHeaderLayout->addWidget(pLabel_headerTitle);
    // Between two equal stretches, so the room the grip is drawn in is left in
    // the middle of the strip rather than wherever the heading happens to end
    pHeaderLayout->addStretch(1);
    pHeaderLayout->addSpacing(scmEditorCodeHeaderGripGap);
    pHeaderLayout->addStretch(1);

    mpWidget_editorCompileChip = new QWidget(mpWidget_editorCodeHeader);
    mpWidget_editorCompileChip->setObjectName(qsl("editorCompileChip"));
    auto* pChipLayout = new QHBoxLayout(mpWidget_editorCompileChip);
    pChipLayout->setContentsMargins(7, 2, 8, 2);
    pChipLayout->setSpacing(5);
    mpLabel_editorCompileDot = new QLabel(mpWidget_editorCompileChip);
    mpLabel_editorCompileDot->setObjectName(qsl("editorCompileDot"));
    mpLabel_editorCompileDot->setFixedSize(scmEditorCompileDotDiameter, scmEditorCompileDotDiameter);
    pChipLayout->addWidget(mpLabel_editorCompileDot);
    mpLabel_editorCompileState = new QLabel(mpWidget_editorCompileChip);
    mpLabel_editorCompileState->setObjectName(qsl("editorCompileState"));
    pChipLayout->addWidget(mpLabel_editorCompileState);
    pHeaderLayout->addWidget(mpWidget_editorCompileChip);

    // Index 1 is the handle over mpSourceEditorArea, which is the second of the
    // three panes the right hand splitter stacks
    splitter_right->setHeaderHandle(1, mpWidget_editorCodeHeader);
    updateEditorCompileChip();
}

// Both what the chip says and what it is drawn in, so a theme change and a
// compile failure arrive at the same place
void dlgTriggerEditor::updateEditorCompileChip()
{
    if (!mpWidget_editorCompileChip || !mpLabel_editorCompileDot || !mpLabel_editorCompileState) {
        return;
    }

    // Nothing to report is the whole of what "it compiled" means here
    const bool compiled = mEditorCompileMessage.isEmpty();
    const QColor stateColor = uiDesign::stateColor(compiled ? uiDesign::scmStateHue_ok : uiDesign::scmStateHue_error, uiDesign::themeTokens().darkPage);

    if (compiled) {
        //: Chip on the heading over the editor's code pane, saying the last save of it compiled
        mpLabel_editorCompileState->setText(tr("No errors"));
        mpLabel_editorCompileState->setToolTip(QString());
    } else {
        // What showError() was given is rich text, and a heading has room for a
        // line of it at most - the whole of it goes to the tooltip
        const QString plainMessage = QTextDocumentFragment::fromHtml(mEditorCompileMessage).toPlainText().simplified();
        mpLabel_editorCompileState->setText(mpLabel_editorCompileState->fontMetrics().elidedText(plainMessage, Qt::ElideRight, scmEditorCompileMessageWidth));
        mpLabel_editorCompileState->setToolTip(plainMessage);
    }
    // The strip is transparent to the mouse, so that a drag anywhere on it still
    // resizes - which also puts a tooltip set on it out of the pointer's reach.
    // The handle carrying the strip is what hears the pointer instead.
    if (QWidget* pHandle = mpWidget_editorCodeHeader ? mpWidget_editorCodeHeader->parentWidget() : nullptr) {
        pHandle->setToolTip(mpLabel_editorCompileState->toolTip());
    }

    mpLabel_editorCompileDot->setStyleSheet(qsl("#editorCompileDot { background-color: %1; border-radius: %2px; }").arg(stateColor.name(), QString::number(scmEditorCompileDotDiameter / 2)));
    mpWidget_editorCompileChip->setStyleSheet(qsl("#editorCompileChip { background-color: %1; border-radius: %3px; }"
                                                  "#editorCompileState { color: %2; font-size: 92%; }")
                                                      .arg(uiDesign::rgba(stateColor, 0.14), stateColor.name(), QString::number(uiDesign::scmRadiusChip)));
}

void dlgTriggerEditor::clearCompileState()
{
    mEditorCompileMessage.clear();
    updateEditorCompileChip();
}

void dlgTriggerEditor::beginSaveErrorCapture()
{
    mEditorSaveErrorCaptureOpen = true;
    mEditorSaveErrorCaptured.clear();
}

void dlgTriggerEditor::endSaveErrorCapture()
{
    mEditorSaveErrorCaptureOpen = false;
    mEditorCompileMessage = mEditorSaveErrorCaptured;
    updateEditorCompileChip();
}

// A row of names beside pictures, whatever icon size the preferences ask for:
// the grouping the toolbar is read by only works if the names are there
void dlgTriggerEditor::applyEditorToolbarButtonStyles()
{
    if (!toolBar) {
        return;
    }
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    // Two arrows side by side read as one control, and spelling them out costs
    // more width than the pair is worth
    for (QAction* pAction : {mpUndoAction, mpRedoAction}) {
        if (!pAction) {
            continue;
        }
        if (auto* pButton = qobject_cast<QToolButton*>(toolBar->widgetForAction(pAction))) {
            pButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
        }
    }
}

// Every colour is mixed from the application palette rather than written out as
// a literal, so the editor follows whichever appearance is in force.
//
// The sheet goes on the regions themselves rather than on this window, because
// Host::setProfileStyleSheet() assigns the profile's own Lua stylesheet to the
// window - which would replace this one outright. The application's palette is
// the one to read rather than this widget's: a stylesheet freezes the palette of
// what it is set on, and the palette change is an event still undelivered when
// changeEvent() runs.
// The pattern rows, which are inside the trigger form and so are drawn by the
// same sheet. A row is quiet until the mouse is over it: the grip and the button
// that takes it away are chrome the rows would otherwise be a wall of.
QString dlgTriggerEditor::patternRowStyleSheet() const
{
    const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
    // The colour a button with nothing left to add is drawn in - and, for the
    // frame that button paints, the one restyleAddPatternIcon() hands it
    const QColor quietestText = uiDesign::blend(tokens.card, tokens.text, 0.55);

    return qsl("#label_dragHandle { background: transparent; }"
               "#label_patternNumber { color: %2; background: transparent; }"
               // Its own tint over the row's, or the button would be no more
               // than the row it is already sitting on
               "#toolButton_deletePattern { border: none; border-radius: 4px; background: transparent; }"
               "#toolButton_deletePattern:hover { background-color: %5; }"
               // The one place in the form that is not a control but a place one
               // more of them would go. The dashed frame round it is the button's
               // own painting - see uiDesign::PlaceholderButton - so nothing here
               // draws a border; the margins it insets that frame by are the ones
               // named below, and both come from the same two constants.
               "#editorAddPattern { color: %2; border: none; padding: 4px 10px;"
               " margin: %6px 0px %7px 0px; background: transparent; }"
               "#editorAddPattern:hover { color: %3; }"
               "#editorAddPattern:disabled { color: %1; }"
               "#editorPatternDropIndicator { background-color: %4; border: none; border-radius: 1px; }"
               // Named outright, so that a profile stylesheet cannot put the
               // field colour back under the rows: the scroll area, the
               // viewport Qt gives it, and the widget scrolled inside it. Named
               // rather than "> QWidget", which would take the scroll bars too.
               "#editorPatternScroll, #editorPatternScroll > #qt_scrollarea_viewport, #editorPatternList"
               " { background: transparent; border: none; }")
                   .arg(quietestText.name(),
                        tokens.mutedText.name(),
                        tokens.text.name(),
                        tokens.accent.name(),
                        uiDesign::rgba(tokens.text, 0.14),
                        QString::number(scmEditorAddPatternMarginTop),
                        QString::number(scmEditorAddPatternMarginBottom))
           // Styling the scroll area at all takes its scroll bar with it, so
           // the bar is given the same one the trees use
           + uiDesign::scrollBarStyleSheet(qsl("#editorPatternScroll"), tokens);
}

// What a row is washed with while the mouse is on it. Painted by the row rather
// than left to a stylesheet rule on a property: a property that has to be
// re-polished re-runs the whole sheet over the row's controls for a tint.
QColor dlgTriggerEditor::patternHoverTint() const
{
    QColor tint = uiDesign::themeTokens().text;
    tint.setAlphaF(scmEditorPatternHoverStrength);
    return tint;
}

void dlgTriggerEditor::applyEditorShellStyle()
{
    if (!toolBar) {
        return;
    }

    const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
    const QColor cardColor = tokens.card;
    const QColor fieldColor = tokens.field;
    const QColor textColor = tokens.text;
    const QColor accentColor = tokens.accent;

    // Every rule below, and every glyph restyleEditorIcons() tints, is mixed
    // from these four and nothing else - so a change event that leaves all
    // four where they were has nothing to redo. One setProfileStyleSheet()
    // from Lua sends both a StyleChange and a PaletteChange, which is what this
    // stops from restyling the whole editor twice over.
    const EditorShellStyleInputs styleInputs{tokens.page.rgb(), fieldColor.rgb(), textColor.rgb(), accentColor.rgb()};
    if (mEditorShellStyleApplied && styleInputs == mEditorShellStyleInputs) {
        return;
    }
    mEditorShellStyleInputs = styleInputs;
    mEditorShellStyleApplied = true;

    const QColor pageColor = tokens.page;
    // The panel of items is a pane of its own between two columns drawn on the
    // page, and what parts one pane from the next is a groove rather than a
    // hairline
    const QColor paneColor = tokens.pane;
    const QColor separatorColor = tokens.separator;
    const QColor borderColor = tokens.border;
    const QColor mutedText = tokens.mutedText;
    const QColor disabledText = tokens.disabledText;
    const QString hoverSoft = tokens.hoverSoft;
    const QString accentSoft = tokens.accentSoft;
    const QColor accentText = tokens.accentText;
    // Every form in the window is filled in through the same set of controls, so
    // they are all drawn from one recipe. It goes on each form rather than on the
    // window: a rule naming QLineEdit on the window would reach the code pane's
    // find bar and the trees' editors as well as the fields it is meant for.
    const QString inputRules = uiDesign::inputStyleSheet(tokens);

    restyleEditorIcons();

    const QString toolBarRules = qsl("QToolBar#editorActionsToolbar { background-color: %1; border: none; border-bottom: 1px solid %2; spacing: 2px; padding: 4px 6px; }"
                                     "QToolBar#editorActionsToolbar::separator { background-color: %2; width: 1px; margin: 5px 6px; }"
                                     // The transparent border keeps the label from stepping
                                     // sideways when a hovered button gains one
                                     "QToolBar#editorActionsToolbar QToolButton { color: %3; border: 1px solid transparent; border-radius: 6px; padding: 3px 7px; }"
                                     "QToolBar#editorActionsToolbar QToolButton:hover { color: %4; background-color: %5; }"
                                     "QToolBar#editorActionsToolbar QToolButton:pressed { background-color: %6; }"
                                     "QToolBar#editorActionsToolbar QToolButton:disabled { color: %7; }"
                                     // Styling the button at all takes the arrow's own
                                     // separator with it, so the menu half is drawn as one
                                     // piece with the rest
                                     "QToolBar#editorActionsToolbar QToolButton::menu-button { border: none; background: transparent; width: 14px; }")
                                         .arg(pageColor.name(), borderColor.name(), mutedText.name(), textColor.name(), hoverSoft, accentSoft, disabledText.name());

    const QString statusBarRules = qsl("QStatusBar#editorStatusBar { background-color: %1; border-top: 1px solid %2; }"
                                       // Or the platform style draws a sunken frame around
                                       // every widget the bar holds
                                       "QStatusBar#editorStatusBar::item { border: none; }"
                                       "QStatusBar#editorStatusBar QLabel { color: %3; font-size: 92%; padding: 0px 6px; }")
                                           .arg(pageColor.name(), borderColor.name(), mutedText.name());

    const QString shellStyleSheet = toolBarRules + statusBarRules;
    toolBar->setStyleSheet(shellStyleSheet);
    if (QStatusBar* pStatusBar = QMainWindow::statusBar()) {
        pStatusBar->setStyleSheet(shellStyleSheet);
    }

    // The window's own surface, painted rather than left to fall back on
    // QPalette::Window. The toolbar, the status bar, the panel down the left
    // and the trees on it all name the page colour, while everything from the
    // frame an item is edited in down to the seven forms inside it is
    // transparent - so without this the two halves of the window agree only for
    // as long as the page colour and QPalette::Window do, which is not the case
    // on a palette that answers the same thing to Window and to Base, macOS in
    // light appearance among them: themeTokens() steps the page down from Window
    // there to keep the cards above it, and the edit column would be left a
    // shade lighter than the panel beside it.
    //
    // On the shell rather than on the window, whose stylesheet
    // Host::setProfileStyleSheet() assigns the profile's Lua one to, and named
    // outright rather than written as a bare QWidget rule, which would paint
    // every widget in the window over the top of what draws it.
    if (QWidget* pShell = QMainWindow::centralWidget()) {
        pShell->setStyleSheet(qsl("#editorShell { background-color: %1; }").arg(pageColor.name()));
    }

    // The panel of items, painted once for the whole column: the search row at
    // its head shows this through, and the trees under it name the same tone.
    // A pane rather than the page, so that the panel reads as a column of its
    // own between the sidebar and the column an item is edited in, both of
    // which are the page.
    frame_left->setStyleSheet(qsl("#editorItemPane { background-color: %1; }").arg(paneColor.name()));

    // The trees on that pane. No colour is named for an unselected row: what a
    // row is drawn in says whether the thing it stands for is running, and
    // EditorTreeDelegate is what knows that.
    const QString treeRules = qsl("QTreeWidget { background-color: %1; border: none; outline: none; show-decoration-selected: 1; }"
                                  "QTreeWidget::item { border-radius: 6px; padding: 2px 4px; }"
                                  "QTreeWidget::item:hover { background-color: %2; }"
                                  "QTreeWidget::item:selected { color: %5; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %3, stop:1 %4); }")
                                      .arg(paneColor.name(), hoverSoft, uiDesign::rgba(accentColor, 0.24), uiDesign::rgba(accentColor, 0.10), accentText.name())
                              + uiDesign::scrollBarStyleSheet(qsl("QTreeWidget"), tokens, paneColor);

    const QList<QTreeWidget*> panelTrees{treeWidget_triggers, treeWidget_aliases, treeWidget_timers, treeWidget_scripts, treeWidget_actions, treeWidget_keys, treeWidget_variables};
    for (QTreeWidget* pTreeWidget : panelTrees) {
        pTreeWidget->setStyleSheet(treeRules);
    }
    for (uiDesign::EditorTreeDelegate* pDelegate : std::as_const(mEditorTreeDelegates)) {
        pDelegate->restyle();
    }

    // The results take the trees' hover and selection so that one panel reads as
    // one thing; what a row holds is drawn by SearchResultDelegate, which is
    // where the rest of the look comes from - the heading rows' larger type
    // among it. The branch column is left to the style: a heading is a group the
    // reader can fold away.
    treeWidget_searchResults->setStyleSheet(treeRules + qsl("QTreeWidget#editorSearchResults::item { padding: 0px 2px; }"));
    if (mpSearchResultDelegate) {
        mpSearchResultDelegate->restyle();
    }

    // Only the field is drawn: the row around it is the panel it sits on, so
    // the box is the one thing here that carries the sunken colour
    widget_searchTerm->setStyleSheet(qsl("#editorSearchRow { background: transparent; }"
                                         // Taller than a form control and the one thing the panel
                                         // under it is worked from, so it takes a panel's corner
                                         "#editorSearchRow QComboBox { background-color: %1; border: 1px solid %2; border-radius: %5px; min-height: 32px; padding: 0px 6px; color: %3; }"
                                         "#editorSearchRow QComboBox:focus { border: 1px solid %4; }"
                                         // A field to type into rather than a list to pick from:
                                         // what the box holds is the searches already run, which
                                         // the down arrow key and the field's own completer both
                                         // still open. The drop-down is given no width at all, so
                                         // the room goes to the term instead.
                                         "#editorSearchRow QComboBox::drop-down { width: 0px; border: none; background: transparent; }"
                                         "#editorSearchRow QComboBox::down-arrow { width: 0px; height: 0px; image: none; }"
                                         // Or the field is drawn a second time, in
                                         // its own frame, inside the one above
                                         "#editorSearchRow QComboBox QLineEdit { background: transparent; border: none; }")
                                             .arg(fieldColor.name(), borderColor.name(), textColor.name(), accentColor.name(), QString::number(uiDesign::scmRadiusProminentInput)));

    if (mpWidget_editorCodeHeader) {
        // Only what the strip holds is drawn: the bar behind it is painted by
        // the handle carrying it, which is where the grip comes from too
        mpWidget_editorCodeHeader->setStyleSheet(qsl("#editorCodeHeader { background: transparent; }"
                                                     "#editorCodeHeaderTitle { color: %1; font-size: 92%; }")
                                                         .arg(mutedText.name()));
        updateEditorCompileChip();
    }

    if (mpTriggersMainArea) {
        // The check indicator a checkable card's title begins with, drawn the
        // one way the settings dialog's cards draw theirs
        const QString cardIndicatorRules = uiDesign::cardIndicatorStyleSheet(uiDesign::scmProp_editorCard, tokens);
        // How much of the card's top padding is the title's rather than the gap
        // under it is a line of the type the title is drawn in - and the box
        // measured against the rules above has to be one they select on
        const int cardTitleHeight = uiDesign::measuredCardTitleHeight(mpTriggersMainArea, cardIndicatorRules, uiDesign::scmProp_editorCard);
        // Styling the options column's scroll area takes its scroll bar with
        // it, the same way the pattern list's does
        const QString optionsScrollBarRules = uiDesign::scrollBarStyleSheet(qsl("#editorTriggerOptionsScroll"), tokens);

        // The two words in the ID pill are given their type here rather than in
        // the sheet, because the pill's corner is half the height those metrics
        // come to: a font-size the sheet applied would only be known after it
        // had been, and a corner named larger than half the height is clamped
        // into an ellipse rather than drawn rounder.
        const int idChipHeight = styleEditorIdChip(mpTriggersMainArea);

        // A control pressed rather than a surface: the button lifts a shade
        // further off the row while the pointer is on it, and the outlined strip
        // under it draws its hairline a shade nearer the words instead
        const QColor hoveredButton = uiDesign::blend(cardColor, textColor, scmEditorRaisedHoverWeight);
        const QColor hoveredBorder = uiDesign::blend(borderColor, textColor, scmEditorHoveredBorderWeight);

        // The cards the options column is laid out in, drawn the one way the
        // settings dialog's pages draw theirs
        mpTriggersMainArea->setStyleSheet(uiDesign::cardStyleSheet(cardMetrics(cardTitleHeight), tokens)
                                          + qsl(
                                                    // The rows the cards are built out of show the card through them,
                                                    // named outright so a profile stylesheet cannot paint a band across one
                                                    "QWidget[editorPanelSurface=\"true\"] { background: transparent; border: none; }"
                                                    "QLabel[editorFieldLabel=\"true\"] { color: %3; font-size: 92%; }"
                                                    // What a field on the form's own row is, as against what a card's
                                                    // rows hold: read at the size the rest of the row is read at
                                                    "QLabel[editorRowLabel=\"true\"] { color: %3; background: transparent; }"
                                                    // The box naming a matching mode; the chosen one carries the accent
                                                    "#editorModeChip { color: %3; border: 1px solid %2; border-radius: %8px; padding: 1px 0px;"
                                                    " background: transparent; font-family: monospace; font-weight: bold; font-size: 85%; }"
                                                    "#editorModeChip[editorModeChipActive=\"true\"] { color: %5; border: 1px solid %4; background-color: %6; }"
                                                    // The button the options are opened from is one to press, so it is
                                                    // lifted off the row the way a card is lifted off the page; the
                                                    // strip that stands in for them while they are away is a line of
                                                    // readings to click, so it is only outlined
                                                    "#toolButton_toggleExtraControls { color: %3; border: 1px solid %2; border-radius: %10px;"
                                                    " padding: %13px %14px; background-color: %1; }"
                                                    "#toolButton_toggleExtraControls:hover { color: %7; background-color: %11; }"
                                                    "#toolButton_toggleExtraControls:checked { color: %5; border: 1px solid %4; background-color: %6; }"
                                                    "#editorOptionsSummary { color: %3; border: 1px solid %2; border-radius: 6px; padding: 6px 10px;"
                                                    " background: transparent; text-align: left; }"
                                                    "#editorOptionsSummary:hover { color: %7; border: 1px solid %12; }"
                                                    // The ID reads as a label on the trigger, not as a second field: a
                                                    // pill, whose corner is half the height its own type comes to.
                                                    // Anything larger is clamped into an ellipse rather than rounded
                                                    // further, which is why the number is measured rather than named.
                                                    "#frameId { border: 1px solid %2; border-radius: %9px; background: transparent; }"
                                                    "#frameId QLabel { color: %3; background: transparent; }"
                                                    // The cards are what is drawn in the options column: the scroll
                                                    // area holding them and the viewport Qt gives it show the page
                                                    // through. Named outright, as the pattern rows are, so that a
                                                    // profile stylesheet cannot put the field colour back behind them.
                                                    "#editorTriggerOptionsScroll, #editorTriggerOptionsScroll > #qt_scrollarea_viewport, #widget_right"
                                                    " { background: transparent; border: none; }")
                                                    .arg(cardColor.name(), borderColor.name(), mutedText.name(), accentColor.name(), accentText.name(), accentSoft, textColor.name())
                                                    .arg(QString::number(uiDesign::scmRadiusChip), QString::number(idChipHeight / 2))
                                                    .arg(QString::number(uiDesign::scmRadiusInput), hoveredButton.name(), hoveredBorder.name())
                                                    .arg(QString::number(scmEditorRowButtonPaddingVertical), QString::number(scmEditorRowButtonPaddingHorizontal))
                                          + cardIndicatorRules + patternRowStyleSheet() + optionsScrollBarRules + inputRules);
        // The chips are measured in the font the sheet just gave them
        restyleTriggerMatchModeChips();
        // ...and the options column against the bar it just sized, so that the
        // cards keep their own width whether or not that bar is there
        if (mpScrollArea_triggerOptions) {
            mpScrollArea_triggerOptions->setFixedWidth(scmEditorTriggerOptionsWidth + mpScrollArea_triggerOptions->verticalScrollBar()->sizeHint().width());
        }
    }

    if (mpScriptsMainArea) {
        // The other list in the editor that shows what is there rather than
        // taking something typed: the handlers a script is registered for, added
        // and removed with the controls beside it. Left to itself it is filled
        // with QPalette::Base like a field, which reads as a sunken box on the
        // form; it takes the trees' treatment instead, keeping only the hairline
        // that says where the list ends.
        QListWidget* pHandlerList = mpScriptsMainArea->listWidget_script_registered_event_handlers;
        pHandlerList->setObjectName(qsl("editorScriptHandlers"));
        pHandlerList->setFrameShape(QFrame::NoFrame);
        pHandlerList->viewport()->setAutoFillBackground(false);
        mpScriptsMainArea->setStyleSheet(qsl("#editorScriptHandlers { background: transparent; border: 1px solid %1; border-radius: 6px; outline: none; }"
                                             // The frame is the list's own, so the viewport inside it draws none
                                             "#editorScriptHandlers > #qt_scrollarea_viewport { background: transparent; border: none; }"
                                             "#editorScriptHandlers::item { border-radius: 4px; padding: 2px 4px; }"
                                             "#editorScriptHandlers::item:hover { background-color: %2; }"
                                             "#editorScriptHandlers::item:selected { color: %5;"
                                             " background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %3, stop:1 %4); }")
                                                 .arg(borderColor.name(), hoverSoft, uiDesign::rgba(accentColor, 0.24), uiDesign::rgba(accentColor, 0.10), accentText.name())
                                         + uiDesign::scrollBarStyleSheet(qsl("#editorScriptHandlers"), tokens) + inputRules);
    }

    // The forms with nothing but fields on them: a name, a delay, a key, a
    // value. The two above are here as well, appended to what else they carry.
    for (QWidget* pMainArea : {static_cast<QWidget*>(mpTimersMainArea),
                               static_cast<QWidget*>(mpAliasMainArea),
                               static_cast<QWidget*>(mpActionsMainArea),
                               static_cast<QWidget*>(mpKeysMainArea),
                               static_cast<QWidget*>(mpVarsMainArea)}) {
        if (pMainArea) {
            pMainArea->setStyleSheet(inputRules);
        }
    }

    if (mpSystemMessageArea) {
        // A notice rather than a strip of highlighter pen: the accent the rest
        // of the editor points with, and the picture beside the words is what
        // says which of the three readings this one is
        mpSystemMessageArea->frame_notificationArea->setStyleSheet(qsl("QFrame#frame_notificationArea { background-color: %1; border: 1px solid %2; border-radius: %4px; }"
                                                                       "QFrame#frame_notificationArea QLabel { background: transparent; color: %3; }")
                                                                           .arg(accentSoft, accentColor.name(), textColor.name(), QString::number(uiDesign::scmRadiusPanel)));
        // The .ui file sizes the area around a 64px picture; what it holds now
        // is a line of text beside a small one
        mpSystemMessageArea->setMinimumSize(0, 0);
        mpSystemMessageArea->frame_notificationArea->setMinimumHeight(0);
        mpSystemMessageArea->verticalSpacer_closeButton->changeSize(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
        if (QLayout* pNoticeLayout = mpSystemMessageArea->frame_notificationArea->layout()) {
            pNoticeLayout->setContentsMargins(10, 8, 10, 8);
            pNoticeLayout->setSpacing(8);
            pNoticeLayout->invalidate();
        }
    }

    if (!mpWidget_editorSidebarPane) {
        return;
    }
    const EditorSidebarWidths widths = editorSidebarWidths();
    mpWidget_editorSidebarPane->setStyleSheet(qsl("#editorSidebarPane { background-color: %1; border-right: 1px solid %2; }").arg(pageColor.name(), separatorColor.name())
                                              // The list itself is the settings dialog's sidebar, drawn from the
                                              // same rules; the names are quieter here, as the rest of the
                                              // editor's chrome is
                                              + uiDesign::sidebarStyleSheet(qsl("editorSidebar"), qsl("editorSidebarSeparator"), mutedText, editorSidebarMetrics(widths.expanded), tokens)
                                              + uiDesign::scrollBarStyleSheet(qsl("#editorSidebar"), tokens));

    // A different font is a different width for the names, so the breakpoint is
    // re-measured with the look it belongs to
    updateEditorSidebarMode();
}

// A count is a walk of the whole tree, which filling one out would otherwise
// pay for once per item added
void dlgTriggerEditor::scheduleEditorItemCountUpdate()
{
    // Left running rather than restarted: a burst of adds is one walk either
    // way, and pushing the wait back on each of them is how a long import ends
    // up never counting at all
    if (mpTimer_statusCounts && !mpTimer_statusCounts->isActive()) {
        mpTimer_statusCounts->start();
    }
}

void dlgTriggerEditor::updateEditorItemCounts()
{
    if (!mpLabel_statusCounts) {
        return;
    }

    // Which tree is walked, what makes one of its items active, and how the two
    // numbers are read out: three answers to the one question, so it is asked
    // once rather than again for every item in the tree. Both are plain function
    // pointers - what they need is handed to them.
    QTreeWidgetItem* pBaseItem = nullptr;
    bool (*wantedOn)(Host*, const int) = nullptr;
    QString (*countText)(const int, const int) = nullptr;
    switch (mCurrentView) {
    case EditorViewType::cmTriggerView:
        pBaseItem = mpTriggerBaseItem;
        wantedOn = [](Host* pHost, const int id) {
            TTrigger* pT = pHost->getTriggerUnit()->getTrigger(id);
            return pT && pT->shouldBeActive();
        };
        countText = [](const int total, const int active) {
            //: Editor status bar. %n is how many triggers there are in total, %1 how many of those are turned on.
            return tr("%n trigger(s) - %1 active", "", total).arg(active);
        };
        break;
    case EditorViewType::cmAliasView:
        pBaseItem = mpAliasBaseItem;
        wantedOn = [](Host* pHost, const int id) {
            TAlias* pT = pHost->getAliasUnit()->getAlias(id);
            return pT && pT->shouldBeActive();
        };
        countText = [](const int total, const int active) {
            //: Editor status bar. %n is how many aliases there are in total, %1 how many of those are turned on.
            return tr("%n alias(es) - %1 active", "", total).arg(active);
        };
        break;
    case EditorViewType::cmTimerView:
        pBaseItem = mpTimerBaseItem;
        wantedOn = [](Host* pHost, const int id) {
            TTimer* pT = pHost->getTimerUnit()->getTimer(id);
            return pT && pT->shouldBeActive();
        };
        countText = [](const int total, const int active) {
            //: Editor status bar. %n is how many timers there are in total, %1 how many of those are turned on.
            return tr("%n timer(s) - %1 active", "", total).arg(active);
        };
        break;
    case EditorViewType::cmScriptView:
        pBaseItem = mpScriptsBaseItem;
        wantedOn = [](Host* pHost, const int id) {
            TScript* pT = pHost->getScriptUnit()->getScript(id);
            return pT && pT->shouldBeActive();
        };
        countText = [](const int total, const int active) {
            //: Editor status bar. %n is how many scripts there are in total, %1 how many of those are turned on.
            return tr("%n script(s) - %1 active", "", total).arg(active);
        };
        break;
    case EditorViewType::cmActionView:
        pBaseItem = mpActionBaseItem;
        wantedOn = [](Host* pHost, const int id) {
            TAction* pT = pHost->getActionUnit()->getAction(id);
            return pT && pT->shouldBeActive();
        };
        countText = [](const int total, const int active) {
            //: Editor status bar. %n is how many buttons, menus and toolbars there are in total, %1 how many of those are turned on.
            return tr("%n button(s) - %1 active", "", total).arg(active);
        };
        break;
    case EditorViewType::cmKeysView:
        pBaseItem = mpKeyBaseItem;
        wantedOn = [](Host* pHost, const int id) {
            TKey* pT = pHost->getKeyUnit()->getKey(id);
            return pT && pT->shouldBeActive();
        };
        countText = [](const int total, const int active) {
            //: Editor status bar. %n is how many keybindings there are in total, %1 how many of those are turned on.
            return tr("%n key(s) - %1 active", "", total).arg(active);
        };
        break;
    default:
        // A variable is neither counted nor activated, and the unknown view has
        // no tree to count at all
        break;
    }
    if (!pBaseItem || mpHost.isNull()) {
        mpLabel_statusCounts->clear();
        return;
    }

    int total = 0;
    int active = 0;
    QList<QTreeWidgetItem*> pending;
    for (int i = 0, last = pBaseItem->childCount(); i < last; ++i) {
        pending.append(pBaseItem->child(i));
    }
    while (!pending.isEmpty()) {
        QTreeWidgetItem* pItem = pending.takeLast();
        for (int i = 0, last = pItem->childCount(); i < last; ++i) {
            pending.append(pItem->child(i));
        }
        ++total;
        // What the user switched on, rather than what is running right now: a
        // trigger inside a closed filter chain is off through no choice of theirs
        if (wantedOn(mpHost, pItem->data(0, Qt::UserRole).toInt())) {
            ++active;
        }
    }

    mpLabel_statusCounts->setText(countText(total, active));
}

// One glyph in two colours rather than an arrow pointing two ways: what the
// button opens is named beside it, so the picture only has to say which control
// it is and whether it is on.
void dlgTriggerEditor::updateExtraControlsToggleIcon()
{
    const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
    const QPixmap source(qsl(":/icons/editor-options.png"));
    QIcon icon;
    icon.addPixmap(uiDesign::tintedGlyph(source, tokens.mutedText), QIcon::Normal, QIcon::Off);
    icon.addPixmap(uiDesign::tintedGlyph(source, tokens.accentText), QIcon::Normal, QIcon::On);
    mpTriggersMainArea->toolButton_toggleExtraControls->setIcon(icon);

    // The strip that stands in for the options is led by the same glyph, so that
    // the row and the button it reopens are read as the one thing
    if (mpButton_triggerOptionsSummary) {
        mpButton_triggerOptionsSummary->setIcon(icon);
    }
}

// In case the profile was reset while the editor was out of focus, checks for any script loading errors and displays them
void dlgTriggerEditor::changeEvent(QEvent* e)
{
    QMainWindow::changeEvent(e);

    // Each of these moves what a sidebar row measures: its name, the font it is
    // drawn in, or the metrics the style hands out for it - and the mode the
    // measurement decides is re-taken here rather than left to the next resize,
    // as the restyle below is now free to skip
    if (e->type() == QEvent::LanguageChange || e->type() == QEvent::StyleChange || e->type() == QEvent::FontChange) {
        invalidateEditorSidebarWidths();
        updateEditorSidebarMode();
        // The same three move the widest of the pattern type names, which is
        // what a row's type column is held to
        mPatternTypeColumnWidth = 0;
    }

    // A search result's row height is measured off the font the results are
    // drawn in, which the restyle below is not told about: it runs on the
    // colours changing
    if (e->type() == QEvent::FontChange && mpSearchResultDelegate) {
        mpSearchResultDelegate->restyle();
    }

    // the appearance can be switched between light and dark while the editor is
    // open; restyleEditorIcons() is where every tinted glyph is redone, the
    // trigger form's Options button among them
    if ((e->type() == QEvent::StyleChange || e->type() == QEvent::PaletteChange) && mpTriggersMainArea) {
        applyEditorShellStyle();
    }

    if (e->type() == QEvent::ActivationChange && this->isActiveWindow()) {
        if (mCurrentView == EditorViewType::cmScriptView) {
            auto scriptTreeWidgetItem = treeWidget_scripts->currentItem();
            if (!scriptTreeWidgetItem) {
                return;
            }

            TScript* script = mpHost->getScriptUnit()->getScript(scriptTreeWidgetItem->data(0, Qt::UserRole).toInt());
            if (!script) {
                return;
            }
            if (auto error = script->getLoadingError(); error) {
                showWarning(tr("While loading the profile, this script had an error that has since been fixed, "
                               "possibly by another script. The error was:%2%3")
                                    .arg(qsl("<br>"), error.value()));
            }
        }
    }
}

void dlgTriggerEditor::showIDLabels(const bool visible)
{
    mpAliasMainArea->frameId->setVisible(visible);
    mpActionsMainArea->frameId->setVisible(visible);
    mpKeysMainArea->frameId->setVisible(visible);
    mpScriptsMainArea->frameId->setVisible(visible);
    mpTimersMainArea->frameId->setVisible(visible);
    mpTriggersMainArea->frameId->setVisible(visible);
}

void dlgTriggerEditor::checkForMoreThanOneTriggerItem()
{
    int activeItems = 0;
    if (!mpWidget_triggerItems || !mpWidget_triggerItems->layout()) {
        return;
    }
    auto pLayout = mpWidget_triggerItems->layout();
    for (qsizetype i = 0, total = pLayout->count(); i < total; ++i) {
        auto pLayoutItem = pLayout->itemAt(i)->widget();
        if (pLayoutItem) {
            auto* psingleLineTextEdit_pattern = pLayoutItem->findChild<SingleLineTextEdit*>(qsl("singleLineTextEdit_pattern"));
            auto* pComboBox_type = pLayoutItem->findChild<QComboBox*>(qsl("comboBox_patternType"));
            if (pComboBox_type && (pComboBox_type->currentIndex() == REGEX_PROMPT || pComboBox_type->currentIndex() == REGEX_LINE_SPACER)) {
                // These automatically counts as an active item - though if there
                // isn't any GA signals the first won't work...
                ++activeItems;
            } else {
                if (psingleLineTextEdit_pattern && !psingleLineTextEdit_pattern->toPlainText().isEmpty()) {
                    ++activeItems;
                }
            }
        }
    }

    // The hidden group box is where the mode is still saved from; the radio pair
    // that shows it is the one thing there is to switch off
    const bool canCombinePatterns = activeItems > 1;
    if (mpWidget_matchModeRows) {
        mpWidget_matchModeRows->setEnabled(canCombinePatterns);
    }
    // ...and with nothing to combine yet, the caption saying so takes the place
    // of a pair of greyed-out rows with no explanation
    if (mpLabel_matchModeHint) {
        mpLabel_matchModeHint->setVisible(!canCombinePatterns);
    }
}

void dlgTriggerEditor::setDisplayFont(const QFont& newFont)
{
    if (mpErrorConsole) {
        mpErrorConsole->setFont(newFont);
    }

    auto config = mpSourceEditorEdbee->config();
    config->beginChanges();
    config->setFont(newFont);
    config->endChanges();

    // A pattern is read in the display font too, and every measurement a row is
    // built from - the number column's width, the row's height, what its
    // controls are capped at - comes off that font's metrics
    for (auto* patternEdit : std::as_const(mTriggerPatternEdit)) {
        applyPatternWidgetStyle(patternEdit);
    }
}

void dlgTriggerEditor::slot_bannerDismissClicked()
{
    handleBannerDismiss();
}

// Helper function to find a tree item by its ID recursively
QTreeWidgetItem* findItemByID(QTreeWidgetItem* parent, int itemID)
{
    if (!parent) {
        return nullptr;
    }

    // Check if this item matches
    if (parent->data(0, Qt::UserRole).toInt() == itemID) {
        return parent;
    }

    // Recursively search children
    for (int i = 0; i < parent->childCount(); ++i) {
        QTreeWidgetItem* found = findItemByID(parent->child(i), itemID);
        if (found) {
            return found;
        }
    }

    return nullptr;
}

// Helper function to find a nearby item when a deleted item is not found
// Tries to select: 1) sibling above (at position-1), or 2) parent
QTreeWidgetItem* findNearbyItem(QTreeWidgetItem* rootItem, int parentID, int positionInParent)
{
    if (!rootItem) {
        return nullptr;
    }

    // Find the parent item
    QTreeWidgetItem* parentItem = findItemByID(rootItem, parentID);
    if (!parentItem) {
        return nullptr;
    }

    // Try to select sibling above (at position - 1)
    if (positionInParent > 0 && parentItem->childCount() >= positionInParent) {
        // Position is 0-indexed, so position-1 is the item that was above the deleted one
        QTreeWidgetItem* siblingAbove = parentItem->child(positionInParent - 1);
        if (siblingAbove) {
            return siblingAbove;
        }
    }

    // No sibling above, return the parent
    return parentItem;
}

// Helper function to collect IDs of all expanded items in a tree
QSet<int> collectExpandedItemIDs(QTreeWidgetItem* parent)
{
    QSet<int> expandedIDs;
    if (!parent) {
        return expandedIDs;
    }

    // If this item is expanded, record its ID
    if (parent->isExpanded()) {
        int itemID = parent->data(0, Qt::UserRole).toInt();
        if (itemID > 0) { // Valid ID
            expandedIDs.insert(itemID);
        }
    }

    // Recursively collect from children
    for (int i = 0; i < parent->childCount(); ++i) {
        expandedIDs.unite(collectExpandedItemIDs(parent->child(i)));
    }

    return expandedIDs;
}

// Helper function to restore expansion state based on saved IDs
void restoreExpansionState(QTreeWidgetItem* parent, const QSet<int>& expandedIDs)
{
    if (!parent) {
        return;
    }

    // Check if this item should be expanded
    int itemID = parent->data(0, Qt::UserRole).toInt();
    if (itemID > 0 && expandedIDs.contains(itemID)) {
        parent->setExpanded(true);
    }

    // Recursively restore for children
    for (int i = 0; i < parent->childCount(); ++i) {
        restoreExpansionState(parent->child(i), expandedIDs);
    }
}

void dlgTriggerEditor::slot_itemsChanged(EditorViewType viewType, QList<int> affectedItemIDs)
{
    if (mCurrentView != viewType) {
        switch (viewType) {
        case EditorViewType::cmTriggerView:
            slot_showTriggers();
            break;
        case EditorViewType::cmAliasView:
            slot_showAliases();
            break;
        case EditorViewType::cmTimerView:
            slot_showTimers();
            break;
        case EditorViewType::cmScriptView:
            slot_showScripts();
            break;
        case EditorViewType::cmKeysView:
            slot_showKeys();
            break;
        case EditorViewType::cmActionView:
            slot_showActions();
            break;
        default:
            break;
        }
    }

    switch (viewType) {
    case EditorViewType::cmTriggerView: {
        // Clear the current item pointer to avoid use-after-free
        mpCurrentTriggerItem = nullptr;

        QSet<int> expandedIDs = collectExpandedItemIDs(mpTriggerBaseItem);

        // Block signals on the selection model to prevent it from emitting during tree deletion
        // This prevents slot_triggerSelected from being called with dangling pointers
        QItemSelectionModel* selModel = treeWidget_triggers->selectionModel();
        selModel->blockSignals(true);

        QList<QTreeWidgetItem*> children = mpTriggerBaseItem->takeChildren();
        qDeleteAll(children);

        selModel->blockSignals(false);

        populateTriggers();

        // Temporarily disable animation for instant expansion (looks better for undo/redo)
        // Must be disabled before scrollToItem() which auto-expands parents
        bool wasAnimated = treeWidget_triggers->isAnimated();
        treeWidget_triggers->setAnimated(false);

        mpTriggerBaseItem->setExpanded(true);
        restoreExpansionState(mpTriggerBaseItem, expandedIDs);

        if (!affectedItemIDs.isEmpty()) {
            QTreeWidgetItem* itemToSelect = findItemByID(mpTriggerBaseItem, affectedItemIDs.first());
            if (itemToSelect) {
                // Block signals on the selection model to prevent premature selection change cascades
                // Note: Must block on selectionModel(), not the widget itself, as the signal originates from QItemSelectionModel
                QItemSelectionModel* selModel = treeWidget_triggers->selectionModel();
                selModel->blockSignals(true);
                treeWidget_triggers->setCurrentItem(itemToSelect);
                selModel->blockSignals(false);
                treeWidget_triggers->scrollToItem(itemToSelect);
                slot_triggerSelected(itemToSelect);
            } else {
                // Item not found (was deleted) - try to select a nearby item
                QTreeWidgetItem* nearbyItem = nullptr;

                // Query the undo stack for deleted item info
                if (mpUndoStack) {
                    const QUndoCommand* lastCmd = mpUndoStack->getLastExecutedCommand();
                    if (auto* deleteCmd = dynamic_cast<const EditorDeleteItemCommand*>(lastCmd)) {
                        const auto* deletedInfo = deleteCmd->getDeletedItemInfo(affectedItemIDs.first());
                        if (deletedInfo) {
                            nearbyItem = findNearbyItem(mpTriggerBaseItem, deletedInfo->parentID, deletedInfo->positionInParent);
                        }
                    } else if (auto* addCmd = dynamic_cast<const EditorAddItemCommand*>(lastCmd)) {
                        // Item was deleted via Add undo
                        nearbyItem = findNearbyItem(mpTriggerBaseItem, addCmd->getParentID(), addCmd->getPositionInParent());
                    }
                }

                // If no nearby item found, select the top-level "Triggers" item
                if (!nearbyItem) {
                    nearbyItem = mpTriggerBaseItem;
                }

                if (nearbyItem) {
                    QItemSelectionModel* selModel = treeWidget_triggers->selectionModel();
                    selModel->blockSignals(true);
                    treeWidget_triggers->setCurrentItem(nearbyItem);
                    selModel->blockSignals(false);
                    treeWidget_triggers->scrollToItem(nearbyItem);
                    slot_triggerSelected(nearbyItem);
                }
            }
        } else {
            for (auto* patternEdit : std::as_const(mTriggerPatternEdit)) {
                patternEdit->singleLineTextEdit_pattern->clear();
                if (patternEdit->singleLineTextEdit_pattern->isHidden()) {
                    patternEdit->singleLineTextEdit_pattern->show();
                }
                patternEdit->pushButton_fgColor->hide();
                patternEdit->pushButton_bgColor->hide();
                patternEdit->label_prompt->hide();
                patternEdit->spinBox_lineSpacer->hide();
                patternEdit->comboBox_patternType->setCurrentIndex(1);
                patternEdit->comboBox_patternType->setCurrentIndex(0);
            }

            mpTriggersMainArea->lineEdit_trigger_name->clear();
            mpTriggersMainArea->label_idNumber->clear();
            clearDocument(mpSourceEditorEdbee);
            mpTriggersMainArea->lineEdit_trigger_command->clear();
        }

        treeWidget_triggers->setAnimated(wasAnimated);
        break;
    }
    case EditorViewType::cmTimerView: {
        mpCurrentTimerItem = nullptr;

        QSet<int> expandedIDs = collectExpandedItemIDs(mpTimerBaseItem);

        // Block signals on the selection model to prevent it from emitting during tree deletion
        // This prevents slot_timerSelected from being called with dangling pointers
        QItemSelectionModel* selModel = treeWidget_timers->selectionModel();
        selModel->blockSignals(true);

        QList<QTreeWidgetItem*> children = mpTimerBaseItem->takeChildren();
        qDeleteAll(children);

        selModel->blockSignals(false);

        populateTimers();

        // Temporarily disable animation for instant expansion (looks better for undo/redo)
        bool wasAnimated = treeWidget_timers->isAnimated();
        treeWidget_timers->setAnimated(false);

        mpTimerBaseItem->setExpanded(true);
        restoreExpansionState(mpTimerBaseItem, expandedIDs);

        if (!affectedItemIDs.isEmpty()) {
            QTreeWidgetItem* itemToSelect = findItemByID(mpTimerBaseItem, affectedItemIDs.first());
            if (itemToSelect) {
                // Block signals on selection model to prevent premature selection change cascades
                QItemSelectionModel* selModel = treeWidget_timers->selectionModel();
                selModel->blockSignals(true);
                treeWidget_timers->setCurrentItem(itemToSelect);
                selModel->blockSignals(false);
                treeWidget_timers->scrollToItem(itemToSelect);
                slot_timerSelected(itemToSelect);
            } else {
                // Item not found (was deleted) - try to select a nearby item
                QTreeWidgetItem* nearbyItem = nullptr;

                // Query the undo stack for deleted item info
                if (mpUndoStack) {
                    const QUndoCommand* lastCmd = mpUndoStack->getLastExecutedCommand();
                    if (auto* deleteCmd = dynamic_cast<const EditorDeleteItemCommand*>(lastCmd)) {
                        const auto* deletedInfo = deleteCmd->getDeletedItemInfo(affectedItemIDs.first());
                        if (deletedInfo) {
                            nearbyItem = findNearbyItem(mpTimerBaseItem, deletedInfo->parentID, deletedInfo->positionInParent);
                        }
                    } else if (auto* addCmd = dynamic_cast<const EditorAddItemCommand*>(lastCmd)) {
                        // Item was deleted via Add undo
                        nearbyItem = findNearbyItem(mpTimerBaseItem, addCmd->getParentID(), addCmd->getPositionInParent());
                    }
                }

                // If no nearby item found, select the top-level "Timers" item
                if (!nearbyItem) {
                    nearbyItem = mpTimerBaseItem;
                }

                if (nearbyItem) {
                    QItemSelectionModel* selModel = treeWidget_timers->selectionModel();
                    selModel->blockSignals(true);
                    treeWidget_timers->setCurrentItem(nearbyItem);
                    selModel->blockSignals(false);
                    treeWidget_timers->scrollToItem(nearbyItem);
                    slot_timerSelected(nearbyItem);
                }
            }
        }

        treeWidget_timers->setAnimated(wasAnimated);
        break;
    }
    case EditorViewType::cmAliasView: {
        mpCurrentAliasItem = nullptr;

        QSet<int> expandedIDs = collectExpandedItemIDs(mpAliasBaseItem);

        // Block signals on the selection model to prevent it from emitting during tree deletion
        // This prevents slot_aliasSelected from being called with dangling pointers
        QItemSelectionModel* selModel = treeWidget_aliases->selectionModel();
        selModel->blockSignals(true);

        QList<QTreeWidgetItem*> children = mpAliasBaseItem->takeChildren();
        qDeleteAll(children);

        selModel->blockSignals(false);

        populateAliases();

        // Temporarily disable animation for instant expansion (looks better for undo/redo)
        bool wasAnimated = treeWidget_aliases->isAnimated();
        treeWidget_aliases->setAnimated(false);

        mpAliasBaseItem->setExpanded(true);
        restoreExpansionState(mpAliasBaseItem, expandedIDs);

        if (!affectedItemIDs.isEmpty()) {
            QTreeWidgetItem* itemToSelect = findItemByID(mpAliasBaseItem, affectedItemIDs.first());
            if (itemToSelect) {
                // Block signals on selection model to prevent premature selection change cascades
                QItemSelectionModel* selModel = treeWidget_aliases->selectionModel();
                selModel->blockSignals(true);
                treeWidget_aliases->setCurrentItem(itemToSelect);
                selModel->blockSignals(false);
                treeWidget_aliases->scrollToItem(itemToSelect);
                slot_aliasSelected(itemToSelect);
            } else {
                // Item not found (was deleted) - try to select a nearby item
                QTreeWidgetItem* nearbyItem = nullptr;

                // Query the undo stack for deleted item info
                if (mpUndoStack) {
                    const QUndoCommand* lastCmd = mpUndoStack->getLastExecutedCommand();
                    if (auto* deleteCmd = dynamic_cast<const EditorDeleteItemCommand*>(lastCmd)) {
                        const auto* deletedInfo = deleteCmd->getDeletedItemInfo(affectedItemIDs.first());
                        if (deletedInfo) {
                            nearbyItem = findNearbyItem(mpAliasBaseItem, deletedInfo->parentID, deletedInfo->positionInParent);
                        }
                    } else if (auto* addCmd = dynamic_cast<const EditorAddItemCommand*>(lastCmd)) {
                        // Item was deleted via Add undo
                        nearbyItem = findNearbyItem(mpAliasBaseItem, addCmd->getParentID(), addCmd->getPositionInParent());
                    }
                }

                // If no nearby item found, select the top-level "Aliases" item
                if (!nearbyItem) {
                    nearbyItem = mpAliasBaseItem;
                }

                if (nearbyItem) {
                    QItemSelectionModel* selModel = treeWidget_aliases->selectionModel();
                    selModel->blockSignals(true);
                    treeWidget_aliases->setCurrentItem(nearbyItem);
                    selModel->blockSignals(false);
                    treeWidget_aliases->scrollToItem(nearbyItem);
                    slot_aliasSelected(nearbyItem);
                }
            }
        }

        treeWidget_aliases->setAnimated(wasAnimated);
        break;
    }
    case EditorViewType::cmScriptView: {
        mpCurrentScriptItem = nullptr;

        QSet<int> expandedIDs = collectExpandedItemIDs(mpScriptsBaseItem);

        // Block signals on the selection model to prevent it from emitting during tree deletion
        // This prevents slot_scriptsSelected from being called with dangling pointers
        QItemSelectionModel* selModel = treeWidget_scripts->selectionModel();
        selModel->blockSignals(true);

        QList<QTreeWidgetItem*> children = mpScriptsBaseItem->takeChildren();
        qDeleteAll(children);

        selModel->blockSignals(false);

        populateScripts();

        // Temporarily disable animation for instant expansion (looks better for undo/redo)
        bool wasAnimated = treeWidget_scripts->isAnimated();
        treeWidget_scripts->setAnimated(false);

        mpScriptsBaseItem->setExpanded(true);
        restoreExpansionState(mpScriptsBaseItem, expandedIDs);

        if (!affectedItemIDs.isEmpty()) {
            QTreeWidgetItem* itemToSelect = findItemByID(mpScriptsBaseItem, affectedItemIDs.first());
            if (itemToSelect) {
                // Block signals on selection model to prevent premature selection change cascades
                QItemSelectionModel* selModel = treeWidget_scripts->selectionModel();
                selModel->blockSignals(true);
                treeWidget_scripts->setCurrentItem(itemToSelect);
                selModel->blockSignals(false);
                treeWidget_scripts->scrollToItem(itemToSelect);
                slot_scriptsSelected(itemToSelect);
            } else {
                // Item not found (was deleted) - try to select a nearby item
                QTreeWidgetItem* nearbyItem = nullptr;

                // Query the undo stack for deleted item info
                if (mpUndoStack) {
                    const QUndoCommand* lastCmd = mpUndoStack->getLastExecutedCommand();
                    if (auto* deleteCmd = dynamic_cast<const EditorDeleteItemCommand*>(lastCmd)) {
                        const auto* deletedInfo = deleteCmd->getDeletedItemInfo(affectedItemIDs.first());
                        if (deletedInfo) {
                            nearbyItem = findNearbyItem(mpScriptsBaseItem, deletedInfo->parentID, deletedInfo->positionInParent);
                        }
                    } else if (auto* addCmd = dynamic_cast<const EditorAddItemCommand*>(lastCmd)) {
                        // Item was deleted via Add undo
                        nearbyItem = findNearbyItem(mpScriptsBaseItem, addCmd->getParentID(), addCmd->getPositionInParent());
                    }
                }

                // If no nearby item found, select the top-level "Scripts" item
                if (!nearbyItem) {
                    nearbyItem = mpScriptsBaseItem;
                }

                if (nearbyItem) {
                    QItemSelectionModel* selModel = treeWidget_scripts->selectionModel();
                    selModel->blockSignals(true);
                    treeWidget_scripts->setCurrentItem(nearbyItem);
                    selModel->blockSignals(false);
                    treeWidget_scripts->scrollToItem(nearbyItem);
                    slot_scriptsSelected(nearbyItem);
                }
            }
        }

        treeWidget_scripts->setAnimated(wasAnimated);
        break;
    }
    case EditorViewType::cmActionView: {
        mpCurrentActionItem = nullptr;

        QSet<int> expandedIDs = collectExpandedItemIDs(mpActionBaseItem);

        // Block signals on the selection model to prevent it from emitting during tree deletion
        // This prevents slot_actionSelected from being called with dangling pointers
        QItemSelectionModel* selModel = treeWidget_actions->selectionModel();
        selModel->blockSignals(true);

        QList<QTreeWidgetItem*> children = mpActionBaseItem->takeChildren();
        qDeleteAll(children);

        selModel->blockSignals(false);

        populateActions();

        // Temporarily disable animation for instant expansion (looks better for undo/redo)
        bool wasAnimated = treeWidget_actions->isAnimated();
        treeWidget_actions->setAnimated(false);

        mpActionBaseItem->setExpanded(true);
        restoreExpansionState(mpActionBaseItem, expandedIDs);

        if (!affectedItemIDs.isEmpty()) {
            QTreeWidgetItem* itemToSelect = findItemByID(mpActionBaseItem, affectedItemIDs.first());
            if (itemToSelect) {
                // Block signals on selection model to prevent premature selection change cascades
                QItemSelectionModel* selModel = treeWidget_actions->selectionModel();
                selModel->blockSignals(true);
                treeWidget_actions->setCurrentItem(itemToSelect);
                selModel->blockSignals(false);
                treeWidget_actions->scrollToItem(itemToSelect);
                slot_actionSelected(itemToSelect);
            } else {
                // Item not found (was deleted) - try to select a nearby item
                QTreeWidgetItem* nearbyItem = nullptr;

                // Query the undo stack for deleted item info
                if (mpUndoStack) {
                    const QUndoCommand* lastCmd = mpUndoStack->getLastExecutedCommand();
                    if (auto* deleteCmd = dynamic_cast<const EditorDeleteItemCommand*>(lastCmd)) {
                        const auto* deletedInfo = deleteCmd->getDeletedItemInfo(affectedItemIDs.first());
                        if (deletedInfo) {
                            nearbyItem = findNearbyItem(mpActionBaseItem, deletedInfo->parentID, deletedInfo->positionInParent);
                        }
                    } else if (auto* addCmd = dynamic_cast<const EditorAddItemCommand*>(lastCmd)) {
                        // Item was deleted via Add undo
                        nearbyItem = findNearbyItem(mpActionBaseItem, addCmd->getParentID(), addCmd->getPositionInParent());
                    }
                }

                // If no nearby item found, select the top-level "Actions" item
                if (!nearbyItem) {
                    nearbyItem = mpActionBaseItem;
                }

                if (nearbyItem) {
                    QItemSelectionModel* selModel = treeWidget_actions->selectionModel();
                    selModel->blockSignals(true);
                    treeWidget_actions->setCurrentItem(nearbyItem);
                    selModel->blockSignals(false);
                    treeWidget_actions->scrollToItem(nearbyItem);
                    slot_actionSelected(nearbyItem);
                }
            }
        }

        treeWidget_actions->setAnimated(wasAnimated);
        break;
    }
    case EditorViewType::cmKeysView: {
        mpCurrentKeyItem = nullptr;

        QSet<int> expandedIDs = collectExpandedItemIDs(mpKeyBaseItem);

        // Block signals on the selection model to prevent it from emitting during tree deletion
        // This prevents slot_keySelected from being called with dangling pointers
        QItemSelectionModel* selModel = treeWidget_keys->selectionModel();
        selModel->blockSignals(true);

        QList<QTreeWidgetItem*> children = mpKeyBaseItem->takeChildren();
        qDeleteAll(children);

        selModel->blockSignals(false);

        populateKeys();

        // Temporarily disable animation for instant expansion (looks better for undo/redo)
        bool wasAnimated = treeWidget_keys->isAnimated();
        treeWidget_keys->setAnimated(false);

        mpKeyBaseItem->setExpanded(true);
        restoreExpansionState(mpKeyBaseItem, expandedIDs);

        if (!affectedItemIDs.isEmpty()) {
            QTreeWidgetItem* itemToSelect = findItemByID(mpKeyBaseItem, affectedItemIDs.first());
            if (itemToSelect) {
                // Block signals on selection model to prevent premature selection change cascades
                QItemSelectionModel* selModel = treeWidget_keys->selectionModel();
                selModel->blockSignals(true);
                treeWidget_keys->setCurrentItem(itemToSelect);
                selModel->blockSignals(false);
                treeWidget_keys->scrollToItem(itemToSelect);
                slot_keySelected(itemToSelect);
            } else {
                // Item not found (was deleted) - try to select a nearby item
                QTreeWidgetItem* nearbyItem = nullptr;

                // Query the undo stack for deleted item info
                if (mpUndoStack) {
                    const QUndoCommand* lastCmd = mpUndoStack->getLastExecutedCommand();
                    if (auto* deleteCmd = dynamic_cast<const EditorDeleteItemCommand*>(lastCmd)) {
                        const auto* deletedInfo = deleteCmd->getDeletedItemInfo(affectedItemIDs.first());
                        if (deletedInfo) {
                            nearbyItem = findNearbyItem(mpKeyBaseItem, deletedInfo->parentID, deletedInfo->positionInParent);
                        }
                    } else if (auto* addCmd = dynamic_cast<const EditorAddItemCommand*>(lastCmd)) {
                        // Item was deleted via Add undo
                        nearbyItem = findNearbyItem(mpKeyBaseItem, addCmd->getParentID(), addCmd->getPositionInParent());
                    }
                }

                // If no nearby item found, select the top-level "Keys" item
                if (!nearbyItem) {
                    nearbyItem = mpKeyBaseItem;
                }

                if (nearbyItem) {
                    QItemSelectionModel* selModel = treeWidget_keys->selectionModel();
                    selModel->blockSignals(true);
                    treeWidget_keys->setCurrentItem(nearbyItem);
                    selModel->blockSignals(false);
                    treeWidget_keys->scrollToItem(nearbyItem);
                    slot_keySelected(nearbyItem);
                }
            }
        }

        treeWidget_keys->setAnimated(wasAnimated);
        break;
    }
    default:
        break;
    }
}

void dlgTriggerEditor::handleBannerDismiss()
{
    // With no banner on display the close button was pressed on the "Banner
    // hidden" undo toast itself - just close it instead of treating it as
    // another banner dismissal (which would suppress the whole view's banners
    // and stash the toast text as restorable banner content)
    if (mCurrentBannerKey.isEmpty()) {
        cancelBannerUndoTimer();
        hideSystemMessageArea();
        return;
    }

    mLastDismissedBannerView = mCurrentView;
    mLastDismissedBannerContent = mpSystemMessageArea->notificationAreaMessageBox->text();
    mLastDismissedBannerKey = mCurrentBannerKey;

    const QString settingsKey = bannerSettingsKey(mCurrentView, mCurrentBannerKey);
    if (!settingsKey.isEmpty()) {
        mTemporarilyHiddenBanners.insert(settingsKey);
    }

    hideSystemMessageArea();
    mCurrentBannerKey.clear();
    showBannerUndoToast();
}

void dlgTriggerEditor::cancelBannerUndoTimer()
{
    if (mpBannerUndoTimer) {
        mpBannerUndoTimer->stop();
        mpBannerUndoTimer->deleteLater();
        mpBannerUndoTimer = nullptr;
    }
}

void dlgTriggerEditor::showBannerUndoToast()
{
    cancelBannerUndoTimer();

    mCurrentBannerKey.clear();

    mpBannerUndoTimer = new QTimer(this);
    mpBannerUndoTimer->setSingleShot(true);
    mpBannerUndoTimer->setInterval(std::chrono::seconds(5));

    //: Toast notification shown when user dismisses an editor tip banner. Allows them to undo or permanently hide the tips for this editor view type.
    QString toastMessage = tr("Banner hidden. <a href='undo' style='color: inherit; text-decoration: underline;'>Undo</a> | <a href='hide-permanently' style='color: inherit; text-decoration: "
                              "underline;'>Hide permanently</a>");
    // Fix up the colour here rather than in the tr() text so existing
    // translations stay valid
    toastMessage.replace(qsl("color: inherit"), qsl("color: ") + themedBannerLinkColor());

    mpSystemMessageArea->notificationAreaIconLabelError->hide();
    mpSystemMessageArea->notificationAreaIconLabelWarning->hide();
    mpSystemMessageArea->notificationAreaIconLabelInformation->show();
    mpSystemMessageArea->notificationAreaMessageBox->setText(toastMessage);
    mpSystemMessageArea->show();

    connect(mpBannerUndoTimer, &QTimer::timeout, this, &dlgTriggerEditor::hideSystemMessageArea);
    mpBannerUndoTimer->start();

    disconnect(mpSystemMessageArea->notificationAreaMessageBox, &QLabel::linkActivated, this, &dlgTriggerEditor::slot_clickedMessageBox);
    connect(mpSystemMessageArea->notificationAreaMessageBox, &QLabel::linkActivated, this, [this](const QString& link) {
        if (link == "undo") {
            undoBannerDismiss();
        } else if (link == "hide-permanently") {
            handlePermanentBannerDismiss();
        } else {
            slot_clickedMessageBox(link);
        }
    });
}

// The banner and toast link colours are baked into the message HTML when it is
// built, so swap them for the new theme's colour if the appearance changes
// while a message is still on screen
void dlgTriggerEditor::slot_refreshBannerLinkColors()
{
    const QString freshColor = themedBannerLinkColor();
    const QString staleColor = freshColor == qsl("black") ? qsl("rgb(230, 230, 230)") : qsl("black");
    const QString stale = qsl("color: %1;").arg(staleColor);
    const QString fresh = qsl("color: %1;").arg(freshColor);

    // keep the stored content fresh too, so undoing a dismissal after a theme
    // switch does not restore links in the old theme's colour
    mLastDismissedBannerContent.replace(stale, fresh);

    if (!mpSystemMessageArea) {
        return;
    }
    auto* messageBox = mpSystemMessageArea->notificationAreaMessageBox;
    QString text = messageBox->text();
    if (text.contains(stale)) {
        messageBox->setText(text.replace(stale, fresh));
    }
}

void dlgTriggerEditor::undoBannerDismiss()
{
    cancelBannerUndoTimer();

    const QString settingsKey = bannerSettingsKey(mLastDismissedBannerView, mLastDismissedBannerKey);
    if (!settingsKey.isEmpty()) {
        mTemporarilyHiddenBanners.remove(settingsKey);
    }

    setBannerPermanentlyHidden(mLastDismissedBannerView, mLastDismissedBannerKey, false);

    // Remove the undo toast before restoring the banner so the new content can
    // be shown immediately without being blocked by the active notification.
    if (mpSystemMessageArea) {
        mpSystemMessageArea->hide();
    }
    mCurrentBannerKey.clear();

    if (mLastDismissedBannerView == mCurrentView && !mLastDismissedBannerContent.isEmpty()) {
        showHideableBanner(mLastDismissedBannerContent, mLastDismissedBannerKey);
    }
}


void dlgTriggerEditor::handlePermanentBannerDismiss()
{
    setBannerPermanentlyHidden(mLastDismissedBannerView, mLastDismissedBannerKey, true);
    hideSystemMessageArea();
    mCurrentBannerKey.clear();
}

bool dlgTriggerEditor::bannerPermanentlyHidden(EditorViewType viewType, const QString& bannerKey, bool includeBasePreference)
{
    const QString key = bannerSettingsKey(viewType, bannerKey);
    const QString baseKey = bannerSettingsKey(viewType, QString());
    const QString legacyKey = legacyBannerSettingsKey(viewType, bannerKey);
    const QString legacyBaseKey = legacyBannerSettingsKey(viewType, QString());
    if (key.isEmpty()) {
        return false;
    }

    QSettings* settings = mudlet::getQSettings();
    if (!settings) {
        return false;
    }

    auto migrateLegacyKey = [settings](const QString& newKey, const QString& oldKey) {
        if (newKey.isEmpty() || oldKey.isEmpty() || newKey == oldKey) {
            return;
        }

        const QString oldPath = qsl("Editor/banner_permanently_hidden/%1").arg(oldKey);
        if (!settings->contains(oldPath)) {
            return;
        }

        settings->remove(oldPath);
    };

    migrateLegacyKey(key, legacyKey);
    migrateLegacyKey(baseKey, legacyBaseKey);

    if (includeBasePreference && !bannerKey.isEmpty() && !baseKey.isEmpty()) {
        if (settings->value(qsl("Editor/banner_permanently_hidden/%1").arg(baseKey), false).toBool()) {
            return true;
        }
    }

    return settings->value(qsl("Editor/banner_permanently_hidden/%1").arg(key), false).toBool();
}

void dlgTriggerEditor::setBannerPermanentlyHidden(EditorViewType viewType, const QString& bannerKey, bool hidden)
{
    const QString key = bannerSettingsKey(viewType, bannerKey);
    const QString legacyKey = legacyBannerSettingsKey(viewType, bannerKey);
    if (key.isEmpty()) {
        return;
    }

    QSettings* settings = mudlet::getQSettings();
    settings->setValue(qsl("Editor/banner_permanently_hidden/%1").arg(key), hidden);

    if (!legacyKey.isEmpty() && legacyKey != key) {
        settings->remove(qsl("Editor/banner_permanently_hidden/%1").arg(legacyKey));
    }

    if (!hidden) {
        mTemporarilyHiddenBanners.remove(key);
    }
}

// Helper function for per-property trigger saves
// Creates an undo command for a single property change with time-based merging support
static void
pushTriggerPropertyCommand(EditorUndoStack* undoStack, Host* host, int triggerID, const QString& triggerName, const QString& propertyName, const QString& oldStateXML, const QString& newStateXML)
{
    if (oldStateXML == newStateXML) {
        return; // No change
    }

    auto* cmd = new EditorModifyPropertyCommand(EditorViewType::cmTriggerView, triggerID, triggerName, oldStateXML, newStateXML, host);
    cmd->setPropertyId(qsl("trigger:%1:%2").arg(QString::number(triggerID), propertyName));
    undoStack->pushCommand(cmd);
}

void dlgTriggerEditor::slot_saveProperty_TriggerName()
{
    if (mBlockPropertySave || !mpCurrentTriggerItem) {
        return;
    }

    const int triggerID = mpCurrentTriggerItem->data(0, Qt::UserRole).toInt();
    TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(triggerID);
    if (!pT) {
        return;
    }

    mpTriggersMainArea->trimName();
    const QString newName = mpTriggersMainArea->lineEdit_trigger_name->text();

    // Skip if no actual change
    if (pT->getName() == newName) {
        return;
    }

    QString oldStateXML = exportTriggerToXML(pT);
    pT->setName(newName);
    mpCurrentTriggerItem->setText(0, newName);
    QString newStateXML = exportTriggerToXML(pT);

    pushTriggerPropertyCommand(mpUndoStack, mpHost, triggerID, newName, qsl("name"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_TriggerCommand()
{
    if (mBlockPropertySave || !mpCurrentTriggerItem) {
        return;
    }

    const int triggerID = mpCurrentTriggerItem->data(0, Qt::UserRole).toInt();
    TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(triggerID);
    if (!pT) {
        return;
    }

    const QString newCommand = mpTriggersMainArea->lineEdit_trigger_command->text();

    if (pT->getCommand() == newCommand) {
        return;
    }

    QString oldStateXML = exportTriggerToXML(pT);
    pT->setCommand(newCommand);
    QString newStateXML = exportTriggerToXML(pT);

    pushTriggerPropertyCommand(mpUndoStack, mpHost, triggerID, pT->getName(), qsl("command"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_TriggerStayOpen()
{
    if (mBlockPropertySave || !mpCurrentTriggerItem) {
        return;
    }

    const int triggerID = mpCurrentTriggerItem->data(0, Qt::UserRole).toInt();
    TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(triggerID);
    if (!pT) {
        return;
    }

    const int newValue = mpTriggersMainArea->spinBox_stayOpen->value();

    if (pT->mStayOpen == newValue) {
        return;
    }

    QString oldStateXML = exportTriggerToXML(pT);
    pT->mStayOpen = newValue;
    QString newStateXML = exportTriggerToXML(pT);

    pushTriggerPropertyCommand(mpUndoStack, mpHost, triggerID, pT->getName(), qsl("stayOpen"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_TriggerLineMargin()
{
    if (mBlockPropertySave || !mpCurrentTriggerItem) {
        return;
    }

    const int triggerID = mpCurrentTriggerItem->data(0, Qt::UserRole).toInt();
    TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(triggerID);
    if (!pT) {
        return;
    }

    const int newValue = mpTriggersMainArea->spinBox_lineMargin->value();
    const bool newIsMultiline = newValue >= 0;

    // Check if anything actually changed
    if (pT->isMultiline() == newIsMultiline && (!newIsMultiline || pT->getConditionLineDelta() == newValue)) {
        return;
    }

    QString oldStateXML = exportTriggerToXML(pT);
    if (newValue >= 0) {
        pT->setConditionLineDelta(newValue);
        pT->setIsMultiline(true);
    } else {
        pT->setIsMultiline(false);
    }
    QString newStateXML = exportTriggerToXML(pT);

    pushTriggerPropertyCommand(mpUndoStack, mpHost, triggerID, pT->getName(), qsl("lineMargin"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_TriggerFilterTrigger()
{
    if (mBlockPropertySave || !mpCurrentTriggerItem) {
        return;
    }

    const int triggerID = mpCurrentTriggerItem->data(0, Qt::UserRole).toInt();
    TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(triggerID);
    if (!pT) {
        return;
    }

    const bool newValue = mpTriggersMainArea->checkBox_filterTrigger->isChecked();

    if (pT->mFilterTrigger == newValue) {
        return;
    }

    QString oldStateXML = exportTriggerToXML(pT);
    pT->mFilterTrigger = newValue;
    QString newStateXML = exportTriggerToXML(pT);

    pushTriggerPropertyCommand(mpUndoStack, mpHost, triggerID, pT->getName(), qsl("filterTrigger"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_TriggerPerlSlashG()
{
    if (mBlockPropertySave || !mpCurrentTriggerItem) {
        return;
    }

    const int triggerID = mpCurrentTriggerItem->data(0, Qt::UserRole).toInt();
    TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(triggerID);
    if (!pT) {
        return;
    }

    const bool newValue = mpTriggersMainArea->checkBox_perlSlashGOption->isChecked();

    if (pT->mPerlSlashGOption == newValue) {
        return;
    }

    QString oldStateXML = exportTriggerToXML(pT);
    pT->mPerlSlashGOption = newValue;
    QString newStateXML = exportTriggerToXML(pT);

    pushTriggerPropertyCommand(mpUndoStack, mpHost, triggerID, pT->getName(), qsl("perlSlashG"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_TriggerSoundEnabled()
{
    if (mBlockPropertySave || !mpCurrentTriggerItem) {
        return;
    }

    const int triggerID = mpCurrentTriggerItem->data(0, Qt::UserRole).toInt();
    TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(triggerID);
    if (!pT) {
        return;
    }

    const bool newValue = mpTriggersMainArea->groupBox_soundTrigger->isChecked();

    if (pT->mSoundTrigger == newValue) {
        return;
    }

    QString oldStateXML = exportTriggerToXML(pT);
    pT->mSoundTrigger = newValue;
    QString newStateXML = exportTriggerToXML(pT);

    pushTriggerPropertyCommand(mpUndoStack, mpHost, triggerID, pT->getName(), qsl("soundEnabled"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_TriggerSoundFile()
{
    if (mBlockPropertySave || !mpCurrentTriggerItem) {
        return;
    }

    const int triggerID = mpCurrentTriggerItem->data(0, Qt::UserRole).toInt();
    TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(triggerID);
    if (!pT) {
        return;
    }

    const QString newValue = mpTriggersMainArea->lineEdit_soundFile->text();

    if (pT->mSoundFile == newValue) {
        return;
    }

    QString oldStateXML = exportTriggerToXML(pT);
    pT->setSound(newValue);
    QString newStateXML = exportTriggerToXML(pT);

    pushTriggerPropertyCommand(mpUndoStack, mpHost, triggerID, pT->getName(), qsl("soundFile"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_TriggerColorizer()
{
    if (mBlockPropertySave || !mpCurrentTriggerItem) {
        return;
    }

    const int triggerID = mpCurrentTriggerItem->data(0, Qt::UserRole).toInt();
    TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(triggerID);
    if (!pT) {
        return;
    }

    const bool newValue = mpTriggersMainArea->groupBox_triggerColorizer->isChecked();

    if (pT->isColorizerTrigger() == newValue) {
        return;
    }

    QString oldStateXML = exportTriggerToXML(pT);
    pT->setIsColorizerTrigger(newValue);
    QString newStateXML = exportTriggerToXML(pT);

    pushTriggerPropertyCommand(mpUndoStack, mpHost, triggerID, pT->getName(), qsl("colorizer"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_TriggerPattern(int patternIndex)
{
    if (mBlockPropertySave || !mpCurrentTriggerItem) {
        return;
    }

    const int triggerID = mpCurrentTriggerItem->data(0, Qt::UserRole).toInt();
    TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(triggerID);
    if (!pT) {
        return;
    }

    // This slot would need to capture all patterns and update them
    // For now, patterns are handled by saveTrigger()
    Q_UNUSED(patternIndex);
}

void dlgTriggerEditor::slot_saveProperty_TriggerPatternType(int patternIndex)
{
    if (mBlockPropertySave || !mpCurrentTriggerItem) {
        return;
    }

    const int triggerID = mpCurrentTriggerItem->data(0, Qt::UserRole).toInt();
    TTrigger* pT = mpHost->getTriggerUnit()->getTrigger(triggerID);
    if (!pT) {
        return;
    }

    // This slot would need to capture all patterns and update them
    // For now, patterns are handled by saveTrigger()
    Q_UNUSED(patternIndex);
}

// =============================================================================
// Alias Per-Property Save Slots
// =============================================================================

// Helper function for per-property alias saves
static void pushAliasPropertyCommand(EditorUndoStack* undoStack, Host* host, int aliasID, const QString& aliasName, const QString& propertyName, const QString& oldStateXML, const QString& newStateXML)
{
    if (oldStateXML == newStateXML) {
        return;
    }

    auto* cmd = new EditorModifyPropertyCommand(EditorViewType::cmAliasView, aliasID, aliasName, oldStateXML, newStateXML, host);
    cmd->setPropertyId(qsl("alias:%1:%2").arg(QString::number(aliasID), propertyName));
    undoStack->pushCommand(cmd);
}

void dlgTriggerEditor::slot_saveProperty_AliasName()
{
    if (mBlockPropertySave || !mpCurrentAliasItem) {
        return;
    }

    const int aliasID = mpCurrentAliasItem->data(0, Qt::UserRole).toInt();
    TAlias* pT = mpHost->getAliasUnit()->getAlias(aliasID);
    if (!pT) {
        return;
    }

    mpAliasMainArea->trimName();
    const QString newName = mpAliasMainArea->lineEdit_alias_name->text();

    if (pT->getName() == newName) {
        return;
    }

    QString oldStateXML = exportAliasToXML(pT);
    pT->setName(newName);
    mpCurrentAliasItem->setText(0, newName);
    QString newStateXML = exportAliasToXML(pT);

    pushAliasPropertyCommand(mpUndoStack, mpHost, aliasID, newName, qsl("name"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_AliasPattern()
{
    if (mBlockPropertySave || !mpCurrentAliasItem) {
        return;
    }

    const int aliasID = mpCurrentAliasItem->data(0, Qt::UserRole).toInt();
    TAlias* pT = mpHost->getAliasUnit()->getAlias(aliasID);
    if (!pT) {
        return;
    }

    QString newPattern = mpAliasMainArea->lineEdit_alias_pattern->text();
    unmarkQString(&newPattern);

    if (pT->getRegexCode() == newPattern) {
        return;
    }

    // Mirror the explicit-save guard: refuse a pattern that would make the alias loop.
    const QString substitution = mpAliasMainArea->lineEdit_alias_command->text();
    if (aliasSubstitutionLoops(newPattern, substitution)) {
        showAliasLoopWarning(mpCurrentAliasItem, pT->getName());
        return;
    }

    QString oldStateXML = exportAliasToXML(pT);
    pT->setRegexCode(newPattern);
    QString newStateXML = exportAliasToXML(pT);

    pushAliasPropertyCommand(mpUndoStack, mpHost, aliasID, pT->getName(), qsl("pattern"), oldStateXML, newStateXML);

    // Surface a faulty regex the same way the explicit Save button does.
    applyAliasState(mpCurrentAliasItem, pT);
}

void dlgTriggerEditor::slot_saveProperty_AliasCommand()
{
    if (mBlockPropertySave || !mpCurrentAliasItem) {
        return;
    }

    const int aliasID = mpCurrentAliasItem->data(0, Qt::UserRole).toInt();
    TAlias* pT = mpHost->getAliasUnit()->getAlias(aliasID);
    if (!pT) {
        return;
    }

    const QString newCommand = mpAliasMainArea->lineEdit_alias_command->text();

    if (pT->getCommand() == newCommand) {
        return;
    }

    // Mirror the explicit-save guard: a substitution must not match its own pattern.
    QString regex = mpAliasMainArea->lineEdit_alias_pattern->text();
    unmarkQString(&regex);
    if (aliasSubstitutionLoops(regex, newCommand)) {
        showAliasLoopWarning(mpCurrentAliasItem, pT->getName());
        return;
    }

    QString oldStateXML = exportAliasToXML(pT);
    pT->setCommand(newCommand);
    QString newStateXML = exportAliasToXML(pT);

    pushAliasPropertyCommand(mpUndoStack, mpHost, aliasID, pT->getName(), qsl("command"), oldStateXML, newStateXML);

    // Refresh the item so a fixed alias loses any stale error flag.
    applyAliasState(mpCurrentAliasItem, pT);
}

// =============================================================================
// Timer Per-Property Save Slots
// =============================================================================

// Helper function for per-property timer saves
static void pushTimerPropertyCommand(EditorUndoStack* undoStack, Host* host, int timerID, const QString& timerName, const QString& propertyName, const QString& oldStateXML, const QString& newStateXML)
{
    if (oldStateXML == newStateXML) {
        return;
    }

    auto* cmd = new EditorModifyPropertyCommand(EditorViewType::cmTimerView, timerID, timerName, oldStateXML, newStateXML, host);
    cmd->setPropertyId(qsl("timer:%1:%2").arg(QString::number(timerID), propertyName));
    undoStack->pushCommand(cmd);
}

void dlgTriggerEditor::slot_saveProperty_TimerName()
{
    if (mBlockPropertySave || !mpCurrentTimerItem) {
        return;
    }

    const int timerID = mpCurrentTimerItem->data(0, Qt::UserRole).toInt();
    TTimer* pT = mpHost->getTimerUnit()->getTimer(timerID);
    if (!pT) {
        return;
    }

    mpTimersMainArea->trimName();
    const QString newName = mpTimersMainArea->lineEdit_timer_name->text();

    if (pT->getName() == newName) {
        return;
    }

    QString oldStateXML = exportTimerToXML(pT);
    pT->setName(newName);
    mpCurrentTimerItem->setText(0, newName);
    QString newStateXML = exportTimerToXML(pT);

    pushTimerPropertyCommand(mpUndoStack, mpHost, timerID, newName, qsl("name"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_TimerCommand()
{
    if (mBlockPropertySave || !mpCurrentTimerItem) {
        return;
    }

    const int timerID = mpCurrentTimerItem->data(0, Qt::UserRole).toInt();
    TTimer* pT = mpHost->getTimerUnit()->getTimer(timerID);
    if (!pT) {
        return;
    }

    const QString newCommand = mpTimersMainArea->lineEdit_timer_command->text();

    if (pT->getCommand() == newCommand) {
        return;
    }

    QString oldStateXML = exportTimerToXML(pT);
    pT->setCommand(newCommand);
    QString newStateXML = exportTimerToXML(pT);

    pushTimerPropertyCommand(mpUndoStack, mpHost, timerID, pT->getName(), qsl("command"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_TimerTime()
{
    if (mBlockPropertySave || !mpCurrentTimerItem) {
        return;
    }

    const int timerID = mpCurrentTimerItem->data(0, Qt::UserRole).toInt();
    TTimer* pT = mpHost->getTimerUnit()->getTimer(timerID);
    if (!pT) {
        return;
    }

    const QTime newTime(mpTimersMainArea->timeEdit_timer_hours->time().hour(),
                        mpTimersMainArea->timeEdit_timer_minutes->time().minute(),
                        mpTimersMainArea->timeEdit_timer_seconds->time().second(),
                        mpTimersMainArea->timeEdit_timer_msecs->time().msec());

    if (pT->getTime() == newTime) {
        return;
    }

    QString oldStateXML = exportTimerToXML(pT);
    pT->setTime(newTime);
    QString newStateXML = exportTimerToXML(pT);

    pushTimerPropertyCommand(mpUndoStack, mpHost, timerID, pT->getName(), qsl("time"), oldStateXML, newStateXML);
}

// =============================================================================
// Script Per-Property Save Slots
// =============================================================================

// Helper function for per-property script saves
static void
pushScriptPropertyCommand(EditorUndoStack* undoStack, Host* host, int scriptID, const QString& scriptName, const QString& propertyName, const QString& oldStateXML, const QString& newStateXML)
{
    if (oldStateXML == newStateXML) {
        return;
    }

    auto* cmd = new EditorModifyPropertyCommand(EditorViewType::cmScriptView, scriptID, scriptName, oldStateXML, newStateXML, host);
    cmd->setPropertyId(qsl("script:%1:%2").arg(QString::number(scriptID), propertyName));
    undoStack->pushCommand(cmd);
}

void dlgTriggerEditor::slot_saveProperty_ScriptName()
{
    if (mBlockPropertySave || !mpCurrentScriptItem) {
        return;
    }

    const int scriptID = mpCurrentScriptItem->data(0, Qt::UserRole).toInt();
    TScript* pT = mpHost->getScriptUnit()->getScript(scriptID);
    if (!pT) {
        return;
    }

    mpScriptsMainArea->trimName();
    const QString newName = mpScriptsMainArea->lineEdit_script_name->text();

    if (pT->getName() == newName) {
        return;
    }

    QString oldStateXML = exportScriptToXML(pT);
    pT->setName(newName);
    mpCurrentScriptItem->setText(0, newName);
    QString newStateXML = exportScriptToXML(pT);

    pushScriptPropertyCommand(mpUndoStack, mpHost, scriptID, newName, qsl("name"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_ScriptEventHandlers()
{
    if (mBlockPropertySave || !mpCurrentScriptItem) {
        return;
    }

    const int scriptID = mpCurrentScriptItem->data(0, Qt::UserRole).toInt();
    TScript* pT = mpHost->getScriptUnit()->getScript(scriptID);
    if (!pT) {
        return;
    }

    // Collect event handlers from the list widget
    QStringList newHandlers;
    for (int i = 0; i < mpScriptsMainArea->listWidget_script_registered_event_handlers->count(); ++i) {
        newHandlers << mpScriptsMainArea->listWidget_script_registered_event_handlers->item(i)->text();
    }

    if (pT->getEventHandlerList() == newHandlers) {
        return;
    }

    QString oldStateXML = exportScriptToXML(pT);
    pT->setEventHandlerList(newHandlers);
    QString newStateXML = exportScriptToXML(pT);

    pushScriptPropertyCommand(mpUndoStack, mpHost, scriptID, pT->getName(), qsl("eventHandlers"), oldStateXML, newStateXML);
}

// =============================================================================
// Key Per-Property Save Slots
// =============================================================================

// Helper function for per-property key saves
static void pushKeyPropertyCommand(EditorUndoStack* undoStack, Host* host, int keyID, const QString& keyName, const QString& propertyName, const QString& oldStateXML, const QString& newStateXML)
{
    if (oldStateXML == newStateXML) {
        return;
    }

    auto* cmd = new EditorModifyPropertyCommand(EditorViewType::cmKeysView, keyID, keyName, oldStateXML, newStateXML, host);
    cmd->setPropertyId(qsl("key:%1:%2").arg(QString::number(keyID), propertyName));
    undoStack->pushCommand(cmd);
}

void dlgTriggerEditor::slot_saveProperty_KeyName()
{
    if (mBlockPropertySave || !mpCurrentKeyItem) {
        return;
    }

    const int keyID = mpCurrentKeyItem->data(0, Qt::UserRole).toInt();
    TKey* pT = mpHost->getKeyUnit()->getKey(keyID);
    if (!pT) {
        return;
    }

    mpKeysMainArea->trimName();
    const QString newName = mpKeysMainArea->lineEdit_key_name->text();

    if (pT->getName() == newName) {
        return;
    }

    QString oldStateXML = exportKeyToXML(pT);
    pT->setName(newName);
    mpCurrentKeyItem->setText(0, newName);
    QString newStateXML = exportKeyToXML(pT);

    pushKeyPropertyCommand(mpUndoStack, mpHost, keyID, newName, qsl("name"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_KeyCommand()
{
    if (mBlockPropertySave || !mpCurrentKeyItem) {
        return;
    }

    const int keyID = mpCurrentKeyItem->data(0, Qt::UserRole).toInt();
    TKey* pT = mpHost->getKeyUnit()->getKey(keyID);
    if (!pT) {
        return;
    }

    const QString newCommand = mpKeysMainArea->lineEdit_key_command->text();

    if (pT->getCommand() == newCommand) {
        return;
    }

    QString oldStateXML = exportKeyToXML(pT);
    pT->setCommand(newCommand);
    QString newStateXML = exportKeyToXML(pT);

    pushKeyPropertyCommand(mpUndoStack, mpHost, keyID, pT->getName(), qsl("command"), oldStateXML, newStateXML);
}

// =============================================================================
// Action Per-Property Save Slots
// =============================================================================

// Helper function for per-property action saves
static void
pushActionPropertyCommand(EditorUndoStack* undoStack, Host* host, int actionID, const QString& actionName, const QString& propertyName, const QString& oldStateXML, const QString& newStateXML)
{
    if (oldStateXML == newStateXML) {
        return;
    }

    auto* cmd = new EditorModifyPropertyCommand(EditorViewType::cmActionView, actionID, actionName, oldStateXML, newStateXML, host);
    cmd->setPropertyId(qsl("action:%1:%2").arg(QString::number(actionID), propertyName));
    undoStack->pushCommand(cmd);
}

void dlgTriggerEditor::slot_saveProperty_ActionName()
{
    if (mBlockPropertySave || !mpCurrentActionItem) {
        return;
    }

    const int actionID = mpCurrentActionItem->data(0, Qt::UserRole).toInt();
    TAction* pT = mpHost->getActionUnit()->getAction(actionID);
    if (!pT) {
        return;
    }

    mpActionsMainArea->trimName();
    const QString newName = mpActionsMainArea->lineEdit_action_name->text();

    if (pT->getName() == newName) {
        return;
    }

    QString oldStateXML = exportActionToXML(pT);
    pT->setName(newName);
    mpCurrentActionItem->setText(0, newName);
    QString newStateXML = exportActionToXML(pT);

    pushActionPropertyCommand(mpUndoStack, mpHost, actionID, newName, qsl("name"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_ActionCommandDown()
{
    if (mBlockPropertySave || !mpCurrentActionItem) {
        return;
    }

    const int actionID = mpCurrentActionItem->data(0, Qt::UserRole).toInt();
    TAction* pT = mpHost->getActionUnit()->getAction(actionID);
    if (!pT) {
        return;
    }

    const QString newCommand = mpActionsMainArea->lineEdit_action_button_command_down->text();

    if (pT->getCommandButtonDown() == newCommand) {
        return;
    }

    QString oldStateXML = exportActionToXML(pT);
    pT->setCommandButtonDown(newCommand);
    QString newStateXML = exportActionToXML(pT);

    pushActionPropertyCommand(mpUndoStack, mpHost, actionID, pT->getName(), qsl("commandDown"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_ActionCommandUp()
{
    if (mBlockPropertySave || !mpCurrentActionItem) {
        return;
    }

    const int actionID = mpCurrentActionItem->data(0, Qt::UserRole).toInt();
    TAction* pT = mpHost->getActionUnit()->getAction(actionID);
    if (!pT) {
        return;
    }

    const QString newCommand = mpActionsMainArea->lineEdit_action_button_command_up->text();

    if (pT->getCommandButtonUp() == newCommand) {
        return;
    }

    QString oldStateXML = exportActionToXML(pT);
    pT->setCommandButtonUp(newCommand);
    QString newStateXML = exportActionToXML(pT);

    pushActionPropertyCommand(mpUndoStack, mpHost, actionID, pT->getName(), qsl("commandUp"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_ActionIsPushDown()
{
    if (mBlockPropertySave || !mpCurrentActionItem) {
        return;
    }

    const int actionID = mpCurrentActionItem->data(0, Qt::UserRole).toInt();
    TAction* pT = mpHost->getActionUnit()->getAction(actionID);
    if (!pT) {
        return;
    }

    const bool newValue = mpActionsMainArea->checkBox_action_button_isPushDown->isChecked();

    if (pT->isPushDownButton() == newValue) {
        return;
    }

    QString oldStateXML = exportActionToXML(pT);
    pT->setIsPushDownButton(newValue);
    QString newStateXML = exportActionToXML(pT);

    pushActionPropertyCommand(mpUndoStack, mpHost, actionID, pT->getName(), qsl("isPushDown"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_ActionBarColumns()
{
    if (mBlockPropertySave || !mpCurrentActionItem) {
        return;
    }

    const int actionID = mpCurrentActionItem->data(0, Qt::UserRole).toInt();
    TAction* pT = mpHost->getActionUnit()->getAction(actionID);
    if (!pT) {
        return;
    }

    const int newValue = mpActionsMainArea->spinBox_action_bar_columns->value();

    if (pT->getButtonColumns() == newValue) {
        return;
    }

    QString oldStateXML = exportActionToXML(pT);
    pT->setButtonColumns(newValue);
    QString newStateXML = exportActionToXML(pT);

    pushActionPropertyCommand(mpUndoStack, mpHost, actionID, pT->getName(), qsl("buttonColumn"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_ActionBarFillerOffset()
{
    if (mBlockPropertySave || !mpCurrentActionItem) {
        return;
    }

    const int actionID = mpCurrentActionItem->data(0, Qt::UserRole).toInt();
    TAction* pT = mpHost->getActionUnit()->getAction(actionID);
    if (!pT) {
        return;
    }

    const int newValue = mpActionsMainArea->spinBox_action_bar_offsetToFirstButton->value();

    if (pT->getButtonFillerOffset() == newValue) {
        return;
    }

    QString oldStateXML = exportActionToXML(pT);
    pT->setButtonFillerOffset(newValue);
    QString newStateXML = exportActionToXML(pT);

    pushActionPropertyCommand(mpUndoStack, mpHost, actionID, pT->getName(), qsl("buttonFillerOffset"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_ActionBarOrientation()
{
    if (mBlockPropertySave || !mpCurrentActionItem) {
        return;
    }

    const int actionID = mpCurrentActionItem->data(0, Qt::UserRole).toInt();
    TAction* pT = mpHost->getActionUnit()->getAction(actionID);
    if (!pT) {
        return;
    }

    // 0 = horizontal, 1 = vertical
    const int newValue = mpActionsMainArea->comboBox_action_bar_orientation->currentIndex();

    if (pT->mOrientation == newValue) {
        return;
    }

    QString oldStateXML = exportActionToXML(pT);
    pT->mOrientation = newValue;
    QString newStateXML = exportActionToXML(pT);

    pushActionPropertyCommand(mpUndoStack, mpHost, actionID, pT->getName(), qsl("orientation"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_ActionBarLocation()
{
    if (mBlockPropertySave || !mpCurrentActionItem) {
        return;
    }

    const int actionID = mpCurrentActionItem->data(0, Qt::UserRole).toInt();
    TAction* pT = mpHost->getActionUnit()->getAction(actionID);
    if (!pT) {
        return;
    }

    // CHECKME: This may need the increment if it isn't zero!
    const int newValue = mpActionsMainArea->comboBox_action_bar_location->currentIndex();

    if (pT->mLocation == newValue) {
        return;
    }

    QString oldStateXML = exportActionToXML(pT);
    pT->mLocation = newValue;
    QString newStateXML = exportActionToXML(pT);

    pushActionPropertyCommand(mpUndoStack, mpHost, actionID, pT->getName(), qsl("barLocation"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_ActionButtonRotation()
{
    if (mBlockPropertySave || !mpCurrentActionItem) {
        return;
    }

    const int actionID = mpCurrentActionItem->data(0, Qt::UserRole).toInt();
    TAction* pT = mpHost->getActionUnit()->getAction(actionID);
    if (!pT) {
        return;
    }

    const int newValue = mpActionsMainArea->comboBox_action_button_rotation->currentIndex();

    if (pT->getButtonRotation() == newValue) {
        return;
    }

    QString oldStateXML = exportActionToXML(pT);
    pT->setButtonRotation(newValue);
    QString newStateXML = exportActionToXML(pT);

    pushActionPropertyCommand(mpUndoStack, mpHost, actionID, pT->getName(), qsl("buttonRotation"), oldStateXML, newStateXML);
}

void dlgTriggerEditor::slot_saveProperty_ActionCSS()
{
    if (mBlockPropertySave || !mpCurrentActionItem) {
        return;
    }

    const int actionID = mpCurrentActionItem->data(0, Qt::UserRole).toInt();
    TAction* pT = mpHost->getActionUnit()->getAction(actionID);
    if (!pT) {
        return;
    }

    const QString newCSS = mpActionsMainArea->plainTextEdit_action_css->toPlainText();

    if (pT->css == newCSS) {
        return;
    }

    QString oldStateXML = exportActionToXML(pT);
    pT->css = newCSS;
    QString newStateXML = exportActionToXML(pT);

    pushActionPropertyCommand(mpUndoStack, mpHost, actionID, pT->getName(), qsl("css"), oldStateXML, newStateXML);
}
