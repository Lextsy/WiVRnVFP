# AGP hands CMake a backslashed ANDROID_NDK. openxr_loader globs
# ${ANDROID_NDK}/shader-tools with CONFIGURE_DEPENDS, and CMake writes that
# pattern verbatim into VerifyGlobs.cmake, where "\P" is an invalid escape.
# The toolchain file FORCE-sets ANDROID_NDK, so a -D on the command line loses.
# This hook runs after the toolchain and before any subdirectory is added, so
# normalizing the path here fixes every downstream glob.
if(ANDROID_NDK)
    file(TO_CMAKE_PATH "${ANDROID_NDK}" ANDROID_NDK)
endif()
