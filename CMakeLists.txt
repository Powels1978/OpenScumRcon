cmake_minimum_required(VERSION 3.22)

project(OpenScumRcon LANGUAGES CXX)

# UE4SS-Entwicklungsbaum wird nicht mitversioniert (eigene, große Historie).
# Erwartet unter vendor/RE-UE4SS/ oder per -DUE4SS_DIR=<pfad> beim Konfigurieren.
if(NOT DEFINED UE4SS_DIR)
    set(UE4SS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/RE-UE4SS")
endif()

if(EXISTS "${UE4SS_DIR}/CMakeLists.txt")
    add_subdirectory(${UE4SS_DIR} RE-UE4SS)
else()
    message(FATAL_ERROR
        "UE4SS-Entwicklungsbaum nicht gefunden unter ${UE4SS_DIR}. "
        "Siehe README.md: separat beziehen und unter vendor/RE-UE4SS/ ablegen, "
        "oder -DUE4SS_DIR=<pfad> setzen.")
endif()

add_subdirectory(native_module)
add_subdirectory(tools/pe_xref_scanner)
