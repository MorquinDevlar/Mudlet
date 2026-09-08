---
name: design-language
description: >-
  Read before adding, changing or restyling any window, dialog, panel, form, toolbar, button,
  field, icon or colour in Mudlet's C++ UI. Gives the shared recipes in src/uiDesign.h, the
  rules every styled surface follows, which windows have adopted the design and which have not,
  and the checklist for bringing a surface in. docs/design-language.md holds the reasoning.
license: GPL-2.0-or-later
---

## When to use

Any task that touches what a user sees in a Qt widget: a new dialog, a new row on a form, a
new button, an icon, a colour, a stylesheet, a `.ui` change, a palette tweak, "make X look like
Y". Read this first, then the sections of `docs/design-language.md` this file points at. The
script editor (`src/dlgTriggerEditor.cpp`) is the reference look; the settings dialog
(`src/dlgProfilePreferences.cpp`) is the worked example of shelling an existing `.ui` file.

## The rules that never bend

1. **No colour literal.** Every colour is mixed at runtime from `uiDesign::themeTokens()`
   with `blend()`, `rgba()`, `readableOn()` or `stateColor()`. `test/DesignColourLiteralTest.cpp`
   scans every adopted file and fails on a hex string, an `rgb(`/`rgba(` with its channels
   written out, `Qt::red`, `QColor(r, g, b)`, CSS colour words in a declaration and a literal
   that is a colour word on its own - `qsl("black")` fed to an `.arg()` counts. A colour that
   is a *value being shown* (a colour well, a console palette)
   is marked `// theme-fixed: <why>` on its line. Never use that marker to skip mixing chrome.
2. **Read `qApp`'s palette, not the widget's.** Assigning a stylesheet freezes a widget's
   palette. `themeTokens()` reads the application palette; `darkPage` is measured from the
   page colour, never from `mudlet::inDarkMode()`.
3. **One recipe per control kind.** A field, a check mark, a button, a card, a sidebar, a
   scrollbar each have exactly one stylesheet builder in `src/uiDesign.h`. Call it. Never write
   a second rule for the same control in a window's own sheet, and never copy a builder's rules
   out into another file.
4. **Scope input and control rules, never set them on a window.** `inputStyleSheet()`,
   `choiceStyleSheet()` and `buttonStyleSheet()` take a `selectorPrefix`. The settings dialog
   passes `#settingsStack`, the editor sets them on each form widget. Unscoped on a window they
   reach search fields, tree inline editors and the code pane's find bar.
5. **Glyphs are Lucide SVGs, loaded only through `uiDesign::glyphPixmap()`** and tinted with
   `tintedGlyph()` / `tintedIcon()`. Stroke width is the token `scmGlyphStrokeWidth`. A
   stylesheet that needs a picture points at a PNG written into the glyph cache by a helper
   shaped like `themedArrowFile()` in `src/uiDesign.cpp` (1x file plus an `@2x` twin). Never
   `QPixmap(":/icons/foo.png")` for chrome; never a new raster icon. See `docs/design-language.md`
   section 2.
6. **Radius follows control size** (`scmRadiusChip` 4, `scmRadiusInput` 5, `scmRadiusPanel` 8,
   `scmRadiusProminentInput` 8). No rule writes a radius of its own.
7. **Font sizes in stylesheets are percentages**, never points.
8. **Chrome words are `mutedText`; typed values are `text`; chosen or hovered is `accentText`.**
   Every ink is walked to a measured contrast floor (`scmTextMinimumRatio` 4.5:1,
   `scmQuietMinimumRatio` 3:1) and `ReadabilityAuditTest` checks both appearances.
9. **Shell over `.ui`.** Do not rewrite a `.ui` file to restyle it. Detach widgets with
   `detachFromLayout()` / `removeFromLayoutTree()` and move them into a runtime-built shell, so
   object names, connections and translations survive. The exception is a form whose layout is
   itself wrong (the Buttons form was rewritten as one grid).
10. **Object names and dynamic properties are the test interface.** Prefix them per window
    (`settings*`, `editor*`, `about*`); three are shared by contract: `settingsRail`,
    `settingsFocused`, `settingsSurface`. Setting a property after show needs
    `uiDesign::repolish()`. Full tables: `docs/design-language.md` section 3.
11. **Human copy.** Controls say what happens in a player's words; every user-visible string
    is `tr()` with a `//:` translator note on the line above.
12. **Measured responsiveness.** Breakpoints come from font metrics and `sizeHint()`s, with a
    different collapse and restore threshold so nothing oscillates.

## Tokens

`uiDesign::ThemeTokens` (`src/uiDesign.h`). Pick by what the widget *is*:

| Token | Use for |
| --- | --- |
| `page` | The window and its scaffolding: toolbar, status bar, sidebar pane, columns, scroll areas |
| `card` | A panel raised off the page; menus and popups |
| `pane` | A column told apart from the page beside it (a fifth of the card lift) |
| `field` | Anything typed into or picked in; a check mark's fill |
| `border` | The hairline round a card, field, button or mark |
| `separator` | The seam between two panes |
| `text` | Typed values and body copy |
| `mutedText` | Every word of chrome: labels, headings, status, chips |
| `disabledText` | An unavailable word |
| `accent`, `accentText`, `accentSoft`, `hoverSoft` | The chosen thing, its ink, its wash, the hover wash |
| `marker` | The search highlight pen |
| `darkPage` | Which of the two treatments a rule takes |

State colours (ok, warning, error) come from `stateColor(scmStateHue_*, darkPage)`.

## Recipes (all in `src/uiDesign.h`)

| Need | Call | Notes |
| --- | --- | --- |
| Line edit, text edit, combo box, spin box | `inputStyleSheet(tokens, prefix)` | Claims combos and spin boxes only once the chevron PNGs exist; accent frame on focus. Then `letPopupsTakeTheFieldsCorner(container)` on the container the sheet was set on, and again wherever a combo box is built after that pass, or a dropped-down list's corner is cut out over the square corner of the window it lives in |
| Check box, radio button, checkable card mark | `choiceStyleSheet(tokens, prefix)` | One mark: field fill, hairline, accent when set, a dash for `Qt::PartiallyChecked`; cards get it through `cardIndicatorStyleSheet()`. Then `keepClickFocusOffControls(container)` on the container the sheet was set on, or a click leaves the focus accent behind on every base style that answers `SH_Button_FocusPolicy` with `Qt::StrongFocus` |
| Push button, button with a menu | `buttonStyleSheet(tokens, prefix)` | Same height and radius as a field; Lucide chevron as the menu indicator. Colour wells keep their own per-widget sheet. Same follow-up call as the row above - one `keepClickFocusOffControls()` covers both recipes |
| Card with a title inside the frame | `cardStyleSheet(CardMetrics, tokens)` + `cardIndicatorStyleSheet()` + `measuredCardTitleHeight()` | Measure the title height with the indicator rules in force, or the first control paints over the title |
| Sidebar list with rail collapse | `sidebarStyleSheet(...)` + `setSidebarCollapsed(...)` + `SidebarItemDelegate` + `SidebarToggle` | The rail width, the paddings, the row height and the glyph are the component's own `scmSidebar*` constants; `SidebarMetrics` carries only what a window has a reason to differ by - the expanded width, which each measures off its widest row name in bold, and the vertical padding. `SidebarToggle` is the chevron on the seam, a child of the shell holding both panes, kept under `<window>SidebarLabelsShown` |
| Scrollbars on a surface | `scrollBarStyleSheet(prefix, tokens, surface)` | A scroll area's bars answer only to a descendant selector |
| Toolbar or sidebar glyphs | `tintedIcon(glyph, tokens)` / `restyleActionGlyphs(ActionGlyph ledger, tokens)` | Re-run on every appearance change |
| A word in a box (chip) | `uiDesign::ChipRow` / `Chip` (`src/ChipRow.h`), `chipFont()` | `scmRadiusChip` |
| Segmented two-way control | Editor's `QRadioButton[editorSegment]` rules | Read before adding a third |
| A row of "word field word" | `buildControlSentenceRow(row, sentence, controls)` | Placeholders reorder per language |
| Grip dots on a draggable thing | `gripGlyphFile(colour, alongTheBar)` | Background image, not `image:` (it stretches) |
| Focus ring for a deep link | `spotlightStyleSheet(accent, strength)` | |
| A link in rich text | `withLinkColour(richText, colour)` and keep the raw text in a property | A label bakes link colour at `setText` time |
| Instant apply and dirty tracking | `SettingsSnapshot`, `controlValue()`, `beingTypedInto()` | See `docs/design-language.md` section 5 |
| Search over a widget tree | `collectSearchText()`, `foldForSearch()`, `setSearchMatch()` | Section 4 |

## Which surfaces are in

Adopted (styled through `uiDesign`, guarded by the tests below):

- Settings dialog `dlgProfilePreferences` (shell, sidebar, cards, fields, marks, buttons, search)
- Script editor `dlgTriggerEditor` (toolbar, sidebar, trees via `EditorTreeDelegate` /
  `VariableTreeDelegate`, seven forms, pattern rows, options strip, chips, code heading, notice)
- The editor's notice `dlgSystemMessageArea`, which styles itself from the tokens on every
  appearance change rather than being written to by the window holding it
- About dialog `dlgAboutDialog` (`AboutLinkButton`, `AboutSupporterBanner`)
- Main window toolbar and detached-window toolbars (`mudlet.cpp`, `TDetachedWindow.cpp`) - glyphs only
- Debug filter bar `TDebugFilterBar`

Not yet adopted (platform or Fusion drawn; a new feature there still uses the recipes above
for anything it adds, and adoption is one window per pull request):

- Main window body: profile tab bar, command line, search field, bottom icon buttons, dock title bars
- Mapper dock `dlgMapper` and its map controls; `dlgRoomProperties`, `dlgRoomExits`, `dlgMapLabel`
- Connection dialog `dlgConnectionProfiles`
- Package manager, module manager, package exporter (`dlgPackageManager`, `dlgModuleManager`,
  `dlgPackageExporter`)
- Notepad `dlgNotepad`, composer `dlgComposer`, IRC `dlgIRC`
- Colour trigger picker `dlgColorTrigger`, UI tour
- User-made toolbars `TToolBar` / `TEasyButtonBar` (profile-owned look, mostly out of scope)

## Bringing a surface in: the checklist

1. Screenshot it first, both appearances, with the harness recipe under "Seeing the result".
2. Build the shell at runtime over the existing `.ui` (rule 9). Name every new widget with
   the window's prefix; put `markAsShellSurface()` on scaffolding a profile stylesheet must not
   paint.
3. In one `apply<Window>ShellStyle()` compose the sheet from the recipes: page/pane surfaces,
   `cardStyleSheet` if it has cards, `inputStyleSheet` / `choiceStyleSheet` /
   `buttonStyleSheet` scoped to the content container, `scrollBarStyleSheet` per scroll area,
   glyph ledgers through `restyleActionGlyphs`. Set it on the shell widget, never on the
   dialog: `mudlet` assigns the profile's Lua stylesheet to dialogs on show.
4. Re-run that function on `mudlet::signal_appearanceChanged` and on font or language change.
5. Add the window's files to `scannedFiles()` in `test/DesignColourLiteralTest.cpp` and the
   window to the walk in `test/functional_tests/ReadabilityAuditTest.cpp`. A surface in
   neither list is a surface nothing checks.
6. Add a shell test to the right grouped binary (`*_GROUP_TEST_SOURCES` in
   `test/functional_tests/CMakeLists.txt`), reaching widgets by object name. Prove it fails
   without the change.
7. Document the window's names and properties in `docs/design-language.md` section 3, and its
   adoption in this file's roster.

## Seeing the result

Nothing about a stylesheet can be judged from the source. Grab the window:

- The functional tests link `mudlet_core` statically; a **standalone** test binary drops the
  `mudlet.qrc` resources, so call `Q_INIT_RESOURCE(mudlet)` first or every SVG glyph comes back
  null and every combo box falls back to the platform's drawing. The grouped binaries init
  resources in `GroupedTestMain.cpp`.
- Build the dialog the way the app does (`ReadabilityAuditTest::initTestCase()` is the
  template: XDG dirs, `TelnetServerStub`, `TestProfile::create`, then the dialog), `show()`
  it, switch appearance through the settings dialog's `comboBox_appearance` (the path that
  restyles every open window), then `pWindow->grab().toImage().save(...)`.
- Run on the real platform, not `QT_QPA_PLATFORM=offscreen`, when the question is how a
  control looks: macOS dark uses the Fusion-based `DarkTheme` proxy, macOS light the native
  style, and offscreen is Fusion for both.
- Sample pixels rather than trusting your eye for a contrast claim; the audit tests show how.

## Guards to keep green

`DesignColourLiteralTest`, `ReadabilityAuditTest`, `EditorChromeInkTest`, `EditorSurfaceToneTest`,
`SettingsAppearanceTest`, `SettingsShellNavigationTest`, and the shell test of whichever window
you touched. When a stylesheet builder is refactored, diff the generated sheets before and after
in both appearances with `test/functional_tests/StyleSheetDumpTest.cpp` (wired in by hand, then
removed) - see "Verifying a stylesheet extraction" in `docs/design-language.md`.
