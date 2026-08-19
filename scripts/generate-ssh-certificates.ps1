# Generates the long-lived TLS certificate and SSH host key pair that a
# future SSH inbound transport (planned as a third McpInboundServer
# transport alongside stdio and Streamable HTTP) will use once it is wired
# in. This script only produces key material -- no SSH server code exists
# yet. Everything is written under config/certificates/, inside the
# already git-ignored /config/ tree, and is refused to be committed by an
# explicit .gitignore rule too (see .gitignore). Re-run any time fresh
# material is needed; existing files are left alone unless -Force is given,
# mirroring initialize_private_certificate_authority()'s own
# refuse-to-overwrite behavior (src/masterai.hpp).
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Medium')]
param(
    [switch]$Force,
    # Certificate subject fields, all optional -- unset fields fall back to
    # the Ultimanium Designs / Melbourne defaults below rather than being
    # omitted, so every generated certificate still has a complete subject.
    [string]$Organization = 'Ultimanium Designs',
    [string]$Locality = 'Melbourne',
    [string]$State = 'Victoria',
    [string]$Country = 'AU',
    [string]$CommonName = 'masterai-ssh-transport',
    [int]$ValidityDays = 182500
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$certRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'config/certificates'))

if (-not (Test-Path -LiteralPath $certRoot)) {
    New-Item -ItemType Directory -Path $certRoot -Force | Out-Null
}

# Prefer a canonical OpenSSL install over whichever "openssl" happens to be
# first on PATH -- some bundled copies (e.g. Strawberry Perl's) ship with a
# baked-in openssl.cnf path from their own build machine that does not exist
# here, and fail with a config-file error rather than actually running.
$opensslCandidates = @(
    'C:\Program Files\OpenSSL-Win64\bin\openssl.exe',
    'D:\Program Files\OpenSSL-Win64\bin\openssl.exe'
) + @(Get-Command openssl -All -ErrorAction SilentlyContinue | ForEach-Object { $_.Source })
$opensslPath = $opensslCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $opensslPath) {
    throw 'openssl was not found; install OpenSSL to generate the TLS certificate.'
}
$sshKeygenCmd = Get-Command ssh-keygen -ErrorAction SilentlyContinue
if (-not $sshKeygenCmd) {
    throw 'ssh-keygen was not found on PATH; install an OpenSSH client to generate the SSH host key.'
}

# Default validity is 100 years: this is a long-lived internal certificate,
# not a publicly trusted one, and is meant to outlive routine rotation
# cycles. Override with -ValidityDays if a shorter lifetime is wanted.
$tlsCertFile = Join-Path $certRoot 'ssh-transport-tls.crt'
$tlsKeyFile = Join-Path $certRoot 'ssh-transport-tls.key'
$sshHostKeyFile = Join-Path $certRoot 'ssh_host_ed25519_key'
$sshHostKeyPubFile = "$sshHostKeyFile.pub"

$subject = "/O=$Organization/L=$Locality/ST=$State/C=$Country/CN=$CommonName"

$tlsExists = (Test-Path -LiteralPath $tlsCertFile) -and (Test-Path -LiteralPath $tlsKeyFile)
if ($tlsExists -and -not $Force) {
    Write-Host "TLS certificate already exists, leaving in place: $tlsCertFile"
} elseif ($PSCmdlet.ShouldProcess($tlsCertFile, 'Generate self-signed TLS certificate')) {
    & $opensslPath req -x509 -newkey rsa:4096 -nodes `
        -keyout $tlsKeyFile -out $tlsCertFile `
        -days $ValidityDays -subj $subject
    if ($LASTEXITCODE -ne 0) {
        throw "openssl exited with code $LASTEXITCODE while generating the TLS certificate."
    }
    Write-Host "Generated TLS certificate: $tlsCertFile"
}

$sshHostKeyExists = Test-Path -LiteralPath $sshHostKeyFile
if ($sshHostKeyExists -and -not $Force) {
    Write-Host "SSH host key already exists, leaving in place: $sshHostKeyFile"
} elseif ($PSCmdlet.ShouldProcess($sshHostKeyFile, 'Generate SSH host key pair')) {
    if ($sshHostKeyExists) {
        Remove-Item -LiteralPath $sshHostKeyFile, $sshHostKeyPubFile -Force -ErrorAction SilentlyContinue
    }
    & $sshKeygenCmd.Path -t ed25519 -f $sshHostKeyFile -N '""' -C $CommonName
    if ($LASTEXITCODE -ne 0) {
        throw "ssh-keygen exited with code $LASTEXITCODE while generating the SSH host key."
    }
    Write-Host "Generated SSH host key: $sshHostKeyFile"
}

Write-Host "Certificate material is in: $certRoot"
