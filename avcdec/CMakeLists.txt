add_library(avcdec STATIC
    src/AvcDecoder.cpp
)

target_include_directories(avcdec
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(avcdec PRIVATE jmdec)
