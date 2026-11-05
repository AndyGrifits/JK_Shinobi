# OpenJK for macOS (Apple Silicon)

`OpenJK-macos-arm64.tar.gz` is the official build from
https://github.com/JACoders/OpenJK/releases/tag/latest (asset `OpenJK-macos-arm64.tar.gz`,
built 2026-09-29), kept here so the Mac package doesn't change under us.

It holds `openjk.arm64.app` (SDL2 inside, macOS 11+), the SP app and the dedicated server.
Not signed by Apple, and the signature of the bundled `libSDL2-2.0.0.dylib` is broken (its
install name was rewritten after linking) - on Apple Silicon that kills the app at launch. The
Mac package's `1_Install.command` therefore removes the quarantine flag and re-signs
everything ad hoc (`codesign --sign -`), as the JKHub install guide does by hand.

The bundled `libSDL2-2.0.0.dylib` is Homebrew's sdl2-compat: it loads SDL3 at run time
(`@loader_path/libSDL3.dylib` first), and SDL3 is not in the build - on a Mac without Homebrew's
SDL3 the game stops with "Failed loading SDL3 library". `libSDL3.dylib` here is the arm64 slice
of SDL3.framework from the official SDL 3.4.20 release (`SDL3-3.4.20.dmg`, zlib license:
`SDL3-LICENSE.txt`); tools/package-mac.sh puts it next to libSDL2 in `Contents/Frameworks`.
