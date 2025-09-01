add_cmake_project(json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.zip
    URL_HASH SHA256=34660b5e9a407195d55e8da705ed26cc6d175ce5a6b1fb957e701fb4d5b04022

    # This fixes a clear mistake in v3.12.0, which is already fixed in the develop branch.
    # Most probably this can be removed with the next version!
    PATCH_COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/json.patch

    CMAKE_ARGS
        -DJSON_BuildTests=OFF

        # This is recommanded by the author, as in the future it will be the default.
        -DJSON_ImplicitConversions=OFF
)
