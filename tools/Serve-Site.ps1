# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Timer Tom: serve docs\ on http://localhost so Web MIDI works locally,
# then open the page in Chrome or Edge. Stop with Ctrl+C.
[CmdletBinding()]
param(
    [int]$Port = 8766,
    [string]$Page = 'install/index.html'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\docs'))
if (!(Test-Path -LiteralPath $root)) { throw "Folder not found: $root" }

$types = @{
    '.html' = 'text/html; charset=utf-8'; '.htm' = 'text/html; charset=utf-8'
    '.js' = 'text/javascript; charset=utf-8'; '.css' = 'text/css; charset=utf-8'
    '.json' = 'application/json'; '.png' = 'image/png'; '.svg' = 'image/svg+xml'
    '.md' = 'text/plain; charset=utf-8'; '.txt' = 'text/plain; charset=utf-8'
    '.wasm' = 'application/wasm'; '.fwsc' = 'application/octet-stream'
}

$listener = New-Object System.Net.HttpListener
$prefix = "http://localhost:$Port/"
$listener.Prefixes.Add($prefix)
try { $listener.Start() } catch {
    throw "Cannot start the local server on port $Port. Close the other window that uses it, or run with -Port 8766."
}

$url = $prefix + $Page
Write-Host ''
Write-Host "Timer Tom local server: $prefix"
Write-Host "Opening $url"
Write-Host 'Keep this window open while installing. Press Ctrl+C to stop.'
Write-Host ''

# Prefer Chrome or Edge: they support Web MIDI with SysEx.
$browsers = @(
    "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
    "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"
)
$browser = $browsers | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if ($browser) { Start-Process -FilePath $browser -ArgumentList $url } else { Start-Process $url }

try {
    while ($listener.IsListening) {
        $context = $listener.GetContext()
        $response = $context.Response
        try {
            $path = [Uri]::UnescapeDataString($context.Request.Url.AbsolutePath).TrimStart('/')
            if ($path -eq '' -or $path.EndsWith('/')) { $path += 'index.html' }
            $file = [IO.Path]::GetFullPath((Join-Path $root $path))
            if (!$file.StartsWith($root, [StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $file -PathType Leaf)) {
                $response.StatusCode = 404
                $bytes = [Text.Encoding]::UTF8.GetBytes('Not found')
            } else {
                $extension = [IO.Path]::GetExtension($file).ToLowerInvariant()
                $response.ContentType = if ($types.ContainsKey($extension)) { $types[$extension] } else { 'application/octet-stream' }
                $response.Headers.Add('Cache-Control', 'no-store')
                $bytes = [IO.File]::ReadAllBytes($file)
            }
            $response.ContentLength64 = $bytes.Length
            $response.OutputStream.Write($bytes, 0, $bytes.Length)
        } catch {
            Write-Warning $_
        } finally {
            $response.OutputStream.Close()
        }
    }
} finally {
    $listener.Stop()
    $listener.Close()
}
