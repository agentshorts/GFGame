# Package a Win64 Development build that boots into L_Hub.
# Usage:
#   powershell -ExecutionPolicy Bypass -File Tools/package_win64.ps1 -EngineDir <UE 5.8 root> -OutputDir <folder>
# -EngineDir defaults to the UE_ENGINE_DIR environment variable.
# The engine root is the folder that contains Engine\ (a path that already ends in Engine\ is accepted too).

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$EngineDir = $env:UE_ENGINE_DIR,

    [Parameter(Mandatory = $true, Position = 1)]
    [string]$OutputDir
)

$ErrorActionPreference = "Continue"

if ([string]::IsNullOrWhiteSpace($EngineDir)) {
    Write-Error "Pass -EngineDir or set UE_ENGINE_DIR to the Unreal Engine 5.8 root."
    exit 1
}
if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) {
    Write-Error "Run this script with -File so it can find GFGame.uproject."
    exit 1
}

$EngineDir = $EngineDir.Trim().TrimEnd('\', '/')
$EngineDir = [System.IO.Path]::GetFullPath($EngineDir)

$RunUAT = Join-Path $EngineDir "Engine\Build\BatchFiles\RunUAT.bat"
if (-not (Test-Path -LiteralPath $RunUAT)) {
    $EngineFolderUAT = Join-Path $EngineDir "Build\BatchFiles\RunUAT.bat"
    if (Test-Path -LiteralPath $EngineFolderUAT) {
        $RunUAT = $EngineFolderUAT
    }
}
if (-not (Test-Path -LiteralPath $RunUAT)) {
    Write-Error "Could not find RunUAT.bat under $EngineDir."
    exit 1
}

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$UProject = Join-Path $ProjectRoot "GFGame.uproject"
if (-not (Test-Path -LiteralPath $UProject)) {
    Write-Error "Could not find project file: $UProject"
    exit 1
}
$UProject = (Resolve-Path -LiteralPath $UProject).Path

$OutputDir = [System.IO.Path]::GetFullPath($OutputDir).TrimEnd('\', '/')
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Write-Host "RunUAT: $RunUAT"
Write-Host "Project: $UProject"
Write-Host "Archive: $OutputDir"

& cmd.exe /d /c "call `"$RunUAT`" BuildCookRun -project=`"$UProject`" -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory=`"$OutputDir`" -nop4 -utf8output"
$UatExit = $LASTEXITCODE
if ($UatExit -ne 0) {
    Write-Error "RunUAT BuildCookRun failed with exit code $UatExit."
    exit $UatExit
}

$Preferred = $null
$Fallback = $null
foreach ($Name in @("GFGame.exe", "GFGame-Win64-Development.exe")) {
    $Found = @(Get-ChildItem -LiteralPath $OutputDir -Recurse -File -Filter $Name -ErrorAction SilentlyContinue)
    foreach ($Item in $Found) {
        if ($Item.DirectoryName -match '[\\/]Binaries[\\/]Win64$') {
            $Preferred = $Item
            break
        }
        if (-not $Fallback) {
            $Fallback = $Item
        }
    }
    if ($Preferred) {
        break
    }
}

$Exe = $Preferred
if (-not $Exe) {
    $Exe = $Fallback
}
if (-not $Exe) {
    Write-Error "BuildCookRun finished, but no GFGame executable was found under $OutputDir."
    exit 1
}

Write-Host "Packaged executable: $($Exe.FullName)"

# Binaries\Win64 sits two folders under the staged project root.
if ($Exe.DirectoryName -match '[\\/]Binaries[\\/]Win64$') {
    $ProjectDir = Split-Path -Parent (Split-Path -Parent $Exe.DirectoryName)
    $CsvDir = Join-Path $ProjectDir "Data\CSV"
    if (Test-Path -LiteralPath $CsvDir) {
        $CsvCount = @(Get-ChildItem -LiteralPath $CsvDir -File -Filter *.csv -ErrorAction SilentlyContinue).Count
        Write-Host "Staged CSV directory: $CsvDir ($CsvCount csv files)"
    }
    else {
        Write-Warning "CSV directory was not staged at $CsvDir."
    }
}

exit 0
