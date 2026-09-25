# License scope and runtime notices

The root `LICENSE` (Unlicense) applies to the original project source code and documentation. It does not relicense icon artwork, system fonts, trademarks, or compiler/runtime components.

- Icon artwork: see `native/assets/LICENSE.txt` (included as `licenses/Icon-assets.txt` in portable packages).
- Documentation screenshots: `docs/images/` may show third-party desktop wallpaper and Windows UI. They are included to illustrate the application and are not covered by the source-code Unlicense or a grant to reuse the pictured wallpaper.
- Zig compiler/runtime: the build uses Zig 0.14.1. Applicable MIT notice is reproduced in `licenses/Zig-MIT.txt`. The compiler itself is not included in the portable package.
- MinGW-w64: startup/runtime components supplied with Zig's Windows GNU target are subject to their own terms. The bundled distribution notice is reproduced in `licenses/MinGW-w64.txt`; portions explicitly designated public domain retain that status.
- Windows system DLLs and Segoe Fluent Icons: used from the installed Windows system and not redistributed.
- Product names and marks, including Dolby and DTS, belong to their respective owners. This project does not grant trademark rights or claim endorsement.

The application contains no AutoHotkey runtime, SoundVolumeView executable, or WinUI runtime. Compiler/runtime notices above describe build output, not an additional installed application dependency.
