/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2019                                                               *
 * For conditions of distribution and use, see copyright notice in sgct.h                *
 ****************************************************************************************/

#ifndef __SGCT__READ_CONFIG__H__
#define __SGCT__READ_CONFIG__H__

#include <sgct/config.h>
#include <string>

namespace sgct::core {

[[nodiscard]] sgct::config::Cluster readConfig(const std::string& filename);

} // namespace sgct::core

#endif // __SGCT__READ_CONFIG__H__
