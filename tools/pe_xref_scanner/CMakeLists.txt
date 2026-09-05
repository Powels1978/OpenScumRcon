set(TARGET PeXrefScanner)

add_executable(${TARGET} main.cpp)
target_compile_features(${TARGET} PRIVATE cxx_std_23)
target_link_libraries(${TARGET} PRIVATE Zydis)
