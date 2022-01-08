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

	# compile any C file starting with a lower case
	file(GLOB CSOURCES [a-z]*.c)

	# Define project Targets
    add_library(${TARGET_NAME} SHARED ${CSOURCES})
    target_link_libraries(${TARGET_NAME} libafb-glue.so)

	SET_TARGET_PROPERTIES(${TARGET_NAME} PROPERTIES
		PREFIX "_"
		LABELS "LIBRARY"
		IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/*${TARGET_NAME}.so
		OUTPUT_NAME ${TARGET_NAME}
	)
