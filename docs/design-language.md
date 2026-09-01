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
(accent). Light and dark treatments are chosen from the measured lightness of the
page colour (`darkPage`), not from `mudlet::inDarkMode()`, so a dark system theme
under "follow the system" is handled too.

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
| `field` | `QPalette::Base` | Anything the user types into or picks a value in: line edits, combo boxes, spin boxes, the search field, a check indicator's fill |

The order is fixed - `field` is sunk into `card`, `card` is lifted off `page` -
and it is what makes the windows read as having depth rather than as flat
collapsing to black. Deriving the page from `QPalette::Base` gets this exactly
backwards: Base is the *input field* colour, near-black on a dark theme, so a
page mixed off it ends up darker than the fields lying on it.

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
| `separator` | `page` taken towards black (36% on dark, 10% on light) | The seam between two panes, drawn where the handle that resizes them is (`GripSplitterHandle`, including the one carrying the code pane's heading) and along the sidebar's right edge. A groove cut into the window, as against `border`, which is a hairline drawn on it |

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

### Shell over .ui

The `.ui` file is not rewritten. Existing widgets are detached from their
designer layouts and moved into a runtime-built shell:

- `detachFromLayout(pWidget)` - removes a widget from its parent's layout tree.
- `moveIntoCard(pCard, controls)` - reparents controls into a card's layout.
- `buildPage(objectSuffix, cards)` - detaches cards, marks them `settingsCard`,
  stacks them in a scrolling column.

This keeps object names, signal connections and every translated string intact,
so a redesign costs no translation churn and no `.ui` merge conflicts.

## 2. Icons

- Monochrome line icons from [Lucide](https://lucide.dev), ISC licence,
  attributed in the About dialog (`src/dlgAboutDialog.cpp`).
- Shipped as 128px alpha PNGs in `src/icons/` (`settings-general.png`,
  `settings-appearance.png`, ...). The shape lives in the alpha channel; the RGB
  content is irrelevant.
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

The editor redesign follows the same scheme with an `editor*` prefix:
`editorShell`, `editorSidebar`, `editorPage_<key>`, `editorCard`, and so on.
`settingsRail` and `settingsFocused` are the two exceptions and keep these names
in both windows: they are the contract `SidebarItemDelegate` and
`sidebarStyleSheet()` are written against.

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
