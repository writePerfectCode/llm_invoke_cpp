param(
    [string]$ExecutablePath
)

$ErrorActionPreference = 'Stop'

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

function Start-McpServerProcess([string]$demoExecutable) {
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $demoExecutable
    $startInfo.WorkingDirectory = Split-Path -Parent $demoExecutable
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.StandardOutputEncoding = [System.Text.Encoding]::UTF8
    $startInfo.StandardErrorEncoding = [System.Text.Encoding]::UTF8

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    $process.Start() | Out-Null
    return $process
}

function Read-McpMessage([System.IO.StreamReader]$reader) {
    $contentLength = $null

    while ($true) {
        $line = $reader.ReadLine()
        if ($null -eq $line) {
            return $null
        }

        if ([string]::IsNullOrEmpty($line)) {
            if ($null -eq $contentLength) {
                continue
            }

            break
        }

        $separatorIndex = $line.IndexOf(':')
        if ($separatorIndex -lt 0) {
            continue
        }

        $name = $line.Substring(0, $separatorIndex).Trim().ToLowerInvariant()
        $value = $line.Substring($separatorIndex + 1).Trim()
        if ($name -eq 'content-length') {
            $contentLength = [int]$value
        }
    }

    if ($null -eq $contentLength) {
        throw 'Missing Content-Length header in MCP response.'
    }

    $payload = New-Object char[] $contentLength
    $offset = 0
    while ($offset -lt $contentLength) {
        $readCount = $reader.Read($payload, $offset, $contentLength - $offset)
        if ($readCount -le 0) {
            throw 'Unexpected end of stream while reading MCP response body.'
        }

        $offset += $readCount
    }

        $jsonText = -join $payload

        for ($extraChars = 0; $extraChars -le 64; $extraChars++) {
            try {
                return $jsonText | ConvertFrom-Json
            }
            catch {
                if ($extraChars -eq 64) {
                    throw
                }

                $nextChar = New-Object char[] 1
                $readMore = $reader.Read($nextChar, 0, 1)
                if ($readMore -le 0) {
                    throw
                }

                $jsonText += -join $nextChar[0..($readMore - 1)]
            }
        }
}

function Send-McpMessage([System.Diagnostics.Process]$process, $messageObject) {
    $jsonText = $messageObject | ConvertTo-Json -Depth 20 -Compress
    $frame = New-McpFrame $jsonText
    $payload = [System.Text.Encoding]::UTF8.GetBytes($frame)
    $process.StandardInput.BaseStream.Write($payload, 0, $payload.Length)
    $process.StandardInput.BaseStream.Flush()
}

function Invoke-McpRequest([System.Diagnostics.Process]$process, [hashtable]$messageObject) {
    Send-McpMessage -process $process -messageObject $messageObject

    $response = Read-McpMessage $process.StandardOutput
    if ($null -eq $response) {
        throw 'MCP server closed stdout before returning a response.'
    }

    if ($response.PSObject.Properties.Name -contains 'error') {
        throw ("MCP request failed: {0}" -f ($response.error | ConvertTo-Json -Compress))
    }

    return $response
}

function Send-McpNotification([System.Diagnostics.Process]$process, [hashtable]$messageObject) {
    Send-McpMessage -process $process -messageObject $messageObject
}

function Get-ToolTextResult($response) {
    if ($response.result.isError) {
        throw ("MCP tool call returned isError=true: {0}" -f ($response.result.content | ConvertTo-Json -Depth 20 -Compress))
    }

    return [string]$response.result.content[0].text
}

function Assert-ToolPresent($listResponse, [string]$toolName) {
    $toolNames = @($listResponse.result.tools | ForEach-Object { $_.name })
    if ($toolNames -notcontains $toolName) {
        throw "Expected tool '$toolName' to be present. Available tools: $($toolNames -join ', ')"
    }
}

$repoRoot = Get-RepoRoot
$demoExecutable = Resolve-DemoExecutable -repoRoot $repoRoot -overridePath $ExecutablePath
$process = Start-McpServerProcess -demoExecutable $demoExecutable

try {
    $initializeResponse = Invoke-McpRequest -process $process -messageObject @{
        jsonrpc = '2.0'
        id = 1
        method = 'initialize'
        params = @{
            protocolVersion = '2025-03-26'
            capabilities = @{}
            clientInfo = @{
                name = 'test-client'
                version = '1.0.0'
            }
        }
    }
    Write-Host ("initialize ok: protocolVersion={0}, server={1}@{2}" -f
        $initializeResponse.result.protocolVersion,
        $initializeResponse.result.serverInfo.name,
        $initializeResponse.result.serverInfo.version)

    Send-McpNotification -process $process -messageObject @{
        jsonrpc = '2.0'
        method = 'notifications/initialized'
    }

    $listResponse = Invoke-McpRequest -process $process -messageObject @{
        jsonrpc = '2.0'
        id = 2
        method = 'tools/list'
        params = @{}
    }

    foreach ($toolName in @('sum', 'create_counter', 'counter_add', 'counter_value', 'destroy_counter')) {
        Assert-ToolPresent -listResponse $listResponse -toolName $toolName
    }
    $toolNames = @($listResponse.result.tools | ForEach-Object { $_.name })
    Write-Host ("tools/list ok: {0}" -f ($toolNames -join ', '))

    $sumResponse = Invoke-McpRequest -process $process -messageObject @{
        jsonrpc = '2.0'
        id = 3
        method = 'tools/call'
        params = @{
            name = 'sum'
            arguments = @{
                left = 2
                right = 5
            }
        }
    }
    $sumText = Get-ToolTextResult -response $sumResponse
    if ($sumText -ne '7') {
        throw "Expected sum result 7, got '$sumText'."
    }
    Write-Host 'sum ok: 7'

    $createResponse = Invoke-McpRequest -process $process -messageObject @{
        jsonrpc = '2.0'
        id = 4
        method = 'tools/call'
        params = @{
            name = 'create_counter'
            arguments = @{
                initial = 5
            }
        }
    }
    $handle = (Get-ToolTextResult -response $createResponse) | ConvertFrom-Json
    if (-not $handle.object_id) {
        throw 'create_counter did not return an object handle with object_id.'
    }
    Write-Host ("create_counter ok: object_id={0}" -f $handle.object_id)

    $addResponse = Invoke-McpRequest -process $process -messageObject @{
        jsonrpc = '2.0'
        id = 5
        method = 'tools/call'
        params = @{
            name = 'counter_add'
            arguments = @{
                handle = @{
                    object_id = $handle.object_id
                    object_type = $handle.object_type
                }
                delta = 7
            }
        }
    }
    [void](Get-ToolTextResult -response $addResponse)
    Write-Host 'counter_add ok: delta=7'

    $valueResponse = Invoke-McpRequest -process $process -messageObject @{
        jsonrpc = '2.0'
        id = 6
        method = 'tools/call'
        params = @{
            name = 'counter_value'
            arguments = @{
                handle = @{
                    object_id = $handle.object_id
                    object_type = $handle.object_type
                }
            }
        }
    }
    $counterValueText = Get-ToolTextResult -response $valueResponse
    if ($counterValueText -ne '12') {
        throw "Expected counter_value result 12, got '$counterValueText'."
    }
    Write-Host 'counter_value ok: 12'

    $destroyResponse = Invoke-McpRequest -process $process -messageObject @{
        jsonrpc = '2.0'
        id = 7
        method = 'tools/call'
        params = @{
            name = 'destroy_counter'
            arguments = @{
                handle = @{
                    object_id = $handle.object_id
                    object_type = $handle.object_type
                }
            }
        }
    }
    [void](Get-ToolTextResult -response $destroyResponse)
    Write-Host 'destroy_counter ok'
}
finally {
    if (-not $process.HasExited) {
        $process.StandardInput.Close()
        [void]$process.WaitForExit(2000)
        if (-not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
    }

    $stderrText = $process.StandardError.ReadToEnd()
    if (-not [string]::IsNullOrWhiteSpace($stderrText)) {
        Write-Warning $stderrText.Trim()
    }

    $process.Dispose()
}
