project(tiger-compiler)

cmake_minimum_required(VERSION 3.2)

if ("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
    message(FATAL_ERROR "In-source builds are not allowed.")
endif ()

find_program(FLEXCPP flexc++)
if (NOT FLEXCPP)
    message(FATAL_ERROR "Program flexc++ not found!")
endif ()

find_program(BISONCPP bisonc++)
if (NOT BISONCPP)
    message(FATAL_ERROR "Program bisonc++ not found!")
endif ()

set(CMAKE_CXX_STANDARD 17)

include_directories(src)
include_directories(src/tiger/lex)
include_directories(src/tiger/parse)

file(GLOB SLP_SOURCES "src/straightline/*.cc")

file(GLOB TIGER_SOURCES
        "src/tiger/symbol/*.cc"
        "src/tiger/absyn/*.cc"
        "src/tiger/errormsg/*.cc"
        "src/tiger/env/*.cc"
        "src/tiger/escape/*.cc"
        "src/tiger/semant/*.cc"
        "src/tiger/frame/*.cc"
        "src/tiger/translate/*.cc"
        "src/tiger/canon/*.cc"
        "src/tiger/codegen/*.cc"
        "src/tiger/liveness/*.cc"
        "src/tiger/regalloc/*.cc"
        "src/tiger/output/*.cc"
        )

# Lab2 needs the parser's token declarations, not the unfinished Lab3 parser.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(LAB2_TOKEN_DIR "${PROJECT_BINARY_DIR}/generated/tiger/parse")
file(MAKE_DIRECTORY "${LAB2_TOKEN_DIR}")
include_directories(BEFORE "${PROJECT_BINARY_DIR}/generated")
set(LAB2_LEX_SOURCES
    "${PROJECT_SOURCE_DIR}/src/tiger/lex/lex.cc"
    "${PROJECT_SOURCE_DIR}/src/tiger/lex/scannerbase.h"
    "${LAB2_TOKEN_DIR}/parserbase.h"
    "${PROJECT_SOURCE_DIR}/src/tiger/errormsg/errormsg.cc")
set_source_files_properties(
    "${PROJECT_SOURCE_DIR}/src/tiger/lex/lex.cc"
    "${PROJECT_SOURCE_DIR}/src/tiger/lex/scannerbase.h"
    "${LAB2_TOKEN_DIR}/parserbase.h" PROPERTIES GENERATED TRUE)
add_custom_command(
    OUTPUT "${LAB2_TOKEN_DIR}/parserbase.h"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/lab2_token_grammar.py"
        "${PROJECT_SOURCE_DIR}/src/tiger/parse/tiger.y" "${LAB2_TOKEN_DIR}/tokens.y"
    COMMAND "${BISONCPP}" tokens.y
    DEPENDS "${PROJECT_SOURCE_DIR}/scripts/lab2_token_grammar.py"
        "${PROJECT_SOURCE_DIR}/src/tiger/parse/tiger.y"
    WORKING_DIRECTORY "${LAB2_TOKEN_DIR}")
add_custom_command(
    OUTPUT "${PROJECT_SOURCE_DIR}/src/tiger/lex/lex.cc"
        "${PROJECT_SOURCE_DIR}/src/tiger/lex/scannerbase.h"
    COMMAND "${FLEXCPP}" tiger.lex
    DEPENDS "${PROJECT_SOURCE_DIR}/src/tiger/lex/tiger.lex"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/src/tiger/lex")
add_custom_target(lab2_lexer_sources DEPENDS ${LAB2_LEX_SOURCES})

# lab 1
add_executable(test_slp ${SLP_SOURCES})

# lab 2
add_executable(test_lex "src/tiger/main/test_lex.cc" ${LAB2_LEX_SOURCES})
add_dependencies(test_lex lab2_lexer_sources)

# lab 3
# add_executable(test_parse "src/tiger/main/test_parse.cc" ${LAB2_LEX_SOURCES})
# add_dependencies(test_parse lex_parse_sources)
#
# # lab 4
# add_executable(test_semant "src/tiger/main/test_semant.cc" ${LAB2_LEX_SOURCES})
# add_dependencies(test_semant lex_parse_sources)
#
# # lab 5 & lab 6
# add_executable(tiger-compiler "src/tiger/main/main.cc" ${LAB2_LEX_SOURCES})
# add_dependencies(tiger-compiler lex_parse_sources)

# Lab2 byte-preserving regression driver; uses exactly the production scanner.
add_executable(lab2_scan tests/lab2_scan.cc ${LAB2_LEX_SOURCES})
add_dependencies(lab2_scan lab2_lexer_sources)
