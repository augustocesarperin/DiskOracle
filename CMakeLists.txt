cmake_minimum_required(VERSION 3.10)
project(HDGuardian C)
set(CMAKE_C_STANDARD 11)

include_directories(include)
add_executable(hdguardian \
    src/main.c \
    src/pal_linux.c \
    src/pal_windows.c \
    src/pal_macos.c \
    src/smart.c \
    src/surface.c \
    src/predict.c \
    src/report.c \
    src/logging.c)

add_executable(test_smart tests/test_smart.c src/smart.c)
add_executable(test_surface tests/test_surface.c src/surface.c)
add_executable(test_predict tests/test_predict.c src/predict.c)
add_executable(test_report tests/test_report.c src/report.c src/predict.c)
