param(
    [string]$MayaVersion = "2026"
)

$ErrorActionPreference = "Stop"
$packageRoot = Split-Path $PSScriptRoot -Parent
$sourceRoot = Join-Path $packageRoot "maya_module"
$documents = [Environment]::GetFolderPath("MyDocuments")
$modulesRoot = Join-Path $documents "maya\modules"
$moduleDestination = Join-Path $modulesRoot "ReactionDiffusionBifrost"
$versionDestination = Join-Path $moduleDestination "0.2.0"

New-Item -ItemType Directory -Force -Path $modulesRoot | Out-Null
New-Item -ItemType Directory -Force -Path $moduleDestination | Out-Null

if (Test-Path $versionDestination) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    Move-Item -Path $versionDestination -Destination "$versionDestination.backup_$stamp"
}

Copy-Item -Recurse -Force -Path (Join-Path $sourceRoot "ReactionDiffusionBifrost\0.2.0") -Destination $moduleDestination
Copy-Item -Force -Path (Join-Path $sourceRoot "ReactionDiffusionBifrost.mod") -Destination $modulesRoot

Write-Host "ReactionDiffusionBifrost 0.2.0 Maya module installed."
Write-Host "Module root: $versionDestination"
Write-Host "Maya target: $MayaVersion"
