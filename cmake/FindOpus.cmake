# Prefer vcpkg's config package so runtime DLL dependencies are visible to CMake.
find_package(Opus CONFIG QUIET)

if(TARGET Opus::opus)
    set(OPUS_FOUND TRUE)
    set(OPUS_INCLUDE_DIRS "")
    set(OPUS_LIBRARIES Opus::opus)
    if(NOT TARGET Opus::Opus)
        add_library(Opus::Opus ALIAS Opus::opus)
    endif()
else()
    find_path(OPUS_INCLUDE_DIR
        NAMES opus/opus.h
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/Opus/include
            ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/vcpkg/installed/x64-windows/include
    )
    find_library(OPUS_LIBRARY
        NAMES opus
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/Opus/lib
            ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/vcpkg/installed/x64-windows/lib
    )
    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(Opus
        REQUIRED_VARS OPUS_LIBRARY OPUS_INCLUDE_DIR
    )
    if(OPUS_FOUND)
        set(OPUS_INCLUDE_DIRS ${OPUS_INCLUDE_DIR})
        set(OPUS_LIBRARIES ${OPUS_LIBRARY})
        if(NOT TARGET Opus::Opus)
            add_library(Opus::Opus UNKNOWN IMPORTED)
            set_target_properties(Opus::Opus PROPERTIES
                IMPORTED_LOCATION "${OPUS_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${OPUS_INCLUDE_DIR}"
            )
        endif()
    endif()
endif()

mark_as_advanced(OPUS_INCLUDE_DIR OPUS_LIBRARY)
