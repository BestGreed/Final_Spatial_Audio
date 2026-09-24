# Architecture

[简体中文](ARCHITECTURE.md) | English

FSA uses a single user process. The UI thread handles tray menus, while a worker thread serializes device reads, rule resolution and audio writes. Drawing the menu does not change the system audio mode.

Foreground events and Running observations feed the same resolver. The highest-priority matching rule wins; ties follow configuration order. Switching uses debounce, state comparison and read-back verification. External changes return the application to manual lock.

PCM uses MMDevice / WASAPI and PolicyConfig to capture endpoint settings. Spatial audio selection uses a separate spatial policy interface; encoded transport format bytes are not replayed. Profiles are stored per endpoint with version validation and temporary-file replacement.

Language is selected once at startup, before the worker starts, using the optional INI override and `GetUserDefaultUILanguage`. An immutable table provides Chinese and English strings for custom menus, standard accessibility menus, tray status and application dialogs. The menu receives the selected language as part of its state. No language polling, additional thread, external localization file or runtime dependency is required. Rule identifiers and diagnostic logs stay stable across languages.

| File | Responsibility |
|---|---|
| native/final_spatial_audio.c | Entry point, worker, tray and scheduling |
| native/audio_layout.c/.h | PCM snapshots, validation, application and restoration |
| native/spatial_policy.c | Spatial policy interface and diagnostic entry point |
| native/profile_store.h | Profile generation, persistence and validation |
| native/resolver.h | Rule priority, debounce and wait scheduling |
| native/fluent_menu.c/.h | Menu drawing, keyboard/mouse navigation and submenus |
| native/language.h | Language selection and UI translation table |

Runtime DLL dependencies are Windows system components. Binaries also contain compiler-supplied startup/runtime support under separately listed licenses. Neither source control nor portable packages include user configuration data.
