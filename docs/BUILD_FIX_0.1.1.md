# ODFF 0.1.1 build fix

This update fixes the Visual Studio build errors reported from ODFF 0.1.0.

## Fixed

- Removed Dear ImGui docking-branch-only `ImGuiConfigFlags_DockingEnable`.
- Removed docking-branch-only `ImGui::SetNextWindowViewport`.
- Replaced internal `ImGui::SeparatorEx` and `ImGuiSeparatorFlags_Vertical` usage with public ImGui API.
- Fixed the C++ most-vexing-parse error in `RenderWareReader.cpp`.
- Fixed the same iterator-construction error in `TxdReader.cpp`.
- Added explicit `<iterator>` includes.
- Added `shell32` and `ole32` to the Windows linker dependencies for the folder picker.

## Rebuild

Delete the old generated build directory before rebuilding:

```bat
rmdir /s /q build
cmake -S . -B build -A x64
cmake --build build --config Release
```
