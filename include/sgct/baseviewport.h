/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2019                                                               *
 * For conditions of distribution and use, see copyright notice in sgct.h                *
 ****************************************************************************************/

#ifndef __SGCT__BASEVIEWPORT__H__
#define __SGCT__BASEVIEWPORT__H__

#include <sgct/frustum.h>
#include <sgct/projection.h>
#include <sgct/projectionplane.h>
#include <glm/glm.hpp>
#ifdef WIN32
#pragma warning(push)
#pragma warning(disable : 4127)
#endif // WIN32

#include <glm/gtc/quaternion.hpp>

#ifdef WIN32
#pragma warning(pop)
#endif // WIN32

#include <string>

namespace sgct {

class Window;
class User;

/// This class holds and manages viewportdata and calculates frustums
class BaseViewport {
public:
    BaseViewport(const Window* parent);
    virtual ~BaseViewport() = default;

    void setPos(glm::vec2 position);
    void setSize(glm::vec2 size);
    void setEnabled(bool state);
    void setUser(User* user);
    void setUserName(std::string userName);
    void setEye(Frustum::Mode eye);
    
    const glm::vec2& position() const;
    const glm::vec2& size() const;
    float horizontalFieldOfViewDegrees() const;

    User& user() const;
    const Window& window() const;
    Frustum::Mode eye() const;

    const Projection& projection(Frustum::Mode frustumMode) const;
    ProjectionPlane& projectionPlane();

    bool isEnabled() const;
    void linkUserName();

    void calculateFrustum(Frustum::Mode mode, float nearClip, float farClip);

    /// Make projection symmetric relative to user
    void calculateNonLinearFrustum(Frustum::Mode mode, float nearClip, float farClip);
    void setViewPlaneCoordsUsingFOVs(float up, float down, float left, float right,
        glm::quat rot, float dist = 10.f);
    void updateFovToMatchAspectRatio(float oldRatio, float newRatio);
    void setHorizontalFieldOfView(float hFov);

protected:
    const Window* _parent = nullptr;

    Projection _monoProj;
    Projection _stereoLeftProj;
    Projection _stereoRightProj;
    
    ProjectionPlane _projPlane;
    Frustum::Mode _eye = Frustum::Mode::MonoEye;

    User* _user;

    std::string _userName;
    bool _isEnabled = true;
    glm::vec2 _position = glm::vec2(0.f, 0.f);
    glm::vec2 _size = glm::vec2(1.f, 1.f);

    struct {
        glm::vec3 lowerLeft = glm::vec3(0.0);
        glm::vec3 upperLeft = glm::vec3(0.0);
        glm::vec3 upperRight = glm::vec3(0.0);
    } _viewPlane;
    glm::quat _rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
};

} // namespace sgct

#endif // __SGCT__BASEVIEWPORT__H__
