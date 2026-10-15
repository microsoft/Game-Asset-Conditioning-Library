#-------------------------------------------------------------------------------------
# setupCLER.ps1
#
# Game Asset Conditioning Library - Microsoft toolkit for game asset compression
#
# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.
#
#-------------------------------------------------------------------------------------

<#
.SYNOPSIS
    Sets up CLER (Component-Level Entropy Reduction) ONNX models for GACL.

.PARAMETER ModelsDir
    Directory where ONNX models will be written.
    Defaults to <repo root>/ThirdParty/models/ when running from the source tree.
    When installed via vcpkg, pass the desired output path explicitly, e.g.:
        setupCLER.ps1 -ModelsDir "C:\MyProject\ThirdParty\models"
#>
param(
    [string]$ModelsDir = ""
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition

# Resolve ModelsDir: use caller-supplied path, or fall back to the repo-relative default.
if ($ModelsDir -eq "") {
    # Running from the source tree: scripts/ -> Tools/ -> repo root
    $ToolsDir = Split-Path -Parent $ScriptDir
    $RepoRoot = Split-Path -Parent $ToolsDir
    $ModelsDir = Join-Path $RepoRoot "ThirdParty\models"
}

$venvPath = Join-Path $ScriptDir ".setupCLER-venv"

# ==========================================================
# INSTALL MODE
# ==========================================================

Write-Host "=== CLER (Component Level Entropy Reduction) Setup ==="
Write-Host ""
Write-Host "This script will configure the following dependencies:"
Write-Host "  1. uv package manager for a temporary virtual environment"
Write-Host "  2. Python 3.12 environment with ONNX packages in the venv"
Write-Host ""

# ==========================================================
# Create models directory
# ==========================================================

if (-not (Test-Path $ModelsDir)) {
    New-Item -ItemType Directory -Path $ModelsDir -Force | Out-Null
    Write-Host "Created: $ModelsDir"
}

Write-Host ""

# ==========================================================
# Check/Install uv package manager
# ==========================================================

Write-Host "--- Checking for uv package manager ---"

$uvFound = Get-Command uv -ErrorAction SilentlyContinue
if ($uvFound) {
    $uvVersion = uv --version
    Write-Host "Found: $uvVersion"
}
else {
    Write-Host "uv not found. Installing uv..."
    
    try {
        Invoke-WebRequest -Uri "https://astral.sh/uv/install.ps1" -UseBasicParsing | Invoke-Expression
        
        # Refresh PATH in current session
        $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
        
        $uvVersion = uv --version
        Write-Host "Installed: $uvVersion"
    }
    catch {
        Write-Warning "Failed to install uv automatically."
        Write-Host "Please install uv manually from: https://docs.astral.sh/uv/getting-started/installation/"
        Write-Host "After installation, re-run this script."
        exit 1
    }
}

Write-Host ""

# ==========================================================
# Create Python Environment with uv
# ==========================================================

Write-Host "--- Setting up Python 3.12 Environment with uv ---"

if (Test-Path $venvPath) {
    Remove-Item $venvPath -Recurse -Force
    Write-Host "Removed temporary environment: $venvPath"
}

if (-not (Test-Path $venvPath)) {
    Write-Host "Creating environment at: $venvPath"
    uv venv $venvPath --python 3.12.10
    Write-Host "Environment created."
}
else {
    Write-Host "Environment already exists at: $venvPath"
}

Write-Host ""

# ==========================================================
# Install Python Dependencies using uv pip
# ==========================================================

Write-Host "--- Installing Python Dependencies ---"
Write-Host "This may take a few minutes (downloading torch ~2GB)..."
Write-Host "Installing torch, onnx, and lpips..."

# Use uv pip to install into the venv (much faster than regular pip)
$env:VIRTUAL_ENV = $venvPath
uv pip install torch==2.5.1+cu118 --index-url https://download.pytorch.org/whl/cu118 --extra-index-url https://pypi.org/simple --index-strategy unsafe-best-match onnx==1.18.0 onnxscript==0.3.2 lpips==0.1.4

if ($LASTEXITCODE -ne 0) {
    Write-Host "Failed to install Python dependencies." -ForegroundColor Red
    if (Test-Path $venvPath) {
        Remove-Item $venvPath -Recurse -Force
    }
    exit 1
}

Write-Host "Dependencies installed."
Write-Host ""

# ==========================================================
# Export Models
# ==========================================================

Write-Host "--- Exporting ONNX Models ---"
Write-Host "You may encounter some expected warnings such as a pretained/weights deprecation, torch.load() security warning, and TracerWarning"

$pythonExe = Join-Path $venvPath "Scripts\python.exe"
$env:GACL_MODELS_DIR = $ModelsDir
& $pythonExe "$ScriptDir\onnxExporter.py"

if ($LASTEXITCODE -ne 0) {
    Write-Host "Model export failed." -ForegroundColor Red
    if (Test-Path $venvPath) {
        Remove-Item $venvPath -Recurse -Force
    }
    exit 1
}

Write-Host "Models exported successfully."
Write-Host ""

# ==========================================================
# Clean up virtual environment
# ==========================================================

Write-Host "--- Cleaning up virtual environment ---"

# Clear environment variables set for this session
$env:VIRTUAL_ENV = $null
$env:GACL_MODELS_DIR = $null

if (Test-Path $venvPath) {
    Remove-Item $venvPath -Recurse -Force
    Write-Host "Removed temporary environment: $venvPath"
}

Write-Host ""

# ==========================================================
# Summary
# ==========================================================

Write-Host ""
Write-Host "========================================"
Write-Host "  CLER Setup Complete!"
Write-Host "========================================"
Write-Host ""
Write-Host "Configuration Summary:"
Write-Host "  [OK] ONNX models exported to: $ModelsDir"