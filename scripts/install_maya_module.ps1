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

$versionBackup = ""
if (Test-Path $versionDestination) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $versionBackup = "$versionDestination.backup_$stamp"
    Move-Item -Path $versionDestination -Destination $versionBackup
}

Copy-Item -Recurse -Force -Path (Join-Path $sourceRoot "ReactionDiffusionBifrost\0.2.0") -Destination $moduleDestination
# A Python/UI-only reinstall must not make a previously complete module
# unusable. Preserve the compiled pack from the version backup; install_all
# will replace it with a freshly verified build in its next stage.
if ($versionBackup) {
    $existingBifrostPack = Join-Path $versionBackup "bifrost"
    if (Test-Path $existingBifrostPack) {
        Copy-Item -Recurse -Force -Path $existingBifrostPack -Destination $versionDestination
        Write-Host "Preserved existing Bifrost pack from: $versionBackup"
    }
}
Copy-Item -Force -Path (Join-Path $sourceRoot "ReactionDiffusionBifrost.mod") -Destination $modulesRoot

Write-Host "ReactionDiffusionBifrost 0.2.0 Maya module installed."
Write-Host "Module root: $versionDestination"
Write-Host "Maya target: $MayaVersion"
