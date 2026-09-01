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
| `page` | `QPalette::Window` | The window itself and every piece of it: toolbar, status bar, sidebar pane, tree and list viewports, scroll areas, the row a search field sits in, splitter handles |
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

### Font sizes are relative

Stylesheet font sizes are percentages, never points: `#settingsPageTitle
{ font-size: 145%; }`, `#settingsWordmark { 125% }`, `#settingsHeroHeadline
{ 115% }`. Absolute point sizes break accessibility settings and every platform's
interface font.

### Cards, not bare group boxes

Content is grouped into cards: a `QGroupBox` carrying the `settingsCard` dynamic
property, styled with a background, a 1px border, an 8px radius and 16px
padding. Pages are a single column of cards with 16px spacing
(`createScrollPage()`, `buildPage()`).

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
| `settingsCardPlain` | A card without the top margin its title would need |
| `settingsCardTitleInset` | Non-checkable card - title starts at the frame edge, so checkable and plain titles line up |
| `settingsHero` | The prominent lead card of a page |
| `settingsChevronRow` | A button that reads as a navigation row into a subpage |
| `settingsRail` | The sidebar is collapsed to icons only |
| `settingsFocused` | The category list has keyboard focus |
| `searchMatch` | Marker-pen highlight on a search hit |
| `searchKeywords` | Comma-separated synonyms fed into the search index |

The editor redesign follows the same scheme with an `editor*` prefix:
`editorShell`, `editorSidebar`, `editorPage_<key>`, `editorCard`, and so on.

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
