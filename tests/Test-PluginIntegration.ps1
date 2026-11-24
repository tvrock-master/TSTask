#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('x64', 'x86')]
    [string]$Platform,

    [string]$TvRockOnTVTestSource,
    [string]$TvTestSource
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path $PSScriptRoot -Parent
if (!$TvRockOnTVTestSource) { $TvRockOnTVTestSource = Join-Path $root '../tvrockontvtest' }
if (!$TvTestSource) { $TvTestSource = Join-Path $root '../TVTest' }
$source = (Resolve-Path -LiteralPath $TvRockOnTVTestSource).ProviderPath
$sdk = Join-Path (Resolve-Path -LiteralPath $TvTestSource).ProviderPath 'sdk'
$output = if ($Platform -eq 'x64') { 'x64/Release' } else { 'Win32/Release' }
$app = Get-Item -LiteralPath (Join-Path $root "$output/TSTask.exe")
$tempRoot = (Get-Item -LiteralPath ([IO.Path]::GetTempPath())).FullName.TrimEnd('\')
$testRoot = Join-Path $tempRoot ('ts-test-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $testRoot | Out-Null

function Build-TestPlugins {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -products '*' -version '[17.0,18.0)' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$installation) { throw 'Visual Studio 2022 C++ Build Tools are required.' }
    $devcmd = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'

    $common = @('/nologo', '/LD', '/EHsc', '/std:c++17', '/utf-8', '/MT', '/DUNICODE', '/D_UNICODE', '/DWIN32')
    $compilerArguments = @{
        BonDriver = $common + @(('"' + (Join-Path $PSScriptRoot 'MockBonDriver.cpp') + '"'),
            '/link', '/EXPORT:CreateBonDriver', '/OUT:BonDriver_CI.dll')
        TvRockDTV = $common + @(('/I"' + $source + '"'), ('"' + (Join-Path $PSScriptRoot 'MockTvRockDTV.cpp') + '"'),
            '/link', '/EXPORT:CreateTvRockDTV', '/OUT:TvRockDTV.dll')
        DiskRelay = $common + @(('/I"' + $sdk + '"'), ('/I"' + (Join-Path $sdk 'Samples/DiskRelay') + '"'),
            ('"' + (Join-Path $PSScriptRoot 'DiskRelayTest.cpp') + '"'),
            '/link', ('/DEF:"' + (Join-Path $source 'Exports.def') + '"'), '/OUT:DiskRelay.tvtp', 'user32.lib', 'ole32.lib')
        TvRockOnTVTest = $common + @('/D_WINDOWS', '/D_USRDLL', ('"' + (Join-Path $source 'TvRockOnTVTest.cpp') + '"'),
            '/link', ('/DEF:"' + (Join-Path $source 'Exports.def') + '"'), '/OUT:TvRockOnTVTest.tvtp',
            'user32.lib', 'gdi32.lib', 'shell32.lib', 'shlwapi.lib')
    }
    $commands = @('@echo off', ('call "{0}" -no_logo -arch={1} -host_arch=x64' -f $devcmd, $Platform),
        'if errorlevel 1 exit /b %errorlevel%')
    foreach ($name in $compilerArguments.Keys) {
        [IO.File]::WriteAllText((Join-Path $testRoot "$name.rsp"), ($compilerArguments[$name] -join ' '), [Text.Encoding]::Unicode)
        $commands += @("cl @$name.rsp", 'if errorlevel 1 exit /b %errorlevel%')
    }
    [IO.File]::WriteAllLines((Join-Path $testRoot 'compile.cmd'), $commands, [Text.Encoding]::Default)
    Push-Location -LiteralPath $testRoot
    try {
        & $env:ComSpec /d /c compile.cmd
        if ($LASTEXITCODE -ne 0) { throw 'Could not compile plugin integration fixtures.' }
    } finally {
        Pop-Location
    }
}

function Test-Recording([string]$Path, [int]$Service) {
    $data = [IO.File]::ReadAllBytes($Path)
    if ($data.Length -eq 0 -or $data.Length % 188 -ne 0) {
        throw "Recording is empty or is not TS-packet-aligned ($Path)."
    }
    $foundVideo = $false
    for ($offset = 0; $offset -lt $data.Length; $offset += 188) {
        if ($data[$offset] -ne 0x47) { throw "Invalid TS sync byte in $Path at $offset." }
        $packetId = (($data[$offset + 1] -band 0x1f) -shl 8) -bor $data[$offset + 2]
        if ($packetId -eq 0x101) {
            if ($data[$offset + 4] -ne $Service) { throw "Unexpected service data in $Path." }
            $foundVideo = $true
        }
    }
    if (!$foundVideo) { throw "No test service data in $Path." }
}

function Invoke-PluginCase([string]$Case) {
    $directory = Join-Path $testRoot $Case
    $plugins = Join-Path $directory 'Plugins'
    New-Item -ItemType Directory -Path $plugins, (Join-Path $directory 'backup') -Force | Out-Null
    Copy-Item -LiteralPath $app.FullName, (Join-Path $testRoot 'BonDriver_CI.dll') -Destination $directory
    if ($Case -ne 'missing') {
        Copy-Item -LiteralPath (Join-Path $testRoot 'TvRockOnTVTest.tvtp'), (Join-Path $testRoot 'TvRockDTV.dll') -Destination $plugins
        Set-Content -LiteralPath (Join-Path $plugins 'TvRockOnTVTest.ini') -Encoding Unicode -Value @(
            '[DID_A]', 'TerrestrialIndex=0')
    }
    $ini = @('[Settings]', "Recording.Directory=$directory", 'Recording.WriteBufferSize=18800',
        '[Plugins]', 'Plugin1=TvRockOnTVTest.tvtp')
    if ($Case -eq 'integration') {
        Copy-Item -LiteralPath (Join-Path $testRoot 'DiskRelay.tvtp') -Destination $plugins
        Set-Content -LiteralPath (Join-Path $plugins 'DiskRelay.ini') -Encoding Unicode -Value @(
            '[Settings]', "Folder0=$(Join-Path $directory 'backup')", 'LowFreeSpace=64')
        $ini += 'Plugin2=DiskRelay.tvtp'
    }
    Set-Content -LiteralPath (Join-Path $directory 'TSTask.ini') -Encoding Unicode -Value $ini
    Set-Content -LiteralPath (Join-Path $directory 'BonDriver_CI.ch2') -Encoding Ascii -Value @(
        'First,0,0,1,1,41,30848,16400,1', 'Second,0,1,2,1,42,30848,16401,1')

    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $directory 'TSTask.exe'
    $start.WorkingDirectory = $directory
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.Arguments = '/d BonDriver_CI.dll /did A /ch 0 /sid 41 /xclient false /log true /loglevel 5'
    if ($Case -ne 'integration') {
        $start.Arguments += ' /rec true /recfile recovery.ts /recduration 2 /recexit true'
    }
    if ($Case -eq 'init-failure') { $start.EnvironmentVariables['TSTASK_TEST_FAIL_INIT'] = '1' }
    $process = [Diagnostics.Process]::Start($start)
    $log = Join-Path $directory 'TSTask.log'
    try {
        if (!$process.WaitForExit(45000)) { throw "$Case timed out." }
        if ($process.ExitCode -ne 0) { throw "$Case exited with code $($process.ExitCode)." }
        $logText = Get-Content -LiteralPath $log -Raw -Encoding Unicode
        if ($Case -eq 'integration') {
            $result = Get-Content -LiteralPath (Join-Path $directory 'tvrock-result.txt') -Raw
            if ($result -ne 'OK') { throw $result }
            if ((Get-Content -LiteralPath (Join-Path $directory 'tvrock-disposed.txt') -Raw) -ne 'OK' -or
                !$logText.Contains('TvRockOnTVTest.tvtp is handling TvRockDTV commands.') -or
                !$logText.Contains('DiskRelay.tvtp is monitoring recording free space.')) {
                throw 'Missing confirmation of plugin load, relay or shutdown.'
            }
            Test-Recording (Join-Path $directory 'primary.ts') 42
            Test-Recording (Join-Path $directory 'backup/primary.part2.ts') 42
        } else {
            $expected = if ($Case -eq 'missing') { 'Could not load the control plugin' } else { 'TvRockOnTVTest.tvtp could not initialize.' }
            if (!$logText.Contains($expected)) { throw "Expected the plugin error in the log ($Case)." }
            if ($Case -eq 'init-failure' -and
                (Get-Content -LiteralPath (Join-Path $directory 'tvrock-init-refused.txt') -Raw) -ne 'OK') {
                throw 'The mock TvRockDTV did not reject initialization.'
            }
            Test-Recording (Join-Path $directory 'recovery.ts') 41
        }
        Write-Host "Verified plugin integration case: $Case ($Platform)."
    } catch {
        if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Encoding Unicode -Tail 100 }
        throw
    } finally {
        if (!$process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
        $process.Dispose()
    }
}

try {
    Build-TestPlugins
    foreach ($case in 'missing', 'init-failure', 'integration') {
        Invoke-PluginCase $case
    }
} finally {
    $directory = Get-Item -LiteralPath $testRoot
    if ($directory.Parent.FullName.TrimEnd('\') -eq $tempRoot) {
        Remove-Item -LiteralPath $directory.FullName -Recurse -Force
    }
}
