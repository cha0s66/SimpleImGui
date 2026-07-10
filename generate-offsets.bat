@echo off
setlocal

set "ROOT=%~dp0"
set "DUMPER=%ROOT%cs2-dumper\cs2-dumper.exe"
set "OUTPUT=%ROOT%include\offsets"

if not exist "%DUMPER%" (
    echo ERROR: cs2-dumper.exe not found in "%ROOT%cs2-dumper\"
    exit /b 1
)

if not exist "%OUTPUT%" mkdir "%OUTPUT%"

echo Generating CS2 header files into "%OUTPUT%"...
"%DUMPER%" --file-types hpp --output "%OUTPUT%" --process-name cs2.exe --no-log-file
if errorlevel 1 (
    echo ERROR: cs2-dumper failed.
    exit /b %errorlevel%
)

echo Done. Generated .hpp files are now in "%OUTPUT%".
exit /b 0
