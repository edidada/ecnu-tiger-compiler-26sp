cmake_minimum_required(VERSION 3.16)
project(tiger-compiler LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_program(FLEXCPP flexc++ REQUIRED)
find_program(BISONCPP bisonc++ REQUIRED)
include_directories(src src/tiger/lex src/tiger/parse)
add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/tiger/lex/lex.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/tiger/lex/scannerbase.h
  COMMAND ${FLEXCPP} tiger.lex WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/tiger/lex DEPENDS src/tiger/lex/tiger.lex)
add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/tiger/parse/parse.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/tiger/parse/parserbase.h
  COMMAND ${BISONCPP} tiger.y WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/tiger/parse DEPENDS src/tiger/parse/tiger.y)
file(GLOB TIGER_SOURCES src/tiger/symbol/*.cc src/tiger/absyn/*.cc src/tiger/errormsg/*.cc src/tiger/env/*.cc
  src/tiger/escape/*.cc src/tiger/semant/*.cc src/tiger/frame/*.cc src/tiger/translate/*.cc
  src/tiger/canon/*.cc src/tiger/codegen/*.cc src/tiger/output/*.cc)
add_library(tiger_lab5 STATIC ${TIGER_SOURCES} src/tiger/lex/lex.cc src/tiger/parse/parse.cc)
foreach(name lex parse semant translate)
  add_executable(test_${name} src/tiger/main/test_${name}.cc)
  target_link_libraries(test_${name} tiger_lab5)
endforeach()
file(GLOB SLP_SOURCES src/straightline/*.cc)
add_executable(test_slp ${SLP_SOURCES})
add_executable(test_codegen src/tiger/main/test_codegen.cc)
target_link_libraries(test_codegen tiger_lab5)
