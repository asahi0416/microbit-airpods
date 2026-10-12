@echo off
chcp 65001 > nul
echo ========================================
echo micro:bit v1 AirPods Notifier 書き込みツール
echo ========================================

cd /d "%~dp0"

echo [1/2] スケッチをコンパイルしています...
.\tools\arduino-cli.exe compile -b sandeepmistry:nRF5:BBCmicrobit:softdevice=none --output-dir build microbit_airpods
if %ERRORLEVEL% neq 0 (
    echo [エラー] コンパイルに失敗しました。
    pause
    exit /b 1
)

echo [2/2] micro:bit (COM5) に書き込んでいます...
.\tools\arduino-cli.exe upload -p COM5 -b sandeepmistry:nRF5:BBCmicrobit:softdevice=none microbit_airpods
if %ERRORLEVEL% neq 0 (
    echo [情報] COM5での書き込みに失敗したため、MICROBITドライブへの直接コピーを試みます...
    if exist "E:\DETAILS.TXT" (
        copy /Y "build\microbit_airpods.ino.hex" "E:\"
        echo [完了] Eドライブへの書き込みが完了しました！
    ) else (
        echo [エラー] micro:bit が見つかりません。USB接続を確認してください。
        pause
        exit /b 1
    )
) else (
    echo [完了] 書き込みが正常に完了しました！
)

pause
