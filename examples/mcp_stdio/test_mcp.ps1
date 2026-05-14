param(
    [string]$ExecutablePath
)

function Get-RepoRoot {
    $scriptDir = $PSScriptRoot
    if (-not $scriptDir) {
        $scriptDir = Split-Path -Parent $PSCommandPath
    }
    return [System.IO.Path]::GetFullPath((Join-Path $scriptDir "..\.."))
}

function Resolve-DemoExecutable([string]$repoRoot, [string]$overridePath) {
    if ($overridePath) {
        return [System.IO.Path]::GetFullPath($overridePath)
    }

    $candidates = @(
        (Join-Path $repoRoot "build\mcp_stdio_server_demo.exe"),
        (Join-Path $repoRoot "build\Debug\mcp_stdio_server_demo.exe"),
        (Join-Path $repoRoot "build\Release\mcp_stdio_server_demo.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    throw "Could not find mcp_stdio_server_demo.exe. Build it first with: cmake --build build --target mcp_stdio_server_demo"
}

function New-McpFrame([string]$jsonText) {
    $length = [System.Text.Encoding]::UTF8.GetByteCount($jsonText)
    return "Content-Length: $length`r`n`r`n$jsonText"
}

$repoRoot = Get-RepoRoot
$demoExecutable = Resolve-DemoExecutable -repoRoot $repoRoot -overridePath $ExecutablePath

$initialize = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test-client","version":"1.0.0"}}}'
$initialized = '{"jsonrpc":"2.0","method":"notifications/initialized"}'
$listTools = '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'
$callTool = '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"sum","arguments":{"left":2,"right":5}}}'

$payload =
    (New-McpFrame $initialize) +
    (New-McpFrame $initialized) +
    (New-McpFrame $listTools) +
    (New-McpFrame $callTool)

$payload | & $demoExecutable
