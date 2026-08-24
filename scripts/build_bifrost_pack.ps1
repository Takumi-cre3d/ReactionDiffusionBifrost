param(
    [string]$MayaVersion = "2026",
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Release",
    [string]$InstallRoot = ""
)

$ErrorActionPreference = "Stop"
$packageRoot = Split-Path $PSScriptRoot -Parent

function Find-BifrostRoot {
    if ($env:BIFROST_LOCATION -and (Test-Path (Join-Path $env:BIFROST_LOCATION "sdk"))) {
        return $env:BIFROST_LOCATION
    }
    $mayaBifrostRoot = "C:\Program Files\Autodesk\Bifrost\Maya$MayaVersion"
    if (-not (Test-Path $mayaBifrostRoot)) {
        throw "Bifrost for Maya $MayaVersion was not found. Set BIFROST_LOCATION to the Bifrost folder containing sdk."
    }
    $candidates = Get-ChildItem -Path $mayaBifrostRoot -Directory | Where-Object {
        Test-Path (Join-Path $_.FullName "bifrost\sdk")
    } | Sort-Object Name -Descending
    if (-not $candidates) {
        throw "No Bifrost SDK was found below $mayaBifrostRoot."
    }
    return (Join-Path $candidates[0].FullName "bifrost")
}

function Find-VectorLengthExampleRoot([string]$bifrostRoot) {
    $source = Get-ChildItem -Path (Join-Path $bifrostRoot "sdk") -Recurse -File -Filter "VectorLength.cpp" |
        Where-Object { $_.FullName -match "VectorLength" } |
        Select-Object -First 1
    if (-not $source) {
        throw "The official SDK VectorLength example was not found. Reinstall Bifrost with its SDK examples."
    }
    $directory = $source.Directory
    while ($directory -and $directory.Name -ne "VectorLength") {
        $directory = $directory.Parent
    }
    if (-not $directory) {
        throw "Could not resolve the VectorLength example project root."
    }
    return $directory.FullName
}

if (-not $InstallRoot) {
    $documents = [Environment]::GetFolderPath("MyDocuments")
    $InstallRoot = Join-Path $documents "maya\modules\ReactionDiffusionBifrost\0.2.0"
}

$bifrostRoot = Find-BifrostRoot
$exampleRoot = Find-VectorLengthExampleRoot $bifrostRoot
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$localAppData = [Environment]::GetFolderPath("LocalApplicationData")
if (-not $localAppData) {
    $localAppData = $env:TEMP
}
$workingRoot = Join-Path $localAppData "ReactionDiffusionBifrost\Builds\0.2.0_$stamp"
$sourceRoot = Join-Path $workingRoot "ReactionDiffusion"
$buildRoot = Join-Path $workingRoot "build"
New-Item -ItemType Directory -Force -Path $workingRoot | Out-Null
Copy-Item -Recurse -Force -Path $exampleRoot -Destination $sourceRoot

$textExtensions = @(".txt", ".cmake", ".cpp", ".h", ".hpp", ".json", ".info", ".in", ".md")
Get-ChildItem -Path $sourceRoot -Recurse -File | Where-Object {
    $textExtensions -contains $_.Extension -or $_.Name -eq "CMakeLists.txt"
} | ForEach-Object {
    $content = [IO.File]::ReadAllText($_.FullName)
    $content = $content.Replace("VECTOR_LENGTH", "REACTION_DIFFUSION")
    $content = $content.Replace("VectorLength", "ReactionDiffusion")
    $content = $content.Replace("vector_length", "reaction_diffusion")
    $content = $content.Replace("1.0.0", "0.2.0")
    [IO.File]::WriteAllText($_.FullName, $content)
}

Get-ChildItem -Path $sourceRoot -Recurse | Sort-Object { $_.FullName.Length } -Descending | Where-Object {
    $_.Name -match "VectorLength"
} | ForEach-Object {
    Rename-Item -Path $_.FullName -NewName ($_.Name.Replace("VectorLength", "ReactionDiffusion"))
}

$operatorCpp = Get-ChildItem -Path $sourceRoot -Recurse -File -Filter "ReactionDiffusion.cpp" | Select-Object -First 1
$operatorHeader = Get-ChildItem -Path $sourceRoot -Recurse -File -Filter "ReactionDiffusion.h" | Select-Object -First 1
if (-not $operatorCpp -or -not $operatorHeader) {
    throw "The transformed SDK example does not contain the expected operator source files."
}
$operatorDir = $operatorCpp.Directory.FullName
Copy-Item -Force -Path (Join-Path $packageRoot "native\bifrost\ReactionDiffusion.cpp") -Destination $operatorCpp.FullName
Copy-Item -Force -Path (Join-Path $packageRoot "native\bifrost\ReactionDiffusion.h") -Destination $operatorHeader.FullName
Copy-Item -Force -Path (Join-Path $packageRoot "native\core\ReactionDiffusionCore.h") -Destination (Join-Path $operatorDir "ReactionDiffusionCore.h")
Copy-Item -Force -Path (Join-Path $packageRoot "native\core\ReactionDiffusionVolumeCore.h") -Destination (Join-Path $operatorDir "ReactionDiffusionVolumeCore.h")

$headerText = [IO.File]::ReadAllText($operatorHeader.FullName)
$compatibilityDefine = "_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH"
if (-not $headerText.Contains($compatibilityDefine)) {
    throw "The Bifrost operator header is missing the MSVC STL parser compatibility guard."
}
$explicitExport = "REACTION_DIFFUSION_NODE_EXPORT"
if (-not $headerText.Contains($explicitExport)) {
    throw "The Bifrost operator header is missing the explicit native export declaration."
}

# The SDK VectorLength example only operates on primitive values, so its
# generated target does not need the Amino runtime import libraries.  This
# operator creates Amino::Array values and assigns Amino::String values, which
# requires both runtime targets on Windows.  Resolve the transformed target
# from the copied example rather than assuming its name.
$operatorCMakePath = Join-Path $operatorDir "CMakeLists.txt"
if (-not (Test-Path $operatorCMakePath)) {
    throw "The transformed SDK example is missing src/CMakeLists.txt."
}
$operatorCMakeText = [IO.File]::ReadAllText($operatorCMakePath)
$targetMatch = [regex]::Match(
    $operatorCMakeText,
    "add_library\s*\(\s*([A-Za-z0-9_.+-]+)",
    [Text.RegularExpressions.RegexOptions]::IgnoreCase
)
if (-not $targetMatch.Success) {
    throw "Could not resolve the Bifrost operator library target from src/CMakeLists.txt."
}
$operatorTarget = $targetMatch.Groups[1].Value
$requiredAminoTargets = @("Amino::Cpp", "Amino::Core")
$missingAminoTargets = @($requiredAminoTargets | Where-Object {
    -not $operatorCMakeText.Contains($_)
})
if ($missingAminoTargets.Count -gt 0) {
    $runtimeLinkLine = "target_link_libraries($operatorTarget PUBLIC $($missingAminoTargets -join ' '))"
    $operatorCMakeText = $operatorCMakeText.TrimEnd() + "`r`n`r`n# Required by ReactionDiffusion Amino arrays and strings.`r`n$runtimeLinkLine`r`n"
    [IO.File]::WriteAllText($operatorCMakePath, $operatorCMakeText)
}
$verifiedOperatorCMakeText = [IO.File]::ReadAllText($operatorCMakePath)
foreach ($requiredAminoTarget in $requiredAminoTargets) {
    if (-not $verifiedOperatorCMakeText.Contains($requiredAminoTarget)) {
        throw "Failed to add required runtime target $requiredAminoTarget to src/CMakeLists.txt."
    }
}

$env:BIFROST_LOCATION = $bifrostRoot
Write-Host "Bifrost SDK parser compatibility: enabled for current Visual Studio STL"
Write-Host "Amino runtime links: Amino::Cpp, Amino::Core -> $operatorTarget"
Write-Host "Build workspace: $workingRoot"
cmake -S $sourceRoot -B $buildRoot
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE." }
cmake --build $buildRoot --config $Configuration --target install
if ($LASTEXITCODE -ne 0) { throw "Bifrost pack build failed with exit code $LASTEXITCODE." }

# A generated node-definition JSON can make a node searchable even when the
# corresponding DLL entry point is not exported. Inspect the PE export table
# before installing so that such a pack cannot report a false build success.
$operatorDll = Get-ChildItem -Path $buildRoot -Recurse -File -Filter "ReactionDiffusionOps.dll" |
    Where-Object { $_.FullName -match "[\\/]$Configuration[\\/]" } |
    Select-Object -First 1
if (-not $operatorDll) {
    $operatorDll = Get-ChildItem -Path $buildRoot -Recurse -File -Filter "ReactionDiffusionOps.dll" |
        Select-Object -First 1
}
if (-not $operatorDll) {
    throw "Build completed, but ReactionDiffusionOps.dll was not found."
}

$cmakeCache = Join-Path $buildRoot "CMakeCache.txt"
$dumpbinPath = ""
$linkerPath = ""
if (Test-Path $cmakeCache) {
    $cmakeCacheText = [IO.File]::ReadAllText($cmakeCache)
    $dumpbinMatch = [regex]::Match(
        $cmakeCacheText,
        "(?m)^CMAKE_DUMPBIN:FILEPATH=(.+)$"
    )
    if ($dumpbinMatch.Success) {
        $dumpbinPath = $dumpbinMatch.Groups[1].Value.Trim()
    }
    $linkerMatch = [regex]::Match(
        $cmakeCacheText,
        "(?m)^CMAKE_LINKER:FILEPATH=(.+)$"
    )
    if ($linkerMatch.Success) {
        $linkerPath = $linkerMatch.Groups[1].Value.Trim()
    }
}
if (-not $dumpbinPath -or -not (Test-Path $dumpbinPath)) {
    if ($linkerPath -and (Test-Path $linkerPath)) {
        $siblingDumpbin = Join-Path (Split-Path $linkerPath -Parent) "dumpbin.exe"
        if (Test-Path $siblingDumpbin) {
            $dumpbinPath = $siblingDumpbin
        }
    }
}
if (-not $dumpbinPath -or -not (Test-Path $dumpbinPath)) {
    $dumpbinCommand = Get-Command "dumpbin.exe" -ErrorAction SilentlyContinue
    if ($dumpbinCommand) {
        $dumpbinPath = $dumpbinCommand.Source
    }
}
if (-not $dumpbinPath -or -not (Test-Path $dumpbinPath)) {
    throw "dumpbin.exe could not be located next to the active MSVC linker; native operator exports cannot be verified."
}

function Invoke-ExportTableInspector {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ToolPath,
        [Parameter(Mandatory = $true)]
        [string]$DllPath
    )

    $quotedDllPath = '"' + $DllPath.Replace('"', '\"') + '"'
    $arguments = "/NOLOGO /EXPORTS $quotedDllPath"

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $ToolPath
    $startInfo.Arguments = $arguments
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Failed to start the Visual Studio export-table inspector: $ToolPath"
    }
    $standardOutput = $process.StandardOutput.ReadToEnd()
    $standardError = $process.StandardError.ReadToEnd()
    $process.WaitForExit()

    return [PSCustomObject]@{
        Tool = $ToolPath
        ExitCode = $process.ExitCode
        Output = $standardOutput + $standardError
    }
}

$successfulInspection = Invoke-ExportTableInspector `
    -ToolPath $dumpbinPath `
    -DllPath $operatorDll.FullName
if ($successfulInspection.ExitCode -ne 0) {
    Write-Host "Export inspector: $($successfulInspection.Tool)"
    Write-Host "Export inspector exit code: $($successfulInspection.ExitCode)"
    if ($successfulInspection.Output) {
        Write-Host $successfulInspection.Output
    }
    throw "The Visual Studio export-table inspection failed for ReactionDiffusionOps.dll."
}
$exportTable = $successfulInspection.Output
$requiredExports = @(
    "reaction_diffusion_initialize_grid",
    "reaction_diffusion_grid_step",
    "reaction_diffusion_initialize_volume",
    "reaction_diffusion_volume_step"
)
foreach ($requiredExport in $requiredExports) {
    if (-not $exportTable.Contains($requiredExport)) {
        throw "Required native operator export was not found: $requiredExport"
    }
}
Write-Host "Export inspector: $($successfulInspection.Tool)"
Write-Host "Native operator exports verified: initialize_grid, grid_step, initialize_volume, volume_step"

$packSource = Join-Path $buildRoot "ReactionDiffusion-0.2.0"
$packConfig = Join-Path $packSource "ReactionDiffusionPackConfig.json"
$packDll = Join-Path $packSource "lib\ReactionDiffusionOps.dll"
$packDefinition = Join-Path $packSource "json\ReactionDiffusion\ReactionDiffusion.json"
$requiredPackFiles = @(
    @{ Name = "pack config"; Path = $packConfig },
    @{ Name = "operator DLL"; Path = $packDll },
    @{ Name = "operator definition"; Path = $packDefinition }
)
foreach ($requiredPackFile in $requiredPackFiles) {
    if (-not (Test-Path $requiredPackFile.Path)) {
        throw "The installed build pack is missing $($requiredPackFile.Name): $($requiredPackFile.Path)"
    }
}

$packDestination = Join-Path $InstallRoot "bifrost\ReactionDiffusion-0.2.0"
New-Item -ItemType Directory -Force -Path (Split-Path $packDestination -Parent) | Out-Null
if (Test-Path $packDestination) {
    Move-Item -Path $packDestination -Destination "$packDestination.backup_$stamp"
}
Copy-Item -Recurse -Force -Path $packSource -Destination $packDestination

$installedDll = Join-Path $packDestination "lib\ReactionDiffusionOps.dll"
if (-not (Test-Path $installedDll)) {
    throw "The Bifrost operator DLL was not copied to the Maya module: $installedDll"
}
$sourceDllHash = (Get-FileHash -Algorithm SHA256 -Path $packDll).Hash
$installedDllHash = (Get-FileHash -Algorithm SHA256 -Path $installedDll).Hash
if ($sourceDllHash -ne $installedDllHash) {
    throw "The installed Bifrost operator DLL does not match the verified build output."
}

Write-Host "ReactionDiffusion Bifrost pack 0.2.0 built and installed."
Write-Host "Bifrost root: $bifrostRoot"
Write-Host "Installed operator DLL verified: $installedDll"
Write-Host "Pack config: $(Join-Path $packDestination 'ReactionDiffusionPackConfig.json')"
Write-Host "Restart Maya before testing the nodes."
