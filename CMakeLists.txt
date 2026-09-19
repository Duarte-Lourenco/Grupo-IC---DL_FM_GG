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

# Cada ficheiro src/NOME.cpp dá origem ao programa build/NOME.
# Não é preciso editar este ficheiro para acrescentar programas:
# basta criar o .cpp e correr "cmake --build build".
file(GLOB PROGRAMS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
foreach(src ${PROGRAMS})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src})
  target_include_directories(${name} PRIVATE src ${SNDFILE_INCLUDE_DIRS})
  target_link_libraries(${name} PRIVATE ${SNDFILE_LIBRARIES})
endforeach()
