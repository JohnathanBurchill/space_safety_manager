#
#    Space Safety Manager  CMakeLists.txt
#
#    Copyright (C) 2026  Johnathan K Burchill
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.
#

cmake_minimum_required(VERSION 3.20)
project(space_safety_manager C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CURL REQUIRED)

set(SOURCES
    src/main.c
    src/api.c
    src/opm.c
    src/oem.c
    src/propagate.c
    src/state.c
    src/util.c
    third_party/cJSON.c
)

add_executable(ssm ${SOURCES})

target_include_directories(ssm PRIVATE src third_party)
target_link_libraries(ssm PRIVATE CURL::libcurl m)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ssm PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

install(TARGETS ssm DESTINATION $ENV{HOME}/bin)
