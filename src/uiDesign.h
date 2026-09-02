#ifndef MUDLET_UIDESIGN_H
#define MUDLET_UIDESIGN_H

/***************************************************************************
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

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QKeySequence>
#include <QList>
#include <QMap>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariant>

class QAbstractButton;
class QAction;
class QBoxLayout;
class QGridLayout;
class QLayout;
class QLineEdit;
class QListWidget;
class QObject;
class QTimer;
class QWidget;
class TKeySequenceEdit;

// The shell the settings dialog is built into: the pieces of it that describe a
// look rather than a setting, so another dialog can be given the same one.
namespace uiDesign {

// Synonyms a control is searchable by that it does not show anywhere. A property
// earns a constant here once something outside one window's own rules reads it -
// a shared builder, or a delegate. The rest stay literals where they are
// selected on: a constant cannot be interpolated into a QStringLiteral, and a
// rule written out in one place has nothing to drift from.
inline constexpr char scmProp_searchKeywords[] = "searchKeywords";
// The sidebar is collapsed to icons only. Named for the dialog it started in
// and kept that way in the editor, since it is the shared delegate's contract.
inline constexpr char scmProp_rail[] = "settingsRail";
// ...and likewise the list holding the keyboard focus, which a QSS rule cannot
// ask about on its own
inline constexpr char scmProp_focused[] = "settingsFocused";
// What a column was painted with, so that the seam between two of them can
// carry each one's tone up to the line. A stylesheet is what fills the editor's
// columns and a palette knows nothing about one, so the column says so itself -
// and a splitter handle reads it off whichever widgets it happens to lie
// between rather than being told which pair those are.
inline constexpr char scmProp_paneTone[] = "uiPaneTone";
// This group box is a card. Both windows draw their cards from cardStyleSheet()
// below, so which property says so is the first of the few things that differ.
inline constexpr char scmProp_settingsCard[] = "settingsCard";
inline constexpr char scmProp_editorCard[] = "editorCard";
// ...and a card carrying a single option, which needs no heading and so no room
// inside for one. Only the settings dialog has such a card so far, but the
// builder takes the name rather than assuming it.
inline constexpr char scmProp_settingsCardPlain[] = "settingsCardPlain";

// Every surface in either window is mixed from four palette colours and nothing
// else, so that a theme change moves all of them together. The recipes live
// here rather than in each window: two windows deriving a border by eye is how
// they drift apart.
//
// Three tones carry the depth, and which one a rule reaches for is the whole of
// what says how deep the thing it draws sits:
//   page  - the window itself, and anything that is a piece of the window: the
//           toolbar, the status bar, the sidebar, the column an item is edited
//           in
//   pane  - a column of the window that is a surface of its own rather than a
//           piece of the page: the editor's panel of items, taken as one thing
//           from the search row at its head to the trees under it
//   card  - a panel lifted off the page: the options cards, a popup surface
//   field - sunk into whatever holds it, because the user types into it or
//           picks a value in it: line edits, combo boxes, spin boxes, the code
//           pane, a check indicator
// ...and one tone is a line rather than a surface:
//   separator - the seam between two panes, drawn where the handle that resizes
//           them is
struct ThemeTokens
{
    // The words on the page: QPalette::WindowText pulled a little way back
    // towards the page it is written on, since a palette answers pure white on
    // a dark theme and pure black on a light one and neither is a colour a page
    // of text is set in. Every muted tone below is mixed from this, so the whole
    // scale softens with it.
    QColor text;
    // Straight off the palette: the colour the theme points with
    QColor accent;
    // Off the palette rather than mudlet::inDarkMode(), so a dark system theme
    // under "follow the system" gets the dark treatment too
    bool darkPage = false;
    // The window's own surface, QPalette::Window itself - every other surface is
    // measured from it, and every muted ink is mixed over it
    QColor page;
    // Lifted off the page, so that a card reads as nearer than what carries it
    QColor card;
    // A fifth of that lift: enough for a column to be told apart from the page
    // beside it, and far short of what would read as a panel laid on top of it
    QColor pane;
    // QPalette::Base, what Qt paints an editable control with - lifted back
    // towards the page on a dark theme, where Base is near-black and a well cut
    // that deep reads as a hole in the window rather than as a surface sunk into
    // it. A light theme's Base is white and is left alone: lifting white towards
    // a grey page only muddies it.
    QColor field;
    // The hairline round a card. Measured from the page and text colours, the
    // one pair a palette must keep apart to be usable at all: Mudlet's light
    // appearance has window, base and mid within three levels, so a border mixed
    // from those is invisible.
    QColor border;
    // The seam between two panes: the page taken towards black, so it reads as a
    // groove cut into the window rather than as a hairline drawn on it - which
    // is what the border tone is for. A light page is near enough to white that
    // the drop has to be the smaller one, or the seam becomes a grey rule across
    // the window.
    QColor separator;
    // Quieter than the body text but the same class of thing - a card's
    // description, a chip's word, the status bar - so it carries the same floor
    // and is walked towards the words until it meets it on all three surfaces
    QColor mutedText;
    // ...and the same walk to the lower floor an unavailable word is held to,
    // over the three surfaces and the field it may be typed into
    QColor disabledText;
    // A saturated highlight colour rarely holds its own against either page, so
    // it is taken towards the end of the lightness scale the page is not at -
    // and walked on from there until it can be read on a wash of that same
    // accent, which is what a chosen row, a sidebar's pill and a lit chip all
    // are. See scmAccentWashStrength.
    QColor accentText;
    // A marker pen whose lightness is chosen for the page it lies on: an opaque
    // pale wash under dark text, a darker one light text still shows through
    QColor marker;
    // Washes rather than colours, ready to go into a stylesheet: what a hovered
    // row is tinted with, and what a chosen chip is filled with
    QString hoverSoft;
    QString accentSoft;
};

ThemeTokens themeTokens();

// The deepest wash of the accent anything is drawn on: a chosen row in one of
// the editor's item trees. A sidebar's pill and a chip are lighter washes of the
// same colour, so the accent ink is measured against this one and holds on all
// of them.
inline constexpr qreal scmAccentWashStrength = 0.24;

// What a dot, a chip or a banner is drawn in: the hue says which reading it is
// while the lightness comes off the page it is drawn on, so one colour holds
// against a light and a dark theme alike
inline constexpr qreal scmStateHue_ok = 0.34;
inline constexpr qreal scmStateHue_warning = 0.09;
inline constexpr qreal scmStateHue_error = 0.02;
QColor stateColor(const qreal hue, const bool darkPage);

// A scroll bar is chrome the reader is not meant to notice until they reach for
// it, and one window's idea of that is every window's. The prefix is what the
// rules are scoped by - a scroll area's own bars answer only to a descendant
// selector. The groove is the surface the bar is set into, which is the page
// unless the caller names the one it is actually drawing over.
QString scrollBarStyleSheet(const QString& selectorPrefix, const ThemeTokens& tokens, const QColor& surface = QColor());

// The measurements one window's sidebar differs from the other's by; the colour
// an unchosen name is written in is the only other difference, and travels
// beside these. What is in neither - the pill, its accent bar, the hover wash,
// the ring that says the list has the keyboard - is the same in both and is
// drawn out of them.
struct SidebarMetrics
{
    // What the sidebar is drawn at with the names showing. The settings dialog
    // knows this ahead of time; the editor measures its longest name for it,
    // so it is a runtime number there.
    int expandedWidth = 0;
    // ...and once the names are given up, leaving the icons
    int railWidth = 0;
    // What is left either side of the items at each of those two widths, which
    // is the whole of why the accent bar is a different fraction of each
    int padding = 0;
    int railPadding = 0;
    // Above and below the items. No rule asks for it - the pane's layout does.
    int verticalPadding = 0;
    // How far a divider row is held off the sidebar's edges with the names
    // showing. Collapsed both windows hold it off by the same 2px, so only the
    // expanded inset is asked for.
    int separatorInset = 0;
};

// The list of places down the left of a window, drawn the one way in both of
// them. The two names are object names rather than selectors, because the
// collapse below finds the separators by the same string the rules select on.
// itemColor is what an unchosen row's name is written in, the one colour the
// two windows answer differently: the editor's chrome is muted throughout,
// while the settings sidebar is the whole of that dialog's navigation.
QString sidebarStyleSheet(const QString& listName, const QString& separatorName, const QColor& itemColor, const SidebarMetrics& metrics, const ThemeTokens& tokens);

// Collapsing that sidebar to a rail of icons and back: the pane's width and
// margins, and the property both the shared delegate and the rules above read
// the mode off. That property is the mode, so nothing else has to remember it
// - and the answer is whether anything moved, which lets a window with more to
// do at that moment skip it in the same breath.
bool setSidebarCollapsed(QWidget* pPane, QListWidget* pList, const QString& separatorName, const bool collapsed, const SidebarMetrics& metrics);

// How round a corner is says how big the thing behind it is: the radius that
// reads as a card's corner turns a chip into a lozenge, and the one that suits a
// chip leaves a card looking square. So the scale is proportional - the smaller
// the control, the tighter the corner - and these four are the whole of it. No
// rule writes a radius of its own.
//
// A word in a box: the ID beside an item's name, the kind beside a search
// result, the OR/AND beside a matching mode
inline constexpr int scmRadiusChip = 4;
// The accent bar down the leading edge of a chosen row, in the sidebar and in
// the editor's item trees alike - one number, so that the two lists cannot come
// to disagree about how wide the mark that says "this one" is. It is a border
// rather than a gap, so whatever leaves room for it takes that much out of its
// own padding and the row's contents stay where they were.
inline constexpr int scmAccentBarWidth = 3;
// The controls a form is filled in through, a little over 30px tall: line edits,
// combo boxes, spin boxes. Tighter than the card they sit on, so that a control
// inside one does not echo the box around it.
inline constexpr int scmRadiusInput = 5;
// The boxes a window is laid out in: an options card, a notice, the ring a deep
// link is pointed out with
inline constexpr int scmRadiusPanel = 8;
// A search field is the one control a panel is headed by rather than one of
// several filled in on it, and is drawn taller than a form control - so it takes
// the corner of the panel it heads rather than the form controls' one
inline constexpr int scmRadiusProminentInput = scmRadiusPanel;

// Everything a value is typed into or picked in, drawn as one control: the
// field surface sunk into whatever holds it, a hairline round it and the corner
// the rest of the shell uses. Set on a form rather than on a window, so that
// only the controls under that form are claimed - a code pane paints its own,
// and a tree is a list rather than a field. A window with no form to set it on,
// but with one container everything it means sits under, passes that container's
// selector as selectorPrefix and keeps the same sheet on the shell instead.
QString inputStyleSheet(const ThemeTokens& tokens, const QString& selectorPrefix = QString());

// The height a field's contents are given, what is left round them, and what
// the whole control therefore comes out at - which a form laying a field into a
// row of its own has to leave room for
inline constexpr int scmInputContentHeight = 24;
inline constexpr int scmInputPaddingVertical = 3;
inline constexpr int scmInputBorderWidth = 1;
inline constexpr int scmInputHeight = scmInputContentHeight + 2 * (scmInputPaddingVertical + scmInputBorderWidth);

// How far apart two colours are to read, on the scale WCAG measures it: 1 is a
// colour on itself and 21 is black on white
qreal contrastRatio(const QColor& first, const QColor& second);

// What a word has to clear against what it is written on: the floor every ink
// mixed here is walked until it meets, and the one ReadabilityAuditTest holds
// both windows to
inline constexpr qreal scmTextMinimumRatio = 4.5;
// ...and the lower one WCAG allows a word that is unavailable, or that stands in
// for one not typed yet
inline constexpr qreal scmQuietMinimumRatio = 3.0;

// The nearest colour to the one asked for that can be read on a given
// background: its lightness is walked away from that background until it clears
// minimumRatio, and if the hue runs out of room first, the fallback is used.
// What a syntax theme calls a keyword is chosen against that theme's own
// background, so it has to be brought over before it means anything on a field.
QColor readableOn(const QColor& background, const QColor& wanted, const QColor& fallback, const qreal minimumRatio);

// QLayout::setAlignment() is documented not to look in child layouts, and a
// card's controls are nested in them
bool alignInLayoutTree(QLayout* pLayout, const QWidget* pWidget, const Qt::Alignment alignment);

// QLayout::removeWidget() only looks at its own items, and the .ui file nests
// controls several layouts deep. Qt would find it from QLayout::addWidget()
// instead, but warns once per widget - which for a dialog's worth of moves
// buries anything else on the console.
bool removeFromLayoutTree(QLayout* pLayout, QWidget* pWidget);

void detachFromLayout(QWidget* pWidget);

void invalidateLayoutsUpTo(QWidget* pWidget, const QWidget* pTop);

// A profile's Lua stylesheet is applied to the whole dialog and reaches every
// widget it does not name, so the shell's own scaffolding carries a property
// the shell stylesheet keeps it transparent by.
void markAsShellSurface(QWidget* pWidget);

// A grid has no notion of inserting a row, so every item is taken out and put
// back one row lower, carrying its row properties. The columns are untouched,
// which keeps a .ui file's column stretches meaning what they said.
void insertGridRowAtTop(QGridLayout* pGrid, QWidget* pWidget);

// A control that sits inside a sentence - "Keep firing for [3] more lines" -
// laid out from one translatable string with a %1 where the control goes, so a
// language that reads the number first or last only moves the placeholder.
// Whatever is either side of it becomes a label; the gaps are the row's own
// spacing, so each half is trimmed. A translation that lost its %1 still reads
// as a sentence: all of it leads, and the control follows.
void buildControlSentenceRow(QBoxLayout* pRow, const QString& translatedSentence, QWidget* pControl);

// The same for a sentence holding several controls - "Fires every %1 h %2 min
// %3 s %4 ms" - where %1 is the first of the list, %2 the second and so on
// wherever the translation puts them. A placeholder the sentence does not carry
// leaves its control after the words, as the single-control form does; a
// placeholder naming a control that is not there is left in the sentence, so a
// mistranslation shows rather than swallowing a field.
//
// Each control keeps whatever accessible name it came with: one sentence cannot
// name four fields. The word beside a control names it to a reader looking at
// the row and to nobody else - a screen reader announces a field by its own
// accessible name - so the caller has to give each control one before it builds
// the row.
void buildControlSentenceRow(QBoxLayout* pRow, const QString& translatedSentence, const QList<QWidget*>& controls);

// A row that leads somewhere rather than setting something; the chevron at its
// right edge is drawn by the shell stylesheet from the property this puts on.
void makeChevronRow(QAbstractButton* pButton);

void collectFocusableInLayoutOrder(const QLayout* pLayout, QList<QWidget*>& chain);

QString spotlightStyleSheet(const QColor& accent, const qreal strength);

// Rich text, the & of keyboard accelerators, accents and case are all folded
// away, so that "fonte" finds "Fonté" and "save" finds "&Save"
QString foldForSearch(const QString& text);

// A combo box is not here: what it shows is one of its items, and the two
// callers want its whole list or nothing at all
QString visibleTextOf(const QWidget* pWidget);

// What it shows, what its tooltip says, and any synonyms it was given
void collectSearchText(const QWidget* pWidget, QStringList& parts);

// Synonyms count too: a card found by a keyword still shows which control carries it
QString highlightTextOf(const QWidget* pWidget);

// One ideograph is a word where one Latin letter is not, so it is a query worth
// running; a lone letter matches most of the dialog and answers nothing.
bool wordEnoughToSearch(const QStringList& needles);

// The grid of dots a draggable thing is gripped by: a pattern row, and the bar
// the editor's actions are on. One geometry rather than one per window, so that
// the two grips are read as the same mark.
inline constexpr qreal scmGripDotDiameter = 2.0;
inline constexpr qreal scmGripDotPitch = 3.0;
inline constexpr int scmGripDotsAcross = 2;
inline constexpr int scmGripDotsAlong = 3;

// ...and that grid saved where a stylesheet can point at it, since a rule can
// only take a picture from a file and cannot recolour one on the way in. The
// ink is part of the file's name, the way the arrows above are cached, so a
// theme change writes a new file rather than changing one a stylesheet has
// already read and cached by path. Empty if there is nowhere to write it.
// alongTheBar says which way the dots are the longer way round: a bar running
// across the window is gripped by a tall handle, and one down its side by a
// wide one.
QString gripGlyphFile(const QColor& color, const bool alongTheBar);

// A stylesheet rule selecting on a property only takes effect on a re-polish
void repolish(QWidget* pWidget);

void setSearchMatch(QWidget* pWidget, const QVariant& matched);

// Every surface is blended from the palette rather than written out as hex, so
// the shell follows whichever theme it is handed
QColor blend(const QColor& from, const QColor& to, const qreal amount);

QString rgba(const QColor& color, const qreal alpha);

// The shape lives in the alpha channel: filling through it keeps the
// antialiased edges that recolouring the pixels would harden into a staircase
QPixmap tintedGlyph(const QPixmap& source, const QColor& color);

// One glyph inked for every mode a control asks a QIcon for, so that a toolbar
// action carries the same picture in the same four inks wherever it is drawn:
//
// | Mode | Ink | Why |
// | Normal, Off | mutedText | Quieter than the word under it, the way the editor's toolbar and the settings sidebar are drawn |
// | Normal, On | accentText | A checkable action that is currently doing something reads as lit rather than as merely pressed |
// | Active | accentText | What a QToolButton asks for while the pointer is on it - and the one mode a hover has to change |
// | Selected | accentText | What a view washes a chosen row with, which it would otherwise do itself in the highlight colour |
// | Disabled | disabledText | The tone the words of an unavailable action are set in |
//
// Two files where the two states are different pictures - full screen and its
// way back out, sound on and sound off - and one where they are the same.
QIcon tintedIcon(const QString& glyphOff, const QString& glyphOn, const ThemeTokens& tokens);
QIcon tintedIcon(const QString& glyph, const ThemeTokens& tokens);

// Which glyph a toolbar action carries. A window keeps a list of these because
// it is the only thing that knows which of its actions have one; what the
// tinting is, is the same everywhere.
struct ActionGlyph
{
    QPointer<QAction> pAction;
    QString glyphOff;
    // Left empty where both states are the same picture
    QString glyphOn;
};

// Re-inks every action in a window's list. Cheap enough to run whole rather than
// per action: a theme change is the only thing that calls it.
void restyleActionGlyphs(const QList<ActionGlyph>& glyphs, const ThemeTokens& tokens);

// The measurements one window's cards differ from the other's by. Everything in
// neither - the surface, the hairline, the corner, the title placed as the first
// line inside the frame, the weight it is set in - is the same in both and is
// drawn out of these and the tokens.
struct CardMetrics
{
    // Which of the two windows' cards the rules select. The frame itself is one
    // recipe, so the property is the whole of what says whose cards are being
    // drawn - and it is interpolated into the selectors rather than spelled out
    // a second time.
    const char* cardProperty = scmProp_settingsCard;
    // ...and the property a card carrying a single option is marked with, which
    // gives back the room a title would have taken. Left null by a window whose
    // cards all carry one.
    const char* plainProperty = nullptr;
    // What the card leaves round what it holds - and, since the title is the
    // first line inside the frame rather than a heading above it, how far in
    // from the frame the title starts as well
    int padding = 0;
    // A line of the bold type that title is set in, which is the font the window
    // is running at rather than a number a stylesheet could name: see
    // measuredCardTitleHeight(), whose answer this is
    int titleHeight = 0;
    // Whether a card of this window's can hold a group box the .ui file nested
    // inside it, which would otherwise draw a second frame within the card
    bool flattenNestedGroupBoxes = false;
};

// The card the two windows lay their options out in: the frame, and the title
// drawn as its first line inside it. The check indicator a checkable card's
// title begins with is a sheet of its own, below, because the title height
// asked for above has to be measured with those rules already in force.
QString cardStyleSheet(const CardMetrics& metrics, const ThemeTokens& tokens);

// Fusion draws a group box's check indicator from palette(window) darkened by
// 40%, which on a dark card is a 1.1:1 outline - and the palette pass a window
// makes afterwards cannot rescue it, as that role also carries the card's title
// band. A styled indicator gets no check mark of its own, so the checked state
// has to be drawn out in full.
QString cardIndicatorStyleSheet(const char* cardProperty, const ThemeTokens& tokens);

// A card's title is the first line inside its frame, so the card has to leave
// room for it above the first control: how much is a line of the bold type the
// title is set in, which is the font the window is running at rather than a
// number a stylesheet could name. Measured off a throwaway box rather than
// added up, because what a style leaves round a title - and round the check
// indicator a checkable card's title begins with - is the style's business.
// The throwaway box carries whichever card property the rules being measured
// under select on.
int measuredCardTitleHeight(QWidget* pParent, const QString& indicatorRules, const char* cardProperty = scmProp_settingsCard);

// What is left between a card's title and the first control under it. The
// padding round the card is the window's own - 16px in the settings dialog, 12
// in the editor's narrower column - but the gap under the title is the same in
// both, because it separates two lines rather than a box from its frame.
inline constexpr int scmCardTitleGap = 8;

// A QLabel's rich text reaches a picture only through a URL, and a glyph tinted
// at runtime has no path - so it travels inline
QString inlineGlyph(const QPixmap& glyph);

// The types are the ones connectApplyTriggers() listens to, so everything able
// to schedule an apply can also be told apart from how it was populated
QVariant controlValue(const QObject* pControl);

// Qt sets the modified flag on the first keystroke and slot_lineEditFinished()
// clears it again, so until then the field holds half a word rather than a
// setting - which neither the apply nor the snapshot below takes it for.
bool beingTypedInto(const QObject* pControl);

// What every apply-relevant control held the last time the dialog read the
// settings, so that an apply writes back only what the user changed since
// rather than the whole page (#10165).
//
// Both references are to members of the dialog that owns this, so both outlive
// it. Snapshot keys may dangle if a control is destroyed: they are only ever
// compared, never dereferenced, and a control coming into being after the last
// snapshot reads as dirty, which is the safe way round.
class SettingsSnapshot
{
public:
    Q_DISABLE_COPY(SettingsSnapshot)
    SettingsSnapshot(const QWidget& owner, const QMap<QString, QKeySequence>& shortcuts);
    // Whether a widget holds a value a setting is written from at all
    static bool carriesValue(const QObject* pControl);
    // Called once the controls hold what the settings say, so that anything
    // differing from this afterwards is the user's own edit
    void take();
    // ...and for one control whose list was rebuilt under a dialog already
    // showing it
    void take(const QObject* pControl);
    bool dirty(const QObject* pControl) const;
    bool anyDirty(const QList<const QObject*>& controls) const;
    bool shortcutsDirty() const;
    bool shortcutDirty(const QString& key) const;
    // Anything the user has changed that the settings do not know about yet: a
    // control differing from its snapshot, an uncommitted shortcut, a part-typed
    // line edit, or an apply still waiting out its debounce
    bool pendingEdits(const QTimer* pApplyTimer, const QLineEdit* pSearchField) const;
    // A second profile re-reads the editors the first left behind rather than
    // adding a second row of them
    TKeySequenceEdit* editorFor(const QString& key) const;
    void addEditor(const QString& key, TKeySequenceEdit* pEditor);

private:
    const QWidget& mOwner;
    const QMap<QString, QKeySequence>& mCurrentShortcuts;
    QHash<const QObject*, QVariant> mValues;
    QMap<QString, QKeySequence> mShortcuts;
    QMap<QString, QPointer<TKeySequenceEdit>> mEditors;
};

} // namespace uiDesign

#endif // MUDLET_UIDESIGN_H
