# SimpleImGui

SimpleImGui is a lightweight C++ template for building an ImGui-based overlay or menu injected into a Windows application. It provides a basic Direct3D11 rendering setup, a MinHook-based Present hook, and a simple DLL structure you can extend for your own UI.

## Features

- Windows DLL project structure
- ImGui integration with Direct3D11
- MinHook-based hook template
- Basic cleanup and unload handling
- Easy starting point for custom overlays or menus

## Project structure

- src/ - core DLL entry points, hooks, renderer logic, and global state
- include/ - headers and vendored ImGui sources
- lib/ - supporting libraries such as MinHook
- build.bat - build script for producing the DLL

## Build

1. Install a Windows C++ toolchain with MinGW-w64 and the required development libraries.
2. Make sure the build dependencies are available in the project directories.
3. Run the following from the repository root:

```bat
build.bat
```

The output DLL will be written to the build folder as ImGuiMenu.dll.

## Usage

This repository is intended as a template rather than a finished product. To use it:

- inject the built DLL into a target process,
- adapt the hook and renderer code to your needs,
- customize the ImGui UI in the renderer code.

## Notes

- This project is meant for learning, experimentation, and personal development.
- Always respect the target application's terms of service and applicable laws.
- You may need to adjust the code for specific games or applications.

## License

This project is licensed under the GNU General Public License v3.0. See the LICENSE file for details.
