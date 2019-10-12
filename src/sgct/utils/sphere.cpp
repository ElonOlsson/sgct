/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2019                                                               *
 * For conditions of distribution and use, see copyright notice in sgct.h                *
 ****************************************************************************************/

#include <sgct/utils/sphere.h>

#include <sgct/engine.h>
#include <sgct/messagehandler.h>
#include <sgct/ogl_headers.h>
#include <sgct/helpers/vertexdata.h>
#include <glm/gtc/constants.hpp>
#include <algorithm>

namespace sgct::utils {

Sphere::Sphere(float radius, unsigned int segments) {
    createVBO(radius, segments);

    if (!Engine::checkForOGLErrors()) {
        MessageHandler::instance()->printError("SGCT Utils: Sphere creation error");
    }
}

Sphere::~Sphere() {
    glDeleteBuffers(1, &_vbo);
    _vbo = 0;

    glDeleteBuffers(1, &_ibo);
    _ibo = 0;

    glDeleteVertexArrays(1, &_vao);
    _vao = 0;
}

void Sphere::draw() {
    drawVAO();
}

void Sphere::drawVBO() {
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);

    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);

    glInterleavedArrays(GL_T2F_N3F_V3F, 0, 0);
    glDrawElements(GL_TRIANGLES, _nFaces * 3, GL_UNSIGNED_INT, 0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glPopClientAttrib();
}

void Sphere::drawVAO() {
    glBindVertexArray(_vao);
    glDrawElements(GL_TRIANGLES, _nFaces * 3, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Sphere::createVBO(float radius, unsigned int segments) {
    unsigned int vsegs = std::max<unsigned int>(segments, 2);
    unsigned int hsegs = vsegs * 2;
    _nVertices = 1 + (vsegs - 1) * (hsegs + 1) + 1; // top + middle + bottom
    _nFaces = hsegs + (vsegs - 2) * hsegs * 2 + hsegs; // top + middle + bottom

    std::vector<helpers::VertexData> verts(_nVertices);

    // First vertex: top pole (+y is "up" in object local coords)
    verts[0] = { 0.5f, 1.f, 0.f, 1.f, 0.f, 0.f, radius, 0.f };

    // Last vertex: bottom pole
    verts[_nVertices - 1] = { 0.5f, 0.f, 0.f, -1.f, 0.f, 0.f, -radius, 0.f };

    // All other vertices:
    // vsegs-1 latitude rings of hsegs+1 vertices each
    // (duplicates at texture seam s=0 / s=1)
    for (unsigned int j = 0; j < vsegs - 1; j++) {
        // vsegs-1 latitude rings of vertices
        const double theta = (static_cast<double>(j + 1) / static_cast<double>(vsegs)) *
                              glm::pi<double>();
        const float y = static_cast<float>(cos(theta));
        const float R = static_cast<float>(sin(theta));

        for (unsigned int i = 0; i <= hsegs; i++) {
            // hsegs+1 vertices in each ring (duplicate for texcoords)
            const double phi = (static_cast<double>(i) / static_cast<double>(hsegs)) *
                                glm::two_pi<double>();
            const float x = R * static_cast<float>(cos(phi));
            const float z = R * static_cast<float>(sin(phi));

            verts[1 + j * (hsegs + 1) + i] = {
                static_cast<float>(i) / static_cast<float>(hsegs), // s
                1.f - static_cast<float>(j + 1) / static_cast<float>(vsegs), // t
                x, y, z, // normals
                radius * x,
                radius * y,
                radius * z
            };
        }
    }

    std::vector<unsigned int> indices(_nFaces * 3, 0);
    // The index array: triplets of integers, one for each triangle
    // Top cap
    for (unsigned int i = 0; i < hsegs; i++) {
        indices[3 * i] = 0;
        indices[3 * i + 2] = 1 + i;
        indices[3 * i + 1] = 2 + i;
    }
    // Middle part (possibly empty if vsegs=2)
    for (unsigned int j = 0; j < vsegs - 2; j++) {
        for (unsigned int i = 0; i < hsegs; i++) {
            const unsigned int base = 3 * (hsegs + 2 * (j * hsegs + i));
            const unsigned int i0 = 1 + j * (hsegs + 1) + i;
            indices[base] = i0;
            indices[base + 1] = i0 + 1;
            indices[base + 2] = i0 + hsegs + 1;
            indices[base + 3] = i0 + hsegs + 1;
            indices[base + 4] = i0 + 1;
            indices[base + 5] = i0 + hsegs + 2;
        }
    }
    // Bottom cap
    for (unsigned int i = 0; i < hsegs; i++) {
        const unsigned int base = 3 * (hsegs + 2 * (vsegs - 2) * hsegs);
        indices[base + 3 * i] = _nVertices - 1;
        indices[base + 3 * i + 2] = _nVertices - 2 - i;
        indices[base + 3 * i + 1] = _nVertices - 3 - i;
    }

    glGenVertexArrays(1, &_vao);
    glBindVertexArray(_vao);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    MessageHandler::instance()->printDebug("Sphere: Generating VAO: %d", _vao);

    glGenBuffers(1, &_vbo);
    glGenBuffers(1, &_ibo);
    MessageHandler::instance()->printDebug("Sphere: Generating VBOs: %d %d", _vbo, _ibo);

    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        _nVertices * sizeof(helpers::VertexData),
        verts.data(),
        GL_STATIC_DRAW
    );

    // texcoords
    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(helpers::VertexData),
        reinterpret_cast<void*>(0)
    );
    // normals
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(helpers::VertexData),
        reinterpret_cast<void*>(8)
    );
    // vert positions
    glVertexAttribPointer(
        2,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(helpers::VertexData),
        reinterpret_cast<void*>(20)
    ); 

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        _nFaces * 3 * sizeof(unsigned int),
        indices.data(),
        GL_STATIC_DRAW
    );

    glBindVertexArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

} // namespace sgct::utils