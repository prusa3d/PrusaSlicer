# l10n
set(L10N_DIR "${SLIC3R_RESOURCES_DIR}/localization")

add_custom_target(gettext_make_pot
    COMMAND xgettext
        --keyword=L
        --keyword=_L
        --keyword=_u8L
        --keyword=L_CONTEXT:1,2c
        --keyword=_ctx_u8L:1,2c
        --keyword=_L_PLURAL:1,2
        --add-comments=TRN
        --from-code=UTF-8
        --debug
        --boost
        -f "${L10N_DIR}/list.txt"
        -o "${L10N_DIR}/PrusaSlicer.pot"
    COMMAND hintsToPot "${SLIC3R_RESOURCES_DIR}" "${L10N_DIR}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Generate pot file from strings in the source tree"
    VERBATIM
)

add_dependencies(gettext_make_pot hintsToPot)

add_custom_target(gettext_merge_community_po_with_pot
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Merge community po with new generated pot file"
)

file(GLOB L10N_PO_FILES "${L10N_DIR}/*/PrusaSlicer*.po")

# list of names of directories, which are localized by PS internally
list(APPEND PS_L10N_DIRS "cs" "de" "es" "fr" "it" "ja" "pl")

foreach(po_file ${L10N_PO_FILES})
    get_filename_component(po_dir "${po_file}" DIRECTORY)
    get_filename_component(po_dir_name "${po_dir}" NAME)

    list(FIND PS_L10N_DIRS "${po_dir_name}" found_dir_id)

    # found_dir_id == -1 means that po_dir_name wasn't found in PS_L10N_DIRS
    if(found_dir_id LESS 0)
        add_custom_command(
            TARGET gettext_merge_community_po_with_pot PRE_BUILD
            COMMAND msgmerge -N -o "${po_file}" "${po_file}" "${L10N_DIR}/PrusaSlicer.pot"
            # delete obsolete lines from resulting PO to avoid conflicts after a merging of it with wxWidgets.po
            COMMAND msgattrib --no-obsolete -o "${po_file}" "${po_file}"
            VERBATIM
        )
    endif()
endforeach()

add_custom_target(gettext_concat_wx_po_with_po
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Concatenate and merge wxWidgets localization po with PrusaSlicer po file"
)

file(GLOB L10N_PO_FILES "${L10N_DIR}/*/PrusaSlicer*.po")
foreach(po_file ${L10N_PO_FILES})
    get_filename_component(po_dir "${po_file}" DIRECTORY)
    get_filename_component(po_dir_name "${po_dir}" NAME)

    set(wx_po_file "${L10N_DIR}/wx_locale/${po_dir_name}.po")
    # set(po_new_file "${po_dir}/PrusaSlicer_.po")

    add_custom_command(
        TARGET gettext_concat_wx_po_with_po PRE_BUILD
        COMMAND msgcat --use-first -o "${po_file}" "${po_file}" "${wx_po_file}"
        # delete obsolete lines from resulting PO
        COMMAND msgattrib --no-obsolete -o "${po_file}" "${po_file}"
        VERBATIM
    )
endforeach()

add_custom_target(gettext_po_to_mo
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Generate localization mo files (binary) from po files (texts)"
)

file(GLOB L10N_PO_FILES "${L10N_DIR}/*/PrusaSlicer*.po")
foreach(po_file ${L10N_PO_FILES})
    get_filename_component(po_dir "${po_file}" DIRECTORY)

    set(mo_file "${po_dir}/PrusaSlicer.mo")

    add_custom_command(
        TARGET gettext_po_to_mo PRE_BUILD
        COMMAND msgfmt --check-format -o "${mo_file}" "${po_file}"
        # COMMAND msgfmt --check-compatibility -o "${mo_file}" "${po_file}"
        VERBATIM
    )
endforeach()