set(TARGET OpenScumRconNative)

add_library(${TARGET} SHARED
    src/dllmain.cpp
    src/godmode_trace.cpp
    src/godmode_dispatch.cpp
    src/admin_dispatch.cpp
    src/rcon_protocol.cpp
    src/rcon_server.cpp
)

target_compile_features(${TARGET} PRIVATE cxx_std_23)
target_link_libraries(${TARGET} PUBLIC UE4SS)

if(WIN32)
    target_link_libraries(${TARGET} PUBLIC Ws2_32)
endif()

# Standalone parser checks; no live SCUM process needed.
add_executable(OpenScumGodModeRequestTests tests/godmode_request_tests.cpp)
target_compile_features(OpenScumGodModeRequestTests PRIVATE cxx_std_17)
target_include_directories(OpenScumGodModeRequestTests PRIVATE src)

add_executable(OpenScumCommandAuthorityTests tests/command_authority_tests.cpp)
target_compile_features(OpenScumCommandAuthorityTests PRIVATE cxx_std_17)
target_include_directories(OpenScumCommandAuthorityTests PRIVATE src)
