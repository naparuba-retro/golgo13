@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\SOURCES\golgo
if not exist build\gpu mkdir build\gpu
cl /nologo /std:c11 /W3 /O2 /Iruntime\gpu /Fobuild\gpu\ tests\gpu_test.c runtime\gpu\gpu.c /Fe:build\gpu_test.exe
