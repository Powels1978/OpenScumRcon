set(TARGET OpenScumRconNative)

add_library(${TARGET} SHARED
    src/dllmain.cpp
    src/admin_dispatch.cpp
    src/rcon_protocol.cpp
    src/rcon_server.cpp
)

target_compile_features(${TARGET} PRIVATE cxx_std_23)
target_link_libraries(${TARGET} PUBLIC UE4SS)

if(WIN32)
    target_link_libraries(${TARGET} PUBLIC Ws2_32)
endif()
