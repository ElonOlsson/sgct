##########################################################################################
# SGCT                                                                                   #
# Simple Graphics Cluster Toolkit                                                        #
#                                                                                        #
# Copyright (c) 2012-2020                                                                #
# For conditions of distribution and use, see copyright notice in LICENSE.md             #
##########################################################################################

add_executable(domeimageviewer main.cpp)
set_compile_options(domeimageviewer)
target_link_libraries(domeimageviewer PRIVATE sgct)

set(files
  "${CMAKE_CURRENT_SOURCE_DIR}/two_nodes.xml"
  "${CMAKE_CURRENT_SOURCE_DIR}/two_nodes_stereo_dummy.xml"
)
copy_files(domeimageviewer ${files})
copy_sgct_dynamic_libraries(domeimageviewer)
set_target_properties(domeimageviewer PROPERTIES FOLDER "Examples")
