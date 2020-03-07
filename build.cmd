@echo off
set args=-std=c99 -ffreestanding -nostdlib -Os -s
set args=%args% -fno-stack-check -fno-stack-protector -mno-stack-arg-probe
set args=%args% "-Wl,-eentry" -lkernel32
md obj bin bin\x64 bin\x86 2>nul
windres res/admin.rc obj/admin.o || exit /b
call :build-arch -m64 bin/x64 || exit /b
call :build-arch -m32 bin/x86 || exit /b
tar -cavf bin/shim.zip -C bin --options zip:compression=store x64 x86 || exit /b
goto :eof

:build-arch
gcc %1 -o %2/shim.exe shim.c -mconsole %args% || exit /b
gcc %1 -o %2/shim-gui.exe shim.c -mwindows %args% || exit /b
gcc %1 -o %2/shim-admin.exe shim.c obj/admin.o -mconsole %args% || exit /b
gcc %1 -o %2/shim-gui-admin.exe shim.c obj/admin.o -mwindows %args% || exit /b
goto :eof