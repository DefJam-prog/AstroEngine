@echo off
setlocal
"%~dp0astro_game_ui_demo.exe" --project-root "%~dp0game-data\puzzle-campaign" --campaign starter %*
exit /b %errorlevel%
