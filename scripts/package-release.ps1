param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

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

if(-not (Test-Path $RenderDocRoot))
{
    throw "RenderDoc root not found: $RenderDocRoot"
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

$filesToCopy = @(
    @{ Source = (Join-Path $BuildDir "renderdoc-mcp.exe"); Target = "renderdoc-mcp.exe" },
    @{ Source = (Join-Path $BuildDir "renderdoc.dll"); Target = "renderdoc.dll" },
    @{ Source = (Join-Path $RenderDocBuildDir "d3dcompiler_47.dll"); Target = "d3dcompiler_47.dll" },
    @{ Source = (Join-Path $RenderDocBuildDir "dbghelp.dll"); Target = "dbghelp.dll" },
    @{ Source = (Join-Path $RenderDocBuildDir "symsrv.dll"); Target = "symsrv.dll" },
    @{ Source = (Join-Path $RenderDocBuildDir "symsrv.yes"); Target = "symsrv.yes" },
    @{ Source = "README.md"; Target = "README.md" },
    @{ Source = "LICENSE"; Target = "LICENSE" },
    @{ Source = (Join-Path $RenderDocRoot "LICENSE.md"); Target = "RENDERDOC-LICENSE.md" }
)

foreach($file in $filesToCopy)
{
    if(-not (Test-Path $file.Source))
    {
        throw "Required file not found: $($file.Source)"
    }

    Copy-Item -Force $file.Source (Join-Path $packageDir $file.Target)
}

$noticePath = Join-Path $packageDir "THIRD-PARTY-NOTICES.txt"
@"
This package includes renderdoc-mcp and selected runtime files from RenderDoc v1.36.

Bundled RenderDoc runtime files:
- renderdoc.dll
- d3dcompiler_47.dll
- dbghelp.dll
- symsrv.dll
- symsrv.yes

See RENDERDOC-LICENSE.md for RenderDoc licensing and third-party acknowledgements.
"@ | Set-Content -Path $noticePath -Encoding ascii

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
