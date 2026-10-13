param(
    [string]$Source = $env:GACL_NUGET_SOURCE
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($Source))
{
    $Source = "https://api.nuget.org/v3/index.json"
}

$msbuild = Get-Command msbuild.exe -ErrorAction SilentlyContinue
if ($msbuild)
{
    $msbuildPath = $msbuild.Source
}
else
{
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere))
    {
        throw "MSBuild was not found. Install Visual Studio with Desktop development with C++."
    }

    $msbuildPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
        -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($msbuildPath))
    {
        throw "MSBuild was not found. Install Visual Studio with Desktop development with C++."
    }
}

$repositoryRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$solution = Join-Path $repositoryRoot "gacl.sln"
$configPath = Join-Path ([System.IO.Path]::GetTempPath()) "gacl-nuget-$([guid]::NewGuid()).config"
$escapedRepositoryPath = [System.Security.SecurityElement]::Escape(
    (Join-Path $repositoryRoot "packages"))
$escapedSource = [System.Security.SecurityElement]::Escape($Source)
$config = @"
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <config>
    <add key="repositoryPath" value="$escapedRepositoryPath" />
  </config>
  <packageRestore>
    <add key="enabled" value="true" />
  </packageRestore>
  <packageSources>
    <clear />
    <add key="GACL package source" value="$escapedSource" />
  </packageSources>
</configuration>
"@

Write-Host "Restoring NuGet packages from the configured source."
try
{
    [System.IO.File]::WriteAllText($configPath, $config)
    & $msbuildPath $solution `
        /t:Restore `
        /m `
        /p:RestorePackagesConfig=true `
        "/p:RestoreConfigFile=$configPath" `
        /verbosity:minimal

    if ($LASTEXITCODE -ne 0)
    {
        throw "NuGet package restore failed with exit code $LASTEXITCODE."
    }
}
finally
{
    Remove-Item -LiteralPath $configPath -Force -ErrorAction SilentlyContinue
}
