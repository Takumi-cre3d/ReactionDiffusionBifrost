param([ValidateSet('grid','surface','volume')][string]$Domain='grid',
      [string]$PackConfig='', [string]$PackageRoot='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$env:MAYA_APP_DIR=Join-Path $root 'build/maya-test-profile'
$env:RD_TEST_DOMAIN=$Domain
$env:RD_TEST_PACK=Join-Path $root 'build/validated-module/bifrost/ReactionDiffusion-0.2.0/ReactionDiffusionPackConfig.json'
if ($PackConfig) { $env:RD_TEST_PACK=$PackConfig }
if ($PackageRoot) { $env:RD_TEST_PACKAGE_ROOT=$PackageRoot }
$env:MAYA_SKIP_USERSETUP_PY='1'
$test=(Join-Path $root 'tests/test_maya_interactive.py').Replace('\','/')
$arguments='-command "python(\"import runpy; runpy.run_path('''+$test+''')\")"'
$started=Get-Date
$process=Start-Process 'C:/Program Files/Autodesk/Maya2026/bin/maya.exe' -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(90000)) {
    # This process was launched only for this disposable test, never a user's Maya.
    $process.Kill()
    throw "Interactive $Domain test timed out; PID $($process.Id)"
}
$path=Join-Path $root "build/interactive-$Domain.json"
if (-not (Test-Path $path) -or (Get-Item $path).LastWriteTime -lt $started) {
    throw "Maya exited without a fresh report: $path"
}
$result=Get-Content $path -Raw | ConvertFrom-Json
if (-not $result.passed) { Write-Host $result.error $result.setup_error $result.warnings; throw "Interactive $Domain test failed (full report: $path)" }
Write-Host "$Domain playback PASS; CPU error=$($result.cpu_parity_error); reset error=$($result.reset_error)"
