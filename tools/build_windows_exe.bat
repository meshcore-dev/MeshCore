@echo off
setlocal

cd /d "%~dp0\.."

python -m pip install --upgrade pip
python -m pip install pyinstaller pyserial esptool
python -m PyInstaller --onefile --console --name meshcore-configurator tools\meshcore_configurator.py

echo.
echo Built: dist\meshcore-configurator.exe
