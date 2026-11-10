<div align="center">

# Need for Speed: Underground 2 for Android

**An unofficial native Android port of the original Xbox version of Need for Speed: Underground 2, powered by static recompilation.**

![Android](https://img.shields.io/badge/Android-11%2B-3DDC84?logo=android&logoColor=white)
![Architecture](https://img.shields.io/badge/Architecture-ARM64-blue)
![Vulkan](https://img.shields.io/badge/Graphics-Vulkan-AC162C?logo=vulkan&logoColor=white)
![Status](https://img.shields.io/badge/Status-In%20Development-orange)

[Overview](#overview) · [Getting Started](#getting-started) · [Building](#building-from-source) · [Credits](#credits) · [Licensing](#licensing)

</div>

> [!IMPORTANT]
> **This project is under active development.** Features, compatibility, and stability are not guaranteed. It is not intended to be treated as a finished or production-ready game port.
>
> **No game files are provided.** You must supply your own legally obtained copy of the original Xbox game.

## Overview

This project aims to bring the original Xbox release of **Need for Speed: Underground 2** to Android as a native ARM64 application.

Rather than emulating the entire Xbox console, the project uses **static recompilation** to translate the game's Xbox x86 executable into code compiled for Android, alongside native implementations of Xbox runtime services.

The Android port is built with:

- **Kotlin** for the Android launcher and platform integration.
- **C++23** for native integration and Xbox runtime components.
- **Vulkan** for the graphics backend.
- **JNI** to connect the Android application with the native runtime.
- **XboxXBERecomp** and game-specific recompiled sources.

This is an independent Android-focused development project based on existing Xbox recompilation work.

## Getting Started

### Requirements

- An Android device running **Android 11 (API 30) or newer**.
- A **64-bit ARM** processor (`arm64-v8a`).
- A Vulkan-capable GPU.
- Sufficient storage for the original Xbox disc image.
- Full storage access permission for automatic game discovery.

Hardware compatibility is not guaranteed.

### Game Files

The Android application accepts **Xbox ISO/XISO disc images** as game inputs. Standalone `.xbe` files and extracted game directories are not supported as game inputs.

After granting full storage access, the application uses the following directory:

```text
/sdcard/Underground2/
```

Place your original Xbox disc image there, for example:

```text
/sdcard/Underground2/NFSU2.xiso.iso
```

The application creates the directory when needed, scans it for supported images, and validates candidate discs against the game release used for recompilation. Disc images located directly inside the directory or one subdirectory below it can be discovered.

Alternatively, use **Select Game File** to choose an image through Android's system file picker.

Game images are read directly from storage; the application does not need to extract an ISO or copy the full disc image into its cache.

> [!NOTE]
> Compatibility depends on the original Xbox release and executable used for recompilation. Not every ISO or modified game release will be accepted.

## Building from Source

The Android project is located in [`android/`](android/), with its native CMake configuration in [`tools/android/CMakeLists.txt`](tools/android/CMakeLists.txt).

### Build Requirements

| Component | Configuration |
|---|---|
| Android SDK | Compile SDK 37 |
| Minimum Android version | API 30 |
| Target Android version | API 37 |
| Android NDK | `30.0.16248370` |
| Target architecture | `arm64-v8a` |
| Native language standard | C++23 |
| Native optimization | `-O2` |
| Java compatibility | Java 17 |
| Build tools | Gradle, CMake, Android NDK |

A Linux development environment with the required Android SDK packages, NDK, and JDK is recommended.

### Game-Specific Build Inputs

A complete native build requires developer-provided data and generated recompilation artifacts, including:

```text
assets/default.xbe
generated/game/x86_recomp.*.cpp
generated/game/x86_func_mapping.cpp
```

These files are **not distributed in this repository**. The executable must come from your own game copy, and the generated code must match it.

### Build the APK

From the repository root:

```sh
cd android
./gradlew :app:assembleDebug --no-daemon --stacktrace
```

The debug APK is produced at:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

A successful build does not guarantee that all game functionality works on a given device.

## Repository Structure

| Path | Description |
|---|---|
| [`android/`](android/) | Android application, Kotlin launcher, and UI resources |
| [`tools/android/`](tools/android/) | JNI bridge, Android host integration, and native game validation |
| [`sdk/`](sdk/) | Vendored XboxXBERecomp SDK and runtime |
| [`config/`](config/) | Game-specific recompilation configuration |
| `generated/` | Generated game code; excluded from version control |
| `assets/` | Local game-specific inputs; excluded from version control |

## Credits

This project would not be possible without the work of the original developers and the wider Xbox reverse-engineering and recompilation community.

### Original Project and Recompilation

- **[antoxa2584x/nfsu2-sw](https://github.com/antoxa2584x/nfsu2-sw)** — Original *Need for Speed: Underground 2* static recompilation project and upstream source history.
- **[XboxXBERecomp](https://github.com/abilliontokens/XboxXBERecomp)** — Recompilation framework and Xbox runtime used as the foundation for the Android integration.
- **[TGP482](https://github.com/TGP482)** — Original Xbox XBE Recomp work and related Xbox recompilation projects.
- **[XenonRecomp](https://github.com/hedge-dev/XenonRecomp)** — Technical inspiration for static recompilation and function-hooking approaches.

### Libraries and References

- **[Zydis](https://github.com/zyantific/zydis)** — x86 instruction decoding.
- **[toml++](https://github.com/marzer/tomlplusplus)** — TOML configuration support.
- **[Dear ImGui](https://github.com/ocornut/imgui)** — UI library used by upstream tooling.
- **[SDL](https://github.com/libsdl-org/SDL)** — Platform and input support used by upstream components.
- **[xemu](https://github.com/xemu-project/xemu)** — Reference for original Xbox hardware behavior and the source of certain separately licensed upstream components.
- **[Cxbx-Reloaded](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded)** — Reference for Xbox platform and API behavior.

Acknowledgment does not imply that every listed project is directly included in the Android APK or that its maintainers endorse this port. Additional contributors are credited in the bundled upstream documentation.

## Licensing

This repository includes code originating from multiple projects. Their individual copyright notices and license terms remain applicable:

- [`sdk/LICENSE`](sdk/LICENSE) — MIT license for the vendored XboxXBERecomp SDK.
- [`sdk/THIRD_PARTY.txt`](sdk/THIRD_PARTY.txt) — SDK third-party attributions and notices.

**There is currently no single project-wide license declared at the repository root.** Do not assume that a license applying to one bundled component automatically applies to the entire repository. Review the relevant notices before redistributing source or binaries.

## Disclaimer

This is an **unofficial fan-made project** and is not affiliated with, sponsored by, or endorsed by Electronic Arts, EA Black Box, Microsoft, or any other rights holder.

*Need for Speed*, *Need for Speed: Underground 2*, Xbox, and related names, artwork, and trademarks belong to their respective owners. The original game was developed by EA Black Box and published by Electronic Arts.

**No copyrighted game images, executables, or other proprietary game assets are provided or authorized for redistribution by this repository.**
