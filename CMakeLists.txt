cmake_minimum_required(VERSION 3.10)
project(DiskOracle C)
set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

if(MSVC)
    add_compile_options(/Zp8)
endif()

if(MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /FS")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /FS")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /DEBUG")
endif()

set(EXECUTABLE_NAME diskoracle)

# Diretório de include principal
include_directories(include)

# Lista completa de arquivos fonte do projeto
set(MAIN_SOURCES
    src/main.c
    src/smart.c
    src/surface.c
    src/info.c
    src/predict.c
    src/report.c
    src/style.c
    src/logging.c
    src/nvme_export.c
    src/nvme_alerts.c
    src/nvme_benchmark.c
    src/nvme_cache.c
    src/nvme_orchestrator.c
    src/pal_shared_nvme.c
    src/commands.c
)

# Adiciona a implementação da PAL correta para a plataforma
if(WIN32)
    list(APPEND MAIN_SOURCES src/pal_windows.c)
elseif(UNIX AND NOT APPLE)
    list(APPEND MAIN_SOURCES src/pal_linux.c)
elseif(APPLE)
    list(APPEND MAIN_SOURCES src/pal_macos.c)
else()
    message(FATAL_ERROR "Unsupported platform.")
endif()

add_executable(${EXECUTABLE_NAME} ${MAIN_SOURCES})

# Bibliotecas específicas da plataforma
if(WIN32)
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE ws2_32 setupapi)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE pthread udev)
elseif(APPLE)
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE pthread CoreFoundation IOKit)
endif()

# Define a flag de DEBUG para builds de depuração
target_compile_definitions(${EXECUTABLE_NAME} PRIVATE "$<$<CONFIG:Debug>:DEBUG>")

# Opcional: alvo de instalação
install(TARGETS ${EXECUTABLE_NAME} DESTINATION bin)
