# In-process OpenXR runtime shim over the HTC WaveVR C API.
#
# The VIVE Focus Plus ships no Khronos OpenXR loader (API 25 has no OpenXR
# broker), so the client links this target in place of openxr_loader: it
# exports the OpenXR entry points the client calls and drives WaveVR through
# dlopen("libwvr_api.so").
add_library(wivrn_wave STATIC)

target_sources(wivrn_wave PRIVATE
    wave_api.cpp
    wave_math.cpp
    wave_instance.cpp
    wave_session.cpp
    wave_swapchain.cpp
    wave_vulkan.cpp
    wave_input.cpp
    wave_dispatch.cpp
)

target_include_directories(wivrn_wave PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The client and the shim must agree on the OpenXR struct layouts, which
# depend on these defines; they are PUBLIC so both sides get the same set.
target_include_directories(wivrn_wave SYSTEM PUBLIC ${openxr_loader_SOURCE_DIR}/include)
target_compile_definitions(wivrn_wave PUBLIC
    XR_USE_PLATFORM_ANDROID
    XR_USE_TIMESPEC=1
    XR_USE_GRAPHICS_API_VULKAN
    VK_USE_PLATFORM_ANDROID_KHR
)

target_link_libraries(wivrn_wave PUBLIC
    glm::glm
    spdlog::spdlog
)
