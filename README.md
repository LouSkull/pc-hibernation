# Hibernation 1.0

A native live screensaver for Windows 10/11, built with C++20, Direct3D 11 for scene rendering, and an **HTML interface powered by WebView2**. A single executable, with no accounts or network services required. The application interface is in English.

## Quick Start

1. Run `Hibernation.exe`.

2. On the **Scenes** tab, choose a scene and palette, then adjust the motion settings.

3. On the **Behavior** tab, set how many seconds of inactivity should pass before the screensaver starts.

4. Click **×** in the title bar — the window will minimize to the system tray and automatic activation will become active.

Automatic activation is paused while the settings window is open.

## Requirements

* Windows 10/11 x64.

* **Microsoft Edge WebView2 Runtime** — used to render the application interface. It is already installed on Windows 11 and on most Windows 10 systems together with Edge.

* To build the project: Visual Studio 2022/2026 with **Desktop development with C++**, Windows SDK, CMake ≥ 3.24, and the **Microsoft.Web.WebView2** package available in the NuGet cache. Its path is specified through the `WEBVIEW2_ROOT` variable.

## Interface

There are seven tabs. The clock is not a separate tab; it is a section under **Appearance**. The window has a fixed size and can be dragged by its header with smooth movement. If Windows animations are disabled, the window moves instantly.

**Right-clicking the tray icon** opens a custom 300 × 356 panel instead of the standard system menu. It uses the same visual style and shows the automatic-start status, a **Lock armed** badge, the current hotkeys, and four actions: open the application, start the screensaver immediately, pause or resume automatic activation, and exit.

The panel opens upward from the system tray and closes when you click outside it or press Esc. It is created in advance; its hidden WebView is suspended while closed and resumed when the panel is opened.

| Tab             | Contents                                                                                                                                                                                                          |
| --------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Scenes**      | 12 scenes with live thumbnails using real rendering, 8 palettes, a preview panel for the selected scene, speed, brightness, scale, turbulence, glow, grain, vignette, 4 quick moods, and a preset manager         |
| **Behavior**    | Automatic activation and idle delay, separate rules for games and video, accidental wake protection with a mouse movement threshold and keyboard toggle, fade-in, scene rotation, cursor hiding, and audio muting |
| **Appearance**  | Accent color (6 options), background depth (3 options), corner rounding, the **Cursor** section, and the **Clock** section                                                                                        |
| **Security**    | Password protection on return, an “ask after N minutes” threshold, lock testing, and a clear description of the protection limitations                                                                            |
| **Performance** | Power-saving profiles, FPS from 5–120, render scale from 20–100%, adaptive quality, deep sleep, preview settings, and a diagnostic graph                                                                          |
| **System**      | **Start with Windows**, **customizable hotkeys**, `.ini` import/export, build version, and **GitHub update checking**                                                                                             |
| **Credits**     | Author, repository, and license                                                                                                                                                                                   |

**Search** in the upper-left corner searches the names of settings, sections, and scenes. It shows which tab contains the result and takes you directly to the corresponding row while highlighting it. If nothing is found, it says so.

Settings that only work while their corresponding toggle is enabled — game delay, video delay, scene rotation interval, clock options, and lock threshold — are **dimmed** when the toggle is off, so an inactive slider does not look broken.

**Accidental wake protection** under Behavior filters accidental movement while the screensaver or password screen is active. **Mouse wake distance** defines the movement threshold. After **200 ms without movement**, accumulated movement is reset, so isolated bumps are not added together.

Back-and-forth jitter cancels itself out, and each mouse is tracked independently. The threshold is expressed in device movement units and therefore depends on mouse DPI; a higher value reduces sensitivity. Mouse clicks and wheel movement wake the screensaver immediately.

**Wake with keyboard** allows keyboard input to wake the screensaver. Once the password field is already visible, keyboard input always works. The movement filter is enabled only while the screensaver is active.

## Clock

There are three styles instead of the previous four:

* **Minimal** — clean digits;
* **Glow** — soft glow with a light streak;
* **Ring** — a ring that fills as the seconds pass.

They are selected from the **Choose clock…** popup, which also shows the currently selected style. The same popup contains:

* **24-hour or 12-hour** format;
* whether to show **seconds**;
* digit **weight**: Thin / Regular / Bold;
* **color**: White, Mint, Ice, Violet, Rose, Gold.

Position, size, opacity, and the “primary monitor only” option are configured directly in the Clock section.

## Cursor

The **Cursor** section is located on the Appearance tab:

* **Windows** — the standard Windows pointer;

* **Orbit** — a built-in cursor inspired by osu!: a glowing ring with a center dot and a dark outline so that it remains visible even on bright scenes. It supports the same six colors as the clock;

* **Your file** — use your own `.png`, `.cur`, or `.ani` file up to 4 MB. A copy is stored in `%LOCALAPPDATA%\Hibernation\Cursors\`, so the original file can safely be deleted afterward.

For PNG files, you can choose the click hotspot: the center or the upper-left corner. For `.cur` and `.ani` files, the hotspot is read directly from the file. Animated `.ani` cursors are also animated while the screensaver is running.

For Orbit and custom cursors, three sizes are available:

Small / Medium / Large = 24 / 28 / 32 px at 100% display scaling.

There is also **Trail**, which adds a fading cursor trail and a click ring inside Hibernation windows.

The same cursor is used throughout the entire application: the settings window, tray panel, and screensaver, unless the cursor is hidden on the screensaver.

The image is rendered by the native application layer and supplied to the windows in two resolutions, so it remains sharp on displays using 150–200% scaling.

Cursor sizes above 32 px are not supported because Chromium does not correctly render larger cursors near the edge of a window.

**Hide cursor** on the Behavior tab applies to the screensaver and to the lock screen after the password panel has moved away. While the password panel itself is visible, the cursor is always hidden.

## Presets and Media Library

Presets are standard `.ini` files stored in one folder:

`%LOCALAPPDATA%\Hibernation\presets\`

The **Presets…** button opens a popup where you can:

* **save** the current appearance under a custom name;
* **apply** any saved preset;
* **delete** presets you no longer need;
* **open the folder** to share a preset file with someone;
* use **Refresh list** to rescan the folder if preset files were added or removed externally.

Presets only change visual appearance. They do not modify activation rules, hotkeys, or the password.

The media library on the Scenes tab works the same way. Next to **Add animation…** and **Open library folder**, there is a **Refresh list** button that rescans the `Media` folder and displays what is actually stored there.

## Start with Windows

On the **System** tab, under **Startup**:

* **Start with Windows** adds an entry to `HKCU\…\CurrentVersion\Run`. This affects only the current user and does not require administrator privileges.

  The Windows registry itself is treated as the source of truth: the toggle reflects whether the entry actually exists rather than merely storing the user's intended setting.

* **Open the window on start** controls whether the settings window should open when Hibernation starts with Windows.

  When disabled, the application starts silently and goes directly to the system tray. In this mode, `--tray` is added to the startup command.

  The executable path is wrapped in quotes, so spaces in the path do not break startup.

## Updates and Version

The application version is defined by a **single file**, `VERSION`, located in the repository root.

The build system reads this file and inserts the version number into three places:

* the **About** section;
* the update checker;
* the properties of `Hibernation.exe`, under the Windows **Details** tab as FileVersion / ProductVersion.

The executable also contains an embedded icon at `src/app.ico`, which is visible in File Explorer and on the taskbar.

The **Check for updates** button in the About section requests the latest repository release from GitHub using:

`api.github.com/.../releases/latest`

The request runs on a background thread.

Possible results:

* if the latest version matches your installed version — **“You’re up to date”**;

* if GitHub contains a newer version — an **“Update available”** popup appears with the version number, a short release description, and a **Download update** button. The button opens the release page in the browser. Only URLs on `github.com` are allowed;

* if there are no releases yet or the network is unavailable — an appropriate error message is shown.

Version comparison follows semantic versioning rules:

`1.10.0` is newer than `1.9.0`, and a prerelease such as `1.0.0-beta` is considered older than `1.0.0`.

This is the only feature in the application that accesses the network, and it does so only when the user explicitly presses the update button.

## Hotkeys

On the **System** tab, both configurable key combinations can be **reassigned or disabled completely**.

Click the hotkey field and hold the entire desired key combination. While the field is listening for input, the application temporarily releases its global hotkeys so Windows does not intercept the combination. Press Esc to cancel recording.

Defaults:

| Key            | Action                                                                                    |
| -------------- | ----------------------------------------------------------------------------------------- |
| Ctrl + Alt + H | Start/close the screensaver from any application                                          |
| Ctrl + Alt + P | Enable/pause automatic activation                                                         |
| Esc            | Close the screensaver; on the lock screen, clear the current input                        |
| Alt + F8       | On the lock screen, show/hide the entered password, similar to the Windows sign-in screen |
| Enter          | On the lock screen, used only for passwords saved by an older version; see below          |

If a key combination is already registered by another application, Hibernation reports it directly in the same section.

## Lock on Return

* **Password** is configured in a modal dialog using a password and confirmation field, with a length from 4 to 128 characters.

  The password itself is never stored in plaintext. `[Security] Credential` contains a salt, a `PBKDF2-HMAC-SHA256` hash using 250,000 iterations, and the password length.

  Password comparison is performed in constant time.

* **When to ask** is controlled by the **Require password** toggle and the **Ask after N minutes** slider.

  `0` means the password is required immediately. Otherwise, the lock activates only if the screensaver has been running for at least the selected amount of time.

* **Lock screen** is a separate fullscreen HTML page containing a clock, input field, and failed-attempt counter.

  After 5 incorrect attempts, input is locked for 15 seconds.

* **No Enter key and no button.**

  The password field displays one slot per character. It is impossible to enter more characters than the password contains.

  When the final character is entered, the password is submitted automatically. A correct password returns to the desktop; verification and closing the lock screen take approximately 0.1 seconds.

  An incorrect password is cleared and counted as a failed attempt.

  Older versions performed silent checks after a delay without counting them as attempts. Now every submission is included in the attempt counter.

* **The mouse does not control the password field on the lock screen.**

  There is no visible cursor. Clicks, text selection, scrolling, and dragging are suppressed, and keyboard focus always remains in the password field.

  Wake detection uses the same movement filter as the screensaver. Small mouse jitter does not bring the password panel back and does not extend its hide timer.

* **Passwords created by an older version** do not have the password length stored.

  Therefore, after updating, the first unlock requires pressing Enter once. After a successful unlock, the password length is stored, and future unlocks work without Enter.

  Alternatively, you can set the password again on the Security tab.

* **After 5 seconds of inactivity, the entire password panel slides away**, and any partially entered password is cleared so it is not left visible on an unattended screen.

  Mouse movement above the configured threshold, a click, or allowed keyboard input brings the panel back with an empty field.

* After a successful unlock, the screensaver cannot activate again for **at least 15 seconds**.

  The password is also not requested again until the configured threshold has elapsed since the previous unlock.

  This prevents a loop where the screensaver immediately reappears after entering the password.

A clear description of the security limitations — this feature is not a replacement for Windows sign-in security:

* While the lock is active, **Alt+Tab**, the **Win** key, and **Alt+Esc** are intercepted.

* **Ctrl+Alt+Del** and **Win+L** cannot be disabled because they are controlled directly by Windows.

* **Ctrl+Shift+Esc** is intentionally left available as an emergency route to Task Manager.

* Browser shortcuts such as print, refresh, search, and zoom are disabled in all application windows, along with autofill, password saving, and file drag-and-drop.

  This is especially important on the lock screen because the print dialog could otherwise provide access to File Explorer through **Save As…**.

* Displaying the password length is an intentional trade-off required for automatic submission without Enter.

  Anyone sitting at the computer can therefore see how many characters the password contains.

* The settings themselves are not password-protected.

  If you forget your password, open the **Security** tab and press **Remove**.

* If WebView2 fails to display the lock screen, the application intentionally **fails open** and returns to the desktop rather than leaving the user locked out without any way to enter the password.

## Settings and Data

`%LOCALAPPDATA%\Hibernation\settings.ini` — application settings.

`presets\` — presets.

`Cursors\` — a copy of the custom cursor.

`WebView2\` — interface engine data.

`Hibernation.exe --data-dir <folder>` stores all of this data inside the specified folder and can run alongside a normal Hibernation instance.

This mechanism is also used by the tests described below.

Hibernation is a visual screensaver, not Windows system hibernation.

The application does not lock the Windows session and does not modify the system power plan.

It is added to Windows startup **only if you explicitly enable** **Start with Windows**. This creates a `Run` entry for the current user, which is removed again using the same toggle.

The screensaver closes when the Windows session is locked, the system enters sleep, or the monitor configuration changes.

For idle activation, the application reads only the time of the last user input.

While the screensaver is active, raw input events are used only as wake signals. The contents of keyboard input are not recorded.

## Build

```bat
build.bat

build.bat -Run

build.bat -SmokeTest

build.bat -Fresh
```

`build.ps1` accepts the same arguments.

`-SkipPackage` builds and tests the application without copying the output to the repository root or the `dist` directory.

The MSVC runtime and WebView2 loader are linked statically.

The version is read from the `VERSION` file. To publish a new version, change the number in that file and rebuild the project.

**`clean.bat`** removes generated files and build artifacts:

* `build/`
* `dist/`
* `Hibernation.exe` from the repository root
* logs
* `smoke-report.txt`
* `startup-error.txt`
* `clock-*.png`

Only the source files are left behind, making it easy to create a clean commit and build a release from scratch.

If the WebView2 SDK is not located in the standard NuGet cache:

```bat
cmake -S . -B build -DWEBVIEW2_ROOT="C:\path\to\microsoft.web.webview2\<version>\build\native"
```

## Testing

```powershell
ctest --test-dir build -C Release --output-on-failure

.\Hibernation.exe --smoke-test

.\Hibernation.exe --render-clocks
```

* `core_tests` — context-dependent delays, parameter limits, presets, per-monitor clock behavior, and the `shouldLock` lock policy, including prevention of an immediate second password request after unlocking.

* `wake_tests` — movement reset after a pause, sequences of random bumps, continuous jitter, movement-threshold boundaries, absolute coordinates, and input-event processing delay.

* `settings_tests` — reading and writing all settings, including `[Security] Credential`, clock settings, and reassigned hotkeys.

* `lock_tests` — base64, PBKDF2 determinism and salt sensitivity, constant-time comparison, the full password creation and verification flow, password-length storage, legacy passwords without stored length, and corrupted password-length fields.

* `cursor_tests` — the Orbit cursor, including transparent corners, solid ring and center dot, click hotspot, the native cursor using the same hotspot, data URLs, custom PNG loading, and `.cur` loading with transparency restoration.

* `bridge_tests` — parsing messages from the page, applying settings with value clamping, state serialization, and verification that the password hash is **never** sent to the interface. For a custom cursor, only the filename is exposed.

* `update_tests` — version comparison using semantic versioning, including `v` prefixes, additional components, and prerelease versions, plus parsing GitHub release JSON without accessing the network.

* `tests/interface_checks.cjs` — Node + Playwright tests using headless Edge for pages without the native application layer.

  These cover the Cursor section, the lock screen where mouse input has no effect, input that stops at the password length and submits automatically, and incomplete input that must not be submitted.

* `webview_check` — verifies that the static WebView2 loader links successfully and that the WebView2 Runtime is installed.

* `--smoke-test` — renders 96 different frames: 12 scenes × 8 palettes, all three clock styles, buffer resizing, and rendering across all monitors.

  Results are written to `smoke-report.txt`.

* `--render-clocks` — saves `clock-0.png`, `clock-1.png`, and `clock-2.png` next to the executable.

  A fullscreen screensaver cannot be captured using a normal screenshot because GDI returns a black frame instead of the swap-chain contents, so the clock is tested using this dedicated rendering mode.

## Source Files

* `main.cpp` — Windows application lifecycle, system tray, hotkeys, fullscreen surfaces, lock screen, and presets.

* `webview.cpp` — WebView2 host: creation, messaging, resizing, and the dark background displayed before the page finishes loading.

* `bridge.cpp` — the page ↔ application communication protocol and state serialization.

* `web/app.html`, `web/lock.html`, `web/tray.html` — the main interface, lock screen, and tray panel. They are embedded into the executable as resources.

* `renderer.cpp` — GPU scenes and clocks.

* `config.cpp` — settings.

* `lock.cpp` — PBKDF2 password handling.

* `platform.cpp` — media sessions and audio.

* `core.h` — configuration parameters and pure application logic.

* `cursor.cpp` — Orbit cursor rendering, loading custom `.png` / `.cur` / `.ani` files, native cursor handling, and cursor images used by the HTML windows.

* `update.cpp` — version comparison and the request for the latest GitHub release using WinHTTP.

* `version.h.in` / `version.rc.in` — application version and executable properties generated from `VERSION`.

If hardware Direct3D 11 is unavailable, Hibernation falls back to WARP.

If startup fails, diagnostic information is written to `startup-error.txt` next to the executable.

Preview is available on the Scenes, Behavior, and Appearance tabs.

**Show fullscreen** in the lower-left corner starts the screensaver.

Scene cards animate when hovered or focused, while the full composition is updated separately.

After 5 seconds of inactivity, the password input is cleared and hidden. After another 10 seconds, the password panel gives way to the animation.

The next user activity displays the password panel again while preserving the lock state and failed-attempt counter.

`build.bat` creates `Hibernation.exe` in the project root.

For details about the new settings, see [performance, diagnostics, and media library](PERFORMANCE.md).
