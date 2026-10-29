@echo off

set BUILD_DIR=%1
if "%BUILD_DIR%" == "" set BUILD_DIR=../build/win

set PROJECT_DIR=%~dp0
pushd "%PROJECT_DIR%"

call cmake --platform "win_msvc_v141" -B "%BUILD_DIR%" -S ".."

:do_exit
popd
if %ERRORLEVEL% neq 0 pause
exit /b %ERRORLEVEL%
