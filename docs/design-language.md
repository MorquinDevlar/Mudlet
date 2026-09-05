# Mudlet design language

The shared UI language for Mudlet's redesigned dialogs. Seeded by the settings
redesign (`src/dlgProfilePreferences.cpp`), consumed next by the editor
redesign. Read this before building or migrating a dialog; the settings shell is
the worked example to copy from.

The helpers live in `src/uiDesign.h` (namespace `uiDesign`), implemented in
`src/uiDesign.cpp`; the sidebar's item delegate is in
`src/SidebarItemDelegate.h`. Consume them - do not copy them out into a new
file.

## 1. Principles

### Colour comes from the palette, at runtime

Every colour is derived from `QApplication::palette()` when the shell is styled,
never written as a hex literal. See `applyShellStyle()`
(`src/dlgProfilePreferences.cpp`) and the two helpers it derives colours with
(`src/uiDesign.cpp`):

- `blend(from, to, amount)` - mixes two palette colours to get borders, muted
  text, scrollbar handles.
- `rgba(color, alpha)` - a stylesheet colour string with an alpha component.

Four palette roles carry the design: `QPalette::Window` (page),
`QPalette::Base` (field), `QPalette::WindowText` (text), `QPalette::Highlight`
(accent). Two of the four are taken as they come and two are softened before
anything is mixed from them - see below. Light and dark treatments are chosen
from the measured lightness of the page colour (`darkPage`), not from
`mudlet::inDarkMode()`, so a dark system theme under "follow the system" is
handled too.

This is correctness, not taste: a profile's Lua stylesheet can retheme the whole
application, and a hardcoded colour becomes unreadable the moment it does. Read
`qApp`'s palette rather than the dialog's own - assigning a stylesheet to a
widget freezes that widget's palette.

### Three tones of depth

`themeTokens()` mixes three surfaces, and which one a rule reaches for is the
whole of what says how deep the thing it draws sits. Pick by what the widget
*is*, never by what colour looks right:

| Token | Recipe | Use it for |
| --- | --- | --- |
| `page` | `QPalette::Window` | The window itself and every piece of it: toolbar, status bar, sidebar pane, the column an item is edited in, scroll areas |
| `card` | `page` lifted towards white (6% on dark, 55% on light) | A panel raised off the page: the options cards, popup and menu surfaces |
| `field` | `QPalette::Base`, lifted 30% towards `page` on dark | Anything the user types into or picks a value in: line edits, combo boxes, spin boxes, the search field, a check indicator's fill |

The order is fixed - `field` is sunk into `card`, `card` is lifted off `page` -
and it is what makes the windows read as having depth rather than as flat
collapsing to black. Deriving the page from `QPalette::Base` gets this exactly
backwards: Base is the *input field* colour, near-black on a dark theme, so a
page mixed off it ends up darker than the fields lying on it.

A dark theme's `Base` is near-black - #191919 under a #353535 page - which
reads as a hole cut through the window rather than as a well sunk into it, so
the dark field is lifted back towards the page and lands at #212121. A light
theme's `Base` is white and is left where it is: lifting white towards a grey
page only muddies it.

### The words are softened towards the page

`text` is not `QPalette::WindowText` as it comes. A palette answers pure white
on a dark theme and pure black on a light one, and neither is a colour a page of
text is set in - the white glares against a #353535 page and the black is
heavier than anything else in the window. So the words are pulled a little way
back towards whatever they are written on: 16% on dark, which turns #ffffff into
#dfdfdf, and 10% on light, which turns #000000 into #181818.

| Theme | palette `WindowText` | `text` | `mutedText` | `page` | `card` | `field` |
| --- | --- | --- | --- | --- | --- | --- |
| dark (#353535 window, #191919 base) | #ffffff | #dfdfdf | #afafaf | #353535 | #414141 | #212121 |
| light (#efefef window, white base) | #000000 | #181818 | #585858 | #efefef | #f8f8f8 | #ffffff |

Every other ink is mixed from `text` over a surface, so the whole scale softens
with it - `mutedText`, `disabledText`, `border`, and the `accentText` blend.
That is the point rather than a side effect, and the contrast is measured rather
than assumed (`EditorSurfaceToneTest`, `ReadabilityAuditTest`): body text clears
4.5:1 on all three surfaces in both themes - 9.2:1, 8.9:1 and 7.7:1 on dark,
15.4:1, 15.7:1 and 16.7:1 on light. `mutedText` is a quieter weight of the same
words rather than a different class of thing - a card's description, a chip, the
status bar - so it carries the same floor and is walked until it meets it on all
three: 5.6:1, 5.4:1 and 4.7:1 on dark, 6.2:1, 6.3:1 and 6.7:1 on light.
`disabledText` is walked the same way to the 3:1 an inactive word is held to,
over the three surfaces and the field as well.

#### One ink for the editor's chrome

In the script editor, `mutedText` is not one tone among several - it is *the*
tone. Every word the window says outside a field is written in it: the toolbar's
buttons, the sidebar's names, every row of all seven item trees and their
headings, the search results' titles, a card's title and everything on the card,
check boxes, radio buttons, group boxes, the pattern rows' numbers and prompt
labels, the "Lua script" heading, the summary strip, the Options button and the
status bar. The full `text` tone is what is *inside* a field - a line edit, a
spin box, a combo box's displayed value, the code pane, the error console - and
nothing else has it. Two greys in one window's chrome read as two windows.

Three things keep an ink of their own, and each says a state rather than a tone:
`accentText` for what is chosen or under the pointer, `disabledText` for what is
unavailable, and the state chips - the compile chip, the OR/AND mode chip, and
the note that refuses a duplicate event name - whose colour is walked against
their own fill by `readableOn()`. And what the user typed is content wherever it
is shown, not chrome: a script's own event names on their chips keep the full
`text` tone the way the words in a field do, while Mudlet's own `sys*` events on
the same row are quiet.

One consequence is deliberate: a tree row that is switched off and one that is
running are now the same tone, since `EditorTreeDelegate` already quietened the
off rows to `mutedText`. The **dot** at the row's leading edge says whether the
item is on, not the weight of its name.

A `color:` rule cannot reach a view's rows - a stylesheet only gets at them
through `::item`, and a `::item` rule reaches the widget's palette for some
selectors and not others - nor a control that paints itself, such as the
sidebar's collapse chevron. Those are told outright, through
`inkAsChrome()` in `src/dlgTriggerEditor.cpp`, which writes the tone into
`QPalette::Text`, `WindowText` and `ButtonText` and the accent into
`HighlightedText`. It runs *after* anything that re-polishes the widget: a
re-polish puts back the palette the rules were applied to.

The settings dialog is not held to this. Its sidebar is the whole of that
dialog's navigation and keeps the full tone (the `itemColor` argument to
`sidebarStyleSheet()`), and `cardStyleSheet()` - shared by both windows - writes
no title colour at all, so a settings card's title stays whatever the palette
answers. The editor colours its own cards' titles from its own sheet;
nothing about the shared builder changed.

`test/functional_tests/EditorChromeInkTest.cpp` is the guard. It walks every
visible, enabled thing in the editor that shows words - labels, buttons with
text, group box titles, and each view once by its palette - in every one of the
seven views, with an item on show in each - skipping fields and everything under
them, the state chips and a chip's own name by object name (`editorCompileChip`,
`editorModeChip`, `editorChipNote`, `editorChipLabel`), and anything chosen,
pressed or under the pointer, and compares each ink against `mutedText` on the
dark appearance and then the light. It reports how many things it read and fails
below a pinned floor, so a walk that stopped finding widgets cannot pass. One
case reads pixels rather than palettes: the ink of the Triggers heading row, off
a grab of the window, has to be that same tone to within two levels per channel.

Anything mixed to sit *on* a surface takes that surface as its `from`:
`border`, `mutedText` and `disabledText` are blends over `page`; a card's check
indicator outline is a blend over `card`; placeholder text is a blend over
`field`. The three tones can collapse where a palette leaves no room - macOS
answers white to `Window` and `Base` alike on its light appearance - so
`themeTokens()` steps the page down instead of the card up when the lift would
be under six levels of lightness. Neither window does that arithmetic itself.

Two more tones say where one column of a window ends and the next begins. They
are not a fourth and fifth level of depth - `pane` is the *smallest* step the
model has, and `separator` is a line rather than a surface:

| Token | Recipe | Use it for |
| --- | --- | --- |
| `pane` | a fifth of the way from `page` to `card` | A column that is a surface of its own rather than a piece of the page, taken as one thing from the row heading it to the trees under it: the editor's panel of items (`#editorItemPane`, its trees and their scroll bars). Two levels of lightness on a dark theme - enough to be told apart from the page beside it, and far short of reading as a panel laid on top of it |
| `separator` | `page` taken towards black (36% on dark, 10% on light) | The seam between two panes, and along the sidebar's right edge. A `GripSplitterHandle` carrying nothing draws it as one pixel down the middle of itself, with each neighbour's own tone carried up to it, so the nine pixels the mouse needs are not nine the reader sees; hovered, that line widens to three of the accent. A handle carrying a heading - the code pane's - is the same tone filling a strip deep enough to read it in, with the top corners cut to `scmRadiusPanel` since it is the top of the pane under it; hovered, that one lights the same three pixels of the accent along the edge the drag moves, clipped to the cut corners, since a strip with words on it has no room to say it twice. A groove cut into the window, as against `border`, which is a hairline drawn on it |

The separator's drop is the smaller one on a light theme on purpose: a light page
is near enough to white that a dark-theme drop would draw a grey rule across the
window rather than a seam between two panes. Measure a pane against the page it
lies beside, never against a card: a pane a card's distance off the page stops
reading as part of the window.

### One treatment for every input

`inputStyleSheet(tokens, selectorPrefix)` draws everything a value is typed into
or picked in - line edits, combo boxes, spin boxes - as one control: the `field`
surface, a 1px border, `scmRadiusInput`, an accent frame on focus. Framing a
`QComboBox` or a `QAbstractSpinBox` from a stylesheet removes the arrows the
platform draws inside it, so the sheet claims those two only once it has tinted
arrow PNGs cached to point at, and otherwise leaves them the platform's frame.

Scope it, never set it on a window: the editor sets it on each of its seven
forms, the settings dialog passes `#settingsStack` so the rules stop at the
pages. Unscoped it would take the search field, the sidebar's editors and every
tree's inline editor with it.

### One sidebar for both windows

The panel down the left is one component, not two that resemble each other.
`sidebarStyleSheet(listName, separatorName, itemColor, metrics, tokens)` draws
all of it: the pill per item, the hover wash, the chosen item's gradient and its
accent bar, the ring that says the list holds the keyboard
(`[settingsFocused="true"]`), the collapsed variants (`[settingsRail="true"]`)
and the divider rows. `setSidebarCollapsed(pane, list, separatorName, collapsed,
metrics)` is the other half - the pane's width and margins, the `settingsRail`
property that both those rules and `SidebarItemDelegate` read the mode off, and
the re-polish that makes it take. It answers whether anything moved, so a window
with more to do at that moment can skip it too: the settings dialog hides its
wordmark and offers the hidden names as tooltips, the editor's rows already
carry a tooltip naming their shortcut.

`SidebarMetrics` carries the measurements the two windows differ by - the two
widths, the padding at each, the vertical padding and the divider inset - and
the `itemColor` parameter beside it carries the only other difference, the
colour an unchosen name is written in: muted in the editor where all the chrome
is, full strength in the settings dialog where the sidebar is the navigation.
The accent bar is a gradient stop rather than a `border-left`, which would be
drawn as an arc where the pill's corner radius is and pinched to nothing at both
ends; a stop is a *fraction* of the item, which is why those widths have to be
known numbers.

A chosen row in the editor's item trees carries the same bar, at the same width
- `scmAccentBarWidth` in `src/uiDesign.h`, so the two lists cannot come to
disagree about it - and it reads the same: one straight stroke down the row's
leading edge rather than a bracket. It is neither a stop nor a border. A tree row
is as wide as the panel happens to be dragged to, so there is no fraction to
write a stop at; and a `border-left` follows the pill's corner radius, bending
inward at both ends until the bar is pinched to nothing.
`EditorTreeDelegate::paint()` fills the rectangle itself, over the pill the style
has just drawn - the full height of the row, so the two ends are square, where
the sidebar's is clipped by its own pill and rounds off. The transparent
`border-left` stays on every row, never coloured, because it is what holds the
gutter the bar stands in - the row's padding gives back what it takes, so nothing
steps sideways when a row is chosen and the delegate's dot, chevron and mark stay
where they were.

`treeWidget_variables` is drawn by a delegate of its own, `VariableTreeDelegate`
(`src/VariableTreeDelegate.h`), because what it shows is what the Lua
interpreter holds rather than what the profile is made of: nothing in it is on
or off. Its row is the same grammar - the same pill, the same straight bar
painted by the delegate, the same delegate-drawn chevron and indentation, from
the measurements both delegates take from `src/EditorTreeRowMetrics.h` - with
the two slots at the row's leading edge read differently, which "One mark, one
size" below describes.

### Radius follows control size

How round a corner is says how big the thing behind it is. The same 8px that
reads as a card's corner turns a chip into a lozenge, and the 4px that suits a
chip leaves a card looking square - so the radius is proportional to the control,
and the scale is four named constants in `src/uiDesign.h`. No rule writes a
radius of its own.

| Constant | Value | Use it for |
| --- | --- | --- |
| `scmRadiusChip` | 4px | A word in a box: the ID beside an item's name (`#frameId`), the kind beside a search result (`SearchResultDelegate`), the OR/AND beside a matching mode (`#editorModeChip`), the compile state over the code pane |
| `scmRadiusInput` | 5px | The controls a form is filled in through, a little over 30px tall: line edits, combo boxes, spin boxes - every rule in `inputStyleSheet()` |
| `scmRadiusPanel` | 8px | The boxes a window is laid out in: `settingsCard` and `editorCard` group boxes, the migration banner, the editor's notice frame, the deep-link spotlight ring |
| `scmRadiusProminentInput` | 8px | A search field: the one control a panel is headed by rather than one of several filled in on it, and drawn taller than a form control, so it takes the corner of the panel it heads (`#settingsSearchField`, `#editorSearchRow QComboBox`) |

Sizes not in the scale stay literal on purpose: the 6px of a hovered toolbar
button or a navigation row, the 8px pill of a sidebar item, the 3px of a check
indicator or a marker-pen highlight. Those are rows and glyphs, not the boxes
this scale is about.

### Font sizes are relative

Stylesheet font sizes are percentages, never points: `#settingsPageTitle
{ font-size: 145%; }`, `#settingsWordmark { 125% }`, `#settingsHeroHeadline
{ 115% }`. Absolute point sizes break accessibility settings and every platform's
interface font.

### Cards, not bare group boxes

Content is grouped into cards: a `QGroupBox` carrying the `settingsCard` dynamic
property, styled with a background, a 1px border, a `scmRadiusPanel` radius and
16px padding - 12px in the editor's narrower options column. Pages are a single
column of cards with 16px spacing (`createScrollPage()`, `buildPage()`).

The card is one component for both windows, the way the sidebar is.
`cardStyleSheet(metrics, tokens)` draws the frame and the title inside it, and
`cardIndicatorStyleSheet(cardProperty, tokens)` the check indicator a checkable
card's title begins with - a sheet of its own, because the title height the
frame reserves room for has to be measured with those rules already in force.
`CardMetrics` carries what the two windows differ by: the property the rules
select on (`scmProp_settingsCard` or `scmProp_editorCard`, interpolated into the
selectors rather than spelled out again), the padding, that measured title
height, and the two variants only the settings dialog has so far - a plain
property (`scmProp_settingsCardPlain`) and the flattening of a group box the
`.ui` file nested inside a card. Everything else - the surface, the hairline, the
corner, where the title goes, what the indicator is drawn as - is the same in
both and comes out of those metrics and the tokens.

The title is the card's first line **inside** the frame, not a heading above it:
`subcontrol-origin: padding` with `left` and `top` set to the card's own
padding, so the title starts on the same left edge as the controls under it. A
stylesheet reserves no room for a title placed that way, so the card's top
padding does it - `padding + measuredCardTitleHeight() + scmCardTitleGap` -
and getting that number wrong draws the first control over the title. A
checkable card's title line begins with its check indicator on that same left
edge and the words after it, which is where the style puts them; nothing insets
a plain card's title to match, because on a card whose controls all start at the
padding edge that would indent the heading away from what it heads.

### Human copy

Controls state what happens in words a player understands. A card may carry a
description label (`settingsCardDescription`) under its title. Keywords and
descriptions are `tr()`-wrapped with `//:` translator notes.

### Measured responsiveness

Breakpoints are computed from real content, never hardcoded pixels. The pattern
is `sidebarWidths()` / `updateSidebarMode()` in
`src/dlgProfilePreferences.cpp`:

- Widths are measured off font metrics, `sizeHint()`s and the actual scrollbar
  width, over every page rather than the one on show.
- `fullyExpanded` caps the window's maximum width; `collapseBelow` is a
  deliberately *different*, smaller number, so the sidebar cannot oscillate
  between its expanded and rail modes on a one-pixel drag.
- The test is against the window's own width, not the space left over, so
  collapsing cannot flip the condition that caused it.
- The measurement is the per-window half. What is done with its answer is
  `uiDesign::setSidebarCollapsed()`, shared with `editorSidebarWidths()` /
  `updateEditorSidebarMode()` in `src/dlgTriggerEditor.cpp`, which measures the
  longest row name where the settings dialog knows its width outright.

#### The editor's actions toolbar gives its names up, never its actions

A `QToolBar` too narrow for what it holds posts the tail of itself into a
drop-down behind a chevron a few pixels wide at the far edge of the window.
Nothing says it is there. `fitEditorToolBarToItsLength()` in
`src/dlgTriggerEditor.cpp` takes the names off instead, and nothing is ever
hidden while a word is still written out:

- The bar's buttons are held in groups (`mEditorToolBarGroups`), and that list
  is the order they give their names up in: the four that act on the profile -
  Import, Export, Create Module, Save Profile - first, the four that act on the
  item being edited after. Undo and Redo are pictures from the start.
- A collapsed group is `Qt::ToolButtonIconOnly` per button, through
  `toolBar->widgetForAction()`; the split Save Profile button keeps its menu
  arrow. Every one of the eight leads its tooltip with the words it would
  otherwise be carrying, which for the item four is the view's wording - "Add
  Trigger", "Add Alias" - rebuilt by `updateEditorItemActionToolTips()`.
- What is measured is `toolBar->layout()->sizeHint()` along the bar's
  orientation against the bar's own width or height, so separators, the grip,
  the spacing and a stylesheet's padding are counted the way Qt counts them, a
  bar docked at a side is measured down its length, and the answer does not
  change when Qt has already folded something away.
- Names come back from the other end, and only with `scmEditorToolBarRestoreMargin`
  (16px) to spare. Giving them up costs nothing to spare, so the two tests never
  agree on the same pixel and a drag across the breakpoint settles.
- It runs on the bar's own resize (an event filter, since the bar is the only
  thing the window tells), on the icon size preference, on the view changing
  (the item four are renamed there), on a language, style or font change, on
  `applyEditorShellStyle()`, and once the window has first been shown. Measured
  at 39us a call, so a resize drag is answered rather than coalesced.
- Torn off into a window of its own the bar starts from every name shown, since
  a float is sized to what it holds; the resize that gives it that size fits it
  again.

Measured on the offscreen platform at the default 18px glyph: the profile group
gives its names up at 1120px of window and the item group at 848px; at the
largest the preference offers, 24px, at 1184px and 904px.

**The fold is left in place and is reachable.** A bar with every name given up
still wants 477px of window at 18px glyphs and 537px at 24px, against a window
whose own minimum lets it be dragged down to 279px and below - so between those
figures Qt does fold, on a window far too narrow to edit anything in. A
scrolling host of our own would only be a second way of hiding the same buttons,
so what is done instead is to
make the button unmistakable: the pill is a `qt_toolbar_ext_button` rule in
`applyEditorShellStyle()` - card fill, border hairline - and the chevron on it is
drawn by `inkEditorOverflowChevron()` in `accentText`, because a `QStyle` hands
that button a picture of its own and a stylesheet cannot recolour one.
`test/functional_tests/EditorToolBarOverflowTest.cpp` sweeps the window width and
holds all of it, the numbers above included.

### Shell over .ui

The `.ui` file is not rewritten. Existing widgets are detached from their
designer layouts and moved into a runtime-built shell:

- `detachFromLayout(pWidget)` - removes a widget from its parent's layout tree.
- `moveIntoCard(pCard, controls)` - reparents controls into a card's layout.
- `buildPage(objectSuffix, cards)` - detaches cards, marks them `settingsCard`,
  stacks them in a scrolling column.

This keeps object names, signal connections and every translated string intact,
so a redesign costs no translation churn and no `.ui` merge conflicts.

The editor's five field-only forms - aliases, timers, keys, scripts, variables -
are shelled the same way in `buildEditorFormHeadRows()`
(`src/dlgTriggerEditor.cpp`): the name, the command and the ID pill are lifted
out of each `.ui` grid into a head row, `insertGridRowAtTop()` puts that row
above what is left of the grid, and the rows under it are built in place from
the grid's own controls - the timer's four fields into a sentence, the key's
field into one that listens, the variable's two pickers onto one row. A control
that is replaced rather than moved (the script's event list, which became a row
of chips) leaves the `.ui` file, and the runtime builds its replacement into the
cell the label leads. The `.ui` files did lose their colons and their caps on
field heights in that pass: a string that changes anyway is not churn.

### The rows of a form

Every form the editor fills in leads with the same row: the name, whatever is
typed beside it (a command, where the item has one), and the ID pill. The
trigger form's `widget_top` is the original; `buildEditorFormHeadRows()` gives
the five field-only forms the same row at the same measurements, so a view
switch never moves the Name field. Under it, each row leads with one word.

- **Lead labels share one width.** `alignEditorFormLeadLabels()` measures the
  widest lead word across the forms in the font the window is running at and
  gives every lead label that width, and every Name label that width plus the
  difference between the head row's spacing and the grid's - so a field on any
  row of any form starts at the same x. It runs at style time and again on a
  font change. Labels carry the `editorRowLabel` property and take the quiet
  ink; they lost their colons in the same pass.
- **The ID pill** (`#frameId`, `editorIdChip`) is `styleEditorIdChip()` on all
  six forms: `chipFont()`, a corner of half its measured height, drawn from the
  shared `formRules` rather than once per form.
- **A sentence round a control.** `buildControlSentenceRow()` lays words and
  controls into one row from a translated template - one control with `%1`, or
  several with `%1`..`%n` wherever the translation puts them; a placeholder the
  translation lost still leaves its control on the row. The timer's interval is
  one: "Fires every %1 h %2 min %3 s %4 ms", or "Fires once, ... after the timer
  above it fires" for an offset timer (`editorTimerInterval`). The words are
  scaffolding and take `editorRowLabel`; each control keeps an accessible name
  of its own, since a screen reader never reads the word beside it.
- **A field that listens.** The key binding is a read-only field that arms the
  editor's key grab when clicked, or on Return or Space: it carries
  `editorListening` while it waits, the accent frame and wash with it, its
  placeholder says what to press, and a hint beside it (`editorKeyHint`) says
  what happens next. Escape, losing the focus, a save, a view change or the
  window hiding all end the grab through `endKeyGrab()`; a cross
  (`editorKeyClear`) forgets the keystroke. The grab keeps the keypad modifier.
- **Chips** (`uiDesign::ChipRow`, `src/ChipRow.h`, over `uiDesign::FlowLayout`)
  hold a set of short names the user adds and takes away: a script's events.
  One chip per name with its own cross, and a dashed "Add event" that becomes a
  field in place; Return or a comma commits and keeps the field open for the
  next name, Escape closes it, losing focus keeps a typed name and drops an
  empty one, a name already listed is refused with a note (`editorChipNote`)
  beside the field. Chips wrap, and the row reports the wrapped height through
  its size hint and `heightForWidth()`, so the column it is in follows. A
  script's own event names keep the full `text` tone - they are content, like
  the words in a field - and Mudlet's own `sys*` events read in `mutedText`
  (`editorChipSystem`). `chipFont()` is the one recipe for a word in a box: the
  ID pill, the chips, the hidden-variables count, the value preview in the
  variables tree.

### The seam over the code pane

The heading over the Lua pane is a `GripSplitterHandle` carrying a strip, and
whether it also resizes depends on what the form above it holds. In Triggers and
Buttons the form has something that uses room - a pattern list, a stylesheet
editor - so the heading drags, and `fitFormPaneToItsContents()` snaps the column
to what the item asks for unless the user has dragged that view's handle in this
session (`mDraggedFormPaneHeights`). In Aliases, Timers, Keys, Scripts and
Variables the form is a fixed set of fields: `formPaneResizes()` says no, the
handle is made inert (`GripSplitterHandle::setResizes(false)` - no grip drawn,
no cursor, no drag), and `holdFormPaneToItsContents()` caps the column
`mpNonCodeWidgets` at its size hint and gives the code pane the rest, keeping the
pane's floor `scmEditorSourcePaneFloor`. A `LayoutRequest` on the column re-runs
the cap, so a notice appearing, a row hidden for a key group or chips wrapping
onto a second line all move the seam by themselves. `EditorFormShellTest` holds
both halves: a push on the handle leaves a fixed view's column where it was, and
still moves the trigger form's.

## 2. Icons

- Monochrome line icons from [Lucide](https://lucide.dev), ISC licence,
  attributed in the About dialog (`src/dlgAboutDialog.cpp`).
- Shipped as SVG in `src/icons/` (`settings-general.svg`,
  `settings-appearance.svg`, ...), exactly as Lucide authors them.
- Loaded only through `glyphPixmap(file)`, which rasterises at
  `scmGlyphRasterSize` (128px) and draws every glyph at
  `scmGlyphStrokeWidth`. Lucide authors at a stroke width of 2, which reads
  heavy at the sizes this window draws at; the weight is a token here rather
  than a property of the asset, so changing it is one number and not 83
  re-rendered files. Results are held in `QPixmapCache` - parsing an SVG per use
  is what made the About dialog's chip row measurably slow when its glyphs were
  re-encoded per chip.
- The shape still lives in the alpha channel: `glyphPixmap()` draws onto a
  transparent pixmap, so everything downstream of it is unchanged.
- Tinted at runtime by `tintedGlyph(source, color)`: fill through the alpha with
  `QPainter::CompositionMode_SourceIn`, which keeps the antialiased edges that
  per-pixel recolouring would harden into a staircase.
- Selected-state variants are added explicitly via
  `QIcon::addPixmap(..., QIcon::Selected)`, otherwise the view washes the icon in
  the highlight colour.
- Inline copies for rich text go through `inlineGlyph(glyph)`, which base64s the
  tinted pixmap into a `data:` URI - a `QLabel`'s rich text can only reach a
  picture through a URL, and a runtime-tinted glyph has no path.
- Re-tinting on a theme change happens in one place, `restyleSidebarIcons()`,
  called from `applyShellStyle()`.

The main window toolbar is the third consumer, and the detached profile window
builds the same bar from the same list (`src/mudlet.cpp`,
`src/TDetachedWindow.cpp`). Its own glyphs are named `toolbar-*.svg`. The seven
editor concepts - Triggers, Aliases, Timers, Buttons, Scripts, Keys, Variables -
take the editor's `editor-*.svg` files rather than copies of them, so that a
trigger is the same picture wherever it is offered; the module manager takes
`editor-module.svg` for the same reason. Three glyphs come from outside Lucide,
from [Simple Icons](https://simpleicons.org) under CC0 1.0: the Discord mark
(`toolbar-discord.png`) and the GitHub and Patreon marks in the About dialog.
Those three stay PNG - they are filled shapes with no stroke, so there is no
weight to set, and `glyphPixmap()` reads any non-`.svg` file as a raster.

Each window keeps a `QList<uiDesign::ActionGlyph>` of action to file and a
`restyleToolBarIcons()` that re-inks every entry through
`tintedIcon(glyphOff, glyphOn, tokens)`. That builds all eight mode/state
pairs - `Normal`/Off in `mutedText`, `Normal`/On and every `Active` and
`Selected` in `accentText`, `Disabled` in `disabledText` - so a checkable action
that is currently doing something reads as lit. Full Screen and the Sound family
pass a second file for their On state. The split buttons keep Qt's own
`MenuButtonPopup` arrow and hover frame; nothing is drawn over the glyphs.

Everything else the editor draws as a picture comes from the same family and
the same tinting. The notice banner's three pictures are `editor-notice-info.svg`
(info), `editor-notice-warning.svg` (triangle-alert) and
`editor-notice-error.svg` (circle-x): tinting keeps only the shape the alpha
channel carries, and the old full-colour `dialog-*.png` bitmaps have a solid
alpha, so the information notice came out as a plain disc. The cross on a chip,
beside a key's binding and on the sound field is `editor-clear.svg` (x); the
plus on "Add event" is `editor-add.svg`; the variables tree's type marks and its
hidden mark are listed under "One mark, one size" below. A new glyph is Lucide's
own SVG file, copied in under the name this window knows it by - nothing is
rendered ahead of time.

### One mark, one size, on every row of an editor tree

A row in the editor's six item trees leads with a state dot, and beside it a
mark for what the dot cannot say: `editor-folder.png` for a group,
`editor-filter.png` for a trigger other triggers are matched inside of,
`editor-offset-timer.png` for a timer armed by the one above it,
`editor-errors.png` for an item that will not compile - the same glyph the
Errors view carries in the sidebar - and `editor-new-folder.png` /
`editor-new-item.png` for something the editor has made and nobody has saved.
`EditorTreeDelegate` resolves which from the item the row's id names and draws
it; a row's own `QIcon` is never consulted, so no call site sets one.

Every mark is drawn at 16px, the size `SearchResultDelegate` gives the glyphs in
the results list, and a row leaves that much room whether it carries a mark or
not - so a tree's heading, a folder and a plain item are one height and the
hover fill on one row is the same shape as the selection pill on the next.
The ink is `mutedText`, or the colour the trees' stylesheet writes a chosen row's
name in (`accentText`) while the row is selected, so a mark and the name beside
it are always the one colour.

The seventh tree reads the same two slots differently, on the same row.
`VariableTreeDelegate` draws a kept square where the dot stands: filled in the
green a running dot is filled in when the variable is saved with the profile,
hollow when it is not, half when only some of a table is, and absent for a row
the profile cannot keep - a function, a reference, a table past the size limit -
which is written in `disabledText` with the reason as its tooltip. Beside it
stands a mark for the value's type: `editor-variables.png` for a table (a table
is braces everywhere, the sidebar included), `editor-type-string.png` (quote),
`editor-type-number.png` (hash), `editor-type-boolean.png` (toggle),
`editor-type-function.png` and `editor-type-other.png` (box). After the name -
an index key is drawn as `[n]` - comes `editor-hidden.png` on a row that is
hidden while hidden variables are shown, and at the trailing edge as much of the
value as the panel leaves room for, in `chipFont()`: the string in quotes, the
number, `true` or `false`, `{ n keys }` or `{ n items }`. Everything a row is
drawn from is written into the row's data roles when it is built or written back
(`setVariableRowData()`, `refreshVariableRow()`), so painting never reaches into
Lua or counts a table. A click on the square asks the editor for the toggle
(`slot_toggleVariableKept()`), which is where the rule about which members of a
table may be kept lives. The switch under the tree says how many globals it
holds back (`editorHiddenVariablesCount`).

## 3. Naming

### Object names

The object names are the dialog's test interface as well as its stylesheet
handles; keep them stable.

| Name | What it is |
| --- | --- |
| `settingsShell` | The runtime-built root widget |
| `settingsSidebar` | The category column |
| `settingsCategoryList` | The `QListWidget` of categories |
| `settingsSidebarSeparator` | Divider row inside the category list |
| `settingsWordmark` | "Settings" title at the top of the sidebar |
| `settingsContent` | The right-hand pane |
| `settingsPageTitle`, `settingsPageTitleIcon` | Title row over a page |
| `settingsStack` | The `QStackedWidget` of pages |
| `settingsPage_<key>` | One category or subpage `QScrollArea` |
| `settingsColumn_<key>` | The card column inside that scroll area |
| `settingsSearchField`, `settingsSearchBack`, `settingsSearchEmpty`, `settingsSearchHeader` | Search chrome |
| `settingsSubpageBack` | Chevron back out of a subpage |
| `settingsCardDescription` | Description label under a card title |
| `settingsHeroHeadline`, `settingsHeroDetail`, `settingsHeroLink` | Hero card parts |
| `settingsCheckBoxWrap`, `settingsWrappedLabel` | A check box whose text wraps |
| `settingsSearchDebounce` | The search timer - tests wait on it by name |

### Dynamic properties

Stylesheets select on these; setting one after the widget is shown needs an
`unpolish()`/`polish()` pair to take effect.

| Property | Meaning |
| --- | --- |
| `settingsSurface` | Shell scaffolding a profile's Lua stylesheet must not colour |
| `settingsCard` | This group box is a card |
| `settingsCardPlain` | A card without the top padding its title would need |
| `settingsHero` | The prominent lead card of a page |
| `settingsChevronRow` | A button that reads as a navigation row into a subpage |
| `settingsRail` | The sidebar is collapsed to icons only |
| `settingsFocused` | The category list has keyboard focus |
| `searchMatch` | Marker-pen highlight on a search hit |
| `searchKeywords` | Comma-separated synonyms fed into the search index |
| `settingsRichText` | The text a label was given, before the colour of any link in it was written in - see `withLinkColour()` |

...and the About dialog's own, with an `about*` prefix:

| Property | Meaning |
| --- | --- |
| `aboutCard` | This group box is a card |
| `aboutCardPlain` | A card with no title, and so no room reserved for one |
| `aboutChip` | A word in a box: the channel, the Qt version, a contact handle, a licence kind |
| `aboutChipLit` | ...and that chip filled with the accent, which the build channel is |
| `aboutButton` | An ordinary push button on this dialog |
| `aboutCopied` | ...one of them for the moment after it has copied |
| `aboutPrimaryButton` | The one button that is an invitation rather than a control |
| `aboutNavGlyph` | Which file a navigation button's glyph is re-inked from |
| `aboutRestingText` | What a Copy button reads when it is not saying "Copied" |
| `aboutRichText` | The text a label was given, before its links were inked |

The editor redesign follows the same scheme with an `editor*` prefix:
`editorShell`, `editorSidebar`, `editorPage_<key>`, `editorCard`, and so on, and
the About dialog with an `about*` one: `aboutShell`, `aboutArtColumn`,
`aboutNav`, `aboutNavButton_<key>`, `aboutStack`, `aboutPage_<key>` and
`aboutColumn_<key>`.

Three of the `settings*` names are not the settings dialog's alone.
`settingsRail` and `settingsFocused` are the contract `SidebarItemDelegate` and
`sidebarStyleSheet()` are written against, and `settingsSurface` - which
`markAsShellSurface()` puts on - is what every shell's stylesheet keeps its own
scaffolding transparent by. All three keep these names in every window.

### Anchors are inked in the text, not in a palette

A `QLabel` parses its rich text the moment it is set and bakes the colour of
every anchor into the document then and there, taking it from the *application*
palette. Writing `QPalette::Link` to the widget afterwards does nothing, and
re-polishing it does nothing either - the link stays at Qt's own blue, which is
2.4:1 on a dark page. `uiDesign::withLinkColour(richText, colour)` writes the
colour onto the anchor instead, which a document does honour; the text a label
was given is kept in a dynamic property (`aboutRichText`, `settingsRichText`) so
that an appearance change can ink it again. A `QTextDocument` ignores an
`a { color: ... }` rule in a `<style>` head for the same reason, so the licence
browser's own anchors go through the same helper.

The editor's forms add these, which its tests reach it by:

| Name | What it is |
| --- | --- |
| `widget_top` | The head row of a form: name, command, ID pill |
| `frameId` | The ID pill on every form |
| `editorScriptEvents` | The `ChipRow` of a script's events |
| `editorChip`, `editorChipLabel`, `editorChipRemove` | One chip, its name and its cross |
| `editorChipAdd`, `editorChipEditor`, `editorChipNote` | The dashed add button, the inline field, the "already listed" note |
| `editorTimerInterval` | The sentence row holding a timer's four fields |
| `editorKeyBindingRow`, `editorKeyHint`, `editorKeyClear` | The key binding field's row, the hint beside it, the cross that forgets the keystroke |
| `editorVariableTypes` | The row holding a variable's key and value pickers |
| `editorHiddenVariablesCount` | The count beside the "Show hidden variables" switch |
| `editorCodeHeaderTitle` | The word on the code pane's heading: "Lua script", or "Value" in the variables view |
| `editorNotice*` glyph files | The banner's three pictures |

| Property | Meaning |
| --- | --- |
| `editorRowLabel` | A word leading or joining a form row, written in the quiet ink |
| `editorIdChip` | The frame drawn as the ID pill |
| `editorPanelSurface` | A row widget that shows the form through, so a profile stylesheet cannot paint a band across it |
| `editorChipSystem` | A chip holding one of Mudlet's own `sys*` events, read in the quiet ink |
| `editorListening` | The key binding field while it waits for a keystroke |

## 4. Search over a widget tree

`buildSearchIndex()` (`src/dlgProfilePreferences.cpp`) walks the real widget tree
rather than a hand-written list, so a control added to the `.ui` file later is
searchable without anyone saying so.

- **Index unit is the card.** Each entry records the card, its category, its
  subpage, its folded text, and its home layout plus index.
- **Normalization** is `foldForSearch()`: strips rich-text tags and `&`
  accelerators, NFKD-decomposes, drops non-spacing marks, simplifies whitespace,
  case-folds. So "fonte" finds "Fonté" and "save" finds "&Save".
- **Text sources** per widget (`collectSearchText()`): visible text, tooltip and
  the `searchKeywords` property. Combo box items are indexed; `QFontComboBox`
  items deliberately are not, or every card with a font picker matches "mono".
- **Synonyms** are set as `searchKeywords` with a `//:` translator note telling
  translators to write what a player would type rather than transliterate - see
  the `synonyms` list in `setSearchKeywords()`.
- **Highlighting** sets the `searchMatch` property and re-polishes
  (`setSearchMatch()`).
- **Results** are physically reparented into the results page and returned to
  their recorded home layout and index on the next keystroke
  (`returnSearchedCardsHome()`). Reparenting clears focus, so the category list's
  selection is cleared without clearing its current row - otherwise the focus-in
  that follows selects the first category and ends the search.
- Search input is debounced 150ms; the timer is named `settingsSearchDebounce`.

## 5. Instant apply and live sync

Available as a pattern; the editor keeps its explicit save for now.

- `connectApplyTriggers()` (`src/dlgProfilePreferences.cpp`) wires every control
  *by type* - `QAbstractButton::toggled`,
  `QCheckBox::checkStateChanged` (tri-state boxes),
  checkable `QGroupBox::toggled`, combo/spin/date-time value changes,
  `QLineEdit::editingFinished` - all with `Qt::UniqueConnection`, so it can be
  re-run after new controls appear.
- Applies run off a 400ms debounce timer.
- `SettingsSnapshot` tracks per-control values (`controlValue()` mirrors the same
  type list) and answers `dirty()`, `anyDirty()`, `shortcutsDirty()`,
  `pendingEdits()`, so an apply writes only what actually changed. A line edit
  being typed into is not a setting yet (`beingTypedInto()`).
- `guardScrollWheel()` plus the `eventFilter()` wheel branch stop a wheel passing
  over a spin box or combo box from silently changing a setting: unfocused
  controls ignore the wheel and do not take focus from it.

## 6. Testing

Each shell gets a test helper header plus focused test files, following
`test/functional_tests/SettingsTestHelper.h` and the `Settings*Test.cpp` set
(`SettingsSearchTest`, `SettingsShellNavigationTest`, `SettingsInstantApplyTest`,
`SettingsDirtyApplyTest`, `SettingsLiveSyncTest`, ...).

- The helper is free inline functions, not a `QObject`: a header is not listed in
  `test/functional_tests/CMakeLists.txt`, so nothing runs moc over it.
- Nothing in such a helper may use `QVERIFY`/`QFAIL` - both expand to a bare
  `return`.
- Tests reach into the dialog by the object names above.
- Waits are on named debounce timers and `QSignalSpy`, never `QTest::qSleep()`.
  An apply may already have run inside the interaction that scheduled it, so
  check before waiting.
- Prefer joining a grouped per-subsystem test binary over adding a standalone
  one - see `*_GROUP_TEST_SOURCES` in `test/functional_tests/CMakeLists.txt`.

### Verifying a stylesheet extraction

Moving rules into a shared builder is the one refactor here that no test
catches when it goes wrong. The sheets are built by `.arg()` chains, so
pulling a block out renumbers every placeholder after it, and a sheet that
is one substitution out of step is still a valid sheet - it just paints the
border colour where the background belongs. Nothing asserts on a stylesheet
string, so the suite stays green.

So verify by comparison, not by reading: dump every generated sheet from
every consumer before the change, make the change, dump again, and diff.
Do it in both themes - a wrong substitution can land on two colours that
happen to match on dark and diverge on light.

`test/functional_tests/StyleSheetDumpTest.cpp` is that harness. It is
deliberately not in `test/functional_tests/CMakeLists.txt`: it asserts
nothing and always passes, so wiring it into the suite would buy a profile
boot per run and prove nothing. Add it to the group sources by hand for the
comparison, take the two dumps, and drop it again. What ships is the diff
being empty, not the dumps - they are worth nothing once the question is
answered.

### Guards

Three tests hold the line the rest of this document describes. Text that cannot
be read in one of the two appearances, or that is written in a tone the design
does not use there, now fails a run rather than waiting for somebody to switch
theme and notice.

**`test/DesignColourLiteralTest.cpp`** reads the sources of every surface that
has adopted the design language and fails on a colour written out rather than
taken from `themeTokens()`. It links nothing - the `src/` path arrives through
`MUDLET_SRC_DIR`, the way `CMakeListsConsistencyTest` takes it - so it costs a
compile and a tenth of a second.

Per line, in C++ it flags a hex colour inside a string literal (a raw string
literal included), `QColor(Qt::name)`, a `Qt::` colour name reaching
`QColor`/`QBrush`/`QPen`/`setColor`/`setForeground`/`setBackground`,
`QColor("...")`, `QColor::fromRgb(...)` or `QColor(r, g, b)` with written
numbers, `QColorConstants::` anything but `Transparent`, and a CSS colour
keyword inside a string that also says `color:` or `background`. In a `.ui`
file it flags a `<color>` element and a `styleSheet` property containing any of
those.

Three things are deliberately not flagged. `QColor::fromHslF` and `fromHsvF`
build the semantic state hues, whose lightness already comes off the page.
`Qt::transparent` is never a theme colour. `Qt::white` and `Qt::black` are the
ends of the lightness axis rather than colours, so they pass inside a `blend()`
call or a `fill()` - by context, not globally, so `Qt::white` as an ink is still
caught.

**`// theme-fixed: <why>`** exempts a line, and the reason travels with the
code rather than living in a list inside the test - in a `.ui` file inside an
XML comment, and everywhere else as a `//` one. On a line of its own the
marker covers the run of lines under it, up to the next blank one, which is how
a table - a console's ANSI defaults, a map's palette - is marked without
repeating the reason on every row; and a colour inside a raw string literal,
which can carry no comment of its own, is marked where the literal opens. It is
legitimate for a colour that is a *value being shown* rather than chrome: a well
filled with the colour the user picked, a console or map palette the profile
owns, another application's brand in a picture of its window. It is not a way
to keep a colour somebody has not got round to mixing from the tokens.

**`test/functional_tests/ReadabilityAuditTest.cpp`** opens the editor - on a
trigger with three pattern rows, one of each shape, and its options panel shown
- and the settings dialog, moves the appearance to dark and then to light, and
for every visible thing that shows words compares the ink its palette answers
with against the colour most of the pixels behind it are. A stylesheet's
`color:` rule reaches the widget through `QStyleSheetStyle::polish()`, which is
why the palette is what is read; a case of its own proves that on a styled label
rather than assuming it. Floors are `scmTextMinimumRatio` (4.5:1) for text and
`scmQuietMinimumRatio` (3:1) for what is unavailable or not yet typed, both in
`src/uiDesign.h`. It reports how many things it read per window and per
appearance and fails if that count drops below a pinned floor, so a walk that
silently stopped finding widgets cannot pass as a clean one.

It skips three things, each for a reason rather than for convenience: the edbee
code pane, which carries a syntax theme with a background of its own; a colour
well, whose fill is a value and whose words are chosen against that fill by
`generateButtonStyleSheet()` - skipped by object name, listed in `wellNames()`;
and a *disabled* push button, the one control neither window draws the surface
of, whose bevel switched off is a grey the platform picked and which WCAG
exempts as an inactive component. The ink on that button is still the design's.

It also walks each of the editor's five field-only forms from the form widget
itself, with an item on show, so the words a form adds - the sentence round a
timer's fields, the hint beside a key's binding, a script's chips, a variable's
pickers - are read against their own floors rather than lost in the count for
the whole window.

The tones the audit holds are walked rather than picked: `mutedText`,
`disabledText` and `accentText` each start at a weight and are moved until they
clear their floor on every surface they can be drawn on - the three depths for
the first two, and for `accentText` a wash of the accent at
`scmAccentWashStrength`, which is what a chosen row, a sidebar pill and a lit
chip all are.

**`test/functional_tests/EditorChromeInkTest.cpp`** holds the editor to one ink
for its chrome - see "One ink for the editor's chrome" above for what it walks,
what it leaves out and why, and the painted probe it ends with. The audit asks
whether a word can be read; this asks whether it is the right grey, which a
readable-but-wrong tone would otherwise pass.

**Adding a surface to the design language means adding it to both**: its files
to `scannedFiles()` in the scan, and its window to the walk in the audit.
Neither list can be inferred, and a surface in neither is a surface nothing is
checking.
