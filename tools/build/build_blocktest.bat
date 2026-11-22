@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\SOURCES\golgo
cl /nologo /std:c11 /W3 /O2 /MP /Iruntime\include /Igenerated /Fobuild\obj\ tests\blocktest.c generated\*.c /Fe:build\blocktest.exe
