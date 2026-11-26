# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Timer Tom: one-click local install.
# 1. finds the JieLi toolchain   2. builds the firmware and the installer page
# 3. starts a local server and opens the installer in Chrome or Edge.
# Run from INSTALL-TIMER-TOM.bat in the repository root.
[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [int]$Port = 8766
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location -LiteralPath $root

function Say($text, $color = 'Gray') { Write-Host $text -ForegroundColor $color }
function Stop-WithMessage($text) {
    Say ''
    Say $text 'Yellow'
    Say ''
    Read-Host 'Press Enter to close'
    exit 1
}

Say ''
Say '  Timer Tom - local install' 'Green'
Say '  --------------------' 'DarkGray'
Say ''

$installer = Join-Path $root 'docs\install\index.html'
$built = (Test-Path -LiteralPath $installer) -and -not ([IO.File]::ReadAllText($installer).Contains('has not been built yet'))

if ($SkipBuild -and -not $built) {
    Stop-WithMessage 'The installer is not built yet. Run again without -SkipBuild.'
}

if (-not $SkipBuild) {
    # 1. Toolchain
    $toolchain = $env:FM1_PI32_TOOLCHAIN
    if ([string]::IsNullOrWhiteSpace($toolchain)) {
        foreach ($candidate in @('C:\JL\pi32', 'D:\JL\pi32', "$env:ProgramFiles\JL\pi32")) {
            if (Test-Path -LiteralPath (Join-Path $candidate 'bin\clang.exe')) { $toolchain = $candidate; break }
        }
    }
    if ([string]::IsNullOrWhiteSpace($toolchain) -or -not (Test-Path -LiteralPath (Join-Path $toolchain 'bin\clang.exe'))) {
        Say 'The JieLi compiler (toolchain 2.5.3) is missing.' 'Yellow'
        Say 'Opening the download page. Install it (default C:\JL), then run this file again.'
        Start-Process 'https://jl-update.oss-cn-shenzhen.aliyuncs.com/2.5.3.exe'
        Start-Process 'https://doc.zh-jieli.com/Tools/zh-cn/dev_tools/dev_env/jl_toolchain.html'
        Stop-WithMessage 'After installing: if the compiler is not in C:\JL\pi32, set the FM1_PI32_TOOLCHAIN variable.'
    }
    Say "Compiler: $toolchain" 'Green'

    # 2. Source address (GPL: the installer page links the matching source)
    $source = $env:FM1_SOURCE_URL
    if ([string]::IsNullOrWhiteSpace($source)) {
        $remote = ''
        try { $remote = (& git -C $root config --get remote.origin.url 2>$null) } catch { }
        if ($remote -match 'github\.com[:/](.+?)(\.git)?$') {
            $source = "https://github.com/$($Matches[1])/archive/refs/heads/main.zip"
        }
    }
    if ([string]::IsNullOrWhiteSpace($source)) {
        Say ''
        Say 'Enter the GitHub repository address (for example https://github.com/orion-musica/timer-tom-FM-1).'
        Say 'No repository yet? Press Enter to use a placeholder address for a local test.'
        $answer = Read-Host 'Address'
        if ($answer -match '^https://github\.com/[^/\s]+/[^/\s]+') {
            $source = ($answer.TrimEnd('/')) + '/archive/refs/heads/main.zip'
        } else {
            $source = 'https://github.com/orion-musica/timer-tom-FM-1/archive/refs/heads/main.zip'
            Say 'Placeholder address: build again with the real address before you publish.' 'Yellow'
        }
    }
    Say "Source: $source" 'Green'

    # 3. Build firmware + installer page (first run downloads the WASI SDK)
    Say ''
    Say 'Building the firmware and the installer. The first run can take a few minutes...' 'Cyan'
    try {
        & (Join-Path $PSScriptRoot 'Make-Site.ps1') -Toolchain $toolchain -SourceUrl $source
    } catch {
        Say ''
        Say "BUILD ERROR: $($_.Exception.Message)" 'Green'
        Stop-WithMessage 'Copy all text from this window into a GitHub issue so the error can be fixed.'
    }
}

# 4. Serve and open the installer
Say ''
Say 'Connect the FM-1 by USB and switch it on. Make a backup first if you do not have one.' 'Yellow'
Say 'Allow MIDI device access when the browser asks.' 'Yellow'
& (Join-Path $PSScriptRoot 'Serve-Site.ps1') -Port $Port -Page 'install/index.html'
