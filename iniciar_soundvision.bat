@echo off
title SoundVision AI - Engine de Visao Computacional Assistiva
color 0B

echo ======================================================================
echo           SOUNDVISION AI - INICIALIZADOR DE VISÃO COMPUTACIONAL
echo ======================================================================
echo.

taskkill /F /IM SoundVisionEngine.exe >nul 2>&1

echo [1/3] Compilando motor SoundVision AI (.NET WPF)...
cd vision_engine
dotnet build SoundVisionEngine.csproj
if %errorlevel% neq 0 (
    color 0C
    echo [ERRO] Falha ao compilar o projeto SoundVisionEngine.
    pause
    exit /b 1
)
echo [OK] Compilacao concluida com sucesso!
echo.

echo [2/3] Verificando navegadores do Playwright...
powershell.exe -ExecutionPolicy Bypass -File bin/Debug/net10.0-windows/playwright.ps1 install chromium >nul 2>&1
echo [OK] Playwright Chromium pronto!
echo.

echo [3/3] Iniciando o aplicativo SoundVision Engine...
echo ======================================================================
echo  SoundVision Engine rodando e aguardando comandos da ESP32-CAM e Site!
echo ======================================================================
echo.

dotnet run --no-build
cd ..
