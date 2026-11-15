<#
.SYNOPSIS
    Fetches an official prebuilt RenderDoc release and lays it out for building renderdoc-mcp.

.DESCRIPTION
    Downloads the official Windows x64 RenderDoc zip from renderdoc.org, verifies its SHA256,
    generates the renderdoc.lib import library from the DLL exports, and sparse-checks out the
    matching replay API headers from the upstream tag. The resulting layout is:

        <OutputDir>\LICENSE.md
        <OutputDir>\renderdoc\api\...        (pass <OutputDir> as RENDERDOC_DIR)
        <OutputDir>\x64\Release\...          (pass this as RENDERDOC_BUILD_DIR)
#>
param(
    [Parameter(Mandatory = $false)]
    [string]$Version = "",

    [Parameter(Mandatory = $false)]
    [string]$OutputDir = "renderdoc-sdk",

    [Parameter(Mandatory = $false)]
    [string]$Sha256 = ""
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

# SHA256 of the official RenderDoc_<version>_64.zip for each supported version.
$knownHashes = @{
    "v1.47" = "B51712E58D40066B34CA30C7869D15F72E4B5C001B2D92232F6783A636D5A3C1"
}

if(-not $Version)
{
    $Version = (Get-Content -LiteralPath (Join-Path $repoRoot "renderdoc-version.txt") -Raw).Trim()
}

if(-not $Version.StartsWith("v"))
{
    throw "RenderDoc version must start with 'v' (for example v1.47)."
}

if(-not $Sha256)
{
    $Sha256 = $knownHashes[$Version]
    if(-not $Sha256)
    {
        throw "No known SHA256 for RenderDoc $Version. Add it to `$knownHashes in $PSCommandPath or pass -Sha256."
    }
}

$versionNumber = $Version.TrimStart("v")
$zipUrl = "https://renderdoc.org/stable/$versionNumber/RenderDoc_${versionNumber}_64.zip"
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ("renderdoc-fetch-" + [guid]::NewGuid().ToString("N"))
$OutputDir = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $OutputDir))
$binDir = Join-Path $OutputDir "x64\Release"

function Find-MsvcTool
{
    param([string]$Name)

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if(-not (Test-Path $vswhere))
    {
        throw "vswhere.exe not found; Visual Studio with C++ tools is required."
    }

    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $tool = Get-ChildItem (Join-Path $installPath "VC\Tools\MSVC\*\bin\Hostx64\x64\$Name") -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if(-not $tool)
    {
        throw "$Name not found under $installPath."
    }

    return $tool.FullName
}

New-Item -ItemType Directory -Path $workDir -Force | Out-Null
try
{
    if(Test-Path $OutputDir)
    {
        Remove-Item -Recurse -Force $OutputDir
    }
    New-Item -ItemType Directory -Path $binDir -Force | Out-Null

    $zipPath = Join-Path $workDir "renderdoc.zip"
    Write-Host "Downloading $zipUrl"
    Invoke-WebRequest -Uri $zipUrl -OutFile $zipPath -UseBasicParsing

    $actualHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
    if($actualHash -ne $Sha256.ToUpperInvariant())
    {
        throw "SHA256 mismatch for $zipUrl. Expected $Sha256 but found $actualHash."
    }

    $extractDir = Join-Path $workDir "zip"
    Expand-Archive -LiteralPath $zipPath -DestinationPath $extractDir
    $releaseDir = Join-Path $extractDir "RenderDoc_${versionNumber}_64"

    foreach($file in "renderdoc.dll", "renderdoc.json", "d3dcompiler_47.dll", "dbghelp.dll", "symsrv.dll", "symsrv.yes")
    {
        Copy-Item -LiteralPath (Join-Path $releaseDir $file) -Destination $binDir
    }
    Copy-Item -LiteralPath (Join-Path $releaseDir "LICENSE.md") -Destination $OutputDir

    $dllPath = Join-Path $binDir "renderdoc.dll"
    $dllVersion = (Get-Item -LiteralPath $dllPath).VersionInfo.ProductVersion
    if($dllVersion -ne $Version)
    {
        throw "renderdoc.dll ProductVersion mismatch. Expected $Version but found $dllVersion."
    }

    # The official zip ships no import library; the replay API is plain extern "C" exports,
    # so an import library generated from the export table is equivalent.
    $dumpbin = Find-MsvcTool "dumpbin.exe"
    $libExe = Find-MsvcTool "lib.exe"
    $exports = & $dumpbin /nologo /exports $dllPath |
        Select-String '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+(\S+)' |
        ForEach-Object { $_.Matches[0].Groups[1].Value }
    if($LASTEXITCODE -ne 0 -or -not $exports)
    {
        throw "Failed to read exports from $dllPath."
    }

    $defPath = Join-Path $workDir "renderdoc.def"
    $defLines = @("LIBRARY renderdoc.dll", "EXPORTS")
    foreach($export in $exports)
    {
        if($export -like "Dll*")
        {
            $defLines += "    $export PRIVATE"
        }
        else
        {
            $defLines += "    $export"
        }
    }
    Set-Content -LiteralPath $defPath -Value $defLines -Encoding ascii

    & $libExe /nologo /machine:x64 "/def:$defPath" "/out:$(Join-Path $binDir 'renderdoc.lib')" | Out-Null
    if($LASTEXITCODE -ne 0)
    {
        throw "lib.exe failed with exit code $LASTEXITCODE."
    }
    Remove-Item -Force (Join-Path $binDir "renderdoc.exp") -ErrorAction SilentlyContinue

    Write-Host "Fetching RenderDoc $Version API headers"
    $sourceDir = Join-Path $workDir "src"
    & git clone --quiet --depth 1 --branch $Version --filter=blob:none --no-checkout https://github.com/baldurk/renderdoc.git $sourceDir
    if($LASTEXITCODE -ne 0) { throw "git clone failed with exit code $LASTEXITCODE." }
    & git -C $sourceDir sparse-checkout set --no-cone /renderdoc/api/
    if($LASTEXITCODE -ne 0) { throw "git sparse-checkout failed with exit code $LASTEXITCODE." }
    & git -C $sourceDir checkout --quiet $Version
    if($LASTEXITCODE -ne 0) { throw "git checkout failed with exit code $LASTEXITCODE." }

    $apiTarget = Join-Path $OutputDir "renderdoc"
    New-Item -ItemType Directory -Path $apiTarget -Force | Out-Null
    Copy-Item -Recurse -LiteralPath (Join-Path $sourceDir "renderdoc\api") -Destination $apiTarget

    Write-Host "RenderDoc $Version ready in $OutputDir"
    Write-Host "  RENDERDOC_DIR=$OutputDir"
    Write-Host "  RENDERDOC_BUILD_DIR=$binDir"
}
finally
{
    Remove-Item -Recurse -Force $workDir -ErrorAction SilentlyContinue
}
