param(
    [string]$ServerDir = "",
    [int]$HealthTimeoutSeconds = 120,
    [switch]$CheckOnly,
    [switch]$NoStartBackend,
    [switch]$NoStartServer
)

$ErrorActionPreference = "Stop"

function Write-Step($Message) { Write-Host "[..] $Message" -ForegroundColor Cyan }
function Write-Ok($Message) { Write-Host "[OK] $Message" -ForegroundColor Green }
function Write-Warn($Message) { Write-Host "[WARN] $Message" -ForegroundColor Yellow }
function Write-Fail($Message) { Write-Host "[FAIL] $Message" -ForegroundColor Red }

function Resolve-FullPath($Path) {
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function Find-ServerDir {
    param([string]$Requested)
    if ($Requested) {
        return Resolve-FullPath $Requested
    }
    $candidates = @(
        (Get-Location).Path,
        $PSScriptRoot,
        (Join-Path $PSScriptRoot "build\Debug"),
        (Join-Path $PSScriptRoot "build\Release"),
        (Join-Path $PSScriptRoot "out\build\x64-Debug"),
        (Join-Path $PSScriptRoot "out\build\x64-Release")
    )
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path (Join-Path $candidate "DDNet-Server.exe"))) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }
    return ""
}

function Read-DotEnv {
    param([string]$Path)
    $values = @{}
    if (!(Test-Path $Path)) {
        return $values
    }
    foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
        $trimmed = $line.Trim()
        if (!$trimmed -or $trimmed.StartsWith("#")) {
            continue
        }
        $index = $trimmed.IndexOf("=")
        if ($index -lt 1) {
            continue
        }
        $key = $trimmed.Substring(0, $index).Trim()
        $value = $trimmed.Substring($index + 1).Trim()
        if (($value.StartsWith('"') -and $value.EndsWith('"')) -or ($value.StartsWith("'") -and $value.EndsWith("'"))) {
            $value = $value.Substring(1, $value.Length - 2)
        }
        $values[$key] = $value
    }
    return $values
}

function Env-Value {
    param([hashtable]$EnvValues, [string]$Name, [string]$Default = "")
    if ($EnvValues.ContainsKey($Name) -and $EnvValues[$Name] -ne "") {
        return $EnvValues[$Name]
    }
    return $Default
}

function Test-TcpPort {
    param([string]$HostName, [int]$Port, [int]$TimeoutMs = 1200)
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $task = $client.ConnectAsync($HostName, $Port)
        if (!$task.Wait($TimeoutMs)) {
            return $false
        }
        return $client.Connected
    }
    catch {
        return $false
    }
    finally {
        $client.Dispose()
    }
}

function Get-WebSocketEndpoint {
    param([string]$Url)
    try {
        $uri = [Uri]$Url
        $port = $uri.Port
        if ($port -lt 0) {
            if ($uri.Scheme -eq "wss") {
                $port = 443
            }
            else {
                $port = 80
            }
        }
        return @{ Host = $uri.Host; Port = $port; Scheme = $uri.Scheme }
    }
    catch {
        return $null
    }
}

function Invoke-Health {
    param([string]$Url)
    try {
        return Invoke-RestMethod -Uri $Url -TimeoutSec 2
    }
    catch {
        return $null
    }
}

function Escape-SingleQuotedPowerShellString {
    param([string]$Text)
    return $Text.Replace("'", "''")
}

function Start-PowerShellWindow {
    param([string]$Title, [string]$WorkingDirectory, [string]$Command)
    $safeTitle = Escape-SingleQuotedPowerShellString $Title
    $safeDir = Escape-SingleQuotedPowerShellString $WorkingDirectory
    $script = "`$Host.UI.RawUI.WindowTitle = '$safeTitle'; Set-Location -LiteralPath '$safeDir'; $Command"
    return Start-Process -FilePath "powershell.exe" -ArgumentList @(
        "-NoExit",
        "-ExecutionPolicy", "Bypass",
        "-Command", $script
    ) -WorkingDirectory $WorkingDirectory -PassThru
}

function Wait-Health {
    param([string]$Url, [int]$TimeoutSeconds)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $health = Invoke-Health $Url
        if ($health -and $health.status -eq "ok") {
            return $health
        }
        Start-Sleep -Seconds 2
    } while ((Get-Date) -lt $deadline)
    return $null
}

$serverDir = Find-ServerDir $ServerDir
if (!$serverDir) {
    Write-Fail "DDNet-Server.exe was not found. Run from the build output directory or pass -ServerDir."
    exit 1
}

Write-Step "Using server directory: $serverDir"
$serverExe = Join-Path $serverDir "DDNet-Server.exe"
$backendScript = Join-Path $serverDir "mds.py"
$patcherExe = Join-Path $serverDir "music_map_patcher.exe"
$requirements = Join-Path $serverDir "requirements.txt"
$envExample = Join-Path $serverDir ".env.example"
$envFile = Join-Path $serverDir ".env"

$requiredFiles = @($serverExe, $backendScript, $patcherExe, $requirements)
foreach ($file in $requiredFiles) {
    if (!(Test-Path -LiteralPath $file)) {
        Write-Fail "Missing required same-directory file: $file"
        Write-Host "Build game-server first, or place mds.py / music_map_patcher.exe / requirements.txt next to DDNet-Server.exe."
        exit 1
    }
}
Write-Ok "DDNet-Server.exe same-directory check passed"

if (!(Test-Path -LiteralPath $envFile)) {
    if (Test-Path -LiteralPath $envExample) {
        Copy-Item -LiteralPath $envExample -Destination $envFile
        Write-Warn ".env was missing, created from .env.example: $envFile"
    }
    else {
        Write-Fail ".env is missing and .env.example was not found."
        exit 1
    }
}
else {
    Write-Ok ".env exists"
}

$envValues = Read-DotEnv $envFile
$hostName = Env-Value $envValues "BPMUSIC_HOST" "127.0.0.1"
$portText = Env-Value $envValues "BPMUSIC_PORT" "5000"
$port = 5000
if (![int]::TryParse($portText, [ref]$port)) {
    Write-Warn "BPMUSIC_PORT=$portText is invalid; checking port 5000 instead"
    $port = 5000
}
$healthUrl = "http://$hostName`:$port/health"

$ffmpeg = Get-Command "ffmpeg.exe" -ErrorAction SilentlyContinue
if (!$ffmpeg) {
    $ffmpeg = Get-Command "ffmpeg" -ErrorAction SilentlyContinue
}
if ($ffmpeg) {
    Write-Ok "FFmpeg: $($ffmpeg.Source)"
}
else {
    Write-Fail "FFmpeg was not found. Install FFmpeg and add it to PATH."
    exit 1
}

$qqEnabled = (Env-Value $envValues "BPMUSIC_QQBOT_ENABLED" "0").ToLowerInvariant() -in @("1", "true", "yes", "on")
if ($qqEnabled) {
    $wsUrl = Env-Value $envValues "BPMUSIC_QQBOT_WS_URL" "ws://127.0.0.1:3001"
    $wsEndpoint = Get-WebSocketEndpoint $wsUrl
    if (!$wsEndpoint) {
        Write-Fail "BPMUSIC_QQBOT_WS_URL could not be parsed: $wsUrl"
        exit 1
    }
    if (!(Env-Value $envValues "BPMUSIC_QQBOT_GROUP_IDS" "")) {
        Write-Warn "QQ Bot is enabled but BPMUSIC_QQBOT_GROUP_IDS is empty; backend will refuse to start QQ Bot."
    }
    if (Test-TcpPort $wsEndpoint.Host $wsEndpoint.Port) {
        Write-Ok "NapCat WebSocket port is reachable: $wsUrl"
    }
    else {
        Write-Warn "NapCat WebSocket is not reachable: $wsUrl. Backend will reconnect; start and log in to NapCat for QQ features."
    }
}
else {
    Write-Ok "QQ Bot is disabled; skipping NapCat WebSocket check"
}

$python = Get-Command "python.exe" -ErrorAction SilentlyContinue
if (!$python) {
    $python = Get-Command "python" -ErrorAction SilentlyContinue
}
if (!$python) {
    Write-Fail "Python was not found. Install Python 3.11 or newer."
    exit 1
}
Write-Ok "Python: $($python.Source)"

$health = Invoke-Health $healthUrl
if ($health -and $health.status -eq "ok") {
    Write-Ok "mds.py health is already online: $healthUrl"
}
elseif ($CheckOnly -or $NoStartBackend) {
    Write-Warn "mds.py health is not available: $healthUrl"
}
else {
    Write-Step "Starting mds.py backend window"
    Start-PowerShellWindow -Title "BauPlayerMusic backend" -WorkingDirectory $serverDir -Command "python .\mds.py"
    $health = Wait-Health $healthUrl $HealthTimeoutSeconds
    if ($health -and $health.status -eq "ok") {
        Write-Ok "mds.py health passed: $healthUrl"
    }
    else {
        Write-Fail "Timed out waiting for mds.py health: $healthUrl"
        Write-Host "If the backend window is waiting for NetEase login, finish login, press Enter there, then run this launcher again or start DDNet-Server.exe manually."
        exit 1
    }
}

if ($health) {
    if ($health.ffmpeg -eq $true) {
        Write-Ok "Backend FFmpeg check passed"
    }
    else {
        Write-Warn "Backend /health says FFmpeg is unavailable; check backend window PATH."
    }
    if ($health.map_patcher) {
        Write-Ok "Backend map patcher: $($health.map_patcher)"
    }
    else {
        Write-Warn "Backend /health did not find music_map_patcher."
    }
    if ($health.qqbot -and $qqEnabled) {
        if ($health.qqbot.connected) {
            Write-Ok "QQ Bot is connected to NapCat"
        }
        else {
            Write-Warn "QQ Bot is not connected to NapCat: $($health.qqbot.last_error)"
        }
    }
}

if ($CheckOnly) {
    Write-Ok "Check complete; server was not started."
    exit 0
}

if ($NoStartServer) {
    Write-Ok "Skipping DDNet-Server.exe startup by request."
    exit 0
}

Write-Step "Starting DDNet-Server.exe"
Start-Process -FilePath $serverExe -WorkingDirectory $serverDir | Out-Null
Write-Ok "Startup complete. Backend: $healthUrl; server dir: $serverDir"
