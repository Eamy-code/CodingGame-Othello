@echo off
setlocal

set "TEST_DIR=%~dp0"
for %%I in ("%TEST_DIR%..") do set "REPO_ROOT=%%~fI"

set /p "OLD_RELATIVE_PATH=old_cpp_file: "
if not defined OLD_RELATIVE_PATH (
    echo ERROR: OLD source path is required.
    exit /b 1
)

set /p "NEW_RELATIVE_PATH=new_cpp_file: "
if not defined NEW_RELATIVE_PATH (
    echo ERROR: NEW source path is required.
    exit /b 1
)

set /p "Change_Details=Change_Details: "
if not defined Change_Details (
    echo ERROR: NEW change description is required.
    exit /b 1
)

set /p "MATCH_GAMES=Games: "
if not defined MATCH_GAMES (
    echo ERROR: Games is required.
    exit /b 1
)

set "OLD_RELATIVE_PATH=%OLD_RELATIVE_PATH:"=%"
set "NEW_RELATIVE_PATH=%NEW_RELATIVE_PATH:"=%"
for %%I in ("%REPO_ROOT%\%OLD_RELATIVE_PATH%") do set "OLD_SOURCE=%%~fI"
for %%I in ("%REPO_ROOT%\%NEW_RELATIVE_PATH%") do set "NEW_SOURCE=%%~fI"
set "BUILD_DIR=%TEST_DIR%build"
set "RESULT_DIR=%TEST_DIR%result"
set "OLD_BOT=%BUILD_DIR%\old_bot.exe"
set "NEW_BOT=%BUILD_DIR%\new_bot.exe"
set "CXX=g++"

if exist "C:\msys64\ucrt64\bin\g++.exe" (
    set "CXX=C:\msys64\ucrt64\bin\g++.exe"
    set "PATH=C:\msys64\ucrt64\bin;%PATH%"
)

if not exist "%OLD_SOURCE%" (
    echo ERROR: OLD source file not found: %OLD_SOURCE%
    exit /b 1
)

if not exist "%NEW_SOURCE%" (
    echo ERROR: NEW source file not found: %NEW_SOURCE%
    exit /b 1
)

where python >nul 2>nul
if errorlevel 1 (
    echo ERROR: python was not found in PATH.
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%RESULT_DIR%" mkdir "%RESULT_DIR%"

pushd "%REPO_ROOT%"
call :prepare_bot OLD "%OLD_SOURCE%" "test\build\old_bot.exe"
if errorlevel 1 (
    popd
    exit /b 1
)

call :prepare_bot NEW "%NEW_SOURCE%" "test\build\new_bot.exe"
if errorlevel 1 (
    popd
    exit /b 1
)
popd

echo.
python "%TEST_DIR%src\othello_match_runner.py" ^
    --old "%OLD_BOT%" ^
    --new "%NEW_BOT%" ^
    --old-source "%OLD_RELATIVE_PATH%" ^
    --new-source "%NEW_RELATIVE_PATH%" ^
    --new-change "%Change_Details%" ^
    --games "%MATCH_GAMES%" ^
    --result-dir "%RESULT_DIR%"
exit /b %errorlevel%

:prepare_bot
for %%E in ("%~2") do set "SOURCE_EXTENSION=%%~xE"
if /I "%SOURCE_EXTENSION%"==".py" (
    if /I "%~1"=="OLD" set "OLD_BOT=%~2"
    if /I "%~1"=="NEW" set "NEW_BOT=%~2"
    echo Using %~1 Python bot: %~2
    exit /b 0
)

if /I not "%SOURCE_EXTENSION%"==".cpp" (
    echo ERROR: %~1 bot must be a .cpp or .py file: %~2
    exit /b 1
)

if /I "%CXX%"=="g++" (
    where g++ >nul 2>nul
    if errorlevel 1 (
        echo ERROR: g++ was not found in PATH. It is required for %~1 bot.
        exit /b 1
    )
)

echo Compiling %~1 bot with local metrics...
"%CXX%" -std=c++17 -O2 -DOTHELLO_ENABLE_METRICS "%~2" -o "%~3"
if errorlevel 1 (
    echo ERROR: Failed to compile %~1 bot.
    exit /b 1
)
exit /b 0
