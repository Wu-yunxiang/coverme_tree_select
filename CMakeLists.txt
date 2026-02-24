cmake_minimum_required(VERSION 3.16)
project(coverme_tree_select LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

find_program(CLANG_BIN NAMES clang REQUIRED)
find_program(OPT_BIN NAMES opt REQUIRED)
find_program(LLVM_CONFIG_BIN NAMES llvm-config REQUIRED)

execute_process(
    COMMAND ${LLVM_CONFIG_BIN} --version
    OUTPUT_VARIABLE LLVM_VERSION_STR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX MATCH "^[0-9]+" LLVM_VERSION_MAJOR_STR "${LLVM_VERSION_STR}")
if(LLVM_VERSION_MAJOR_STR LESS 18)
    message(FATAL_ERROR "LLVM >= 18 is required, found ${LLVM_VERSION_STR}")
endif()

execute_process(
    COMMAND ${LLVM_CONFIG_BIN} --cxxflags
    OUTPUT_VARIABLE LLVM_CXXFLAGS_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
separate_arguments(LLVM_CXXFLAGS NATIVE_COMMAND "${LLVM_CXXFLAGS_RAW}")

execute_process(
    COMMAND ${LLVM_CONFIG_BIN} --ldflags
    OUTPUT_VARIABLE LLVM_LDFLAGS_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
separate_arguments(LLVM_LDFLAGS NATIVE_COMMAND "${LLVM_LDFLAGS_RAW}")

execute_process(
    COMMAND ${LLVM_CONFIG_BIN} --libs core analysis support transformutils scalaropts passes
    OUTPUT_VARIABLE LLVM_LIBS_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
separate_arguments(LLVM_LIBS NATIVE_COMMAND "${LLVM_LIBS_RAW}")

execute_process(
    COMMAND ${LLVM_CONFIG_BIN} --system-libs
    OUTPUT_VARIABLE LLVM_SYSTEM_LIBS_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
separate_arguments(LLVM_SYSTEM_LIBS NATIVE_COMMAND "${LLVM_SYSTEM_LIBS_RAW}")

file(STRINGS "${CMAKE_SOURCE_DIR}/target_input.txt" TARGET_INPUT_LINES)
list(LENGTH TARGET_INPUT_LINES TARGET_INPUT_LEN)
if(TARGET_INPUT_LEN LESS 2)
    message(FATAL_ERROR "target_input.txt must provide absolute source path in line1 and function name in line2")
endif()

list(GET TARGET_INPUT_LINES 0 TARGET_SOURCE_PATH)
list(GET TARGET_INPUT_LINES 1 TARGET_FUNCTION_NAME)
string(STRIP "${TARGET_SOURCE_PATH}" TARGET_SOURCE_PATH)
string(STRIP "${TARGET_FUNCTION_NAME}" TARGET_FUNCTION_NAME)

if(NOT EXISTS "${TARGET_SOURCE_PATH}")
    message(FATAL_ERROR "Target source does not exist: ${TARGET_SOURCE_PATH}")
endif()

set(INSERT_PEN_SO "${CMAKE_BINARY_DIR}/insert_pen.so")
add_custom_command(
    OUTPUT "${INSERT_PEN_SO}"
    COMMAND ${CMAKE_CXX_COMPILER} -shared -fPIC
            "${CMAKE_SOURCE_DIR}/src/insert_module/insert_pen.cpp"
            -o "${INSERT_PEN_SO}"
            ${LLVM_CXXFLAGS} ${LLVM_LDFLAGS} ${LLVM_LIBS} ${LLVM_SYSTEM_LIBS}
    DEPENDS "${CMAKE_SOURCE_DIR}/src/insert_module/insert_pen.cpp"
    COMMENT "Building LLVM insert pass plugin"
    VERBATIM
)
add_custom_target(insert_pen ALL DEPENDS "${INSERT_PEN_SO}")

set(TARGET_BC "${CMAKE_BINARY_DIR}/target.bc")
set(TARGET_PEN_BC "${CMAKE_BINARY_DIR}/target.pen.bc")
set(TARGET_PEN_OBJ "${CMAKE_BINARY_DIR}/target.pen.o")

add_custom_command(
    OUTPUT "${TARGET_PEN_OBJ}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/output"
    COMMAND ${CLANG_BIN} -emit-llvm -c -fPIC -Xclang -disable-O0-optnone "${TARGET_SOURCE_PATH}" -o "${TARGET_BC}"
    COMMAND ${OPT_BIN} -load-pass-plugin "${INSERT_PEN_SO}" -passes=insert-pen -funcname=${TARGET_FUNCTION_NAME} "${TARGET_BC}" -o "${TARGET_PEN_BC}"
    COMMAND ${CLANG_BIN} -fPIC -c "${TARGET_PEN_BC}" -o "${TARGET_PEN_OBJ}"
    DEPENDS insert_pen "${TARGET_SOURCE_PATH}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Instrumenting target source and generating object"
    VERBATIM
)

add_custom_target(instrument_target ALL DEPENDS "${TARGET_PEN_OBJ}")

add_library(coverage SHARED
    src/data_structure/branch_tree.cpp
    src/data_structure/prepare_for_update.cpp
    src/data_structure/select_priority.cpp
    src/insert_module/pen.cpp
    src/insert_module/interface_for_py.cpp
    "${TARGET_PEN_OBJ}"
)

target_include_directories(coverage PRIVATE include)
add_dependencies(coverage instrument_target)

set_target_properties(coverage PROPERTIES OUTPUT_NAME _coverage)
