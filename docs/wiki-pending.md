# Wiki entries this work still owes

Every scripting function, event, setting key and user-facing window change on the
`preview/all-in-flight-work` branch that the Mudlet wiki does not describe yet. New
Lua functions go on the wiki's "Area 51" staging page rather than the live manual
until the release that ships them; window changes go on the manual pages that show
those windows.

Append a row whenever a change adds or alters something a player or a script author
would look up. Remove the row when the wiki has it. A row without a commit is a
change that came in through a merged branch; the commit column is a pointer for
whoever writes the entry, not a citation.

## Lua functions

| Function | What it does | Came in with | Status |
| --- | --- | --- | --- |
| `enablePackage(name)` | Switches every item a package installed back on; returns true, or nil and a reason | `561d11880` | not written |
| `disablePackage(name)` | Switches every item a package installed off, remembered across restarts; refuses modules | `561d11880` | not written |
| `setSvgRotation`, `setSvgShear`, `setSvgTint`, `resetSvgRotation`, `resetSvgShear`, `resetSvgTint`, `resetSvgTransform` | SVG label transforms and tint (upstream PR #8935) | `e43c5a2bb`, `36e01a545` | check the PR's own wiki status |
| Map room symbol font functions | Set the 2D map room symbol font from Lua | `a32b39119` | check |

## Events

| Event | Arguments | Came in with | Status |
| --- | --- | --- | --- |
| `sysEnablePackage` | package name | `561d11880` | not written |
| `sysDisablePackage` | package name | `561d11880` | not written |
| `sysSettingChanged` | the seven boolean `getConfig` keys that now ride the existing event (upstream PR #10596) | merged branch `improve/setting-changed-event-keys` | not written |

## `setConfig` / `getConfig` keys

| Key | What it is | Came in with | Status |
| --- | --- | --- | --- |
| Map room symbol font keys | The 2D map's room symbol font, and the refusal when that font cannot draw a symbol | `f9f43d99c`, `a4fa023bb` | check |
| Seven boolean keys on `sysSettingChanged` | The keys the event now reports | merged branch `improve/setting-changed-event-keys` (PR #10596) | not written |

## GMCP

| Message | What it does | Came in with | Status |
| --- | --- | --- | --- |
| Starter UI switch | A game can turn off Mudlet's starter UI | `3d89cb446` | not written |

## Windows a player would read about

| Window | What changed | Came in with | Status |
| --- | --- | --- | --- |
| Package manager | Redesigned: Explore / Installed / Updates chips, search, two-line rows with the version, a dot per row that switches the package off and on, Turn off / Turn on, right-click menu, resizable columns, Website opens the package's own page | `34cb74367`, `561d11880` | not written |
| Script editor | Redesign of the whole window; "Turn off the X package" in every tree's context menu; package folders carry the package glyph; the Add event field offers event names | many; `561d11880`, `b2d2097fc` | not written |
| Connection dialog | Design adoption: notice, rounded game chips with a hover ring, information box as a field | `34cb74367` | not written |
| Notepad | Tabs as chips, notes as fields, the send strip, close confirmation | `2e07345b5`, `458a93e57` | not written |
| Settings dialog, About dialog | Redesigned | merged branches | not written |
