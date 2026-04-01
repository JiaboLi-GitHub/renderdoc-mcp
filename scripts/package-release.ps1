param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [Parameter(Mandatory = $false)]
    [string]$BuildRoot = "build",

    [Parameter(Mandatory = $false)]
    [string]$BuildDir = "build/Release",

    [Parameter(Mandatory = $false)]
    [string]$RenderDocBuildDir = "renderdoc-src/x64/Release",

    [Parameter(Mandatory = $false)]
    [string]$RenderDocRoot = "renderdoc-src",

    [Parameter(Mandatory = $false)]
    [string]$OutputRoot = "dist"
)

$ErrorActionPreference = "Stop"

if(-not $Version.StartsWith("v"))
{
    throw "Version must start with 'v'."
}

$packageName = "renderdoc-mcp-windows-x64-$Version"
$packageDir = Join-Path $OutputRoot $packageName
$archivePath = Join-Path $OutputRoot "$packageName.zip"
$hashPath = Join-Path $OutputRoot "$packageName.sha256"

function Copy-RequiredFile
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,

        [Parameter(Mandatory = $true)]
        [string]$Target
    )

    if(-not (Test-Path $Source))
    {
        throw "Required file not found: $Source"
    }

    $targetDir = Split-Path -Parent $Target
    if($targetDir)
    {
        New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
    }

    Copy-Item -Force $Source $Target
}

if(-not (Test-Path $RenderDocRoot))
{
    throw "RenderDoc root not found: $RenderDocRoot"
}

if(-not (Test-Path $BuildRoot))
{
    throw "Build root not found: $BuildRoot"
}

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

if(Test-Path $packageDir)
{
    Remove-Item -Recurse -Force $packageDir
}

if(Test-Path $archivePath)
{
    Remove-Item -Force $archivePath
}

if(Test-Path $hashPath)
{
    Remove-Item -Force $hashPath
}

New-Item -ItemType Directory -Path $packageDir -Force | Out-Null

$requiredBuildOutputs = @(
    (Join-Path $BuildDir "renderdoc-mcp.exe"),
    (Join-Path $BuildDir "renderdoc-cli.exe")
)

foreach($path in $requiredBuildOutputs)
{
    if(-not (Test-Path $path))
    {
        throw "Required build output not found: $path"
    }
}

$installArgs = @(
    "--install", $BuildRoot,
    "--config", "Release",
    "--prefix", $packageDir
)

& cmake @installArgs
if($LASTEXITCODE -ne 0)
{
    throw "cmake --install failed with exit code $LASTEXITCODE"
}

$filesToCopy = @(
    @{ Source = (Join-Path $RenderDocBuildDir "d3dcompiler_47.dll"); Target = (Join-Path $packageDir "bin\d3dcompiler_47.dll") },
    @{ Source = (Join-Path $RenderDocBuildDir "dbghelp.dll"); Target = (Join-Path $packageDir "bin\dbghelp.dll") },
    @{ Source = (Join-Path $RenderDocBuildDir "symsrv.dll"); Target = (Join-Path $packageDir "bin\symsrv.dll") },
    @{ Source = (Join-Path $RenderDocBuildDir "symsrv.yes"); Target = (Join-Path $packageDir "bin\symsrv.yes") },
    @{ Source = "README.md"; Target = (Join-Path $packageDir "README.md") },
    @{ Source = "README-CN.md"; Target = (Join-Path $packageDir "README-CN.md") },
    @{ Source = "LICENSE"; Target = (Join-Path $packageDir "LICENSE") },
    @{ Source = "install-codex.ps1"; Target = (Join-Path $packageDir "install-codex.ps1") },
    @{ Source = (Join-Path $RenderDocRoot "LICENSE.md"); Target = (Join-Path $packageDir "RENDERDOC-LICENSE.md") }
)

foreach($file in $filesToCopy)
{
    Copy-RequiredFile -Source $file.Source -Target $file.Target
}

$noticePath = Join-Path $packageDir "THIRD-PARTY-NOTICES.txt"
@"
This package includes renderdoc-mcp, renderdoc-cli, the renderdoc-mcp Codex skill,
and selected runtime files from RenderDoc v1.36.

Bundled RenderDoc runtime files:
- bin/renderdoc.dll
- bin/d3dcompiler_47.dll
- bin/dbghelp.dll
- bin/symsrv.dll
- bin/symsrv.yes

Bundled Codex assets:
- skills/renderdoc-mcp
- install-codex.ps1

Bundled executables:
- bin/renderdoc-mcp.exe
- bin/renderdoc-cli.exe

See LICENSE and RENDERDOC-LICENSE.md for licensing details and acknowledgements.
"@ | Set-Content -Path $noticePath -Encoding ascii

$validateScript = Join-Path $PSScriptRoot "validate-release-package.ps1"
& $validateScript -PackageDir $packageDir
if($LASTEXITCODE -ne 0)
{
    throw "Release package validation failed with exit code $LASTEXITCODE"
}

Compress-Archive -Path $packageDir -DestinationPath $archivePath -CompressionLevel Optimal

$hash = (Get-FileHash $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
"{0} *{1}" -f $hash, ([IO.Path]::GetFileName($archivePath)) | Set-Content -Path $hashPath -Encoding ascii

$resolvedPackageDir = (Resolve-Path $packageDir).Path
$resolvedArchive = (Resolve-Path $archivePath).Path
$resolvedHash = (Resolve-Path $hashPath).Path

Write-Host "Created package directory: $resolvedPackageDir"
Write-Host "Created archive: $resolvedArchive"
Write-Host "Created SHA256 file: $resolvedHash"

if($env:GITHUB_OUTPUT)
{
    "package_dir=$resolvedPackageDir" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
    "archive_path=$resolvedArchive" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
    "hash_path=$resolvedHash" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
    "package_name=$packageName" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
}
