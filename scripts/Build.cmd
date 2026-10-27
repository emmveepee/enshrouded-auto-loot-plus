@echo off
setlocal
cd /d "%~dp0.."
where cl >nul 2>nul
if errorlevel 1 (
  if not defined VCVARS64 (
    echo Run from an x64 Native Tools Command Prompt or set VCVARS64 to vcvars64.bat.
    exit /b 1
  )
  call "%VCVARS64%"
  if errorlevel 1 exit /b 1
)
if not defined IMGUI_DIR (echo Set IMGUI_DIR to Dear ImGui v1.91.9b. & exit /b 1)
if not defined VULKAN_INCLUDE_DIR (echo Set VULKAN_INCLUDE_DIR to Vulkan-Headers v1.4.309 include directory. & exit /b 1)
if not exist build mkdir build
pushd build
cl /nologo /O2 /MT /EHsc /std:c++17 /W4 /utf-8 /LD ..\src\client\client_requests.cpp /Fo:Bootstrap.obj /link /OUT:ClientLootRequests.asi kernel32.lib user32.lib advapi32.lib
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++17 /W4 /utf-8 /DCLIENT_REQUESTS_EXTENSION /LD ..\src\client\client_requests.cpp /Fo:Helper169.obj /link /OUT:Probe169.dll kernel32.lib user32.lib advapi32.lib
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++17 /W3 /utf-8 /DNDEBUG /DIMGUI_IMPL_VULKAN_NO_PROTOTYPES /I"%IMGUI_DIR%" /I"%VULKAN_INCLUDE_DIR%" /LD ..\src\menu\main.cpp ..\src\menu\overlay.cpp "%IMGUI_DIR%\imgui.cpp" "%IMGUI_DIR%\imgui_draw.cpp" "%IMGUI_DIR%\imgui_tables.cpp" "%IMGUI_DIR%\imgui_widgets.cpp" "%IMGUI_DIR%\backends\imgui_impl_vulkan.cpp" /link /DEF:..\src\menu\exports.def /OUT:AutoLootPlus.asi kernel32.lib user32.lib
if errorlevel 1 exit /b 1
popd
copy /y build\ClientLootRequests.asi package\GameFiles\ClientLootRequests.asi >nul
copy /y build\AutoLootPlus.asi package\GameFiles\AutoLootPlus.asi >nul
copy /y build\Probe169.dll package\GameFiles\mods\AutoLootClient\Probe169.dll >nul
echo Build169 complete. Package contains the freshly built safety helper.


