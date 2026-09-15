# Relay

Relay is a keyboard launcher for Windows. Press **Alt+Space**, type a few letters, and press **Enter** to open an app.

The apps you use often and have opened recently rise to the top. Type `/app ` for an alphabetical list, or `/` to browse commands.

## Get started

Download the Windows x64 ZIP from [Releases](https://github.com/frinky04/relay/releases/latest), extract it, and run `relay.exe`. Keep the extracted files together.

Restart Relay after installing or removing apps to refresh its app list.

## Commands

| Type | Action |
| --- | --- |
| `fire` | Find Firefox and open it |
| `/window code` | Find an open window by title or app name and switch to it |
| `/web "lua documentation"` | Search Google in your browser |
| `/web "lua documentation" DuckDuckGo` | Search DuckDuckGo instead |
| `5 + 5` | Show `10`; Enter copies the result |
| `/process paint` | Find a running process to stop |
| `/relay ` | Open settings, manage plugins or quit Relay |

Commands follow **noun, arguments, verb**: `/app "Google Chrome" Open`. Leave off the verb to use its default action. **Tab** fills names and adds quotes where needed; the hints show what comes next.

**Kill requires two Enter presses and discards unsaved work.**

## Keys

| Key | Action |
| --- | --- |
| Enter | Run or complete the selected row |
| Tab | Fill the selected completion |
| Up / Down | Select a row |
| Shift+Enter | Run, clear the input and stay open |
| Alt+1 through Alt+9 | Activate a numbered row; destructive actions are excluded |
| Escape | Hide Relay |

Reopening starts with empty input. Use `/relay Quit` to exit.

## Settings and plugins

Type `config` to edit `%APPDATA%/relay/init.lua`. Set `hotkey`, `width` or `max_rows` in its returned table; saving applies the settings.

Add Lua commands through `/relay Open Plugins Folder`, then run `/relay Reload Plugins` to load your changes. Use the [bundled commands](plugins) as examples.

## License

[MIT](LICENSE). See [third-party notices](THIRD_PARTY_NOTICES.md) for bundled components.
