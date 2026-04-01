param(
    [Parameter(Mandatory = $false)]
    [string]$PackageRoot = $PSScriptRoot,

    [Parameter(Mandatory = $false)]
    [string]$CodexHome = "",

    [Parameter(Mandatory = $false)]
    [string]$InstallRoot = "",

    [Parameter(Mandatory = $false)]
    [switch]$SkipPathUpdate,

    [Parameter(Mandatory = $false)]
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$mcpName = "renderdoc-mcp"
$skillName = "renderdoc-mcp"

function Get-BackupPath
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return "$Path.bak-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
}

function Replace-Or-Backup
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if(-not (Test-Path $Path))
    {
        return
    }

    if($Force)
    {
        Remove-Item -Recurse -Force -LiteralPath $Path
        return
    }

    $backupPath = Get-BackupPath -Path $Path
    Move-Item -LiteralPath $Path -Destination $backupPath
    Write-Host "Backed up $Path to $backupPath"
}

if(-not $CodexHome)
{
    if($env:CODEX_HOME)
    {
        $CodexHome = $env:CODEX_HOME
    }
    else
    {
        $CodexHome = Join-Path $HOME ".codex"
    }
}

if(-not $InstallRoot)
{
    $InstallRoot = Join-Path (Join-Path $CodexHome "vendor_imports") $mcpName
}

$PackageRoot = (Resolve-Path $PackageRoot).Path
$CodexHome = [System.IO.Path]::GetFullPath($CodexHome)
$InstallRoot = [System.IO.Path]::GetFullPath($InstallRoot)

$requiredPaths = @(
    (Join-Path $PackageRoot "bin\renderdoc-mcp.exe"),
    (Join-Path $PackageRoot "bin\renderdoc-cli.exe"),
    (Join-Path $PackageRoot "bin\renderdoc.json"),
    (Join-Path $PackageRoot "skills\renderdoc-mcp\SKILL.md"),
    (Join-Path $PackageRoot "skills\renderdoc-mcp\agents\openai.yaml")
)

foreach($path in $requiredPaths)
{
    if(-not (Test-Path $path))
    {
        throw "Package is missing required content: $path"
    }
}

New-Item -ItemType Directory -Path $CodexHome -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $CodexHome "skills") -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path -Parent $InstallRoot) -Force | Out-Null

if(-not [string]::Equals($PackageRoot, $InstallRoot, [System.StringComparison]::OrdinalIgnoreCase))
{
    Replace-Or-Backup -Path $InstallRoot
    New-Item -ItemType Directory -Path $InstallRoot -Force | Out-Null
    Copy-Item -Recurse -Force -Path (Join-Path $PackageRoot "*") -Destination $InstallRoot
}

$skillSource = Join-Path $InstallRoot "skills\$skillName"
$skillTarget = Join-Path (Join-Path $CodexHome "skills") $skillName
Replace-Or-Backup -Path $skillTarget
Copy-Item -Recurse -Force -LiteralPath $skillSource -Destination (Join-Path $CodexHome "skills")

$configPath = Join-Path $CodexHome "config.toml"
$serverCommand = Join-Path $InstallRoot "bin\renderdoc-mcp.exe"
$mcpSection = @(
    "[mcp_servers.$mcpName]",
    "command = '$serverCommand'",
    "args = []"
) -join "`r`n"

$configContent = ""
if(Test-Path $configPath)
{
    $configBackup = Get-BackupPath -Path $configPath
    Copy-Item -Force $configPath $configBackup
    Write-Host "Backed up $configPath to $configBackup"
    $configContent = Get-Content -Path $configPath -Raw
}

$sectionPattern = "(?ms)^\[mcp_servers\.$([regex]::Escape($mcpName))\]\r?\n(?:.*\r?\n)*?(?=^\[|\z)"
if([regex]::IsMatch($configContent, $sectionPattern))
{
    $updatedConfig = [regex]::Replace($configContent, $sectionPattern, $mcpSection + "`r`n")
}
else
{
    if($configContent.Length -gt 0 -and -not $configContent.EndsWith("`n"))
    {
        $configContent += "`r`n"
    }

    if($configContent.Length -gt 0)
    {
        $configContent += "`r`n"
    }

    $updatedConfig = $configContent + $mcpSection + "`r`n"
}

Set-Content -Path $configPath -Value $updatedConfig -Encoding utf8

if(-not $SkipPathUpdate)
{
    $binPath = Join-Path $InstallRoot "bin"
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $pathEntries = @()
    if($userPath)
    {
        $pathEntries = $userPath -split ";" | Where-Object { $_.Trim() -ne "" }
    }

    $alreadyPresent = $false
    foreach($entry in $pathEntries)
    {
        if([string]::Equals($entry.Trim(), $binPath, [System.StringComparison]::OrdinalIgnoreCase))
        {
            $alreadyPresent = $true
            break
        }
    }

    if(-not $alreadyPresent)
    {
        $pathEntries += $binPath
        [Environment]::SetEnvironmentVariable("Path", ($pathEntries -join ";"), "User")

        if($env:Path)
        {
            $env:Path = "$env:Path;$binPath"
        }
        else
        {
            $env:Path = $binPath
        }
    }
}

Write-Host "Installed package root: $InstallRoot"
Write-Host "Installed skill: $skillTarget"
Write-Host "Configured MCP server: $mcpName"
Write-Host "Restart Codex Desktop to pick up the new MCP server and skill."
