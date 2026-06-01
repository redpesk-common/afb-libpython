###########################################################################
# Copyright 2024 IoT.bzh
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
###########################################################################

find_package(PkgConfig REQUIRED)

find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)

# Do not request Python through pkg-config here. The Python pkg-config module
# may point to the system Python version while CMake selected a different
# interpreter for the extension module. Python include/link flags must come
# from find_package(Python3) to keep the built extension ABI-compatible with
# the target interpreter.
pkg_check_modules(deps REQUIRED libafb>=5.1.0 libafb-binder)

set(DLDIR "${Python3_SITEARCH}" CACHE STRING "install dir for python native module")

    # Define project Targets
Python3_add_library(libafb MODULE WITH_SOABI py-afb.c py-callbacks.c py-utils.c)
target_compile_options(libafb PRIVATE ${deps_CFLAGS})
target_link_libraries(libafb PRIVATE Python3::Module ${deps_LDFLAGS})

set_target_properties(libafb PROPERTIES
	IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/*.so
)

install(TARGETS libafb DESTINATION ${DLDIR})
