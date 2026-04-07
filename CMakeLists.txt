cmake_minimum_required(VERSION 3.16)
project(lenia_cpp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(lenia_core STATIC
    src/lenia_board.cpp
    src/lenia_automaton.cpp
    src/lenia_analyzer.cpp
    src/lenia_recorder.cpp
    src/lenia_cppn.cpp
    src/lenia_app.cpp
)
target_include_directories(lenia_core PUBLIC include)
target_compile_options(lenia_core PRIVATE -O3 -march=native -ffast-math)

add_executable(lenia src/main.cpp)
target_link_libraries(lenia PRIVATE lenia_core)
target_compile_options(lenia PRIVATE -O3 -march=native -ffast-math)

# Server mode (JSON stdout for web UI)
add_executable(lenia_server src/lenia_server_mode.cpp)
target_link_libraries(lenia_server PRIVATE lenia_core)
target_compile_options(lenia_server PRIVATE -O3 -march=native -ffast-math)

# Interactive GUI (X11)
find_package(X11)
if(X11_FOUND)
    add_executable(lenia_gui src/lenia_gui_x11.cpp)
    target_link_libraries(lenia_gui PRIVATE lenia_core X11::X11)
    target_compile_options(lenia_gui PRIVATE -O3 -march=native -ffast-math)
endif()
