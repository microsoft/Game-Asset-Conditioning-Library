[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$SourceRoot,

    [Parameter(Mandatory)]
    [string]$DestRoot
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $SourceRoot)) {
    throw "GitHub source not found: $SourceRoot"
}

# ADO-only paths that GitHub must never overwrite
$xdArgs = @('.git', '.gdn', 'BuildPipelines')
$xfArgs = @('nuget.config')

Write-Host "Source: $SourceRoot"
Write-Host "Dest:   $DestRoot"

$roboArgs = @(
    $SourceRoot
    $DestRoot
    '/E'
    '/R:2'
    '/W:2'
) + ($xdArgs | ForEach-Object { '/XD'; $_ }) `
  + ($xfArgs | ForEach-Object { '/XF'; $_ })

Write-Host "robocopy $($roboArgs -join ' ')"
& robocopy @roboArgs

if ($LASTEXITCODE -ge 8) {
    throw "robocopy failed with exit code $LASTEXITCODE"
}

exit 0
