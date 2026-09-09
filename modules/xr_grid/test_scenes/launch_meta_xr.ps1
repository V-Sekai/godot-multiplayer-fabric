# Launches the engine fork's Meta XR Simulator smoke scene with
# XR_RUNTIME_JSON pointed at the Meta OpenXR Simulator manifest so the
# OpenXR loader picks up the simulator runtime instead of whatever
# system runtime is otherwise active.
#
# Usage:
#   ./launch_meta_xr.ps1
#   ./launch_meta_xr.ps1 -SimulatorRoot "C:/Program Files/MetaXRSimulator/v201.0"
#   ./launch_meta_xr.ps1 -GodotBin "..\..\..\..\bin\godot.windows.editor.x86_64.exe"
#
# Boot prints the resolved OpenXR runtime + the registered XRGrid
# classes to stdout, then auto-quits after 5 seconds.

param(
    [string]$SimulatorRoot = "C:\Program Files\MetaXRSimulator\v201.0",
    [string]$GodotBin = "",
    [string]$LogFile = ""
)

$ErrorActionPreference = "Stop"

$ManifestPath = Join-Path $SimulatorRoot "meta_openxr_simulator.json"
if (-not (Test-Path $ManifestPath)) {
    throw "Meta XR Simulator manifest not found at $ManifestPath. Install Meta XR Simulator from developers.meta.com or pass -SimulatorRoot."
}

# Default to a sibling engine binary (relative to this script's project dir).
# $PSScriptRoot = modules/xr_grid/test_scenes → go up 3 to repo root.
if (-not $GodotBin) {
    $RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
    $Candidates = @(
        "bin\godot.windows.editor.double.x86_64.console.exe",
        "bin\godot.windows.editor.x86_64.console.exe",
        "bin\godot.windows.editor.double.x86_64.exe",
        "bin\godot.windows.editor.x86_64.exe"
    )
    foreach ($c in $Candidates) {
        $full = Join-Path $RepoRoot $c
        if (Test-Path $full) {
            $GodotBin = $full
            break
        }
    }
}
if (-not $GodotBin -or -not (Test-Path $GodotBin)) {
    throw "Could not locate a Godot binary. Pass -GodotBin <path>."
}

$ProjectPath = Join-Path $PSScriptRoot "meta_xr_smoke"
if (-not (Test-Path (Join-Path $ProjectPath "project.godot"))) {
    throw "Smoke project not found at $ProjectPath/project.godot."
}

Write-Host "[launch] OpenXR runtime: $ManifestPath"
Write-Host "[launch] Godot binary:   $GodotBin"
Write-Host "[launch] Smoke project:  $ProjectPath"
if ($LogFile) {
    Write-Host "[launch] Log file:       $LogFile"
}
Write-Host ""

$env:XR_RUNTIME_JSON = $ManifestPath
$args = @("--path", $ProjectPath)
if ($LogFile) {
    $args += @("--log-file", $LogFile)
}
& $GodotBin @args
