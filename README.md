# Relay

Relay is a keyboard launcher for Windows.

## Install

Download the [latest release](https://github.com/frinky04/relay/releases/latest). Run the installer, or extract the portable ZIP and open `Relay.exe` in its top folder.

## Use

Press **Alt+Space**, type an app name, and press **Enter** to open it.

Type `/` to list commands. Use **Up/Down** to select a result and **Tab** to complete it. **Shift+Enter** runs the selection and keeps Relay open. **Escape** hides Relay; `/relay Quit` exits it.

| Type | Action |
| --- | --- |
| `/window code` | Find and switch to an open window |
| `/web lua documentation` | Search the web |
| `5 + 5` | Calculate; Enter copies the result |
| `tomorrow at 7pm` | Preview a date and time; Enter copies it |
| `/process paint` | Find a running process to stop |
| `/system ` | Lock, sleep, hibernate, sign out, restart or shut down Windows |

Commands follow **noun, arguments, verb**: `/app "Google Chrome" Open`. Leave off the verb to use the default action.

**Kill requires two Enter presses and discards unsaved work.**

`/system` defaults to Lock. Hibernate appears when available. Sign Out, Restart and Shutdown require two Enter presses; save your work first. You can also find these actions by typing their names without `/system`.

## Settings and plugins

Run `/relay Edit Config` to open settings. The file includes examples; save your changes to apply them.

To add Lua commands, run `/relay Open Plugins Folder` and place your files there. Run `/relay Reload Plugins` after editing them. See the [bundled commands](plugins) for examples.

## License

[MIT](LICENSE). See [third-party notices](THIRD_PARTY_NOTICES.md) for bundled components.
