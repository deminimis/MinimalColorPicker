# 🎨 Minimal Color Picker

A tiny, blazing-fast, and distraction-free color picker for Windows, written in pure C (Win32 API). Designed to be as lightweight and performant as possible while providing essential features for developers and designers.

## Features

* **Zero-Bloat Performance**: Built purely with the Win32 API and GDI. Extremely low CPU and RAM footprint.
* **Instant Activation**: Summon instantly via a global system hotkey without interrupting your workflow.
* **Magnifying Loupe**: A smooth 6x zoom loupe attached to your cursor for precise, pixel-perfect selection.
* **Color Averaging**: Grab exactly what you need. Supports 1x1 (single pixel), 3x3, 5x5, and 15x15 pixel averaging modes.
* **Clipboard Ready**: Instantly copies the selected color to your clipboard in standard Hex format (e.g., `#1A2B3C`).
* **Non-Intrusive UI**: A "Copied" toast notification right at your cursor that instantly fades away.
* **System Tray Integration**: Runs quietly in the background. Right-click the tray icon to change settings or exit.
* **High-DPI Aware**: Renders sharply and maps pixels perfectly on modern 4K/High-DPI displays.
* **Portable Configuration**: Settings are saved automatically to a simple `.ini` file in the exact same folder as the executable.

## Usage

This app is fully portable. Simply extract the .exe anywhere in your User folder (like Documents or a dedicated portable apps folder). Avoid placing it in C:\Program Files\ so the app can successfully save your settings to its .ini file

Once the application is running, it minimizes automatically to your system tray.



### Controls
* **Summon Picker**: Press `Ctrl + Shift + D` (Default, may be changed in the `.ini`
* **Select & Copy**: `Left Click`
* **Cancel/Dismiss**: `Esc` or `Right Click`

### System Tray Menu
Right-click the eyedropper icon in your Windows System Tray to:
* Trigger the picker manually.
* Change the **Sample Size** (1x1, 3x3, 5x5, 15x15).
* Exit the application.

## ⚙️ Configuration (`.ini` file)

Upon first run, the app generates a `[App-Name].ini` file in the same directory as the executable. You can edit this file to customize your hotkey.


# Contributing / Build Instructions

Because this project is engineered to be as tiny and performant as possible, compiling the **Release (x64)** build is a bit more complex than standard Windows applications. We completely strip out the C Runtime (CRT) to achieve a minimal footprint.

If you'd like to contribute or build from source, follow these precise configuration steps.

### Prerequisites
* **Visual Studio** with the "Desktop development with C++" workload installed.
* **Platform Toolset:** `v145` (Ensure you have the latest build tools).
* **Windows SDK:** Version `10.0` or higher.
* **C++ Standard:** ISO C++20 Standard (`/std:c++20`).


### Project Configuration Guide
If you are importing the source files into a new project, you must mimic the provided `.vcxproj` settings. 


#### 1. Compiler Settings (C/C++) for Release/x64
To keep the binary size as small as possible, configure the following in `Configuration Properties > C/C++`:
* **Security Checks:** Disable SDL checks (`/sdl-`).
* **Code Generation:** Disable Buffer Security Check (`/GS-`).
* **Language:** Set C++ Language Standard to `stdcpp20`.

#### 3. Linker Settings for Release/x64 
Because we are building without the standard C library, the linker needs specific instructions to find the entry point. Navigate to `Configuration Properties > Linker`:
* **Input > Ignore All Default Libraries:** Set to **Yes** (`/NODEFAULTLIB`).
* **Advanced > Entry Point:** Explicitly set this to `WinMain`.
