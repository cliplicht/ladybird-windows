add_executable(lbw_bootstrap main_win.cpp)

target_include_directories(lbw_bootstrap PRIVATE
        ${CMAKE_SOURCE_DIR}/platform/include
)

target_link_libraries(lbw_bootstrap PRIVATE user32 gdi32)

# GUI subsystem (no console window)
target_link_options(lbw_bootstrap PRIVATE "/SUBSYSTEM:WINDOWS")

# Place the EXE in a shared bin directory
set_target_properties(lbw_bootstrap PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Copy the platform DLL next to the EXE after build
add_custom_command(TARGET lbw_bootstrap POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:ladybird_platform_windows>
        $<TARGET_FILE_DIR:lbw_bootstrap>
)
