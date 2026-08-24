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
$graphSetupTool = Join-Path $versionRoot "scripts\reaction_diffusion_bifrost\graph_setup.py"

$checks = @(
    @{ Name = "Maya module file"; Path = $moduleFile },
    @{ Name = "Python interaction package"; Path = $pythonPackage },
    @{ Name = "Viewport preview controller"; Path = $previewTool },
    @{ Name = "Painter-to-Bifrost bridge"; Path = $bridgeTool },
    @{ Name = "Bifrost graph builders"; Path = $graphSetupTool },
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
$operatorJsonText = [IO.File]::ReadAllText($operatorJson)
foreach ($stateOperator in @(
    "reaction_diffusion_initialize_state",
    "reaction_diffusion_state_step",
    "reaction_diffusion_state_outputs"
)) {
    if (-not $operatorJsonText.Contains($stateOperator)) {
        throw "Installed Bifrost definition is missing Feedback State operator: $stateOperator"
    }
}
Write-Host "PASS: Feedback State operator definitions"
Write-Host "ReactionDiffusionBifrost 0.2.0 installation check passed for Maya $MayaVersion."
