# Relay

Relay is a keyboard launcher for Windows. Press **Alt+Space**, type a few letters, and press **Enter** to open an app.

The apps you use often and have opened recently rise to the top. Type `/app ` for an alphabetical list, or `/` to browse commands.

## Get started

Download **Setup.exe** from [Releases](https://github.com/frinky04/relay/releases/latest) and run it. Relay installs for your Windows account. For portable use, extract the **Portable.zip** and run `Relay.exe` in its top folder; keep the files together.

Restart Relay after installing or removing apps to refresh its app list.

## Commands

| Type | Action |
| --- | --- |
| `fire` | Find Firefox and open it |
| `/window code` | Find an open window by title or app name and switch to it |
| `/web lua documentation` | Enter searches Google; select Search DuckDuckGo to use that engine |
| `5 + 5` | Show `10`; Enter copies the result |
| `/process paint` | Find a running process to stop |
| `/relay ` | Open settings, manage plugins, update or quit Relay |

Commands follow **noun, arguments, verb**: `/app "Google Chrome" Open`. Leave off the verb to use its default action. **Tab** fills names and adds quotes where needed; the hints show what comes next.

Check for Updates keeps Relay open and shows progress and the result below the app list.

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

Type `config` to edit `%APPDATA%/relay/init.lua`. Comments at the top list every setting and its default, kept current as Relay updates. Copy the settings you want into the returned table, remove their leading `--`, and save to apply them.

Add `start_with_windows = true` to start Relay hidden when you sign in. Set it to `false` to turn startup off.

Add Lua commands through `/relay Open Plugins Folder`, then run `/relay Reload Plugins` to load your changes. Use the [bundled commands](plugins) as examples.

## Updates

Relay downloads updates in the background on startup and applies them on the next restart. Run `/relay Check for Updates` to check now, then `/relay Restart to Update` when an update is ready. Update notices appear below the app list. Settings, user plugins and launch history survive updates.

Coming from v0.1.0? Quit the old copy and install the new release once.

## License

[MIT](LICENSE). See [third-party notices](THIRD_PARTY_NOTICES.md) for bundled components.
