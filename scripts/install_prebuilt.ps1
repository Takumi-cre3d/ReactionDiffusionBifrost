param([string]$PackRoot='', [string]$MayaVersion='2026')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if (-not $PackRoot) { $PackRoot=Join-Path $root 'build/validated-module' }
$pack=Join-Path $PackRoot 'bifrost/ReactionDiffusion-0.2.0'
$definition=Join-Path $pack 'json/ReactionDiffusion/ReactionDiffusion.json'
if (-not (Test-Path $definition) -or -not (Test-Path (Join-Path $pack 'lib/ReactionDiffusionOps.dll'))) {
    throw 'Build and validate the native pack first.'
}
if (-not ([IO.File]::ReadAllText($definition)).Contains('reaction_diffusion_mesh_data')) {
    throw 'This pack predates Preview 8.'
}
if (Get-Process maya -ErrorAction SilentlyContinue) {
    throw 'Close Maya before installing. No files have been changed.'
}
$modules=Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'maya/modules'
$product=Join-Path $modules 'ReactionDiffusionBifrost'
$destination=Join-Path $product '0.2.0'
$backup=Join-Path $product ('0.2.0.backup_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
$moduleFile=Join-Path $modules 'ReactionDiffusionBifrost.mod'
New-Item -ItemType Directory -Force -Path $product | Out-Null
if (Test-Path $destination) { Copy-Item -LiteralPath $destination -Destination $backup -Recurse }
if (Test-Path $moduleFile) { Copy-Item -LiteralPath $moduleFile -Destination ($backup+'.mod') }
try {
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-Item -Path (Join-Path $root 'maya_module/ReactionDiffusionBifrost/0.2.0/*') -Destination $destination -Recurse -Force
    New-Item -ItemType Directory -Force -Path (Join-Path $destination 'bifrost') | Out-Null
    Copy-Item -LiteralPath $pack -Destination (Join-Path $destination 'bifrost') -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $root 'maya_module/ReactionDiffusionBifrost.mod') -Destination $moduleFile -Force
    & (Join-Path $PSScriptRoot 'verify_install.ps1') -MayaVersion $MayaVersion
    $installedDll=Join-Path $destination 'bifrost/ReactionDiffusion-0.2.0/lib/ReactionDiffusionOps.dll'
    if ((Get-FileHash $installedDll).Hash -ne (Get-FileHash (Join-Path $pack 'lib/ReactionDiffusionOps.dll')).Hash) {
        throw 'Installed DLL hash does not match the validated build.'
    }
} catch {
    if (Test-Path $backup) { Copy-Item -Path (Join-Path $backup '*') -Destination $destination -Recurse -Force }
    if (Test-Path ($backup+'.mod')) { Copy-Item -LiteralPath ($backup+'.mod') -Destination $moduleFile -Force }
    throw
}
Write-Host "Preview 8 installed; validated DLL hash matches. Backup: $backup"
