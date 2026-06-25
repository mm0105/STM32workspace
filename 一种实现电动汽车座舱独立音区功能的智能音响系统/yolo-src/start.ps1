param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$MainArgs
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

$venvCandidates = @(".venv", "venv")
$pythonBin = $null
foreach ($venv in $venvCandidates) {
    $candidate = Join-Path $ScriptDir "$venv\Scripts\python.exe"
    if (Test-Path -LiteralPath $candidate) {
        $pythonBin = $candidate
        break
    }
}

if (-not $pythonBin) {
    Write-Host "未找到虚拟环境目录（.venv 或 venv）。"
    Write-Host "请先创建并安装依赖："
    Write-Host "  py -m venv .venv"
    Write-Host "  .\.venv\Scripts\python.exe -m pip install -r requirements.txt"
    exit 1
}

Write-Host "Python: $pythonBin"
& $pythonBin --version

$checkScript = @'
import sys
mods = [
    "torch",
    "torchvision",
    "cv2",
    "ultralytics",
    "numpy",
    "scipy",
    "onnx",
    "onnxruntime",
    "serial",
    "pygame",
]
missing = []
for mod in mods:
    try:
        __import__(mod)
    except Exception:
        missing.append(mod)
if missing:
    print("缺失依赖: " + ", ".join(missing))
    print("当前 Python: " + sys.executable)
    sys.exit(1)
'@

& $pythonBin -c $checkScript
if ($LASTEXITCODE -ne 0) {
    Write-Host "检测到缺失依赖，正在安装 requirements.txt ..."
    & $pythonBin -m pip install -r requirements.txt
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
else {
    Write-Host "所有依赖已就绪"
}

& $pythonBin main.py @MainArgs
exit $LASTEXITCODE
