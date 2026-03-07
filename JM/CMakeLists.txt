file(GLOB JM_LDECOD_SRC ldecod/src/*.c)
file(GLOB JM_LCOMMON_SRC lcommon/src/*.c)

# Remove decoder_test.c from sources
list(REMOVE_ITEM JM_LDECOD_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/ldecod/src/decoder_test.c
)

add_library(jmdec STATIC
    ${JM_LDECOD_SRC}
    ${JM_LCOMMON_SRC}
)

target_include_directories(jmdec PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ldecod/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/lcommon/inc
)
