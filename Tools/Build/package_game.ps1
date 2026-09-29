<#
.SYNOPSIS
    Automatyczny skrypt do budowania (packaging) gry DungeonCrawler w Unreal Engine 5.8.
.PARAMETER Config
    Konfiguracja kompilacji: 'Development' (domyślnie, z konsolą i logami) lub 'Shipping' (zoptymalizowana wersja dla graczy).
.PARAMETER Zip
    Flaga opcjonalna: automatycznie kompresuje folder wynikowy do pojedynczego pliku .zip z datą i godziną.
.PARAMETER Clean
    Flaga opcjonalna: wymusza pełny rebuild zamiast szybkiego (iterative).
#>
param (
    [ValidateSet("Development", "Shipping")]
    [string]$Config = "Development",
    [switch]$Zip = $false,
    [switch]$Clean = $false
)

$ErrorActionPreference = "Stop"

$EngineUAT = "E:\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat"
$ProjectFile = "E:\UE_PROJECTS\MyProject\MyProject.uproject"
$BaseOutputDir = "F:\UNREAL_ENGINE_BUILDS"

$Timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm"
$BuildFolderName = "DungeonCrawler_${Config}_${Timestamp}"
$TargetDir = Join-Path $BaseOutputDir $BuildFolderName

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "   DungeonCrawler - Unreal Engine 5.8 Auto Packaging      " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "Konfiguracja: $Config" -ForegroundColor Yellow
Write-Host "Projekt:      $ProjectFile" -ForegroundColor Gray
Write-Host "Katalog celu: $TargetDir" -ForegroundColor Yellow
Write-Host "Czas startu:  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Gray
Write-Host "==========================================================" -ForegroundColor Cyan

if (-not (Test-Path $BaseOutputDir)) {
    New-Item -ItemType Directory -Path $BaseOutputDir -Force | Out-Null
}

$Arguments = @(
    "BuildCookRun",
    "-project=`"$ProjectFile`"",
    "-platform=Win64",
    "-clientconfig=$Config",
    "-cook",
    "-stage",
    "-pak",
    "-package",
    "-archive",
    "-archivedirectory=`"$TargetDir`""
)

if (-not $Clean) {
    $Arguments += "-iterativecooking"
}

Write-Host "`n[1/3] Uruchamiam Unreal Automation Tool (UAT)..." -ForegroundColor Green
$Process = Start-Process -FilePath $EngineUAT -ArgumentList ($Arguments -join " ") -Wait -PassThru -NoNewWindow

if ($Process.ExitCode -ne 0) {
    Write-Host "`n[BLAD] Proces budowania zakonczyl sie kodem bledu: $($Process.ExitCode)" -ForegroundColor Red
    exit $Process.ExitCode
}

Write-Host "`n[2/3] Budowanie zakonczone sukcesem!" -ForegroundColor Green
Write-Host "Folder z gra: $TargetDir\Windows" -ForegroundColor Cyan

if ($Zip) {
    $ZipPath = Join-Path $BaseOutputDir "$BuildFolderName.zip"
    Write-Host "`n[3/3] Tworze pojedyncze archiwum ZIP: $ZipPath..." -ForegroundColor Yellow
    Compress-Archive -Path "$TargetDir\Windows\*" -DestinationPath $ZipPath -Force
    Write-Host "Archiwum ZIP gotowe do wyslania: $ZipPath" -ForegroundColor Green
} else {
    Write-Host "`n[Wskazowka] Aby automatycznie spakowac do jednego pliku .zip, uruchom z flaga: -Zip" -ForegroundColor Gray
}

Write-Host "`n==========================================================" -ForegroundColor Cyan
Write-Host "   Paczka gotowa!" -ForegroundColor Green
Write-Host "   Plik startowy: $TargetDir\Windows\MyProject.exe" -ForegroundColor White
Write-Host "==========================================================" -ForegroundColor Cyan
