@echo off
set args=-std=c99 -ffreestanding -nostdlib -Os
set args=%args% -fno-stack-check -fno-stack-protector -mno-stack-arg-probe
set args=%args% "-Wl,-entry:entry" -lkernel32
md obj bin bin\x64 bin\x86 2>nul

set "vs2019=C:\Program Files (x86)\Microsoft Visual Studio\2019"
set vsedition=Community
if exist "%vs2019%\Enterprise" set vsedition=Enterprise

call "%vs2019%\%vsedition%\VC\Auxiliary\Build\vcvars64.bat"
set "PATH=%vs2019%\%vsedition%\VC\Tools\Llvm\bin;%PATH%"
rc /fo obj/admin.res res/admin.rc || exit /b
call :build-arch -m64 bin/x64 || exit /b
call "%vs2019%\%vsedition%\VC\Auxiliary\Build\vcvars32.bat"
call :build-arch -m32 bin/x86 || exit /b
goto :eof

:build-arch
clang %1 -o %2/shim.exe shim.c "-Wl,-subsystem:console" %args% || exit /b
clang %1 -o %2/shim-gui.exe shim.c "-Wl,-subsystem:windows" %args% || exit /b
clang %1 -o %2/shim-admin.exe shim.c obj/admin.res "-Wl,-subsystem:console" %args% || exit /b
clang %1 -o %2/shim-gui-admin.exe shim.c obj/admin.res "-Wl,-subsystem:windows" %args% || exit /b
tar -czvf %2/shim.tar.gz -C %2 shim*.exe || exit /b
goto :eof