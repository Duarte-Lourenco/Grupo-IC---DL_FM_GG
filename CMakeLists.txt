cmake_minimum_required(VERSION 3.16)
project(IC_Trabalho1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
add_compile_options(-Wall -Wextra)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED sndfile)

# Função auxiliar: add_tool(nome) compila src/nome.cpp
function(add_tool name)
  add_executable(${name} src/${name}.cpp)
  target_include_directories(${name} PRIVATE src ${SNDFILE_INCLUDE_DIRS})
  target_link_libraries(${name} PRIVATE ${SNDFILE_LIBRARIES})
endfunction()

add_tool(wav_hist)
add_tool(test_midside)
# Próximos: add_tool(wav_quant) add_tool(wav_cmp) add_tool(wav_effects) ...
