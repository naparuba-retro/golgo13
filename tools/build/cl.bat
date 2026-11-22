@echo off
rem Wrapper: charge l'environnement MSVC (Build Tools deja presents sur la machine) puis lance cl avec les arguments recus.
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul 2>&1
cl /nologo %*
