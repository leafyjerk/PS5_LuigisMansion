# Luigi's Mansion for the PlayStation 5 — working notes

Handoff notes for whoever picks this up next. State as of 2026-10-23.

## Where it stands

- **The game runs on a PS5** (2026-10-23): boots to the file select and into the game at
  3840x2160, 60 fps (interpolated), DualSense, saves to userdata. Title PPSA99351.
- The builder apps (Windows, Linux) and their kits: ps5/builder.

## Layout

```
vendor/luigis-mansion-recompiled/   rob646464's repo, submodule at 850a2b9; the port's edits are
                                    ps5/patches/lm-recomp.patch (applied by ps5/tools/setup-source.sh)
ps5/cmake/ps5-lm.cmake              CMAKE_PROJECT_lmport_INCLUDE: Dawn/SDL3/nod for the console, the
                                    platform archive, the link through ps5/tools/link-title.sh
ps5/patches/                        lm-recomp.patch; aurora-9000-ps5.patch (Dusklight's: Dawn surface,
                                    sRGB DDS), copied into LM's own Aurora series; dawn.patch,
                                    dawn-abseil.patch, sdl.patch (Dusklight's)
ps5/src/                            platform.c/h (Mihawk's template), shims.c (Dusklight), lm_shims.c
                                    (strsignal, getcwd, chdir), log.c/h (stdout/stderr -> klog +
                                    userdata/log.txt; exit/_exit/_Exit/raise wraps), startup.c
ps5/sdl/, ps5/nod/                  Dusklight's SDL3 drivers and .iso/.ciso reader
ps5/tools/                          setup-stack, setup-sdk/dawn/sdl/source, prepare-game, build,
                                    link-title, deploy, fetch-log, ship, run-logged, cc-init-array
ps5/builder/                        builder.cpp (lm-builder), app/ (SDL3 + Dear ImGui), build-tools.sh,
                                    fetch-llvm.sh, fetch-app-deps.sh, make-kit.sh, package.sh
ps5/art/make-title-art.py           icon0.png, pic0/pic1 (-> DDS via ps5/tools/png-to-title-dds.py)
.deps/ build/ dist/ gamedata/ logs/ never committed
```

## What the console needed (keep them)

- **Cross build vs. the recompiler**: lm-recomp's CMake builds and runs lmrecompile inside the
  same build; the console's cross build cannot, so prepare-game.sh builds it for the host and
  CMake takes the output through `LM_RECOMP_PREGEN`. Only target `lmrecomp` is built (lmport and
  LuigisMansionRecomp are the PC's).
- **CRLF**: files that pass through Git for Windows get CRLF and break patches and shell scripts:
  `.gitattributes` forces LF.
- **exit**: a title may not call exit(2) (SIGSYS, a crash report with no reason).
  exit/_exit/_Exit are wrapped (log.c): the log is written, then the shell is asked to close the
  title (platform.c catchReturnFromMain). main.cpp returns instead of `_Exit` on PS5.
- **getcwd/chdir**: libc's getcwd calls __getcwd (libkernel_sys only): a null import, a jump to
  address 0. Bound to ps5dl_ shims answering "/app0" (as the Galaxy port does).
- **std::filesystem outside /app0**: anything above the title's folder answers a non-ENOENT error,
  and the throwing overloads throw (std::terminate). Use the error_code forms there
  (hidden_mansion.cpp's overlay search walked up to /).
- **thread_local initialisers** (`_ZTH*`) nothing defines: defsym'd to 0 at the link (3 of them).
- **strsignal**: not exported to a title; shimmed.
- **libc++ 18**: no std::atomic_ref (sched.cpp has a stand-in); FreeBSD's bswap16/32 macros
  (ctx.h undefines them); FreeBSD's mcontext (mc_rip/mc_rsp) in the crash handler.
- **printf** writes to libc's own stdout whatever `stdout` is: printf/vprintf/puts/putchar wrapped.
- **Defaults on PS5**: settings.h targetFps 60; startup.c SDL_PS5_DISPLAY=3840x2160 (the game
  renders at the window's size); the touch pad's click (SDL "back") opens the F1 menu with ImGui's
  gamepad navigation (menu.cpp, controls.cpp).
- **Home screen art**: the shell shows /user/appmeta/<TITLE_ID>/'s copy, made at registration:
  deploy.sh refreshes it.

## Loop

```
ps5/tools/ship.sh          # save lm-recomp edits to the patch, build, deploy (PS5_HOST in .env)
ps5/tools/fetch-log.sh     # userdata/log.txt back, a "=== crash" backtrace symbolised
```

The eboot loads at 0x400000; each build's ELF is kept as build/elf-history/PPSA99351-<id>.elf,
the id (12 hex of eboot.bin's sha256) in the title's sce_sys/build-id.txt. etaHEN's FTP lists a
SELF's unpacked size: never skip eboot.bin by size.

## The builder

- lm-builder steps: nodtool info (Game ID GLME01) -> nodtool convert to PPSA99351/GLME01.iso ->
  nodtool extract (main.dol) -> lmrecompile -> clang -cc1 for each translated file with the
  kit's recorded job (flags/game-cc1.txt) -> llvm-ar -> ld.lld with link/lld.args ({GAME}/libgame.a
  linked whole) -> ps5-native-tool link + self sign -> kit/title files.
- The kit's compiler job is the build's own cc1 job (from the driver's -###), not the driver: no
  SCE_PROSPERO_SDK_DIR or driver differences on the player's computer.
- The link is recorded by link-title.sh when PS5_KIT_DIR is set; the translated objects
  (recomp_gen/*.o) become `--whole-archive {GAME}/libgame.a --no-whole-archive`.
- Windows tools are cross-compiled with MinGW-w64 (posix threads); LLVM is the official 22.1.8
  release for each platform. Linux tools need glibc 2.39+ (built on Ubuntu 24.04).
- **Licence question before a public release**: the kit carries Luigi's Mansion Recompiled's
  runtime compiled, and its repository states no licence: ask rob646464.
