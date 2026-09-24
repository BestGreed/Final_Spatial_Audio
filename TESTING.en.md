# Validation scope

[简体中文](TESTING.md) | English

Builds use `-Wall -Wextra -Werror`. `native/build.ps1` runs PCM structure checks, resolver tests, icon resource checks and language tests. These tests do not write system audio settings.

Language tests cover Chinese UI sublanguages, English and other language fallbacks, missing/invalid configuration, explicit overrides, translation completeness and English menu label widths at 96/120/144/192 DPI.

Interactive Windows desktop tests (run manually):

- `native/bin/popup-test.exe`: keyboard navigation, disabled items, submenu delay, pointer crossing and clicks.
- `native/bin/material-test.exe`: actual composited translucency of both menu levels against the test program's black/white background.
- `native/bin/profile-test.exe <empty-test-directory>`: reads the current device and verifies default profiles, actual snapshot capture and corrupt-file protection in an isolated directory. It does not invoke audio writes.
- `native/bin/FinalSpatialAudio.exe --check`: initializes or validates profiles without switching audio.
- `--smoke`: creates the tray and reads state for approximately two seconds. It exits immediately if another instance already exists, which does not establish that an independent startup test ran.

Desktop tests briefly show windows; some move and restore the pointer. They should not be mandatory CI checks on a noninteractive desktop.

For a manual language check, set `[Manager] Language=en`, restart FSA and inspect both menu levels, tooltip and capture result dialogs. Repeat with `zh`, then restore `auto`. Rule identifiers must remain unchanged. The system high-contrast/screen-reader fallback menu must use the same language. Changing Windows UI language takes effect on the next FSA startup.

Audio switching and playback require real hardware. Automated tests cannot establish compatibility with every driver, speaker layout or encoder and cannot replace per-speaker playback and game-rule verification.
