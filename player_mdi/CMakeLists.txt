find_package(SDL2 REQUIRED)

add_executable(player_mdi
    src/main.cpp
)

target_link_libraries(player_mdi
    PRIVATE avcdec SDL2::SDL2
)
