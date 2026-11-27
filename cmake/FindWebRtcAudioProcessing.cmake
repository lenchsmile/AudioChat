# Find a WebRTC Audio Processing backend.
#
# Preferred: the current vcpkg "webrtc" port, which exports
# unofficial::webrtc::webrtc. The old webrtc-audio-processing port name is not
# available in current vcpkg and uses a different standalone API.

find_package(unofficial-webrtc CONFIG QUIET)
if(NOT TARGET unofficial::webrtc::webrtc)
    find_package(webrtc CONFIG QUIET)
endif()

set(_webrtc_apm_found FALSE)
if(TARGET unofficial::webrtc::webrtc)
    set(_webrtc_apm_found TRUE)
    set(WebRtcAudioProcessing_INCLUDE_DIRS "")
    set(WebRtcAudioProcessing_LIBRARIES unofficial::webrtc::webrtc)
else()
    find_path(WebRtcAudioProcessing_INCLUDE_DIR
        NAMES api/audio/builtin_audio_processing_builder.h
        PATHS
            $ENV{WEBRTC_ROOT}/include
            /usr/include
            /usr/local/include
            ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/webrtc/include
    )
    find_library(WebRtcAudioProcessing_LIBRARY
        NAMES webrtc
        PATHS
            $ENV{WEBRTC_ROOT}/lib
            /usr/lib
            /usr/local/lib
            ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/webrtc/lib
    )
    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(WebRtcAudioProcessing
        REQUIRED_VARS WebRtcAudioProcessing_LIBRARY WebRtcAudioProcessing_INCLUDE_DIR
    )
    set(_webrtc_apm_found ${WebRtcAudioProcessing_FOUND})
    if(_webrtc_apm_found)
        set(WebRtcAudioProcessing_INCLUDE_DIRS ${WebRtcAudioProcessing_INCLUDE_DIR})
        set(WebRtcAudioProcessing_LIBRARIES ${WebRtcAudioProcessing_LIBRARY})
    endif()
endif()

if(_webrtc_apm_found)
    if(NOT TARGET WebRtcAudioProcessing::WebRtcAudioProcessing)
        if(TARGET unofficial::webrtc::webrtc)
            add_library(WebRtcAudioProcessing::WebRtcAudioProcessing ALIAS unofficial::webrtc::webrtc)
        else()
            add_library(WebRtcAudioProcessing::WebRtcAudioProcessing UNKNOWN IMPORTED)
            set_target_properties(WebRtcAudioProcessing::WebRtcAudioProcessing PROPERTIES
                IMPORTED_LOCATION "${WebRtcAudioProcessing_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${WebRtcAudioProcessing_INCLUDE_DIR}"
            )
        endif()
    endif()
    set(WebRtcAudioProcessing_FOUND TRUE)
    message(STATUS "Found WebRTC Audio Processing backend")
else()
    set(WebRtcAudioProcessing_FOUND FALSE)
    message(STATUS "WebRTC Audio Processing not found")
    message(STATUS "Install the vcpkg 'webrtc' port or enable the audiochat[aec] feature")
endif()

mark_as_advanced(
    WebRtcAudioProcessing_INCLUDE_DIR
    WebRtcAudioProcessing_LIBRARY
)
