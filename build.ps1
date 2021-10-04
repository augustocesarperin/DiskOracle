Write-Host "Starting compilation..."
gcc -o hdfix.exe src/main.c src/pal_windows.c src/logging.c src/smart.c src/info.c src/report.c src/surface.c src/predict.c src/nvme_cache.c src/nvme_benchmark.c src/nvme_alerts.c src/nvme_export.c -Iinclude -lsetupapi -Wall -Wextra -DDEBUG
if ($LASTEXITCODE -eq 0) {
    Write-Host "Compilation successful: hdfix.exe created."
} else {
    Write-Host "Compilation failed. Exit code: $LASTEXITCODE"
}
