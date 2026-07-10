@echo off
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "OUT_DIR=%ROOT%build"
set "OUT_FILE=%OUT_DIR%\ImGuiMenu.dll"
set "SRC_DIR=%ROOT%src"
set "INCLUDE_DIR=%ROOT%include"
set "LIB_DIR=%ROOT%lib"

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

echo Building the dll...

set "SOURCES="
set "SOURCES=%SOURCES% %SRC_DIR%\dllmain.cpp"
set "SOURCES=%SOURCES% %SRC_DIR%\globals.cpp"
set "SOURCES=%SOURCES% %SRC_DIR%\hooks.cpp"
set "SOURCES=%SOURCES% %SRC_DIR%\renderer.cpp"
set "SOURCES=%SOURCES% %SRC_DIR%\features.cpp"
set "SOURCES=%SOURCES% %SRC_DIR%\input.cpp"
set "SOURCES=%SOURCES% %SRC_DIR%\memory.cpp"
set "SOURCES=%SOURCES% %INCLUDE_DIR%\imgui\imgui.cpp"
set "SOURCES=%SOURCES% %INCLUDE_DIR%\imgui\imgui_draw.cpp"
set "SOURCES=%SOURCES% %INCLUDE_DIR%\imgui\imgui_widgets.cpp"
set "SOURCES=%SOURCES% %INCLUDE_DIR%\imgui\imgui_tables.cpp"
set "SOURCES=%SOURCES% %INCLUDE_DIR%\imgui\backends\imgui_impl_dx11.cpp"
set "SOURCES=%SOURCES% %INCLUDE_DIR%\imgui\backends\imgui_impl_win32.cpp"

set "INCLUDES=-I%INCLUDE_DIR% -I%INCLUDE_DIR%\imgui -I%INCLUDE_DIR%\imgui\backends"
set "FLAGS=-shared -static-libgcc -static-libstdc++ -Wl,--kill-at -Wl,--subsystem,windows -DUNICODE -D_UNICODE -O2 -s"
set "LIBS=-L%LIB_DIR% -lMinHook -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -lgdi32 -luser32 -lkernel32"

"g++" %FLAGS% -o "%OUT_FILE%" %SOURCES% %INCLUDES% %LIBS%

if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

echo Build succeeded: "%OUT_FILE%"
endlocal
