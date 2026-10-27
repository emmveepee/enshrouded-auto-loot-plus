@echo off
setlocal
cd /d "%~dp0.."
where cl >nul 2>nul
if errorlevel 1 (
  if not defined VCVARS64 (echo Set VCVARS64 or use an x64 Native Tools Command Prompt. & exit /b 1)
  call "%VCVARS64%"
  if errorlevel 1 exit /b 1
)
if not exist build mkdir build
pushd build
for %%T in (animal_produce_test menu_config_test corpse_transfer_policy_test corpse_menu_recovery_test pet_exclusion_test loot_aim_test native_transfer_command_test resource_tail_policy_test radius_config_test backpack_settings_test corpse_dispatch_hitch_test) do (
 cl /nologo /O2 /MT /EHsc /std:c++17 /W4 ..\src\client\%%T.cpp /Fe:%%T.exe /link user32.lib advapi32.lib
 if errorlevel 1 exit /b 1
 %%T.exe
 if errorlevel 1 exit /b 1
)
cl /nologo /O2 /MT /EHsc /std:c++17 /W4 ..\tests\native_collector_safety_test.cpp /Fe:native_collector_safety_test.exe /link user32.lib
if errorlevel 1 exit /b 1
native_collector_safety_test.exe
if errorlevel 1 exit /b 1
popd

