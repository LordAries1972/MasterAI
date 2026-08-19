# Phase 36 runbook: records one real PerformanceCertificationRecord against
# a running MasterAI server on *this* host. No cross-host orchestration --
# an administrator runs this once per physical machine/storage medium/GPU
# configuration they want evidence for, matching docs/PLAN.md Phase 36's
# honest scope (the software-controllable dimensions are all wired to a real
# measurement here; only which physical host you happen to be on is left to
# you). See docs/validation/phase-36-certification-runbook.md for the full
# write-up of what this covers and does not cover.
[CmdletBinding()]
param(
    [string]$Settings = '',
    [string]$BaseUrl = 'http://127.0.0.1:7070',
    [Parameter(Mandatory)]
    [string]$Username,
    [Parameter(Mandatory)]
    [System.Security.SecureString]$Password,
    [Parameter(Mandatory)]
    [string]$ModelId,
    [string]$ProjectId = '',
    [ValidateSet('quick', 'standard', 'extended')]
    [string]$Profile = 'quick',
    [ValidateSet('cold', 'warm')]
    [string]$CacheState = 'warm',
    [int]$Concurrency = 1,
    [string]$AcceleratorPolicy = 'auto',
    [string]$BackendVersion = 'llama.cpp-b10156',
    [string]$BuildId = 'masterai-0.1.0'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Settings) { $Settings = Join-Path $projectRoot 'config\settings.json' }

$plainPassword = [Runtime.InteropServices.Marshal]::PtrToStringAuto(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Password))
try {
    $session = $null
    $login = Invoke-RestMethod -Uri "$BaseUrl/api/v1/auth/login" -Method Post `
        -ContentType 'application/json' -SessionVariable session `
        -Body (@{ username = $Username; password = $plainPassword } | ConvertTo-Json)
} finally {
    $plainPassword = $null
}
$csrfToken = $login.csrfToken
$headers = @{ 'X-CSRF-Token' = $csrfToken }

# hardwareId mirrors the CLI's own convention (see src/main.cpp's
# benchmark-model/calibrate/speculative-benchmark commands): platform-
# architecture-logicalCpuCount, so a certification record from this script
# stays comparable against one from those CLI commands on the same host.
$hardwareId = "windows-x86_64-$env:NUMBER_OF_PROCESSORS"

if ($ProjectId) {
    Write-Host "Ensuring a chat exists to load $ModelId..."
    $chat = Invoke-RestMethod -Uri "$BaseUrl/api/v1/chats" -Method Post -WebSession $session `
        -Headers $headers -ContentType 'application/json' `
        -Body (@{ projectId = $ProjectId; modelId = $ModelId } | ConvertTo-Json)
    Write-Host "Sending a warm-up message so the model is actually resident before certifying..."
    Invoke-RestMethod -Uri "$BaseUrl/api/v1/chats/$($chat.id)/messages" -Method Post -WebSession $session `
        -Headers $headers -ContentType 'application/json' `
        -Body (@{ content = 'Say hello in one short sentence.' } | ConvertTo-Json) | Out-Null
} else {
    Write-Host "No -ProjectId given; assuming $ModelId is already loaded (e.g. an active chat)."
}

Write-Host "Running certification (profile=$Profile, cacheState=$CacheState, concurrency=$Concurrency)..."
$record = Invoke-RestMethod -Uri "$BaseUrl/api/v1/performance/certification" -Method Post -WebSession $session `
    -Headers $headers -ContentType 'application/json' `
    -Body (@{
        modelId = $ModelId
        backendVersion = $BackendVersion
        buildId = $BuildId
        hardwareId = $hardwareId
        profile = $Profile
        cacheState = $CacheState
        concurrency = $Concurrency
        acceleratorPolicy = $AcceleratorPolicy
    } | ConvertTo-Json)

$record | ConvertTo-Json -Depth 6
if (-not $record.accepted) {
    Write-Warning "Certification was NOT accepted: $($record.rejectionReason)"
    exit 1
}
Write-Host "Certification accepted. Record id: $($record.id), fingerprint: $($record.fingerprint)"
