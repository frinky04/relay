# Relay

Relay is a keyboard launcher for Windows.

## Install

Download the [latest release](https://github.com/frinky04/relay/releases/latest). Run the installer, or extract the portable ZIP and open `Relay.exe` in its top folder.

## Use

Press **Alt+Space**, type an app name, and press **Enter** to open it.

Relay finds installed apps and application shortcuts on your desktop. Run `/relay Rescan Apps` after adding or changing shortcuts.

Type `/` to list commands. Use **Up/Down** to select a result and **Tab** to complete it. **Shift+Enter** runs the selection and keeps Relay open. **Escape** hides Relay; `/relay Quit` exits it.

| Type | Action |
| --- | --- |
| `/window code` | Find and switch to an open window |
| `/web lua documentation` | Search the web |
| `5 + 5` | Calculate; Enter copies the result |
| `255 to hex` | Convert exact integers between binary, octal, decimal and hexadecimal |
| `#ff8800 to rgb` | Convert colors between HEX, RGB and HSL, including alpha |
| `10 ft in cm` | Convert units; compatible quantities support arithmetic |
| `tomorrow at 7pm` | Preview a date and time; Enter copies it |
| `days until 25 Dec` | Count calendar days |
| `5pm London in Sydney` | Convert timezones using daylight-saving rules |
| `/process paint` | Find a running process to stop |
| `/system ` | Lock, sleep, hibernate, sign out, restart or shut down Windows |

Commands follow **noun, arguments, verb**: `/app "Google Chrome" Open`. Leave off the verb to use the default action.

`/calc <expression>` handles numbers, bases, colors, units, dates and times. Base arithmetic uses prefixes such as `0b1010 + 5`; colors use `#`, `rgb(...)` or `hsl(...)`. Use `/calc` to select alternate copy formats, or `/calc now` for the current time. Select formats from the results; the expression consumes the remaining input.

**Kill requires two Enter presses and discards unsaved work.**

`/system` defaults to Lock. Hibernate appears when available. Sign Out, Restart and Shutdown require two Enter presses; save your work first. You can also find these actions by typing their names without `/system`.

## Settings and plugins

Run `/relay Edit Config` to open settings. The file includes examples; save your changes to apply them.

Set `hotkey = "win"` in the returned settings table to open or hide Relay by tapping either Windows key instead of opening Start. Win shortcuts such as Win+E and Win+L keep their normal behavior. Exit Relay or restore `hotkey = "alt+space"` to restore normal Win taps.

To add Lua commands, run `/relay Open Plugins Folder` and place your files there. Run `/relay Reload Plugins` after editing them. See the [bundled web command](plugins/web.lua) for examples.

## License

[MIT](LICENSE). See [third-party notices](THIRD_PARTY_NOTICES.md) for bundled components.
