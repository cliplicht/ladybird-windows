add_library(ladybird_platform_windows SHARED
        win32/src/win_platform.cpp
        win32/src/win_window_gdi.cpp
        win32/src/win_window_d3d.cpp
        win32/src/win_eventloop.cpp
        win32/src/win_timers.cpp
        win32/src/win_fs.cpp
        win32/src/win_tasks.cpp
        win32/src/win_log.cpp
        win32/src/win_net.cpp
        win32/src/win_clipboard.cpp
)

target_include_directories(ladybird_platform_windows PUBLIC core/include)
target_compile_definitions(ladybird_platform_windows PRIVATE UNICODE _UNICODE)
target_link_libraries(ladybird_platform_windows PRIVATE user32 gdi32 d3d11 dxgi winhttp imm32 shell32 ole32)

set_target_properties(ladybird_platform_windows PROPERTIES
        OUTPUT_NAME "ladybird_platform_windows"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        WINDOWS_EXPORT_ALL_SYMBOLS ON
)
