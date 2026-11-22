@echo off
rem Build the native runner (generated MIPS C + generated H8 C + runtime). Usage: build_runtime.bat [extra cl flags]
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\SOURCES\golgo
if not exist build\rt mkdir build\rt
if not exist build\rt_h8 mkdir build\rt_h8
cl /nologo /std:c11 /W3 /O2 /MP /D_CRT_SECURE_NO_WARNINGS /Iruntime\include /Iruntime\core /Iruntime\gte /Iruntime\h8 /Igenerated /Igenerated_h8 /Fobuild\rt\ runtime\core\main.c runtime\core\machine.c runtime\core\loader.c runtime\gte\gte.c runtime\gte\rt_gte.c runtime\h8\h8_machine.c generated\*.c generated_h8\*.c /Fe:build\golgo13.exe %*
