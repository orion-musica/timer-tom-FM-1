# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Timer Tom: build the distribution package and refresh the site in docs/.
#   docs/install/flasher.html        official FM1 Open web installer with this firmware inside
#   (docs/install/index.html is the Timer Tom installer page that checks the build and opens flasher.html)
#   docs/downloads/timer-tom.fwsc   the firmware package
#   docs/downloads/timer-tom_README.md    the README
# Usage (PowerShell, from the repository root):
#   $env:FM1_PI32_TOOLCHAIN = 'C:\JL\pi32'
#   $env:FM1_SOURCE_URL = 'https://github.com/<user>/<repo>/archive/refs/heads/main.zip'
#   .\tools\Make-Site.ps1
[CmdletBinding()]
param(
    [string]$Toolchain = $env:FM1_PI32_TOOLCHAIN,
    [string]$SourceUrl = $env:FM1_SOURCE_URL
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($Toolchain)) {
    throw 'Set FM1_PI32_TOOLCHAIN to the JieLi pi32 folder (for example C:\JL\pi32).'
}
if ([string]::IsNullOrWhiteSpace($SourceUrl) -or -not $SourceUrl.StartsWith('https://')) {
    throw 'Set FM1_SOURCE_URL to the HTTPS address of this repository''s source archive, for example https://github.com/<user>/<repo>/archive/refs/heads/main.zip'
}

Write-Host 'Timer Tom: building the distribution package...'
& (Join-Path $root 'sdk\Build.ps1') -Package -Toolchain $Toolchain -SourceUrl $SourceUrl

$installer = Join-Path $root 'build\official-web\FM1-Official-Open-Source.html'
$firmware = Join-Path $root 'build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc'
foreach ($file in @($installer, $firmware)) {
    if (!(Test-Path -LiteralPath $file)) { throw "Build output missing: $file" }
}
$text = [IO.File]::ReadAllText($installer)
if ($text.Contains('Build incomplete')) { throw 'The installer page reports an incomplete build. Fix the build errors first.' }

$docs = Join-Path $root 'docs'
foreach ($dir in @('install', 'downloads')) { [IO.Directory]::CreateDirectory((Join-Path $docs $dir)) | Out-Null }
Copy-Item -LiteralPath $installer -Destination (Join-Path $docs 'install\flasher.html') -Force
Copy-Item -LiteralPath $firmware -Destination (Join-Path $docs 'downloads\timer-tom.fwsc') -Force
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination (Join-Path $docs 'downloads\timer-tom_README.md') -Force
$nojekyll = Join-Path $docs '.nojekyll'
if (!(Test-Path -LiteralPath $nojekyll)) { [IO.File]::WriteAllText($nojekyll, '') }

$hash = (Get-FileHash -LiteralPath (Join-Path $docs 'downloads\timer-tom.fwsc') -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $docs 'downloads\SHA256SUMS.txt'), "$hash  timer-tom.fwsc`n")
# The landing page reads this to show that the installer is ready (works from file:// too).
$bytes = (Get-Item -LiteralPath (Join-Path $docs 'downloads\timer-tom.fwsc')).Length
$stamp = (Get-Date).ToString('yyyy-MM-dd HH:mm')
$info = "window.DP_BUILD = {built: true, sha256: '$hash', bytes: $bytes, date: '$stamp'};`n"
[IO.File]::WriteAllText((Join-Path $docs 'assets\build-info.js'), $info)
Write-Host "Site ready in docs\  (firmware SHA-256 $hash)"
Write-Host 'Commit docs\ and push; GitHub Pages serves it from the /docs folder.'
