# Keep both model lookup locations in a built test artifact, including CI OutDir overrides.
param(
    [Parameter(Mandatory = $true)]
    [string]$BinaryDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$binDir = (Resolve-Path -LiteralPath $BinaryDirectory).Path
if (-not (Test-Path -LiteralPath $binDir -PathType Container)) {
    throw "Test binary directory does not exist: $binDir"
}

# Loss-metric tests load beside the EXE; CLER walks three directory parents first.
$libraryRoot = [System.IO.DirectoryInfo]::new($binDir)
for ($i = 0; $i -lt 3; ++$i) {
    $libraryRoot = $libraryRoot.Parent
    if ($null -eq $libraryRoot) { throw 'Binary directory needs three parent directories.' }
}
$libraryModels = Join-Path $libraryRoot.FullName 'ThirdParty\models'
$modelNames = @('lpips_model.onnx', 'vgg_model.onnx')

# Check all sources before copying so an incomplete export cannot qualify a package.
foreach ($name in $modelNames) {
    $source = Join-Path $repoRoot "ThirdParty\models\$name"
    if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or (Get-Item -LiteralPath $source).Length -eq 0) {
        throw "Missing or empty exported model: $source. Run Tools\scripts\setupCLER.ps1 first."
    }
}

foreach ($destination in @($binDir, $libraryModels)) {
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    foreach ($name in $modelNames) {
        $source = Join-Path $repoRoot "ThirdParty\models\$name"
        $target = Join-Path $destination $name
        if (-not [string]::Equals([System.IO.Path]::GetFullPath($source), [System.IO.Path]::GetFullPath($target), [System.StringComparison]::OrdinalIgnoreCase)) {
            Copy-Item -LiteralPath $source -Destination $target -Force
        }
        Write-Host "Staged model: $target"
    }
}
