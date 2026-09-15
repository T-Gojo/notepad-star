#Requires -Version 5.1
[CmdletBinding()]
param([string] $Path, [switch] $CheckOnly, [switch] $VerifyOnly)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$certificate = $env:NOTEPAD_STAR_CERTIFICATE_SHA1
$tool = $env:NOTEPAD_STAR_SIGNTOOL
$timestamp = $env:NOTEPAD_STAR_TIMESTAMP_URL
if ($certificate -notmatch '^[a-fA-F0-9]{40}$') {
	throw 'NOTEPAD_STAR_CERTIFICATE_SHA1 must select your code-signing certificate in CurrentUser\My.'
}
if (-not $tool -or -not (Test-Path -LiteralPath $tool -PathType Leaf)) {
	throw 'Set NOTEPAD_STAR_SIGNTOOL to the Windows SDK signtool.exe.'
}
$uri = $null
if (-not [Uri]::TryCreate($timestamp, [UriKind]::Absolute, [ref]$uri) -or $uri.Scheme -ne 'https') {
	throw 'Set NOTEPAD_STAR_TIMESTAMP_URL to your certificate provider''s HTTPS RFC 3161 service.'
}
if (-not $VerifyOnly) {
    $signer = Get-Item -LiteralPath ("Cert:\CurrentUser\My\" + $certificate) -ErrorAction Stop
    if (-not $signer.HasPrivateKey -or $signer.NotAfter -le (Get-Date) -or $signer.NotBefore -gt (Get-Date)) {
        throw 'The configured certificate needs a currently valid private key.'
    }
    if (-not @($signer.EnhancedKeyUsageList | Where-Object { $_.ObjectId.Value -eq '1.3.6.1.5.5.7.3.3' }).Count) {
        throw 'The configured certificate is not enabled for code signing.'
    }
}
if ($CheckOnly) { return }
if (-not $Path) { throw 'Path is required for signing or verification.' }
$file = (Get-Item -LiteralPath $Path).FullName
if (-not $VerifyOnly) {
    & $tool sign /sha1 $certificate /s My /fd SHA256 /tr $timestamp /td SHA256 $file
    if ($LASTEXITCODE -ne 0) { throw "Signing failed for $file." }
}
& $tool verify /pa /all /tw $file
if ($LASTEXITCODE -ne 0) { throw "Signature verification failed for $file." }
$signature = Get-AuthenticodeSignature -LiteralPath $file
if ($signature.Status -ne 'Valid' -or $null -eq $signature.SignerCertificate -or
	$signature.SignerCertificate.Thumbprint -ne $certificate -or $null -eq $signature.TimeStamperCertificate) {
	throw 'Expected a trusted timestamped signature from the configured publisher.'
}
