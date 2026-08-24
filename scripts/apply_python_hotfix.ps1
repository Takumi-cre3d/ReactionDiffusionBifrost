param(
    [string]$MayaVersion = "2026"
)

$ErrorActionPreference = "Stop"
$packageRoot = Split-Path $PSScriptRoot -Parent
$sourcePackage = Join-Path $packageRoot "maya_module\ReactionDiffusionBifrost\0.2.0\scripts\reaction_diffusion_bifrost"
$documents = [Environment]::GetFolderPath("MyDocuments")
$moduleRoot = Join-Path $documents "maya\modules\ReactionDiffusionBifrost"
$versionRoot = Join-Path $moduleRoot "0.2.0"
$destinationPackage = Join-Path $versionRoot "scripts\reaction_diffusion_bifrost"
$packConfig = Join-Path $versionRoot "bifrost\ReactionDiffusion-0.2.0\ReactionDiffusionPackConfig.json"

if (-not (Test-Path $sourcePackage)) {
    throw "The hotfix source Python package was not found: $sourcePackage"
}
if (-not (Test-Path $destinationPackage)) {
    throw "ReactionDiffusionBifrost 0.2.0 is not installed: $destinationPackage"
}
if (-not (Test-Path $packConfig)) {
    throw "The existing native Bifrost pack is missing. Run install_all.ps1 instead."
}

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$backupDestination = Join-Path $moduleRoot "0.2.0_python_backup_$stamp"
Copy-Item -Recurse -Force -Path $destinationPackage -Destination $backupDestination
Copy-Item -Recurse -Force -Path (Join-Path $sourcePackage "*") -Destination $destinationPackage

$installedUi = Join-Path $destinationPackage "ui.py"
$installedUiText = [IO.File]::ReadAllText($installedUi)
$expectedUiFix = 'cmds.textFieldGrp(CONTROLS["uv_set"], query=True, text=True)'
if (-not $installedUiText.Contains($expectedUiFix)) {
    throw "The UV Set textFieldGrp fix was not found after installation."
}

$installedPaintContext = Join-Path $destinationPackage "paint_context.py"
$installedPaintContextText = [IO.File]::ReadAllText($installedPaintContext)
$expectedPointFix = 'hit_point = om.MPoint(hit[0])'
if (-not $installedPaintContextText.Contains($expectedPointFix)) {
    throw "The MFloatPoint to MPoint conversion fix was not found after installation."
}
foreach ($requiredTool in @("preview.py", "bridge.py")) {
    if (-not (Test-Path (Join-Path $destinationPackage $requiredTool))) {
        throw "The 0.2.0 Python tool was not installed: $requiredTool"
    }
}
$installedPreview = Join-Path $destinationPackage "preview.py"
$installedPreviewText = [IO.File]::ReadAllText($installedPreview)
if ($installedPreviewText.Contains(".createColorSetWithName(")) {
    throw "The unsupported Maya 2026 createColorSetWithName call is still installed."
}
if (-not $installedPreviewText.Contains("cmds.polyColorSet(")) {
    throw "The Maya 2026 polyColorSet preview fix was not found after installation."
}

& (Join-Path $PSScriptRoot "verify_install.ps1") -MayaVersion $MayaVersion
Write-Host "ReactionDiffusionBifrost 0.2.0 Preview 3 Python update installed successfully."
Write-Host "Python backup: $backupDestination"
Write-Host "The existing native Bifrost pack was preserved."
