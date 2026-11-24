#requires -Version 5.1
<#
.SYNOPSIS
Build TSTask and its companion projects with Visual Studio 2022 MSBuild.
.EXAMPLE
.\build.ps1
.EXAMPLE
.\build.ps1 -Platform x86 -Configuration Debug -Rebuild
.EXAMPLE
.\build.ps1 -Platform Both
.EXAMPLE
.\build.ps1 -Platform Both -VersionTag v0.2.1
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64', 'x86', 'Win32', 'Both')]
    [string]$Platform = 'x64',

    [switch]$Rebuild,

    [string]$MSBuildPath,
    [string]$VersionTag
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$versionArguments = @()
if ($PSBoundParameters.ContainsKey('VersionTag')) {
    if ($VersionTag -cnotmatch '\Av(0|[1-9][0-9]{0,2})\.(0|[1-9][0-9]{0,3})\.(0|[1-9][0-9]{0,3})\z' -or
        [int]$Matches[1] -gt 255 -or [int]$Matches[2] -gt 4095 -or [int]$Matches[3] -gt 4095) {
        throw 'Use a version tag vMAJOR.MINOR.PATCH (maximum 255.4095.4095, no leading zeros).'
    }
    $versionArguments = @(
        "/p:TSTaskVersionMajor=$($Matches[1])"
        "/p:TSTaskVersionMinor=$($Matches[2])"
        "/p:TSTaskVersionRevision=$($Matches[3])"
    )
}

if (-not $MSBuildPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $candidates = @(& $vswhere -latest -products '*' -version '[17.0,18.0)' `
            -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -find 'MSBuild\Current\Bin\MSBuild.exe')
        if ($LASTEXITCODE -ne 0) {
            throw "vswhere failed (exit code $LASTEXITCODE)."
        }
        if ($candidates.Count -gt 0) {
            $MSBuildPath = $candidates[0]
        }
    }
    if (-not $MSBuildPath) {
        $command = Get-Command MSBuild.exe -CommandType Application -ErrorAction SilentlyContinue
        if ($command) {
            $MSBuildPath = $command.Source
        }
    }
}

if (-not $MSBuildPath -or -not (Test-Path -LiteralPath $MSBuildPath -PathType Leaf)) {
    throw 'MSBuild was not found. Install Visual Studio 2022 or Build Tools with Desktop development with C++ (v143 and Windows SDK), or specify -MSBuildPath.'
}

$MSBuildPath = (Resolve-Path -LiteralPath $MSBuildPath).ProviderPath
$solution = Join-Path $PSScriptRoot 'TSTask.sln'
$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
$solutionPlatforms = switch ($Platform) {
    'Both' { 'x64'; 'Win32' }
    'x64'  { 'x64' }
    default { 'Win32' }
}

Write-Host "MSBuild: $MSBuildPath"
Push-Location -LiteralPath $PSScriptRoot
try {
    foreach ($solutionPlatform in $solutionPlatforms) {
        $buildArguments = @(
            $solution
            '/nologo'
            '/verbosity:minimal'
            "/t:$target"
            "/p:Configuration=$Configuration"
            "/p:Platform=$solutionPlatform"
            '/p:PlatformToolset=v143'
        ) + $versionArguments

        Write-Host "Building TSTask: $Configuration / $solutionPlatform ($target)"
        & $MSBuildPath @buildArguments
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed for $Configuration / $solutionPlatform (MSBuild exit code $LASTEXITCODE)."
        }

        $outputDirectory = Join-Path $PSScriptRoot "$solutionPlatform\$Configuration"
        Write-Host "Build succeeded. Output: $outputDirectory"
    }
} finally {
    Pop-Location
}
