@echo off
rem Compile all generated C (syntax/semantic check + objects). Usage: compile_generated.bat [extra cl flags]
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul 2>&1
cd /d C:\SOURCES\golgo
cl /nologo /std:c11 /W3 /O2 /MP /c /Iruntime\include /Igenerated /Fobuild\obj\ generated\*.c %*
