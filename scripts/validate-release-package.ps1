param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDir
)

$ErrorActionPreference = "Stop"

if(-not (Test-Path $PackageDir))
{
    throw "Package directory not found: $PackageDir"
}

$requiredPaths = @(
    "bin\renderdoc-mcp.exe",
    "bin\renderdoc-cli.exe",
    "bin\renderdoc.dll",
    "bin\d3dcompiler_47.dll",
    "bin\dbghelp.dll",
    "bin\symsrv.dll",
    "bin\symsrv.yes",
    "skills\renderdoc-mcp\SKILL.md",
    "skills\renderdoc-mcp\agents\openai.yaml",
    "install-codex.ps1",
    "README.md",
    "README-CN.md",
    "LICENSE",
    "RENDERDOC-LICENSE.md",
    "THIRD-PARTY-NOTICES.txt"
)

$missing = @()
foreach($relativePath in $requiredPaths)
{
    $fullPath = Join-Path $PackageDir $relativePath
    if(-not (Test-Path $fullPath))
    {
        $missing += $relativePath
    }
}

if($missing.Count -gt 0)
{
    foreach($relativePath in $missing)
    {
        Write-Error "Missing required package path: $relativePath"
    }

    exit 1
}

Write-Host "Validated release package contents in $PackageDir"
