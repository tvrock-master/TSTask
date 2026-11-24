#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('x64', 'x86')]
    [string]$Platform,

    [string]$ExpectedVersion
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$outputDirectory = if ($Platform -eq 'x86') { 'Win32/Release' } else { 'x64/Release' }
$expectedMachine = if ($Platform -eq 'x86') { 0x014c } else { 0x8664 }

if (!$ExpectedVersion) {
    $defaultVersion = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Common/AppVersion.h') |
        Select-String '^#define APP_VERSION_(?:MAJOR|MINOR|REVISION) ([0-9]+)$'
    $ExpectedVersion = ($defaultVersion | ForEach-Object { $_.Matches[0].Groups[1].Value }) -join '.'
}

foreach ($name in 'TSTask.exe', 'TSTaskCentre.exe', 'BonDriver_TSTask.dll') {
    $file = Get-Item -LiteralPath (Join-Path $PSScriptRoot "$outputDirectory/$name")
    $reader = [IO.BinaryReader]::new($file.OpenRead())
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Invalid DOS header in $name." }
        $reader.BaseStream.Position = 0x3c
        $peHeader = $reader.ReadInt32()
        if ($peHeader -lt 0x40 -or $peHeader -gt ($reader.BaseStream.Length - 24)) {
            throw "Invalid PE header position in $name."
        }
        $reader.BaseStream.Position = $peHeader
        if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne $expectedMachine) {
            throw "Expected a $Platform PE image in $name."
        }
        $reader.BaseStream.Position = $peHeader + 22
        $isDll = ($reader.ReadUInt16() -band 0x2000) -ne 0
        if ($isDll -ne ($file.Extension -eq '.dll')) { throw "Unexpected PE image type in $name." }
    } finally {
        $reader.Dispose()
    }

    if (!$isDll) {
        $info = $file.VersionInfo
        $fixedVersion = "$($info.FileMajorPart).$($info.FileMinorPart).$($info.FileBuildPart).$($info.FilePrivatePart)"
        $fixedProductVersion = "$($info.ProductMajorPart).$($info.ProductMinorPart).$($info.ProductBuildPart).$($info.ProductPrivatePart)"
        if ($info.FileVersion -ne $ExpectedVersion -or $info.ProductVersion -ne $ExpectedVersion -or
            $fixedVersion -ne "$ExpectedVersion.0" -or $fixedProductVersion -ne "$ExpectedVersion.0") {
            throw "Expected version $ExpectedVersion in $name. Found file version $($info.FileVersion) ($fixedVersion) and product version $($info.ProductVersion) ($fixedProductVersion)."
        }
    }

    Write-Host "Verified $name ($Platform)."
}
