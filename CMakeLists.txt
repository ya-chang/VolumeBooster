cmake_minimum_required(VERSION 3.20)
project(VolumeBooster LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ========== APO DLL ==========
add_library(VolumeBoosterAPO SHARED
    src/APO/VolumeBoosterAPO.cpp
    src/APO/DllMain.cpp
)

target_include_directories(VolumeBoosterAPO PRIVATE src/APO)

target_link_libraries(VolumeBoosterAPO PRIVATE
    ole32 oleaut32 uuid advapi32 kernel32 user32
)

set_target_properties(VolumeBoosterAPO PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    PREFIX ""
)

target_compile_definitions(VolumeBoosterAPO PRIVATE
    WIN32_LEAN_AND_MEAN UNICODE _UNICODE
)

# ========== Device Listener Service ==========
add_executable(DeviceListener
    src/DeviceListener/DeviceListener.cpp
)

target_link_libraries(DeviceListener PRIVATE
    ole32 oleaut32 uuid advapi32 kernel32 user32
)

set_target_properties(DeviceListener PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    OUTPUT_NAME "DeviceListener"
)

target_compile_definitions(DeviceListener PRIVATE
    WIN32_LEAN_AND_MEAN UNICODE _UNICODE
)

# ========== Install ==========
install(TARGETS VolumeBoosterAPO DeviceListener
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
)

install(FILES src/APO/VolumeBoosterAPO.h DESTINATION include)
