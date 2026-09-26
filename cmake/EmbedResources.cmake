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

    # The file list goes through a file: long command lines break on Windows.
    set(list_file "${out_dir}/${ns_dir}/resources.list")
    set(list_content "")
    foreach(file name IN ZIP_LISTS ARG_FILES names)
        string(APPEND list_content "${file}|${name}\n")
    endforeach()
    file(WRITE "${list_file}.tmp" "${list_content}")
    configure_file("${list_file}.tmp" "${list_file}" COPYONLY)

    add_custom_command(
        OUTPUT "${header}" "${source}"
        COMMAND ${CMAKE_COMMAND}
            "-DNAMESPACE=${ARG_NAMESPACE}"
            "-DHEADER=${header}"
            "-DSOURCE=${source}"
            "-DLIST_FILE=${list_file}"
        -P "${_BAASTIK_EMBED_SCRIPT}"
        DEPENDS ${ARG_FILES} "${list_file}" "${_BAASTIK_EMBED_SCRIPT}"
        COMMENT "Embedding resources for ${target}"
        VERBATIM)

    target_sources(${target} PRIVATE "${header}" "${source}")
    target_include_directories(${target} PUBLIC "${out_dir}")
endfunction()
