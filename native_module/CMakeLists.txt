set(TARGET OpenScumRconNative)

add_library(${TARGET} SHARED
    src/dllmain.cpp
    src/godmode_trace.cpp
    src/godmode_dispatch.cpp
    src/registry_dispatch.cpp
    src/response_capture.cpp
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

add_executable(OpenScumDispatchInfrastructureTests tests/dispatch_infrastructure_tests.cpp src/rcon_protocol.cpp)
target_compile_features(OpenScumDispatchInfrastructureTests PRIVATE cxx_std_17)
target_include_directories(OpenScumDispatchInfrastructureTests PRIVATE src)
