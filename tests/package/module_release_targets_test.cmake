if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "module_release_targets_test.cmake requires PROJECT_SOURCE_DIR")
endif()

file(GLOB_RECURSE manifests LIST_DIRECTORIES FALSE
    "${PROJECT_SOURCE_DIR}/modules/*/package.luau")

foreach(manifest_path IN LISTS manifests)
    file(READ "${manifest_path}" manifest)
    string(FIND "${manifest}" "implementation =" implementation_position)
    if(implementation_position LESS 0)
        continue()
    endif()

    string(REGEX MATCH "release[ 	]*=[ 	]*\\{[ 	]*\"windows/x64\"" release_target_match "${manifest}")
    if(NOT release_target_match)
        message(FATAL_ERROR "Native module must publish the implemented windows/x64 target: ${manifest_path}")
    endif()
endforeach()
