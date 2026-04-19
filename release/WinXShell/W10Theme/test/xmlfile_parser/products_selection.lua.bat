--[=[ 2>nul
cd /d "%~dp0"
set "WINXSHELL_LOGFILE=%Temp%\WinXShell.log"

set WINXSHELL=WinXShell.exe
set x8664=x64
if not "x%PROCESSOR_ARCHITECTURE%"=="xAMD64" set x8664=x86
set WINXSHELL=..\..\WinXShell_%x8664%.exe
if not exist "%WINXSHELL%" set WINXSHELL=..\..\WinXShell.exe
if not exist %WINXSHELL% set WINXSHELL=..\..\x64\Debug\WinXShell.exe

:LOOP
%WINXSHELL% -console -script "%~dpn0"
type "%WINXSHELL_LOGFILE%"
pause
goto :LOOP

goto :EOF
]=]

--- -- ====================  lua script  ====================
-- Alias
MsgBox = winapi.show_message
MsgBox("title", "message")
App.Print(App.ScriptFile)
