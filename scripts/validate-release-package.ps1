param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDir,

    [Parameter(Mandatory = $false)]
    [string]$ExpectedRenderDocVersion = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Get-ConfiguredRenderDocVersion
{
    param(
        [Parameter(Mandatory = $false)]
        [string]$VersionOverride
    )

    if($VersionOverride)
    {
        return $VersionOverride.Trim()
    }

    $versionFile = Join-Path $repoRoot "renderdoc-version.txt"
    if(-not (Test-Path $versionFile))
    {
        throw "RenderDoc version file not found: $versionFile"
    }

    return (Get-Content -LiteralPath $versionFile -Raw).Trim()
}

if(-not (Test-Path $PackageDir))
{
    throw "Package directory not found: $PackageDir"
}

$ExpectedRenderDocVersion = Get-ConfiguredRenderDocVersion -VersionOverride $ExpectedRenderDocVersion
$expectedImplementationVersion = [int]($ExpectedRenderDocVersion.TrimStart("v").Split(".")[1])

$requiredPaths = @(
    "bin\renderdoc-mcp.exe",
    "bin\renderdoc-cli.exe",
    "bin\renderdoc.dll",
    "bin\renderdoc.json",
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

$dllPath = Join-Path $PackageDir "bin\renderdoc.dll"
$dllVersion = (Get-Item -LiteralPath $dllPath).VersionInfo.ProductVersion
if($dllVersion -and $dllVersion -ne $ExpectedRenderDocVersion)
{
    throw "renderdoc.dll ProductVersion mismatch. Expected $ExpectedRenderDocVersion but found $dllVersion"
}

$jsonPath = Join-Path $PackageDir "bin\renderdoc.json"
$manifest = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json

if($manifest.layer.name -ne "VK_LAYER_RENDERDOC_Capture")
{
    throw "renderdoc.json layer.name mismatch: $($manifest.layer.name)"
}

$normalizedLibraryPath = ($manifest.layer.library_path -replace "/", "\")
if($normalizedLibraryPath -ne ".\renderdoc.dll")
{
    throw "renderdoc.json library_path mismatch: $($manifest.layer.library_path)"
}

if($manifest.layer.enable_environment.ENABLE_VULKAN_RENDERDOC_CAPTURE -ne "1")
{
    throw "renderdoc.json is missing ENABLE_VULKAN_RENDERDOC_CAPTURE=1"
}

if([int]$manifest.layer.implementation_version -ne $expectedImplementationVersion)
{
    throw "renderdoc.json implementation_version mismatch. Expected $expectedImplementationVersion but found $($manifest.layer.implementation_version)"
}

$expectedDisableVar = "DISABLE_VULKAN_RENDERDOC_CAPTURE_{0}" -f ($ExpectedRenderDocVersion.TrimStart("v").Replace(".", "_"))
$disableVarNames = @($manifest.layer.disable_environment.PSObject.Properties.Name)
if($disableVarNames -notcontains $expectedDisableVar)
{
    throw "renderdoc.json disable_environment is missing $expectedDisableVar"
}

Write-Host "Validated release package contents in $PackageDir"
