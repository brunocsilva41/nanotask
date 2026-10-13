cmake_minimum_required(VERSION 3.25)
project(NanoTask LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fonte única de versão: arquivo VERSION (semver). CI sobrescreve via -DAPP_VERSION_*.
file(READ "${CMAKE_SOURCE_DIR}/VERSION" APP_VERSION_RAW)
string(STRIP "${APP_VERSION_RAW}" APP_VERSION_STR)
if(NOT APP_VERSION_MAJOR)
  string(REPLACE "." ";" _ver "${APP_VERSION_STR}")
  list(GET _ver 0 APP_VERSION_MAJOR)
  list(GET _ver 1 APP_VERSION_MINOR)
  list(GET _ver 2 APP_VERSION_PATCH)
endif()
if(NOT APP_VERSION_BUILD)
  set(APP_VERSION_BUILD 0)
endif()

# RC do MSVC: com o ambiente msvc-dev-cmd no CI, ninja usa o rc.exe correto
add_executable(NanoTask WIN32
    src/main.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/res/version.rc
)

# version.rc gerado: versão vem do arquivo VERSION (ou -DAPP_VERSION_* do CI).
# configure_file elimina expansão de macro no rc.exe (fonte de falhas).
set(CMAKE_SOURCE_DIR_FOR_RC ${CMAKE_SOURCE_DIR})
configure_file(res/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/res/version.rc @ONLY)

target_compile_definitions(NanoTask PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    NTDDI_VERSION=0x0A00000B _WIN32_WINNT=0x0A00
    APP_VERSION_MAJOR=${APP_VERSION_MAJOR}
    APP_VERSION_MINOR=${APP_VERSION_MINOR}
    APP_VERSION_PATCH=${APP_VERSION_PATCH}
    APP_VERSION_BUILD=${APP_VERSION_BUILD}
    APP_VERSION_STR="${APP_VERSION_STR}"
)

# Manifest asInvoker + DPI awareness (sem UAC no boot)
if(MSVC)
  target_link_options(NanoTask PRIVATE
    /MANIFEST:EMBED
    /MANIFESTINPUT:${CMAKE_SOURCE_DIR}/res/app.manifest
    /OPT:REF /OPT:ICF
  )
  target_compile_options(NanoTask PRIVATE /W4 /WX /permissive-)
endif()

target_link_libraries(NanoTask PRIVATE
    user32 gdi32 shell32 advapi32 pdh iphlpapi dnsapi powrprof
    winhttp bcrypt shell32 ole32 ws2_32
)

# Instalação portable (o instalador Inno usa os mesmos arquivos)
install(TARGETS NanoTask RUNTIME DESTINATION .)
install(FILES config/settings.defaults.json DESTINATION config)
install(DIRECTORY assets/tray DESTINATION assets)

# NanoTask.UI (WinUI3) é projeto separado WinAppSDK unpackaged.
# Ver src/UiLauncher.hpp + docs/TASKBAR_MODES.md. Não compila no host headless.
