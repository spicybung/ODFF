# ODFF 0.1.9 standalone build

The Release build is now configured as a single-file Windows application.

## What is compiled into ODFF.exe

- ODFF source
- Dear ImGui core
- Dear ImGui GLFW backend
- Dear ImGui OpenGL 2 backend
- GLFW
- Microsoft static C/C++ runtime

## What remains provided by Windows

ODFF still imports normal Windows operating-system libraries, including OpenGL and window/dialog services. These are already present on Windows and are not files that should be copied beside ODFF.

## Build commands

```bat
cd C:\Users\PieMu\Desktop\ReignsStudio\GTA\SAMP\ODFF
rmdir /s /q build
cmake -S . -B build -A x64
cmake --build build --config Release
```

The distributable executable is copied automatically to:

```text
build\Standalone\ODFF.exe
```

The regular CMake output also remains at:

```text
build\Release\ODFF.exe
```

Both executables are the same standalone binary.
