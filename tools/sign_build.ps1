param (
    [string]$ExePath = "$PSScriptRoot\..\build\bin\ShaderLabIIDE.exe"
)

$ExePath = Resolve-Path $ExePath -ErrorAction SilentlyContinue

if (-not $ExePath -or -not (Test-Path $ExePath)) {
    Write-Error "Executable not found at default location. Please build first."
    exit 1
}

$CertSubject = "CN=ShaderLabDev"
$CertStoreScope = "CurrentUser" 

Write-Host "Checking for existing certificate '$CertSubject'..."
$cert = Get-ChildItem "Cert:\$CertStoreScope\My" -CodeSigningCert | Where-Object { $_.Subject -eq $CertSubject } | Select-Object -First 1

if (-not $cert) {
    Write-Host "Creating new self-signed code signing certificate..."
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $CertSubject -CertStoreLocation "Cert:\$CertStoreScope\My"
} else {
    Write-Host "Found existing certificate: $($cert.Thumbprint)"
}

# Ensure it's in the Trusted Root
$rootStore = New-Object System.Security.Cryptography.X509Certificates.X509Store "Root", $CertStoreScope
$rootStore.Open("ReadWrite")
$isTrusted = $rootStore.Certificates.Find("FindByThumbprint", $cert.Thumbprint, $false).Count -gt 0

if (-not $isTrusted) {
    Write-Host "Installing certificate to Trusted Root Certification Authorities to enable Trust..."
    Write-Host "NOTE: A Windows Security Warning dialog will appear. Please click 'Yes' to install the certificate." -ForegroundColor Yellow
    $rootStore.Add($cert)
}
$rootStore.Close()

Write-Host "Signing $ExePath..."
$sig = Set-AuthenticodeSignature -FilePath $ExePath -Certificate $cert

if ($sig.Status -eq 'Valid') {
    Write-Host "Successfully signed: $ExePath" -ForegroundColor Green
} else {
    Write-Host "Signing failed: $($sig.StatusMessage)" -ForegroundColor Red
    if ($sig.Status -eq "UnknownError") {
        Write-Host "If the error is 'UnknownError', try running this script as Administrator." -ForegroundColor Yellow
    }
}
