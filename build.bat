@echo off
setlocal
set MSBUILD="C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
set CMAKE="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set ROOT=%~dp0
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Debug

if not exist "%ROOT%build\CMakeCache.txt" (
    echo [CMake] First-time configure...
    %CMAKE% -S "%ROOT%" -B "%ROOT%build" -G "Visual Studio 18 2026" -A x64
    if errorlevel 1 goto error
) else (
    rem Cache exists — regenerate quietly (no -G so it uses the cached generator)
    %CMAKE% -S "%ROOT%" -B "%ROOT%build" 2>nul
)

:build
%MSBUILD% "%ROOT%build\ShaderPlayground.vcxproj" /p:Configuration=%CONFIG% /p:PlatformToolset=v143 /m /nologo
if errorlevel 1 goto error

rem --- Sync resource folders to output dir (in case post-build step was skipped) ---
set OUTDIR=%ROOT%build\%CONFIG%
if exist "%OUTDIR%" (
    xcopy /E /I /Y /Q "%ROOT%assets\textures" "%OUTDIR%\textures" >nul
    xcopy /E /I /Y /Q "%ROOT%shaders"  "%OUTDIR%\shaders"  >nul
    if exist "%ROOT%assets" xcopy /E /I /Y /Q "%ROOT%assets" "%OUTDIR%\assets" >nul
)
goto done

:error
echo.
echo *** Build failed! ***
pause
exit /b 1

:done
