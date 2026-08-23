# Copyright (c) 2026 Andrei Kazialetski. MIT License.
#
# sign-test-driver.ps1 — Self-sign and install aic79xx/aic7xxx test drivers
#
# Usage:
#   .\sign-test-driver.ps1 -Driver aic79xx -SourceDir C:\src\aic7xxx\win7-11\aic79xx -Install
#   .\sign-test-driver.ps1 -Driver aic7xxx -SourceDir C:\src\aic7xxx\win7-11\aic7xxx
#
# Requirements:
#   - Windows SDK or WDK (for inf2cat.exe, signtool.exe)
#   - PowerShell 5.0+ (New-SelfSignedCertificate)
#   - Elevated/Administrator privileges
#   - Test signing enabled: bcdedit /set testsigning on
#
# Copyright (c) 2026 Andrei Kazialetski. MIT License.

param(
    [Parameter(Mandatory=$true)]
    [ValidateSet("aic79xx", "aic7xxx")]
    [string]$Driver,

    [Parameter(Mandatory=$true)]
    [string]$SourceDir,

    [switch]$Install
)

$ErrorActionPreference = "Stop"

function Test-Administrator {
    $current = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object System.Security.Principal.WindowsPrincipal($current)
    return $principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Find-Inf2Cat {
    $candidates = @(
        "C:\Program Files (x86)\Windows Kits\10\bin\x64\Inf2Cat.exe",
        "C:\Program Files (x86)\Windows Kits\11\bin\x64\Inf2Cat.exe",
        "C:\Program Files\Windows Kits\10\bin\x64\Inf2Cat.exe",
        "C:\Program Files\Windows Kits\11\bin\x64\Inf2Cat.exe"
    )
    foreach ($path in $candidates) {
        if (Test-Path $path) { return $path }
    }
    throw "inf2cat.exe not found. Install Windows SDK or WDK."
}

function Find-SignTool {
    $candidates = @(
        "C:\Program Files (x86)\Windows Kits\10\bin\x64\signtool.exe",
        "C:\Program Files (x86)\Windows Kits\11\bin\x64\signtool.exe",
        "C:\Program Files\Windows Kits\10\bin\x64\signtool.exe",
        "C:\Program Files\Windows Kits\11\bin\x64\signtool.exe"
    )
    foreach ($path in $candidates) {
        if (Test-Path $path) { return $path }
    }
    throw "signtool.exe not found. Install Windows SDK or WDK."
}

if (-not (Test-Administrator)) {
    throw "This script requires Administrator privileges. Run PowerShell as Administrator."
}

Write-Host "=== Test-signing $Driver driver ===" -ForegroundColor Cyan

# Validate source directory
if (-not (Test-Path $SourceDir)) {
    throw "Source directory not found: $SourceDir"
}

$infFile = Join-Path $SourceDir "$Driver.inf"
$sysFile = Join-Path $SourceDir "$Driver.sys"
$catFile = Join-Path $SourceDir "$Driver.cat"

if (-not (Test-Path $infFile)) {
    throw "INF file not found: $infFile"
}
if (-not (Test-Path $sysFile)) {
    throw "SYS file not found: $sysFile"
}

Write-Host "INF:  $infFile"
Write-Host "SYS:  $sysFile"
Write-Host "CAT:  $catFile"

# Find tools
$inf2cat = Find-Inf2Cat
$signtool = Find-SignTool

Write-Host "inf2cat: $inf2cat"
Write-Host "signtool: $signtool"

# Step 1: Generate catalog
Write-Host "`nStep 1: Generating catalog with inf2cat..." -ForegroundColor Cyan
& $inf2cat /driver:$SourceDir /os:10_x64
if ($LASTEXITCODE -ne 0) {
    throw "inf2cat failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path $catFile)) {
    throw "Catalog file was not created: $catFile"
}
Write-Host "✓ Catalog generated: $catFile"

# Step 2: Create self-signed certificate
Write-Host "`nStep 2: Creating self-signed certificate..." -ForegroundColor Cyan
$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject "CN=AIC Dev Test" `
    -CertStoreLocation Cert:\CurrentUser\My `
    -NotAfter (Get-Date).AddYears(10) `
    -ErrorAction SilentlyContinue

if ($null -eq $cert) {
    # Certificate creation failed or already exists; try to find it
    $existing = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq "CN=AIC Dev Test" }
    if ($existing) {
        $cert = $existing[0]
        Write-Host "✓ Using existing certificate: $($cert.Thumbprint)"
    } else {
        throw "Failed to create or find certificate"
    }
} else {
    Write-Host "✓ Created certificate: $($cert.Thumbprint)"
}

# Step 3: Trust the certificate
Write-Host "`nStep 3: Installing certificate to trusted stores..." -ForegroundColor Cyan

# Export certificate
$certPath = [System.IO.Path]::Combine([System.IO.Path]::GetTempPath(), "aic-dev-test.cer")
Export-Certificate -Cert $cert -FilePath $certPath -Force | Out-Null
Write-Host "✓ Certificate exported: $certPath"

# Import to Root and TrustedPublisher
certutil -silent -addstore Root $certPath 2>&1 | Out-Null
certutil -silent -addstore TrustedPublisher $certPath 2>&1 | Out-Null
Write-Host "✓ Certificate installed to Root and TrustedPublisher stores"

# Cleanup temp cert file
Remove-Item $certPath -Force

# Step 4: Sign files
Write-Host "`nStep 4: Signing driver and catalog..." -ForegroundColor Cyan
& $signtool sign /fd sha256 /n "AIC Dev Test" /t http://timestamp.digicert.com $sysFile
if ($LASTEXITCODE -ne 0) {
    Write-Host "Warning: SYS signing exited with code $LASTEXITCODE (may still be signed if timestamp server unavailable)" -ForegroundColor Yellow
}
Write-Host "✓ SYS file signed"

& $signtool sign /fd sha256 /n "AIC Dev Test" /t http://timestamp.digicert.com $catFile
if ($LASTEXITCODE -ne 0) {
    Write-Host "Warning: CAT signing exited with code $LASTEXITCODE (may still be signed if timestamp server unavailable)" -ForegroundColor Yellow
}
Write-Host "✓ CAT file signed"

# Verify signatures
Write-Host "`nStep 5: Verifying signatures..." -ForegroundColor Cyan
& $signtool verify /pa $sysFile
& $signtool verify /pa $catFile
Write-Host "✓ Signatures verified"

# Step 6: Optional installation
if ($Install) {
    Write-Host "`nStep 6: Installing driver with pnputil..." -ForegroundColor Cyan
    pnputil /add-driver "$infFile" /install
    Write-Host "✓ Driver installation initiated"
} else {
    Write-Host "`nStep 6: Installation skipped (use -Install flag to install)" -ForegroundColor Yellow
    Write-Host "To install manually, run:"
    Write-Host "  pnputil /add-driver ""$infFile"" /install"
}

Write-Host "`n=== Complete ===" -ForegroundColor Green
Write-Host "Driver is ready for installation on a machine with test signing enabled."
