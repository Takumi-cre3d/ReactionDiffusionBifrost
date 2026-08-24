param(
    [string]$MayaVersion = "2026"
)

$ErrorActionPreference = "Stop"
$documents = [Environment]::GetFolderPath("MyDocuments")
$modulesRoot = Join-Path $documents "maya\modules"
$moduleFile = Join-Path $modulesRoot "ReactionDiffusionBifrost.mod"
$versionRoot = Join-Path $modulesRoot "ReactionDiffusionBifrost\0.2.0"
$packRoot = Join-Path $versionRoot "bifrost\ReactionDiffusion-0.2.0"
$packConfig = Join-Path $packRoot "ReactionDiffusionPackConfig.json"
$operatorDll = Join-Path $packRoot "lib\ReactionDiffusionOps.dll"
$operatorJson = Join-Path $packRoot "json\ReactionDiffusion\ReactionDiffusion.json"
$pythonPackage = Join-Path $versionRoot "scripts\reaction_diffusion_bifrost\__init__.py"
$previewTool = Join-Path $versionRoot "scripts\reaction_diffusion_bifrost\preview.py"
$bridgeTool = Join-Path $versionRoot "scripts\reaction_diffusion_bifrost\bridge.py"

$checks = @(
    @{ Name = "Maya module file"; Path = $moduleFile },
    @{ Name = "Python interaction package"; Path = $pythonPackage },
    @{ Name = "Viewport preview controller"; Path = $previewTool },
    @{ Name = "Painter-to-Bifrost bridge"; Path = $bridgeTool },
    @{ Name = "Bifrost pack config"; Path = $packConfig },
    @{ Name = "Bifrost operator DLL"; Path = $operatorDll },
    @{ Name = "Bifrost operator definition"; Path = $operatorJson }
)

$failed = $false
foreach ($check in $checks) {
    if (Test-Path $check.Path) {
        Write-Host "PASS: $($check.Name): $($check.Path)"
    } else {
        Write-Host "FAIL: $($check.Name): $($check.Path)"
        $failed = $true
    }
}
if ($failed) {
    throw "ReactionDiffusionBifrost installation is incomplete."
}
Write-Host "ReactionDiffusionBifrost 0.2.0 installation check passed for Maya $MayaVersion."
