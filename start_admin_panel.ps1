param(
    [switch]$NoBrowser
)

$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $PSScriptRoot

if (!(Test-Path -LiteralPath ".env")) {
    if (!(Test-Path -LiteralPath ".env.example")) {
        throw "找不到 .env.example，无法初始化管理面板配置。"
    }
    Copy-Item -LiteralPath ".env.example" -Destination ".env"
    Write-Host "已从 .env.example 创建 .env。" -ForegroundColor Yellow
}

$python = $null
$venvPython = Join-Path $PSScriptRoot ".venv\Scripts\python.exe"
if (Test-Path -LiteralPath $venvPython) {
    $python = $venvPython
}
else {
    $command = Get-Command "python.exe" -ErrorAction SilentlyContinue
    if (!$command) {
        $command = Get-Command "python" -ErrorAction SilentlyContinue
    }
    if ($command) {
        $python = $command.Source
    }
}

if (!$python) {
    throw "未找到 Python。请安装 Python 3.11+，或先在当前目录创建 .venv。"
}

$arguments = @("admin_panel.py")
if (!$NoBrowser) {
    $arguments += "--open-browser"
}

Write-Host "正在启动 BauPlayerMusic 管理面板…" -ForegroundColor Cyan
& $python @arguments
