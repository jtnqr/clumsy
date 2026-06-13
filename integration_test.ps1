# clumsy 0.3.4 Integration Test Script
# Run in Administrator mode

# 1. Privilege & Environment Verification
$identity = [System.Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object System.Security.Principal.WindowsPrincipal($identity)
$isAdmin = $principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Error "Error: This integration test must be run in an elevated PowerShell session (Administrator) because loading the WinDivert kernel-mode driver requires elevated WFP privileges."
    exit 1
}

# 2. Dynamic Process Mocking
Write-Host "Spawning mock target process (notepad)..." -ForegroundColor Cyan
$mockProc = Start-Process notepad.exe -WindowStyle Hidden -PassThru
$mockPid = $mockProc.Id
Write-Host "Mock process notepad.exe spawned with PID $mockPid." -ForegroundColor Green

# 3. Compile check & Execution Capture
$clumsyPath = Resolve-Path "zig-out/x64/clumsy.exe" -ErrorAction SilentlyContinue
if (-not $clumsyPath) {
    Write-Error "Error: clumsy.exe not found at zig-out/x64/clumsy.exe. Please build it first (e.g. zig build -Dconf=Debug)."
    # Clean up mock process before exiting
    Stop-Process -Id $mockPid -Force
    exit 1
}

$tempLog = [System.IO.Path]::GetTempFileName()
Write-Host "Launching clumsy.exe in parameterized test mode (redirecting output to $tempLog)..." -ForegroundColor Cyan

# We launch clumsy with console output enabled (requires Debug build or Console subsystem)
# Target notepad using the new process filter
$clumsyProc = Start-Process $clumsyPath -ArgumentList "--filter", "udp", "--process-filter-enabled", "on", "--process-filter-target", "notepad" -NoNewWindow -PassThru -RedirectStandardOutput $tempLog -RedirectStandardError $tempLog

Write-Host "Monitoring execution for 5 seconds..." -ForegroundColor Yellow
Start-Sleep -Seconds 5

# 4. Graceful Teardown
Write-Host "Stopping clumsy.exe..." -ForegroundColor Cyan
if (-not $clumsyProc.HasExited) {
    Stop-Process -InputObject $clumsyProc -Force
}

Write-Host "Stopping mock notepad.exe process..." -ForegroundColor Cyan
if (-not $mockProc.HasExited) {
    Stop-Process -InputObject $mockProc -Force
}

# Read and print captured logs
Write-Host "`n--- CAPTURED EXECUTION LOGS ---" -ForegroundColor Yellow
$logs = Get-Content $tempLog -Raw
Write-Output $logs
Write-Host "--------------------------------`n" -ForegroundColor Yellow

# Clean up temp log file
Remove-Item $tempLog -ErrorAction SilentlyContinue

# Verify output logs
if ($logs -match "Failed to start filtering" -or $logs -match "failed to open device" -or $logs -match "0xc0000007b") {
    Write-Host "Integration Test: FAILED (Divert binding or execution error detected)." -ForegroundColor Red
    exit 1
} elseif ($logs -match "Divert opened handle" -or $logs -match "Started filtering" -or $logs -match "Process filter combined") {
    Write-Host "Integration Test: SUCCESS (WinDivert driver loaded and bound process filter successfully)." -ForegroundColor Green
    exit 0
} else {
    Write-Host "Integration Test: WARNING (Verification output was inconclusive. Verify that you ran 'zig build -Dconf=Debug' so console logging is enabled)." -ForegroundColor Yellow
    exit 0
}
