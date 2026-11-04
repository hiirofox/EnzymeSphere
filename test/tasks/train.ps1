$ErrorActionPreference = "Stop"

# 当前脚本所在目录
$taskDir = $PSScriptRoot

# app.exe 路径
$appPath = Join-Path $taskDir "app.exe"

# 最大并发进程数
$maxParallel = 6

# 获取所有 WAV 文件
$wavFiles = @(
    Get-ChildItem -LiteralPath $taskDir -Filter "*.wav" -File |
    Sort-Object Name
)

# 检查 app.exe
if (!(Test-Path $appPath)) {
    Write-Error "找不到 app.exe: $appPath"
    exit 1
}

# 列出所有训练任务
Write-Host "`n========== WAV 训练任务列表 ==========" -ForegroundColor Cyan

foreach ($wav in $wavFiles) {
    Write-Host $wav.BaseName
}

Write-Host "======================================"
Write-Host "总任务数: $($wavFiles.Count)"
Write-Host "最大并发数: $maxParallel`n"

if ($wavFiles.Count -eq 0) {
    Write-Host "没有找到 WAV 文件。"
    exit 0
}

# 当前正在运行的进程
$running = New-Object System.Collections.ArrayList

# 下一个待处理任务的索引
$next = 0

# 主调度循环
while ($next -lt $wavFiles.Count -or $running.Count -gt 0) {

    # 检查已经结束的进程
    for ($i = $running.Count - 1; $i -ge 0; $i--) {

        $item = $running[$i]
        $process = $item.Process

        if ($process.HasExited) {

            Write-Host "[完成] $($item.Name) ExitCode=$($process.ExitCode)" `
                -ForegroundColor Green

            [void]$running.RemoveAt($i)
        }
    }

    # 如果有空闲位置，启动新的训练任务
    while ($next -lt $wavFiles.Count -and
           $running.Count -lt $maxParallel) {

        $wav = $wavFiles[$next]
        $next++

        Write-Host "[启动] $($wav.BaseName)" -ForegroundColor Yellow

        try {
            $process = Start-Process `
                -FilePath $appPath `
                -ArgumentList ('"{0}"' -f $wav.BaseName) `
                -WorkingDirectory $taskDir `
                -NoNewWindow `
                -PassThru

            [void]$running.Add(
                [PSCustomObject]@{
                    Name    = $wav.BaseName
                    Process = $process
                }
            )
        }
        catch {
            Write-Host "[失败] $($wav.BaseName): $_" -ForegroundColor Red
        }
    }

    # 避免 CPU 空转
    if ($running.Count -gt 0) {
        Start-Sleep -Milliseconds 500
    }
}

Write-Host "`n========== 所有训练任务完成 ==========" -ForegroundColor Cyan