param(
    [string]$MayaVersion = "2026",
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

# These are PowerShell child scripts, not native processes.  $LASTEXITCODE is
# intentionally not inspected here because it can retain a non-zero exit code
# from an earlier CMake invocation in the same PowerShell session.  With
# ErrorActionPreference=Stop, any real child-script failure propagates as an
# exception automatically.
Write-Host "Stage 1/3: Installing Maya module..."
& (Join-Path $PSScriptRoot "install_maya_module.ps1") -MayaVersion $MayaVersion

$documents = [Environment]::GetFolderPath("MyDocuments")
$installRoot = Join-Path $documents "maya\modules\ReactionDiffusionBifrost\0.2.0"
Write-Host "Stage 2/3: Building and installing Bifrost pack..."
& (Join-Path $PSScriptRoot "build_bifrost_pack.ps1") -MayaVersion $MayaVersion -Configuration $Configuration -InstallRoot $installRoot

Write-Host "Stage 3/3: Verifying installation..."
& (Join-Path $PSScriptRoot "verify_install.ps1") -MayaVersion $MayaVersion
Write-Host "ReactionDiffusionBifrost 0.2.0 installation completed successfully."
