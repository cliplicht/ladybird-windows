add_library(ladybird_platform_windows SHARED
        src/win_platform.cpp
        src/win_window_gdi.cpp
        src/win_eventloop.cpp
        src/win_timers.cpp
        src/win_fs.cpp
)

target_include_directories(ladybird_platform_windows PUBLIC include)
target_compile_definitions(ladybird_platform_windows PRIVATE UNICODE _UNICODE)
target_link_libraries(ladybird_platform_windows PRIVATE user32 gdi32)

set_target_properties(ladybird_platform_windows PROPERTIES
        OUTPUT_NAME "ladybird_platform_windows"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        WINDOWS_EXPORT_ALL_SYMBOLS ON
)
