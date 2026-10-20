@echo off
setlocal
set NDK=C:\ndk\android-ndk-r30
set API=23
set REPO=C:\hoverdwell-repo
set OUT=C:\hoverdwell-build
set APK=C:\hoverdwell\app\build\outputs\apk\debug\app-debug.apk
set CC=%NDK%\toolchains\llvm\prebuilt\windows-x86_64\bin\armv7a-linux-androideabi%API%-clang.cmd
set ZIP=%USERPROFILE%\Downloads\hoverdwell-v0.1.1-alpha.zip

if not exist "%CC%" (echo FAILED: compiler not found: %CC% & pause & exit /b 1)
if not exist "%APK%" (echo FAILED: APK not found. Build it in Android Studio first: %APK% & pause & exit /b 1)

rmdir /s /q "%OUT%" 2>nul
mkdir "%OUT%\bin"

echo [1/4] compiling daemon
call "%CC%" -O2 "%REPO%\daemon\dwell.c" -o "%OUT%\bin\dwell" -lm
if errorlevel 1 (echo FAILED: compile & pause & exit /b 1)

echo [2/4] copying APK
copy "%APK%" "%OUT%\hoverdwell.apk" >nul
if errorlevel 1 (echo FAILED: copy APK & pause & exit /b 1)

echo [3/4] converting scripts to LF
powershell -NoProfile -ExecutionPolicy Bypass -File "%REPO%\tools\eol.ps1" "%REPO%\module" "%OUT%"
if errorlevel 1 (echo FAILED: line endings or missing file & pause & exit /b 1)

echo [4/4] zipping
if exist "%ZIP%" del "%ZIP%"
cd /d "%OUT%"
tar -a -c -f "%ZIP%" *
if errorlevel 1 (echo FAILED: zip & pause & exit /b 1)

echo Done: %ZIP%
tar -tf "%ZIP%"
pause