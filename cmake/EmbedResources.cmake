# Embeds data files into a C++ target as byte arrays.
#
#   baastik_embed_resources(<target>
#       NAMESPACE <c++ namespace>
#       BASE_DIR  <dir that resource names are relative to>
#       FILES     <file>...)
#
# Generates <namespace>/Resources.h with:
#   std::span<const Resource> all();
#   std::optional<std::string_view> find(std::string_view name);
# Names are paths relative to BASE_DIR with forward slashes.

set(_BAASTIK_EMBED_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/EmbedResourcesScript.cmake")

function(baastik_embed_resources target)
    cmake_parse_arguments(ARG "" "NAMESPACE;BASE_DIR" "FILES" ${ARGN})
    string(REPLACE "::" "/" ns_dir "${ARG_NAMESPACE}")
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/embedded/${target}")
    set(header "${out_dir}/${ns_dir}/Resources.h")
    set(source "${out_dir}/${ns_dir}/Resources.cpp")

    set(names "")
    foreach(file IN LISTS ARG_FILES)
        file(RELATIVE_PATH rel "${ARG_BASE_DIR}" "${file}")
        list(APPEND names "${rel}")
    endforeach()

    # Pass lists with a separator that survives shell quoting.
    string(REPLACE ";" "|" files_arg "${ARG_FILES}")
    string(REPLACE ";" "|" names_arg "${names}")

    add_custom_command(
        OUTPUT "${header}" "${source}"
        COMMAND ${CMAKE_COMMAND}
            "-DNAMESPACE=${ARG_NAMESPACE}"
            "-DHEADER=${header}"
            "-DSOURCE=${source}"
            "-DFILES=${files_arg}"
            "-DNAMES=${names_arg}"
            -P "${_BAASTIK_EMBED_SCRIPT}"
        DEPENDS ${ARG_FILES} "${_BAASTIK_EMBED_SCRIPT}"
        COMMENT "Embedding resources for ${target}"
        VERBATIM)

    target_sources(${target} PRIVATE "${header}" "${source}")
    target_include_directories(${target} PUBLIC "${out_dir}")
endfunction()
