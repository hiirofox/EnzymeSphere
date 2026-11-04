$ErrorActionPreference = "Stop"

# 当前脚本目录
$taskDir = $PSScriptRoot

# 检查 ffmpeg
$ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue

if (!$ffmpeg) {
    Write-Host "错误：找不到 ffmpeg，请先将 ffmpeg 加入 PATH。" -ForegroundColor Red
    exit 1
}

# 获取所有 WAV 文件
$wavFiles = @(
    Get-ChildItem -LiteralPath $taskDir -Filter "*.wav" -File |
    Sort-Object Name
)

Write-Host "`n========== WAV 文件列表 ==========" -ForegroundColor Cyan

foreach ($wav in $wavFiles) {
    Write-Host $wav.Name
}

Write-Host "=================================="
Write-Host "总文件数: $($wavFiles.Count)`n"

$success = 0
$failed = 0

foreach ($wav in $wavFiles) {

    $oldName = $wav.BaseName
    $oldPath = $wav.FullName

    # 生成临时文件
    $tempPath = Join-Path $taskDir "$oldName.tmp.wav"

    Write-Host "`n[转换] $($wav.Name)" -ForegroundColor Yellow

    try {
        # ffmpeg 转换采样率
        & $ffmpeg.Source `
            -hide_banner `
            -loglevel error `
            -y `
            -i $oldPath `
            -ar 22050 `
            -c:a pcm_s16le `
            $tempPath

        if ($LASTEXITCODE -ne 0 -or !(Test-Path $tempPath)) {
            throw "ffmpeg 转换失败"
        }

        # 转换成功，替换原文件
        Move-Item -LiteralPath $tempPath -Destination $oldPath -Force

        Write-Host "  采样率转换成功: 22050 Hz" -ForegroundColor Green

        # 提取文件名前面的数字
        if ($oldName -match '^(\d+)(.*)$') {

            $number = $Matches[1]
            $suffix = $Matches[2]

            # 不足3位补0
            if ($number.Length -lt 3) {

                $newName = $number.PadLeft(3, '0') + $suffix
                $newPath = Join-Path $taskDir "$newName.wav"

                # 防止重命名覆盖已有文件
                if (Test-Path -LiteralPath $newPath) {
                    Write-Host "  [跳过重命名] 目标文件已存在: $newName" `
                        -ForegroundColor Red
                }
                else {
                    Rename-Item -LiteralPath $oldPath -NewName "$newName.wav"

                    Write-Host "  重命名: $oldName.wav -> $newName.wav" `
                        -ForegroundColor Green
                }
            }
            else {
                Write-Host "  文件名无需修改: $oldName.wav"
            }
        }

        $success++
    }
    catch {
        Write-Host "  [失败] $oldName : $_" -ForegroundColor Red
        $failed++

        # 清理临时文件
        if (Test-Path -LiteralPath $tempPath) {
            Remove-Item -LiteralPath $tempPath -Force
        }
    }
}

Write-Host "`n========== 处理完成 ==========" -ForegroundColor Cyan
Write-Host "成功: $success" -ForegroundColor Green
Write-Host "失败: $failed" -ForegroundColor Red
Write-Host "总计: $($wavFiles.Count)"