###########################################################################
# Copyright 201 IoT.bzh
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

PROJECT_TARGET_ADD(afbpyglue)

    find_package (Python3 COMPONENTS Interpreter Development)
    set(DLDIR "${Python3_SITEARCH}" CACHE STRING "install dir for python native module")

    # Define project Targets
    Python3_add_library(${TARGET_NAME} MODULE WITH_SOABI py-afb.c py-callbacks.c py-utils.c)

    SET_TARGET_PROPERTIES(${TARGET_NAME} PROPERTIES
        PREFIX "_"
	IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/*.so
    )

    INSTALL(TARGETS ${TARGET_NAME}
        DESTINATION ${DLDIR}
    )
