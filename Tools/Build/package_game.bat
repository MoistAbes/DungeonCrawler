@echo off
setlocal
cd /d "%~dp0"

echo ========================================================
echo       DungeonCrawler - Unreal Engine Build Launcher
echo ========================================================
echo.
echo Wybierz konfiguracje:
echo  [1] Development (domyslna - z konsola ~ i logami do testow)
echo  [2] Shipping    (zoptymalizowana, bez konsoli - dla graczy)
echo  [3] Development + automatyczny pojedynczy plik .ZIP z data
echo  [4] Shipping    + automatyczny pojedynczy plik .ZIP z data
echo.
set /p CHOICE="Wybierz opcje (1-4) [domyslnie 1]: "

if "%CHOICE%"=="2" goto opt_ship
if "%CHOICE%"=="3" goto opt_dev_zip
if "%CHOICE%"=="4" goto opt_ship_zip
goto opt_dev

:opt_dev
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_game.ps1" -Config Development
goto end

:opt_ship
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_game.ps1" -Config Shipping
goto end

:opt_dev_zip
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_game.ps1" -Config Development -Zip
goto end

:opt_ship_zip
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_game.ps1" -Config Shipping -Zip
goto end

:end
echo.
echo Nacisnij dowolny klawisz, aby zamknac to okno...
pause >nul
