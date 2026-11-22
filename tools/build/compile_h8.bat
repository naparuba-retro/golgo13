@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\SOURCES\golgo
if not exist build\h8 mkdir build\h8
cl /nologo /std:c11 /W3 /O2 /MP /c /Iruntime\h8 /Igenerated_h8 /Fobuild\h8\ generated_h8\*.c %*
