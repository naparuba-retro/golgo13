@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\SOURCES\golgo
if not exist build\gte mkdir build\gte
cl /nologo /std:c++20 /EHsc /O2 /W3 /Itests\shim /Fobuild\gte\ /c tests\gte_ref.cpp tests\gte_test.cpp || exit /b 1
cl /nologo /std:c11 /O2 /W3 /Iruntime\gte /Iruntime\include /Fobuild\gte\ /c runtime\gte\gte.c || exit /b 1
link /nologo /OUT:build\gte_test.exe build\gte\gte_ref.obj build\gte\gte_test.obj build\gte\gte.obj
