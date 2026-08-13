# ---- 翻译（.ts -> .qm）----
# 五种常用语言：简体中文、英语、西班牙语、法语、德语。
# 源码字符串以 en_US 为源；en_US 的 .ts 保持源文本（未翻译，作为回退）。
# 新增/修改界面文案后：
#   lupdate ../src -ts dtk-update_en_US.ts dtk-update_zh_CN.ts \
#       dtk-update_es.ts dtk-update_fr.ts dtk-update_de.ts -source-language en_US
#   python3 _gen.py   # 复用已有译文回填（可选，仅当需批量刷新）
find_package(Qt${QT_VERSION_MAJOR}LinguistTools QUIET)

set(TRANSLATION_FILES
    dtk-update_zh_CN.ts
    dtk-update_en_US.ts
    dtk-update_es.ts
    dtk-update_fr.ts
    dtk-update_de.ts
)

if(Qt${QT_VERSION_MAJOR}LinguistTools_FOUND OR QT_LCONVERT_EXECUTABLE)
    qt_add_translation(QM_FILES ${TRANSLATION_FILES})
    add_custom_target(translations ALL DEPENDS ${QM_FILES})
    install(FILES ${QM_FILES}
        DESTINATION ${TRANSLATION_INSTALL_DIR}
    )
else()
    # 退路：直接用 lrelease 生成 .qm（CI 镜像无 LinguistTools 模块时）
    message(WARNING "Qt LinguistTools not found, falling back to lrelease for translations")
    find_program(LRELEASE_EXECUTABLE lrelease)
    set(QM_FILES)
    foreach(ts ${TRANSLATION_FILES})
        string(REPLACE ".ts" ".qm" qm_name "${ts}")
        set(qm "${CMAKE_CURRENT_BINARY_DIR}/${qm_name}")
        add_custom_command(
            OUTPUT ${qm}
            COMMAND ${LRELEASE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/${ts} -qm ${qm}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${ts}
            COMMENT "Generating ${qm}"
        )
        list(APPEND QM_FILES ${qm})
    endforeach()
    add_custom_target(translations ALL DEPENDS ${QM_FILES})
    install(FILES ${QM_FILES}
        DESTINATION ${TRANSLATION_INSTALL_DIR}
    )
endif()
