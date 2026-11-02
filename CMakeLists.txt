#   Copyright (C) 2026 John Törnblom
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING. If not see
# <http://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.16)

project(vk-triangle C)

find_package(PkgConfig REQUIRED)

pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)

#
# Compile the GLSL shaders to SPIR-V C headers (triangle_vert.h and
# triangle_frag.h). They are generated in the build tree.
#
find_program(GLSLANG_VALIDATOR glslangValidator)
if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found (try: apt install glslang-tools)")
endif()

set(SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

foreach(stage vert frag)
    set(shader_src ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.${stage})
    set(shader_hdr ${SHADER_DIR}/triangle_${stage}.h)

    add_custom_command(
        OUTPUT ${shader_hdr}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_DIR}
        COMMAND ${GLSLANG_VALIDATOR} -V --vn triangle_${stage}
                -o ${shader_hdr} ${shader_src}
        DEPENDS ${shader_src}
        VERBATIM
    )
    list(APPEND SHADER_HEADERS ${shader_hdr})
endforeach()

add_executable(vk-triangle main.c ${SHADER_HEADERS})

target_include_directories(vk-triangle PRIVATE ${SHADER_DIR})

target_compile_options(vk-triangle PRIVATE -Wall -Werror)

target_link_libraries(vk-triangle PRIVATE PkgConfig::VULKAN)

if(ORBIS)
    orbis_add_pkg(vk-triangle
        TITLE "Vulkan Triangle"
        TITLE_ID BREW95438
        CONTENT_ID IV0000-BREW95438_00-VULKANTRIANGLE00
        ASSETS sce_sys
        MODULES libvulkan.so.1
    )
endif()
