# Prefer vcpkg's config package so runtime DLL dependencies are visible to CMake.
find_package(portaudio CONFIG QUIET)

if(TARGET portaudio)
    set(PORTAUDIO_FOUND TRUE)
    set(PORTAUDIO_INCLUDE_DIRS "")
    set(PORTAUDIO_LIBRARIES portaudio)
    if(NOT TARGET PortAudio::PortAudio)
        add_library(PortAudio::PortAudio ALIAS portaudio)
    endif()
elseif(TARGET PortAudio::PortAudio)
    set(PORTAUDIO_FOUND TRUE)
    set(PORTAUDIO_LIBRARIES PortAudio::PortAudio)
else()
    find_path(PORTAUDIO_INCLUDE_DIR
        NAMES portaudio.h
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/PortAudio/include
            ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/vcpkg/installed/x64-windows/include
    )
    find_library(PORTAUDIO_LIBRARY
        NAMES portaudio portaudio-2.0
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/PortAudio/lib
            ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/vcpkg/installed/x64-windows/lib
    )
    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(PortAudio
        REQUIRED_VARS PORTAUDIO_LIBRARY PORTAUDIO_INCLUDE_DIR
    )
    if(PORTAUDIO_FOUND)
        set(PORTAUDIO_INCLUDE_DIRS ${PORTAUDIO_INCLUDE_DIR})
        set(PORTAUDIO_LIBRARIES ${PORTAUDIO_LIBRARY})
        if(NOT TARGET PortAudio::PortAudio)
            add_library(PortAudio::PortAudio UNKNOWN IMPORTED)
            set_target_properties(PortAudio::PortAudio PROPERTIES
                IMPORTED_LOCATION "${PORTAUDIO_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${PORTAUDIO_INCLUDE_DIR}"
            )
        endif()
    endif()
endif()

mark_as_advanced(PORTAUDIO_INCLUDE_DIR PORTAUDIO_LIBRARY)
