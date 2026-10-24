# Credits

This port is a small layer over other people's work. Everything below is theirs; please
support their projects. What each one does in the port is in the README's
[Acknowledgements](README.md#acknowledgements-and-what-each-project-does-here).

## This port

- **leafyjerk**: the port, its testing on the PS5, and this repository.
- **Claude** (Anthropic's AI model, used through Claude Code): wrote most of the port's code,
  scripts, builder and documentation with leafyjerk.

## The game's code on the PC

| Who | What | Licence |
| --- | --- | --- |
| **rob646464** | [Luigi's Mansion Recompiled](https://github.com/rob646464/luigis-mansion-recompiled): the recompiler that translates the disc's code, the runtime that runs it, the audio, the enhancements and the options menu. This port is its static build (`lmrecomp`) for the console, with the changes in `ps5/patches/lm-recomp.patch`. | none stated (see the README's licence section) |
| **SomeJakeGuy** and contributors | [lm-decomp](https://github.com/SomeJakeGuy/lm-decomp): the Luigi's Mansion decompilation, whose symbol map and splits the recompiler uses | CC0 1.0 |
| **Twilit Realm** and contributors | [Dusklight](https://github.com/TwilitRealm/dusklight): the JSystem base Luigi's Mansion Recompiled adapted | CC0 1.0 |
| **Luke Street (encounter)** and contributors | [Aurora](https://github.com/encounter/aurora) (the GameCube's graphics, pads, cards and discs on modern hardware), [nod](https://github.com/encounter/nod) (disc images: the builder's `nodtool`), and the [Dawn fork](https://github.com/encounter/dawn) Aurora uses | MIT; nod MIT or Apache-2.0 |

## The PlayStation 5

| Who | What | Licence |
| --- | --- | --- |
| **Gary Riches (griches)** | [PS5_MarioGalaxy](https://github.com/griches/PS5_MarioGalaxy), the model for this repository, its builder and its kit (the recorded link, the builder's protocol and steps); [PS5_Dusklight](https://github.com/griches/PS5_Dusklight), whose Dawn and SDL3 patches, SDL3 drivers (`ps5/sdl`), disc reader (`ps5/nod`), start-up, shims and link script this port uses | GPL-3.0 (Galaxy); MIT, Zlib (Dusklight's files) |
| **Mihawk (mihawk-99)** | [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) (RADV on the PS5, its link recipe, CRT and `ps5-native-tool`), [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa), [PS5_PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK) (the SDK fork and its platform layer) and the [PS5 Vulkan Template](https://github.com/mihawk-99/PS5_VulkanTemplate) (`ps5/src/platform.c`) | GPL-3.0; MIT (the template) |
| **John Törnblom** | [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk), the PS5 payload SDK | GPL-3.0 |
| **BlackBearReloaded** | the PS5 native app boilerplate behind `libc.prx` and the native tools PS5_Vulkan builds on | GPL-3.0 |
| **The Mesa project** | [RADV](https://mesa3d.org), the Vulkan driver linked into the title | MIT |
| **Google and the Dawn authors** | [Dawn](https://dawn.googlesource.com/dawn), WebGPU on Vulkan | BSD-3-Clause |

## Libraries and tools

- [SDL3](https://libsdl.org) (Sam Lantinga and contributors, Zlib): the title's window, sound and
  pad, and the builder app's window and dialogs.
- [Dear ImGui](https://github.com/ocornut/imgui) (Omar Cornut and contributors, MIT): the game's
  options menu and the builder app.
- [dr_mp3](https://github.com/mackron/dr_libs) (David Reid, public domain or MIT-0): decodes the
  builder app's music.
- [LLVM](https://llvm.org) (Apache-2.0 WITH LLVM-exception): clang and lld, which build the title
  here and on the player's computer.
- [zlib](https://zlib.net) (Jean-loup Gailly and Mark Adler), and the libraries Aurora and Dawn
  fetch: fmt, zstd, libpng, FreeType, SQLite, xxHash, Abseil, Tracy and others, each under its
  own permissive licence.

## Art and music

- **"Sneaky Waltz"** by leafyjerk, made with Suno: the builder app's music
  (`ps5/builder/app/music/sneaky-waltz.mp3`). Original; not from the game.

- **Creepster** by Sideshow ([Google Fonts](https://fonts.google.com/specimen/Creepster), SIL Open
  Font Licence 1.1): the builder app's title.
- **DejaVu** fonts (Bitstream Vera licence): the builder app's text.
- Button pictures in this README: Kenney's [Input Prompts](https://kenney.nl/assets/input-prompts) (CC0).
- The home screen's icon, background and splash, and the builder's icon, are the port's own,
  drawn by `ps5/art/make-title-art.py` and `ps5/builder/app/make-app-icon.py`: nothing in them
  is from the game.

## Not affiliated

Not affiliated with or endorsed by Nintendo, Sony, or any of the projects above. Luigi's Mansion
is a trademark of Nintendo; PlayStation and PS5 are trademarks of Sony Interactive Entertainment.
No part of the game is in this repository or its downloads.
