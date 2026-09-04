set(TARGET OpenScumRconNative)

add_library(${TARGET} SHARED
    src/dllmain.cpp
)

target_compile_features(${TARGET} PRIVATE cxx_std_23)
target_link_libraries(${TARGET} PUBLIC UE4SS)
