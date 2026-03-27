param(
    [Parameter(Mandatory = $false)]
    [string]$ExePath = "build/Release/renderdoc-mcp.exe"
)

$ErrorActionPreference = "Stop"

if(-not (Test-Path $ExePath))
{
    throw "Executable not found: $ExePath"
}

$payload = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"release-smoke-test","version":"1.0"}}}'
$output = $payload | & $ExePath
$jsonText = ($output | Out-String).Trim()

if($LASTEXITCODE -ne 0)
{
    throw "Executable exited with code $LASTEXITCODE"
}

if([string]::IsNullOrWhiteSpace($jsonText))
{
    throw "Executable produced no JSON-RPC output."
}

try
{
    $response = $jsonText | ConvertFrom-Json
}
catch
{
    throw "Executable output was not valid JSON: $jsonText"
}

if($response.jsonrpc -ne "2.0")
{
    throw "Unexpected JSON-RPC version in response: $jsonText"
}

if($null -ne $response.error)
{
    throw "Initialize returned an error: $($response.error | ConvertTo-Json -Compress)"
}

if($null -eq $response.result)
{
    throw "Initialize response did not include a result payload: $jsonText"
}

Write-Host "Smoke test passed for $ExePath"
