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
#include <QFont>
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
class QPainter;
class QRect;
class QTabBar;
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
inline constexpr char scmProp_aboutCard[] = "aboutCard";
// ...and a card carrying no heading, and so no room inside for one: a single
// option in the settings dialog, the thanks and third-party cards on the About
// one. The builder takes the name rather than assuming either.
inline constexpr char scmProp_settingsCardPlain[] = "settingsCardPlain";
// The About dialog's own pair: a maker's card is headed by their name, and the
// thanks and third-party cards carry no heading at all.
inline constexpr char scmProp_aboutCardPlain[] = "aboutCardPlain";
// A tool button standing on a form as a button, whose trailing half opens a
// menu. Opt-in, since a tool button carrying no such property is whatever the
// window it lives in makes of it - the editor's forms hold several that paint
// themselves and must not be drawn as buttons at all.
inline constexpr char scmProp_menuButton[] = "uiMenuButton";

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
    // The same two washes as colours carrying their own alpha, for anything that
    // paints rather than writes a rule: the profile strip's chips are filled
    // through a QPainter, and a second pair of numbers for them would be a
    // second design.
    QColor hoverWash;
    QColor accentWash;
    // Washes rather than colours, ready to go into a stylesheet: what a hovered
    // row is tinted with, and what a chosen chip is filled with. Written out of
    // the two above, so a sheet and a painter cannot drift apart.
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
// A shade off pure red rather than towards orange: at the saturation a red is
// mixed at, the orange side of it reads as tomato
inline constexpr qreal scmStateHue_error = 0.01;
QColor stateColor(const qreal hue, const bool darkPage);

// The one red anything broken is drawn or written in: the note and the dot over
// the editor's code pane, the mark on a broken item's row, the picture on the
// error notice, the note refusing a duplicate event name, a clashing shortcut
// and an untrusted certificate in the settings, and the edge round a field the
// connection dialog will not take. Walked off the strip's surface, the darkest
// any of them sits on; a place whose own surface is lighter than that walks
// this value on with readableOn() against the surface it is really written on,
// which changes nothing wherever it already clears the floor.
QColor errorInk(const ThemeTokens& tokens);

// How much of a state hue a surface is washed in when the reading is about the
// surface rather than about a dot on it - a control the connection has warned
// the reader about. The weight tokens.accentSoft is mixed at, so a called-out
// control and the editor's notice are the same depth of tint, and light enough
// that the words on it are still read against the surface underneath.
inline constexpr qreal scmSoftWashStrength = 0.14;

// ...and the lighter one a row merely under the pointer is tinted with, which
// says where the pointer is without saying anything has been chosen
inline constexpr qreal scmHoverWashStrength = 0.07;

// A scroll bar is chrome the reader is not meant to notice until they reach for
// it, and one window's idea of that is every window's. The prefix is what the
// rules are scoped by - a scroll area's own bars answer only to a descendant
// selector. The groove is the surface the bar is set into, which is the page
// unless the caller names the one it is actually drawing over.
QString scrollBarStyleSheet(const QString& selectorPrefix, const ThemeTokens& tokens, const QColor& surface = QColor());

// The measurements the two sidebars share, so that a rail in one window and a
// rail in the other are the same object rather than two that happen to agree
// today. Only what a window has a measured reason to differ by is left to it -
// the expanded width it measures for itself, and the vertical padding, which is
// the inset its own columns start at.
//
// What is left round the icons once the names are given up. Wide enough for the
// glyph, its pill and the gutter the accent bar stands in, and no wider: it is a
// rail, not a column.
inline constexpr int scmSidebarRailWidth = 46;
inline constexpr int scmSidebarPadding = 12;
inline constexpr int scmSidebarRailPadding = 6;
inline constexpr int scmSidebarSeparatorInset = 12;
inline constexpr int scmSidebarRowHeight = 36;
// The design language's glyph, which both sidebars draw their rows with. The
// editor's icon size preference moves its own away from this, which the
// measurement below reads off the list rather than assuming.
inline constexpr int scmSidebarIconSize = 18;

// The measurements one window's sidebar differs from the other's by; the colour
// an unchosen name is written in is the only other difference, and travels
// beside these. What is in neither - the pill, its accent bar, the hover wash,
// the ring that says the list has the keyboard - is the same in both and is
// drawn out of them.
struct SidebarMetrics
{
    // What the sidebar is drawn at with the names showing. Measured in both
    // windows from the widest of their own row names, so it is a runtime number
    // in each.
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

// What one row of that sidebar comes to at a given name, which is what both
// windows measure their expanded width off. Not a constant: what a style leaves
// round an item's text is a different number in each appearance - two pixels
// either side under the dark theme's Fusion proxy, four under the platform's
// own style on macOS - and QCommonStyle draws the name inside that again, so a
// row wide enough in one appearance elides in the other. Asked of the base
// style rather than the list's, so the answer does not turn on whether the
// sheet above has been built yet: the editor measures before it builds one,
// since the accent bar is a fraction of the width being measured. What the
// sheet then puts on a row, and no style can know about, is added here.
int sidebarRowWidth(const QListWidget* pList, const QString& name);

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

// The surface and the hairline an unavailable field takes, which the sheet
// above draws a disabled one with. Out here as well because a field that cannot
// be typed into for a reason of its own - a read-only one, whose value is still
// there to be read - is let down the same way rather than by a second recipe.
QColor disabledFieldColour(const ThemeTokens& tokens);
QColor disabledBorderColour(const ThemeTokens& tokens);

// One mark for every choice: the box a check box is set in, the circle a radio
// button is set in, and the box a checkable card's title begins with, drawn as
// one control. The three carry the same fill, the same hairline and the same
// accent once they are on, so a choice reads the same wherever it is made - and
// a tri-state box shows a dash rather than the grey filled square a platform
// draws it as, which reads as a different control entirely. Scoped the way
// inputStyleSheet() is: the editor sets it on each form, the settings dialog
// passes the selector of the stack its pages are in.
//
// The editor's segmented control gives its indicator no size at all, from rules
// selecting on a property - which are more specific than the type selectors
// here, so they go on winning.
QString choiceStyleSheet(const ThemeTokens& tokens, const QString& selectorPrefix = QString());

// How big that mark is drawn, on all three of the controls that carry one. A
// styled check indicator has no size of its own to fall back on. How far right
// of the frame edge this leaves a checkable card's title is not a constant to go
// with it: styles disagree on the room after an indicator, which is between the
// box and the words rather than before both.
inline constexpr int scmChoiceIndicatorSize = 13;
// What is drawn inside it once the choice is made, with a little air left round
// it - a tick, a dash, or the dot that says which of a set is chosen
inline constexpr int scmChoiceGlyphSize = scmChoiceIndicatorSize - 4;
inline constexpr int scmChoiceDotDiameter = 6;

// The ordinary button of both windows: the same corner and the same height as a
// field, on a face lifted off the card rather than sunk into it, because a
// button is pressed rather than typed into. A button carrying a menu says so
// with the chevron every other control drops something down under.
//
// A tool button that has to stay one - because the body of it acts and the
// trailing half opens a menu - is drawn as a button too once it carries
// scmProp_menuButton, with the combo box's chevron standing on that half.
//
// What it deliberately does not reach: a colour well, which shows a value
// rather than a surface and carries a sheet of its own; the editor's
// placeholder buttons, which are tool buttons drawing their own dashed frame;
// and a row that leads somewhere, which restates what it wants of this.
QString buttonStyleSheet(const ThemeTokens& tokens, const QString& selectorPrefix = QString());

// The trailing half of such a button, drawn as a segment of its own: two click
// areas have to read as two before the pointer arrives, so the half carries the
// seam on its leading edge, a wash of its own under the pointer and the chevron
// every other control drops something down under.
//
// One recipe, two callers: buttonStyleSheet() puts it on a form's split button,
// and the editor's toolbar puts it on Save Profile, whose face is the flat kind
// a bar draws rather than a button's - which is the whole of what the two
// differ by, so the selector for the button and the corner its own rule rounds
// to are what is passed in. The half's outer corners take that same radius, or
// a lit half pokes square corners out of a rounded button.
//
// Empty when the chevron could not be cached, the way every rule pointing at a
// picture is: a sheet aimed at a file that is not there draws nothing at all.
QString splitButtonMenuHalfStyleSheet(const QString& buttonSelector, const int cornerRadius, const ThemeTokens& tokens);

// Which edge of a bar carries the hairline that tells it from the page under
// or over it: a bar across the top of a window is seamed along its bottom, and
// one at the foot of it along its top.
enum class ToolBarSeam { Bottom, Top };

// The flat bar a window's actions stand on: the page's own surface with the
// seam on the edge given, the grip that says the bar can be dragged, the
// hairline between two groups of actions, and the buttons themselves - drawn
// with no face at rest, since a row of framed buttons across the top of a
// window reads as a row of boxes rather than as a bar. A button lights in the
// hover wash under the pointer and in the accent's while it is held or switched
// on - a checkable action that is on is held down in every way but the pointer -
// and its word takes accentText throughout, because the glyph beside it is inked
// that for QIcon::Active and QIcon::On and the two halves of a button have to
// light as one.
//
// Scoped to the bar rather than to a window: toolBarSelector is that bar's own
// selector, "QToolBar#editorActionsToolbar" in the script editor, so a window
// holding more than one bar draws each of them for itself. Whatever else a
// window's bar carries - Qt's overflow button, a split button - is that
// window's own and follows this.
//
// Used by the script editor's actions bar, the notepad's, the main window's and
// every detached window's.
QString toolBarStyleSheet(const QString& toolBarSelector, const ToolBarSeam seam, const ThemeTokens& tokens);

// What the grip at the leading end of such a bar is given: the six dots are
// five pixels across, and the rest is what holds them off the bar's edge and
// off the first button
inline constexpr int scmToolBarGripExtent = 11;
// The flat face a button on that bar is drawn with. Not on the radius scale -
// this is a row lighting up under the pointer rather than a box a window is
// laid out in - but named all the same, because the trailing half of a split
// button on the bar has to round to the very same corner the body does.
inline constexpr int scmToolBarButtonRadius = 6;
inline constexpr int scmToolBarButtonPaddingVertical = 3;
inline constexpr int scmToolBarButtonPaddingHorizontal = 7;

// The button a strip of extra controls is opened and closed from: a glyph and
// a word, checkable, on a button's own face rather than the row's - it is
// pressed rather than typed into. Quiet at rest, in the chrome tone every other
// word of a window's furniture is written in, and lit in the accent for as long
// as what it opened is on show, which is the ink a switched-on control already
// carries. The hairline follows: the border tone at rest, the accent while it
// is on, and the accent again from the keyboard alone.
//
// buttonSelector is the button's own selector, so the rules reach that one
// control and not every tool button on the form around it.
//
// Used by the script editor's trigger form, for the options strip. The notepad
// was the other caller and gave its fold up: it hid five controls to save no
// space at all, where the count on a button and a warning at the one dangerous
// moment say what a fold never did.
QString disclosureButtonStyleSheet(const QString& buttonSelector, const ThemeTokens& tokens);

// Every menu one of these windows owns: the one a toolbar button drops, the
// options behind a search field, the one the pointer opens over a code pane.
// Drawn as the list a combo box drops down is, since both are a list opened out
// of the window - the same hairline, the same scmRadiusInput corner and the
// same accent under the row being pointed at - on the card tone, because a menu
// is a panel lifted off the page rather than a field opened up. A checkable row
// carries the one mark choiceStyleSheet() draws.
//
// Then letPopupsTakeTheFieldsCorner() on whatever the sheet was set on, for the
// same reason a dropped-down list needs it; a menu built at the moment it is
// needed takes both from the code that builds it, before it is exec'd.
QString menuStyleSheet(const ThemeTokens& tokens, const QString& selectorPrefix = QString());

// The strip of tabs at the head of a QTabWidget, drawn as a row of chips lying
// on the page rather than as the folder tabs a platform cuts. A tab is a word
// in a box the way a chip and a menu row are - quiet at rest, washed under the
// pointer, filled in the accent while it is the one on show - so the strip
// reads as a choice being made rather than as a stack of cards, and the pane
// under it is left to whatever the window puts there.
//
// tabWidgetSelector is that tab widget's own selector, since a bar drawn for
// one is not a bar drawn for every QTabBar the window holds - a window's other
// tab widgets keep whatever they were.
//
// The cross on a closable tab is the same one every other x in the design is
// drawn from, tinted into the glyph cache for a rule to point at; where the
// cache cannot be written the rule is left out rather than aimed at nothing,
// and the tab keeps the cross the platform draws it with.
//
// What is deliberately not drawn: the two buttons a strip too crowded to fit
// scrolls with. Their arrows are a sub-control of a QToolButton the bar makes
// for itself, and a rule that gave those buttons a face without also replacing
// the arrows would leave the reader a blank square to press.
//
// Three things a rule cannot say at all - the band a platform fills the bar with
// behind the tabs, the box a tab's cross is sized in, and where inside the chip
// that box stands. prepareTabStrip() on the bar is the caller's one job, and
// says all three.
QString tabBarStyleSheet(const QString& tabWidgetSelector, const ThemeTokens& tokens);

// The half of the strip above that has to be said to the bar rather than
// written in a rule. Call it on the QTabBar the sheet was written for, once,
// whether or not the bar already holds tabs.
void prepareTabStrip(QTabBar* pTabBar);

// What the strip of tabs above leaves round the pane under it, so that the
// rounded corner of whatever fills that pane opens onto the page rather than
// butting into the window's edge
inline constexpr int scmTabPaneInset = 4;

// The measurements a tab is drawn from. They are here rather than beside the
// sheet they were written for because the profile strip at the head of the main
// window and of every detached one - TTabBar, which paints its chips itself
// rather than being handed a sheet - is drawn from these same numbers, so the
// two strips are one object rather than two that happen to agree today.
//
// A row of a menu is read across rather than typed into, so it is given a
// button's air above and below its word; a tab takes that same air.
inline constexpr int scmMenuItemPaddingVertical = 4;
// A tab is a word in a box, so it is given a menu row's air above and below its
// word and a button's either side of it - and the gap between two of them is
// what says they are two boxes rather than one bar cut into segments.
inline constexpr int scmTabPaddingVertical = scmMenuItemPaddingVertical;
inline constexpr int scmTabPaddingHorizontal = 10;
inline constexpr int scmTabGap = 2;
// How far the first tab is held off the leading edge of the strip: the same
// inset the pane under it is drawn at, so the row of chips and the field below
// start on the same line
inline constexpr int scmTabStripInset = scmTabPaneInset;
// The cross on a closable tab, and the mark drawn inside that box. Smaller than
// the mark a choice carries: what it is beside is a word rather than a control,
// and a cross at the word's own height reads as a second tab.
inline constexpr int scmTabCloseBoxSize = 14;
inline constexpr int scmTabCloseGlyphSize = 8;

// Focus from the keyboard alone, on every control the two recipes above draw:
// the three that carry a mark, and the button. The accent a mark's hairline and
// a button's frame take on focus is there to say where the keyboard is, not to
// remember what was clicked last - and left to itself, which of the two it
// reads as is not the design's to decide. QAbstractButton asks
// QStyle::SH_Button_FocusPolicy once, in its constructor, and keeps whatever
// that style answered; the base style under it is swapped every time the
// appearance changes, Fusion by way of DarkTheme for the dark one and the
// platform's own otherwise. Both answer Qt::StrongFocus here, so a click left
// the accent sitting on the control until something else was clicked, and a
// platform whose style answered Qt::TabFocus instead would have made the same
// code look right. The shell says it rather than inheriting it. Call it on the
// container the sheets were set on, after setting them; a control that wants no
// focus at all keeps that, and re-running it costs nothing.
void keepClickFocusOffControls(QWidget* pRoot);

// The corner inputStyleSheet() gives a dropped-down list, made visible. That
// list lives in a frame of its own which is a window rather than a widget on
// the form, and a window is filled before anything in it is drawn - so a
// rounded frame on the list alone leaves the window's square corners showing
// through in the fill. Two things have to be said for the arc to open onto the
// card or the page behind the popup: the window is asked to be see-through, and
// the frame is left painting nothing. The sheet cannot say the second on its
// own - a stylesheet gives a QFrame subclass no styled background, so its rule
// for the container only reaches that widget's palette - so the brushes are
// named here.
//
// The platform reads the attribute when it makes the window's surface, which
// happens at the popup's first show and never again: setting it afterwards
// leaves the list rounded over an opaque corner for the rest of the session. So
// this runs with the shell's style pass, over every combo box under pRoot, and
// again wherever a window builds a combo box after that pass - a trigger's
// pattern rows are made as triggers are shown. Calling it twice on the same box
// costs nothing.
//
// A menu is a popup too, and takes the corner menuStyleSheet() gives it the
// same way - said on the menu itself, which is its own window rather than a
// list inside one. Every QMenu under pRoot is opened up, and pRoot itself when
// it is one, so a menu built on the fly can be handed straight to this. Only
// call it where the menu sheet reaches: a menu opened up with nothing painting
// its surface would be see-through all over rather than at the corner.
void letPopupsTakeTheFieldsCorner(QWidget* pRoot);

// The height a field's contents are given, what is left round them, and what
// the whole control therefore comes out at - which a form laying a field into a
// row of its own has to leave room for. The horizontal padding is here as well
// because a form eliding what it puts in a field has to know what room the
// field leaves it.
inline constexpr int scmInputContentHeight = 22;
inline constexpr int scmInputPaddingVertical = 2;
inline constexpr int scmInputPaddingHorizontal = 6;
inline constexpr int scmInputBorderWidth = 1;
inline constexpr int scmInputHeight = scmInputContentHeight + 2 * (scmInputPaddingVertical + scmInputBorderWidth);
// ...and the column of arrows a spin box is stepped with, taken out of the
// field's own width, because a form sizing a number box to the number it holds
// has to know what the steppers leave it.
inline constexpr int scmInputStepperWidth = 16;
// ...and the column a combo box's chevron stands in, which is the width a split
// button's menu half is given as well. A window drawing a split button of its
// own has to hold the button's words clear of that half itself: no selector can
// ask whether a button carries a menu, so nothing measures the room for it.
inline constexpr int scmInputDropDownWidth = 18;

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
// ...and the same for a colour already carrying the alpha it is to be written
// at, so a wash mixed once as a QColor and a wash written into a sheet are the
// one value rather than two
QString rgba(const QColor& colourWithAlpha);

// The accent bar down the leading edge of a chosen row, painted over the pill
// the style has drawn for it: the pill's own rounded rectangle, filled in the
// accent and cut to the bar's width. So the bar keeps its width down the whole
// row and takes the pill's corner at both ends - the shape the sidebar draws
// its bar in from a stop in the pill's gradient, and one a border-left cannot
// be, since the corner radius bends that into a bracket. The corner is the
// pill's own: a sidebar row's scmRadiusPanel unless the caller names the one
// its pill was cut to, as the profile strip's chip does with scmRadiusChip.
void paintAccentBar(QPainter* pPainter, const QRect& row, const QColor& accent, const int cornerRadius = scmRadiusPanel);

// The shape lives in the alpha channel: filling through it keeps the
// antialiased edges that recolouring the pixels would harden into a staircase
QPixmap tintedGlyph(const QPixmap& source, const QColor& color);

// The pen every Lucide glyph is drawn with. Lucide authors at 2, which reads
// heavy at the sizes the toolbar and the editor draw at, so the glyphs ship as
// SVG and the weight is set here rather than baked into a raster.
inline constexpr qreal scmGlyphStrokeWidth = 1.5;

// What an SVG glyph is rasterised at. The same size the glyphs were shipped as
// PNGs at, so every caller's scaled() lands where it did before.
inline constexpr int scmGlyphRasterSize = 128;

// The only way a glyph file becomes a pixmap. An .svg is drawn at
// scmGlyphStrokeWidth, anything else is read as a raster - which is what the
// three brand marks (GitHub, Patreon, Discord) still are, being filled shapes
// with no stroke to set. Results are cached: parsing an SVG per chip is what
// made the About dialog's construction measurably slow when its glyphs were
// re-encoded per use.
QPixmap glyphPixmap(const QString& file);

// One glyph inked and centred in a transparent square, the way a control's
// indicator carries its mark: drawn at glyphSize inside a box boxSize across,
// at devicePixelRatio, and cached. The margin is what makes the size hold - a
// mark drawn on its own fills whatever box it lands in.
//
// This is the pixmap a painter draws; the cache file a stylesheet points at is
// written from the same call, so a rule and a painted cross are the one mark.
QPixmap themedGlyphPixmap(const QString& glyphFile, const QColor& color, const int boxSize, const int glyphSize, const qreal devicePixelRatio);

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
//
// The mark itself is choiceStyleSheet()'s, so that a card's title reads as the
// same choice as the check boxes under it. It keeps a function of its own
// because measuredCardTitleHeight() has to lay a box out under these rules
// alone, before the rest of the window's sheet exists.
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

// The colour of a link, written into the link itself.
//
// A QLabel parses its rich text the moment it is set and bakes the colour of
// every anchor into the document then and there, taking it from the
// *application* palette - so writing QPalette::Link to the widget afterwards
// changes nothing, and re-polishing it changes nothing either. Every anchor is
// left at Qt's own blue, which is 2.4:1 on a dark page. Setting the colour on
// the anchor is what a document does honour, so it travels there instead, and
// the text a label was given has to be kept somewhere it can be inked from
// again when the appearance moves.
QString withLinkColour(const QString& richText, const QColor& colour);

// The platform's fixed-pitch face at the size the window is running at: a
// version string, a host name, a build fact. Scaled off the font handed in
// rather than off a number, so it follows the interface font - and a base
// measured in pixels is answered in pixels, since pointSizeF() is -1 there.
QFont fixedPitchFont(const QFont& base, const qreal scale = 1.0);

// The one scale a stylesheet names a size from, and the only place a size is
// named at all.
//
// In points, because Qt's stylesheet parser reads pt and px for font-size and
// nothing else: a percentage is dropped without a word, which is how 28 rules
// across three windows came to say nothing at all. The base is read off the
// application font every time it is asked, so a rule still follows the user's
// interface font the way a percentage was meant to.
//
//   step      at a 13pt base   what it is set on
//   Caption   11pt             chips, captions, the version and copyright
//                              lines, a card's or a section's note
//   Body      13pt             everything not named here; a rule that would
//                              only restate this names no size at all
//   Title     15pt             a section title, a hero headline, a search
//                              header, the sidebar wordmark
//   Display   19pt             the page title over a settings page, and
//                              nothing else
//
// Four steps and no more: two things are set apart only by a difference a
// reader can see, and a fifth size between two of these is not one. Whole
// points rather than a fraction, so that a height measured off a step - the
// corner of an ID pill is half of one - stays an integer.
enum class TypeStep { Caption, Body, Title, Display };
int typeSize(const TypeStep step);

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
