set(_THIS_MODULE_BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")

find_package(Python3 COMPONENTS Interpreter REQUIRED)

function(compile_glsl_aux shader_stage shader_name glsl_filename output target_env)
    set(options)
    set(oneValueArgs KEY_SUFFIX)
    set(multiValueArgs DEFINES)
    cmake_parse_arguments(arg "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    string(TOUPPER ${shader_stage} shader_stage_upper)
    # KEY_SUFFIX selects the shader name the C++ code looks up (multiview / non-multiview variants)
    set(shader_key "${shader_name}${arg_KEY_SUFFIX}")
    cmake_path(REPLACE_FILENAME output "${shader_key}.spv" OUTPUT_VARIABLE SPV_FILE)

    file(APPEND ${output} "\
{ \"${shader_key}\", {
#include \"${shader_key}.spv\"
}},
")

    if (WIVRN_DEBUG_SHADERS)
        set(SHADER_FLAGS -gVS)
    else()
        set(SHADER_FLAGS )
    endif()

    # Extra preprocessor defines, e.g. -DWIVRN_MULTIVIEW=1
    string(JOIN " " wivrn_shader_defines ${arg_DEFINES})
    if(ANDROID)
        add_custom_command(
            OUTPUT "${SPV_FILE}-nopt"
            COMMAND glslangValidator -V --target-env ${target_env} -S ${shader_stage} -D${shader_stage_upper}_SHADER ${wivrn_shader_defines} ${glsl_filename} ${SHADER_FLAGS} -o "${SPV_FILE}-nopt" --depfile "${SPV_FILE}.d"
            DEPENDS "${glsl_filename}"
            DEPFILE "${SPV_FILE}.d"
            VERBATIM
        )
    else()
        add_custom_command(
            OUTPUT "${SPV_FILE}-nopt"
            COMMAND Vulkan::glslangValidator -V --target-env ${target_env} -S ${shader_stage} -D${shader_stage_upper}_SHADER ${wivrn_shader_defines} ${glsl_filename} ${SHADER_FLAGS} -o "${SPV_FILE}-nopt" --depfile "${SPV_FILE}.d"
            DEPENDS "${glsl_filename}"
            DEPFILE "${SPV_FILE}.d"
            VERBATIM
        )
    endif()

    if (WIVRN_OPTIMIZE_SHADERS)
        add_custom_command(
            OUTPUT "${SPV_FILE}-opt"
            COMMAND spirv-opt --target-env=${target_env} -O "${SPV_FILE}-nopt" -o "${SPV_FILE}-opt"
            DEPENDS "${SPV_FILE}-nopt"
            VERBATIM
        )
        set(_spv "${SPV_FILE}-opt")
    else()
        set(_spv "${SPV_FILE}-nopt")
    endif()
    # SPIR-V is a sequence of 32-bit little-endian words and the shaders map is
    # std::vector<uint32_t>. Emit one uint32 per 4-byte word. The previous
    # hexdump-based generation emitted one uint32 per byte, so the first word
    # was 0x00000003 (the magic byte 0x03) instead of 0x07230203, failing
    # reflection at runtime. Use a deterministic Python converter instead.
    add_custom_command(
        OUTPUT "${SPV_FILE}"
        COMMAND "${Python3_EXECUTABLE}" "${_THIS_MODULE_BASE_DIR}/spv2c.py" "${_spv}" > "${SPV_FILE}"
        DEPENDS "${_spv}" "${_THIS_MODULE_BASE_DIR}/spv2c.py"
        VERBATIM
    )

    get_source_file_property(deps "${output}" OBJECT_DEPENDS)
    if (deps)
        set(deps "${deps};")
    else()
        set(deps "")
    endif()
    set_source_files_properties(${output} OBJECT_DEPENDS "${deps}${SPV_FILE}")

endfunction()



function(wivrn_compile_glsl target)
    set(options MULTIVIEW_VARIANTS)
    set(oneValueArgs TARGET_ENV NAMESPACE)
    set(multiValueArgs SHADERS)
    cmake_parse_arguments(_args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT _args_TARGET_ENV)
        set(_args_TARGET_ENV vulkan1.0)
    endif()

    if (_args_NAMESPACE)
        set(_namespace_begin "namespace ${_args_NAMESPACE} {")
        set(_namespace_end "}")
    endif()

    cmake_path(APPEND OUTPUT "${CMAKE_CURRENT_BINARY_DIR}" "${target}_shaders.cpp")

    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${target}_shaders.h" "\
#include <cstdint>
#include <map>
#include <vector>
#include <string>
${_namespace_begin}
extern const std::map<std::string, std::vector<uint32_t>> shaders;
${_namespace_end}")

    file(WRITE ${OUTPUT} "\
#include <cstdint>
#include <map>
#include <vector>
#include <string>
${_namespace_begin}
extern const std::map<std::string, std::vector<uint32_t>> shaders = {
")

    foreach(in_file IN LISTS _args_SHADERS)
        if (in_file MATCHES "\.\(vert|frag|tesc|tese|geom|comp\)\(\.glsl\)?$")
            set(shader_stage ${CMAKE_MATCH_1})
            cmake_path(GET in_file STEM LAST_ONLY shader_name)
            if (_args_MULTIVIEW_VARIANTS)
                compile_glsl_aux(${shader_stage} ${shader_name} ${in_file} ${OUTPUT} ${_args_TARGET_ENV} DEFINES -DWIVRN_MULTIVIEW=1)
                compile_glsl_aux(${shader_stage} ${shader_name} ${in_file} ${OUTPUT} ${_args_TARGET_ENV} KEY_SUFFIX .nomv)
            else()
                compile_glsl_aux(${shader_stage} ${shader_name} ${in_file} ${OUTPUT} ${_args_TARGET_ENV} DEFINES -DWIVRN_MULTIVIEW=1)
            endif()
        else()
            cmake_path(GET in_file STEM LAST_ONLY shader_name)
            if (_args_MULTIVIEW_VARIANTS)
                compile_glsl_aux(vert ${shader_name}.vert ${in_file} ${OUTPUT} ${_args_TARGET_ENV} DEFINES -DWIVRN_MULTIVIEW=1)
                compile_glsl_aux(frag ${shader_name}.frag ${in_file} ${OUTPUT} ${_args_TARGET_ENV} DEFINES -DWIVRN_MULTIVIEW=1)
                compile_glsl_aux(vert ${shader_name}.vert ${in_file} ${OUTPUT} ${_args_TARGET_ENV} KEY_SUFFIX .nomv)
                compile_glsl_aux(frag ${shader_name}.frag ${in_file} ${OUTPUT} ${_args_TARGET_ENV} KEY_SUFFIX .nomv)
            else()
                compile_glsl_aux(vert ${shader_name}.vert ${in_file} ${OUTPUT} ${_args_TARGET_ENV} DEFINES -DWIVRN_MULTIVIEW=1)
                compile_glsl_aux(frag ${shader_name}.frag ${in_file} ${OUTPUT} ${_args_TARGET_ENV} DEFINES -DWIVRN_MULTIVIEW=1)
            endif()
        endif()


    endforeach()

    file(APPEND ${OUTPUT} "};${_namespace_end}")

    target_sources(${target} PRIVATE ${OUTPUT})

    # Target-level dependency: the generated ${target}_shaders.cpp #includes every
    # ${shader_key}.spv. OBJECT_DEPENDS alone does not guarantee the hexdump
    # custom commands have run before this translation unit is compiled (a fresh
    # configure can compile it against empty/absent .spv files, embedding zero-size
    # SPIR-V that fails reflection at runtime). Add a custom target that depends on
    # all generated .spv outputs and make the library depend on it.
    set(_wivrn_shader_spv_files "")
    foreach(in_file IN LISTS _args_SHADERS)
        if (in_file MATCHES "\.\(vert|frag|tesc|tese|geom|comp\)\(\.glsl\)?$")
            cmake_path(GET in_file STEM LAST_ONLY _shader_name)
            if (_args_MULTIVIEW_VARIANTS)
                list(APPEND _wivrn_shader_spv_files "${CMAKE_CURRENT_BINARY_DIR}/${_shader_name}.spv")
                list(APPEND _wivrn_shader_spv_files "${CMAKE_CURRENT_BINARY_DIR}/${_shader_name}.nomv.spv")
            else()
                list(APPEND _wivrn_shader_spv_files "${CMAKE_CURRENT_BINARY_DIR}/${_shader_name}.spv")
            endif()
        else()
            cmake_path(GET in_file STEM LAST_ONLY _shader_name)
            if (_args_MULTIVIEW_VARIANTS)
                list(APPEND _wivrn_shader_spv_files "${CMAKE_CURRENT_BINARY_DIR}/${_shader_name}.vert.spv")
                list(APPEND _wivrn_shader_spv_files "${CMAKE_CURRENT_BINARY_DIR}/${_shader_name}.frag.spv")
                list(APPEND _wivrn_shader_spv_files "${CMAKE_CURRENT_BINARY_DIR}/${_shader_name}.vert.nomv.spv")
                list(APPEND _wivrn_shader_spv_files "${CMAKE_CURRENT_BINARY_DIR}/${_shader_name}.frag.nomv.spv")
            else()
                list(APPEND _wivrn_shader_spv_files "${CMAKE_CURRENT_BINARY_DIR}/${_shader_name}.vert.spv")
                list(APPEND _wivrn_shader_spv_files "${CMAKE_CURRENT_BINARY_DIR}/${_shader_name}.frag.spv")
            endif()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _wivrn_shader_spv_files)
    add_custom_target(${target}_shader_spv DEPENDS ${_wivrn_shader_spv_files})
    add_dependencies(${target} ${target}_shader_spv)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

endfunction()
