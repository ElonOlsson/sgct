##########################################################################################
# SGCT                                                                                   #
# Simple Graphics Cluster Toolkit                                                        #
#                                                                                        #
# Copyright (c) 2012-2020                                                                #
# For conditions of distribution and use, see copyright notice in LICENSE.md             #
##########################################################################################

add_executable(datatransfer main.cpp)
set_compile_options(datatransfer)
target_link_libraries(datatransfer PRIVATE sgct)

set(files
  "${CMAKE_CURRENT_SOURCE_DIR}/two_nodes.xml"
  "${CMAKE_CURRENT_SOURCE_DIR}/../SharedResources/box.png"
)
copy_files(datatransfer ${files})
copy_sgct_dynamic_libraries(datatransfer)
set_target_properties(datatransfer PROPERTIES FOLDER "Examples")

set(GLM_ROOT_DIR "${PROJECT_SOURCE_DIR}/ext/glm")
find_package(GLM REQUIRED)
mark_as_advanced(GLM_INCLUDE_DIR GLM_INCLUDE_DIRS)
target_include_directories(datatransfer SYSTEM PRIVATE ${GLM_INCLUDE_DIRS})