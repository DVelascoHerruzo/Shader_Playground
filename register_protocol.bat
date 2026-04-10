@echo off
:: register_protocol.bat
:: Run this once to register the "shaderplayground://" URL scheme.
:: After that, clicking a link like:
::   <a href="shaderplayground://launch">Launch Demo</a>
:: on any website will open ShaderPlayground.exe.
::
:: No admin rights required — registers under HKEY_CURRENT_USER.

setlocal

set "EXE=%~dp0ShaderPlayground.exe"

if not exist "%EXE%" (
    echo ERROR: ShaderPlayground.exe not found next to this script.
    echo Please run register_protocol.bat from the ShaderPlayground folder.
    pause
    exit /b 1
)

:: Register the URL protocol
reg add "HKCU\SOFTWARE\Classes\shaderplayground"                           /ve /d "URL:Shader Playground Protocol"    /f
reg add "HKCU\SOFTWARE\Classes\shaderplayground"                           /v  "URL Protocol"                         /f
reg add "HKCU\SOFTWARE\Classes\shaderplayground\shell\open\command"        /ve /d "\"%EXE%\" \"%%1\""                 /f

echo.
echo Done! The shaderplayground:// URL scheme is now registered.
echo You can test it by opening this address in your browser:
echo   shaderplayground://launch
echo.
pause
