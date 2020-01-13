/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2020                                                               *
 * For conditions of distribution and use, see copyright notice in LICENSE.md            *
 ****************************************************************************************/

#ifndef __SGCT__VERTEXDATA__H__
#define __SGCT__VERTEXDATA__H__

namespace sgct::helpers {

/// This class stores a vertex which are used to generate vertex buffer objects (VBOs)
struct VertexData {
    float s = 0.f;
    float t = 0.f;  // Texcoord0 -> size=8
    float nx = 0.f;
    float ny = 0.f;
    float nz = 0.f; // size=12
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;  // size=12 ; total size=32 = power of two
};

} // namespace sgct::helpers

#endif // __SGCT__VERTEXDATA__H__