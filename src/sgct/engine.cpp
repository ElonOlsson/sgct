/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2019                                                               *
 * For conditions of distribution and use, see copyright notice in sgct.h                *
 ****************************************************************************************/

#include <sgct/engine.h>

#include <sgct/clustermanager.h>
#include <sgct/commandline.h>
#include <sgct/error.h>
#include <sgct/font.h>
#include <sgct/fontmanager.h>
#include <sgct/freetype.h>
#include <sgct/readconfig.h>
#include <sgct/shadermanager.h>
#include <sgct/shareddata.h>
#include <sgct/statisticsrenderer.h>
#include <sgct/texturemanager.h>
#include <sgct/trackingmanager.h>
#include <sgct/user.h>
#include <sgct/version.h>
#include <sgct/touch.h>
#include <sgct/shaders/internalshaders.h>
#include <iostream>
#include <numeric>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define Error(code, msg) Error(Error::Component::Engine, code, msg)

// Callback wrappers for GLFW
std::function<void(int, int, int, int)> gKeyboardCallbackFn = nullptr;
std::function<void(unsigned int, int)> gCharCallbackFn = nullptr;
std::function<void(int, int, int)> gMouseButtonCallbackFn = nullptr;
std::function<void(double, double)> gMousePosCallbackFn = nullptr;
std::function<void(double, double)> gMouseScrollCallbackFn = nullptr;
std::function<void(int, const char**)> gDropCallbackFn = nullptr;
std::function<void(const sgct::core::Touch*)> gTouchCallbackFn = nullptr;
sgct::core::Touch gCurrentTouchPoints;

bool sRunUpdateFrameLockLoop = true;

namespace {
    constexpr const bool UseSleepToWaitForNodes = false;
    constexpr const bool RunFrameLockCheckThread = true;
    constexpr const std::chrono::milliseconds FrameLockTimeout(100);

    // For feedback: breaks a frame lock wait condition every time interval
    // (FrameLockTimeout) in order to print waiting message.
    void updateFrameLockLoop(void*) {
        bool run = true;

        while (run) {
            sgct::core::mutex::FrameSync.lock();
            run = sRunUpdateFrameLockLoop;
            sgct::core::mutex::FrameSync.unlock();
            sgct::core::NetworkManager::cond.notify_all();
            std::this_thread::sleep_for(FrameLockTimeout);
        }
    }

    std::string getStereoString(sgct::Window::StereoMode stereoMode) {
        switch (stereoMode) {
            case sgct::Window::StereoMode::Active:
                return "active";
            case sgct::Window::StereoMode::AnaglyphRedCyan:
                return "anaglyph_red_cyan";
            case sgct::Window::StereoMode::AnaglyphAmberBlue:
                return "anaglyph_amber_blue";
            case sgct::Window::StereoMode::AnaglyphRedCyanWimmer:
                return "anaglyph_wimmer";
            case sgct::Window::StereoMode::Checkerboard:
                return "checkerboard";
            case sgct::Window::StereoMode::CheckerboardInverted:
                return "checkerboard_inverted";
            case sgct::Window::StereoMode::VerticalInterlaced:
                return "vertical_interlaced";
            case sgct::Window::StereoMode::VerticalInterlacedInverted:
                return "vertical_interlaced_inverted";
            case sgct::Window::StereoMode::Dummy:
                return "dummy";
            case sgct::Window::StereoMode::SideBySide:
                return "side_by_side";
            case sgct::Window::StereoMode::SideBySideInverted:
                return "side_by_side_inverted";
            case sgct::Window::StereoMode::TopBottom:
                return "top_bottom";
            case sgct::Window::StereoMode::TopBottomInverted:
                return "top_bottom_inverted";
            default:
                return "none";
        }
    }

    template <typename Array>
    void addValue(Array& array, double value) {
        std::rotate(std::rbegin(array), std::rbegin(array) + 1, std::rend(array));
        array[0] = value;
    }
} // namespace

namespace sgct {

Engine* Engine::_instance = nullptr;

Engine& Engine::instance() {
    return *_instance;
}

void Engine::create(const Configuration& arg) {
    if (_instance) {
        throw std::logic_error("Engine has already been created");
    }
    _instance = new Engine(arg);
}

void Engine::destroy() {
    delete _instance;
    _instance = nullptr;
}

config::Cluster loadCluster(std::optional<std::string> path) {
    if (path) {
        try {
            return core::readConfig(*path);
        }
        catch (...) {
            // (abock, 2019-10-11) This conversion from string_view to string is necessary
            // to keep this code compiling with VS 2017 15.9.16 (and probably before)
            std::cout << std::string(getHelpMessage()) << '\n';
            throw;
        }
    }
    else {
        config::Cluster cluster;
        // Create a default configuration
        sgct::config::ProjectionPlane proj;
        proj.lowerLeft = glm::vec3(-16.f/9.f, -1.f, 0.f);
        proj.upperLeft = glm::vec3(-16.f/9.f, 1.f, 0.f);
        proj.upperRight = glm::vec3(16.f/9.f, 1.f, 0.f);

        sgct::config::Viewport viewport;
        viewport.projection = proj;

        sgct::config::Window window;
        window.isFullScreen = false;
        window.size = glm::ivec2(1280, 720);
        window.viewports.push_back(viewport);

        sgct::config::Node node;
        node.address = "localhost";
        node.port = 20401;
        node.windows.push_back(window);

        sgct::config::User user;
        user.eyeSeparation = 0.06f;
        user.position = glm::vec3(0.f, 0.f, 4.f);
        cluster.users.push_back(user);

        cluster.masterAddress = "localhost";
        cluster.nodes.push_back(node);
        return cluster;
    }
}

Engine::Engine(const Configuration& config) {
    if (config.isServer) {
        core::ClusterManager::instance().setNetworkMode(
            *config.isServer ?
                core::NetworkManager::NetworkMode::LocalServer :
                core::NetworkManager::NetworkMode::LocalClient
        );
    }
    if (config.logPath) {
        MessageHandler::instance().setLogPath(
            config.logPath->c_str(),
            core::ClusterManager::instance().getThisNodeId()
        );
        MessageHandler::instance().setLogToFile(true);
    }
    if (config.logLevel) {
        MessageHandler::instance().setNotifyLevel(*config.logLevel);
    }
    if (config.showHelpText) {
        _helpMode = true;
        // (abock, 2019-10-11) This conversion from string_view to string is necessary to
        // keep this code compiling with VS 2017 15.9.16 (and probably before)
        std::cout << std::string(getHelpMessage()) << '\n';
    }
    if (config.nodeId) {
        core::ClusterManager::instance().setThisNodeId(*config.nodeId);
    }
    if (config.firmSync) {
        core::ClusterManager::instance().setFirmFrameLockSyncStatus(*config.firmSync);
    }
    if (config.ignoreSync) {
        core::ClusterManager::instance().setUseIgnoreSync(*config.ignoreSync);
    }
    if (config.fxaa) {
        Settings::instance().setDefaultFXAAState(*config.fxaa);
    }
    if (config.msaaSamples) {
        if (*config.msaaSamples > 0) {
            Settings::instance().setDefaultNumberOfAASamples(*config.msaaSamples);
        }
        else {
            MessageHandler::printError("Number of MSAA samples must be positive");
        }
    }
    if (config.captureFormat) {
        Settings::instance().setCaptureFormat(*config.captureFormat);
    }
    if (config.nCaptureThreads) {
        if (*config.nCaptureThreads > 0) {
            Settings::instance().setNumberOfCaptureThreads(*config.nCaptureThreads);
        }
        else {
            MessageHandler::printError("Only positive number of capture threads allowed");
        }
    }
    if (config.checkOpenGL) {
        _checkOpenGLCalls = *config.checkOpenGL;
    }
    if (config.checkFBOs) {
        _checkFBOs = *config.checkFBOs;
    }

    if (_helpMode) {
        return;
    }

    glfwSetErrorCallback([](int error, const char* desc) {
        MessageHandler::printError("GLFW error (%i): %s", error, desc);
    });
    const int res = glfwInit();
    if (res == GLFW_FALSE) {
        _shouldTerminate = true;
    }

    std::fill(_statistics.frametimes.begin(), _statistics.frametimes.end(), 0.0);
    std::fill(_statistics.drawTimes.begin(), _statistics.drawTimes.end(), 0.0);
    std::fill(_statistics.syncTimes.begin(), _statistics.syncTimes.end(), 0.0);
    std::fill(_statistics.loopTimeMin.begin(), _statistics.loopTimeMin.end(), 0.0);
    std::fill(_statistics.loopTimeMax.begin(), _statistics.loopTimeMax.end(), 0.0);
}

Engine::~Engine() {
    MessageHandler::printInfo("Cleaning up");

    core::Node& thisNode = core::ClusterManager::instance().getThisNode();
    if (_cleanUpFn) {
        if (thisNode.getNumberOfWindows() > 0) {
            thisNode.getWindow(0).makeOpenGLContextCurrent(Window::Context::Shared);
        }
        _cleanUpFn();
    }

    // @TODO (abock, 2019-10-15) I don't think this is necessary unless someone is using
    // the callbacks in their shutdown. That should be easy enough to figure out and
    // prevent
    MessageHandler::printDebug("Clearing all callbacks");
    _drawFn = nullptr;
    _draw2DFn = nullptr;
    _preSyncFn = nullptr;
    _postSyncPreDrawFn = nullptr;
    _postDrawFn = nullptr;
    _initOpenGLFn = nullptr;
    _cleanUpFn = nullptr;
    _externalDecodeCallbackFn = nullptr;
    _externalStatusCallbackFn = nullptr;
    _dataTransferDecodeCallbackFn = nullptr;
    _dataTransferStatusCallbackFn = nullptr;
    _dataTransferAcknowledgeCallbackFn = nullptr;
    _contextCreationFn = nullptr;
    _screenShotFn = nullptr;

    gKeyboardCallbackFn = nullptr;
    gMouseButtonCallbackFn = nullptr;
    gMousePosCallbackFn = nullptr;
    gMouseScrollCallbackFn = nullptr;
    gDropCallbackFn = nullptr;

    // kill thread
    if (_thread) {
        MessageHandler::printDebug("Waiting for frameLock thread to finish");

        core::mutex::FrameSync.lock();
        sRunUpdateFrameLockLoop = false;
        core::mutex::FrameSync.unlock();

        _thread->join();
        _thread = nullptr;
        MessageHandler::printDebug("Done");
    }

    // de-init window and unbind swapgroups
    if (core::ClusterManager::instance().getNumberOfNodes() > 0) {
        for (int i = 0; i < thisNode.getNumberOfWindows(); ++i) {
            thisNode.getWindow(i).close();
        }
    }

    // close TCP connections
    MessageHandler::printDebug("Destroying network manager");
    core::NetworkManager::destroy();

    // Shared contex
    if (thisNode.getNumberOfWindows() > 0) {
        thisNode.getWindow(0).makeOpenGLContextCurrent(Window::Context::Shared);
    }

    MessageHandler::printDebug("Destroying shader manager and internal shaders");
    ShaderManager::destroy();

    _shader.fboQuad.deleteProgram();
    _shader.fxaa.deleteProgram();
    _shader.overlay.deleteProgram();

    _statisticsRenderer = nullptr;

    MessageHandler::printDebug("Destroying texture manager");
    TextureManager::destroy();

#ifdef SGCT_HAS_TEXT
    MessageHandler::printDebug("Destroying font manager");
    text::FontManager::destroy();
#endif // SGCT_HAS_TEXT

    // Window specific context
    if (thisNode.getNumberOfWindows() > 0) {
        thisNode.getWindow(0).makeOpenGLContextCurrent(Window::Context::Window);
    }
    
    MessageHandler::printDebug("Destroying shared data");
    SharedData::destroy();
    
    MessageHandler::printDebug("Destroying cluster manager");
    core::ClusterManager::destroy();
    
    MessageHandler::printDebug("Destroying settings");
    Settings::destroy();

    MessageHandler::printDebug("Destroying message handler");
    MessageHandler::destroy();

    // Close window and terminate GLFW
    MessageHandler::printDebug("Terminating glfw");
    glfwTerminate();
 
    MessageHandler::printDebug("Finished cleaning");
}

void Engine::init(RunMode rm, config::Cluster cluster) {
    _runMode = rm;
    MessageHandler::printInfo("%s", getVersion().c_str());

    if (_helpMode) {
        return;
    }

    if (_shouldTerminate) {
        throw Error(3000, "Failed to initialize GLFW");
    }

    MessageHandler::printDebug("Validating cluster configuration");
    config::validateCluster(cluster);
         
    core::ClusterManager::instance().applyCluster(cluster);
    for (const config::Tracker& tracker : cluster.trackers) {
        TrackingManager::instance().applyTracker(tracker);
    }
    if (cluster.checkOpenGL) {
        _checkOpenGLCalls = *cluster.checkOpenGL;
    }
    if (cluster.checkFBOs) {
        _checkFBOs = *cluster.checkFBOs;
    }

    initNetwork();
    initWindows();

    // Window resolution may have been set when reading config. However, it only sets a
    // pending resolution, so it needs to apply it using the same routine as in the end of
    // a frame.
    core::Node& thisNode = core::ClusterManager::instance().getThisNode();
    for (int i = 0; i < thisNode.getNumberOfWindows(); ++i) {
        thisNode.getWindow(i).updateResolutions();
    }

    // if a single node, skip syncing
    if (core::ClusterManager::instance().getNumberOfNodes() == 1) {
        core::ClusterManager::instance().setUseIgnoreSync(true);
    }

    for (int i = 0; i < thisNode.getNumberOfWindows(); ++i) {
        GLFWwindow* window = getWindow(i).getWindowHandle();
        if (gKeyboardCallbackFn) {
            glfwSetKeyCallback(
                window,
                [](GLFWwindow*, int key, int scancode, int action, int mods) {
                    if (gKeyboardCallbackFn) {
                        gKeyboardCallbackFn(key, scancode, action, mods);
                    }
                }
            );
        }
        if (gMouseButtonCallbackFn) {
            glfwSetMouseButtonCallback(
                window,
                [](GLFWwindow*, int button, int action, int mods) {
                    if (gMouseButtonCallbackFn) {
                        gMouseButtonCallbackFn(button, action, mods);
                    }
                }
            );
        }
        if (gMousePosCallbackFn) {
            glfwSetCursorPosCallback(
                window,
                [](GLFWwindow*, double xPos, double yPos) {
                    if (gMousePosCallbackFn) {
                        gMousePosCallbackFn(xPos, yPos);
                    }
                }
            );
        }
        if (gCharCallbackFn) {
            glfwSetCharModsCallback(
                window,
                [](GLFWwindow*, unsigned int ch, int mod) {
                    if (gCharCallbackFn) {
                        gCharCallbackFn(ch, mod);
                    }
                }
            );
        }
        if (gMouseScrollCallbackFn) {
            glfwSetScrollCallback(
                window,
                [](GLFWwindow*, double xOffset, double yOffset) {
                    if (gMouseScrollCallbackFn) {
                        gMouseScrollCallbackFn(xOffset, yOffset);
                    }
                }
            );
        }
        if (gDropCallbackFn) {
            glfwSetDropCallback(
                window,
                [](GLFWwindow*, int count, const char** paths) {
                    if (gDropCallbackFn) {
                        gDropCallbackFn(count, paths);
                    }
                }
            );
        }
        if (gTouchCallbackFn) {
            glfwSetTouchCallback(
                window,
                [](GLFWwindow*, GLFWtouch* touchPoints, int count){
                    sgct::Engine& eng = sgct::Engine::instance();
                    glm::ivec4 c = eng.getCurrentWindow().getCurrentViewportPixelCoords();

                    gCurrentTouchPoints.processPoints(touchPoints, count, c.z, c.w);
                    gCurrentTouchPoints.setLatestPointsHandled();
                }
            );
        }
    }

    initOGL();

    // start sampling tracking data
    if (isMaster()) {
        TrackingManager::instance().startSampling();
    }
}

void Engine::terminate() {
    _shouldTerminate = true;
}

void Engine::initNetwork() {
    core::NetworkManager::create(core::ClusterManager::instance().getNetworkMode());

    // check in cluster configuration which it is
    if (core::ClusterManager::instance().getNetworkMode() ==
        core::NetworkManager::NetworkMode::Remote)
    {
        MessageHandler::printDebug("Matching ip address to find node in configuration");
        core::NetworkManager::instance().retrieveNodeId();
    }
    else {
        MessageHandler::printDebug(
            "Running locally as node %d",
            core::ClusterManager::instance().getThisNodeId()
        );
    }

    // If the user has provided the node _id as an incorrect cmd argument then make the
    // _thisNode invalid
    if (core::ClusterManager::instance().getThisNodeId() >=
        static_cast<int>(core::ClusterManager::instance().getNumberOfNodes()) ||
        core::ClusterManager::instance().getThisNodeId() < 0)
    {
        core::NetworkManager::instance().close();
        throw Error(3001, "Computer is not a part of the cluster configuration");
    }

    const bool networkInitSuccess = core::NetworkManager::instance().init();
    if (!networkInitSuccess) {
        throw Error(3002, "Error initializing network connections");
    }
}

void Engine::initWindows() {
    core::Node& thisNode = core::ClusterManager::instance().getThisNode();
    if (thisNode.getNumberOfWindows() == 0) {
        throw Error(3003, "No windows exist in configuration");
    }

    {
        int ver[3];
        glfwGetVersion(&ver[0], &ver[1], &ver[2]);
        MessageHandler::printInfo("Using GLFW version %d.%d.%d", ver[0], ver[1], ver[2]);
    }

    switch (_runMode) {
        default:
        case RunMode::OpenGL_3_3_Core_Profile:
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            break;
        case RunMode::OpenGL_4_0_Core_Profile:
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            break;
        case RunMode::OpenGL_4_1_Debug_Core_Profile:
            glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
            [[ fallthrough ]];
        case RunMode::OpenGL_4_1_Core_Profile:
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            break;
        case RunMode::OpenGL_4_2_Debug_Core_Profile:
            glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
            [[fallthrough]];
        case RunMode::OpenGL_4_2_Core_Profile:
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            break;
        case RunMode::OpenGL_4_3_Debug_Core_Profile:
            glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
            [[fallthrough]];
        case RunMode::OpenGL_4_3_Core_Profile:
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            break;
        case RunMode::OpenGL_4_4_Debug_Core_Profile:
            glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
            [[fallthrough]];
        case RunMode::OpenGL_4_4_Core_Profile:
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            break;
        case RunMode::OpenGL_4_5_Debug_Core_Profile:
            glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
            [[fallthrough]];
        case RunMode::OpenGL_4_5_Core_Profile:
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            break;
        case RunMode::OpenGL_4_6_Debug_Core_Profile:
            glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
            [[fallthrough]];
        case RunMode::OpenGL_4_6_Core_Profile:
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            break;
    }

    if (_preWindowFn) {
        _preWindowFn();
    }

    GLFWwindow* share = nullptr;
    const int lastWindowIdx = thisNode.getNumberOfWindows() - 1;
    for (int i = 0; i < thisNode.getNumberOfWindows(); ++i) {
        if (i > 0) {
            share = thisNode.getWindow(0).getWindowHandle();
        }

        if (!thisNode.getWindow(i).openWindow(share, lastWindowIdx)) {
            throw Error(3004, "Failed to open window " + std::to_string(i));
        }
    }

    glbinding::Binding::initialize(glfwGetProcAddress);

    if (_checkOpenGLCalls || _checkFBOs) {
        using namespace glbinding;

        // The callback mask needs to be set in order to prevent an infinite loop when
        // calling these functions in the error checking callback
        Binding::setCallbackMaskExcept(
            CallbackMask::After,
            { "glGetError", "glCheckFramebufferStatus" }
        );
        Binding::setAfterCallback([this](const FunctionCall& f) {
            const GLenum error = _checkOpenGLCalls ? glGetError() : GL_NO_ERROR;
            const GLenum fboStatus = _checkFBOs ?
                glCheckFramebufferStatus(GL_FRAMEBUFFER) :
                GL_FRAMEBUFFER_COMPLETE;
            if (error == GL_NO_ERROR && fboStatus == GL_FRAMEBUFFER_COMPLETE) {
                return;
            }

            const char* n = f.function->name();
            switch (error) {
                case GL_NO_ERROR:
                    break;
                case GL_INVALID_ENUM:
                    MessageHandler::printError("OpenGL error. %s: GL_INVALID_ENUM", n);
                    break;
                case GL_INVALID_VALUE:
                    MessageHandler::printError("OpenGL error. %s: GL_INVALID_VALUE", n);
                    break;
                case GL_INVALID_OPERATION:
                    MessageHandler::printError(
                        "OpenGL error. %s: GL_INVALID_OPERATION", n
                    );
                    break;
                case GL_INVALID_FRAMEBUFFER_OPERATION:
                    MessageHandler::printError(
                        "OpenGL error. %s: GL_INVALID_FRAMEBUFFER_OPERATION", n
                    );
                    break;
                case GL_STACK_OVERFLOW:
                    MessageHandler::printError("OpenGL error. %s: GL_STACK_OVERFLOW", n);
                    break;
                case GL_STACK_UNDERFLOW:
                    MessageHandler::printError("OpenGL error. %s: GL_STACK_UNDERFLOW", n);
                    break;
                case GL_OUT_OF_MEMORY:
                    MessageHandler::printError("OpenGL error. %s: GL_OUT_OF_MEMORY", n);
                    break;
                default:
                    MessageHandler::printError("OpenGL error. %s: %i", n, error);
            }

            switch (fboStatus) {
                case GL_FRAMEBUFFER_COMPLETE:
                    break;
                case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
                    MessageHandler::printError(
                        "FBO error. %s: GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT", n
                    );
                    break;
                case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
                    MessageHandler::printError(
                        "FBO error. %s: GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT", n
                    );
                    break;
                case GL_FRAMEBUFFER_UNSUPPORTED:
                    MessageHandler::printError(
                        "FBO error. %s: GL_FRAMEBUFFER_UNSUPPORTED", n
                    );
                    break;
                case GL_FRAMEBUFFER_UNDEFINED:
                    MessageHandler::printError(
                        "FBO error. %s: GL_FRAMEBUFFER_UNDEFINED", n
                    );
                    break;
                case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
                    MessageHandler::printError(
                        "FBO error. %s: GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER", n
                    );
                    break;
                case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
                    MessageHandler::printError(
                        "FBO error. %s: GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER", n
                    );
                    break;
                case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
                    MessageHandler::printError(
                        "FBO error. %s: GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE", n
                    );
                    break;
                case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
                    MessageHandler::printError(
                        "FBO error. %s: GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS", n
                    );
                    break;
                default:
                    MessageHandler::printError(
                        "FBO error. %s: %i", n, static_cast<int>(fboStatus)
                    );
            }
        });
    }

    // clear directly otherwise junk will be displayed on some OSs (OS X Yosemite)
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Window/Context creation callback
    if (thisNode.getNumberOfWindows() > 0) {
        share = thisNode.getWindow(0).getWindowHandle();

        if (_contextCreationFn) {
            _contextCreationFn(share);
        }
    }
    else {
        throw Error(3005, "No windows created on this node");
    }

    for (int i = 0; i < thisNode.getNumberOfWindows(); ++i) {
        thisNode.getWindow(i).init();
    }

    // init draw buffer resolution
    updateDrawBufferResolutions();
    waitForAllWindowsInSwapGroupToOpen();

    if (RunFrameLockCheckThread) {
        if (core::ClusterManager::instance().getNumberOfNodes() > 1) {
            _thread = std::make_unique<std::thread>(updateFrameLockLoop, nullptr);
        }
    }

    // init swap group if enabled
    if (thisNode.isUsingSwapGroups()) {
        Window::initNvidiaSwapGroups();
    }
}

void Engine::initOGL() {
    // Get OpenGL version
    int v[3];
    GLFWwindow* winHandle = getCurrentWindow().getWindowHandle();
    v[0] = glfwGetWindowAttrib(winHandle, GLFW_CONTEXT_VERSION_MAJOR);
    v[1] = glfwGetWindowAttrib(winHandle, GLFW_CONTEXT_VERSION_MINOR);
    v[2] = glfwGetWindowAttrib(winHandle, GLFW_CONTEXT_REVISION);
    MessageHandler::printInfo("OpenGL version %d.%d.%d core profile", v[0], v[1], v[2]);

    MessageHandler::printInfo("Vendor: %s", glGetString(GL_VENDOR));
    MessageHandler::printInfo("Renderer: %s", glGetString(GL_RENDERER));

    if (core::ClusterManager::instance().getNumberOfNodes() > 1) {
        std::string path = Settings::instance().getCapturePath() + "_node";
        path += std::to_string(core::ClusterManager::instance().getThisNodeId());

        Settings::instance().setCapturePath(path, Settings::CapturePath::Mono);
        Settings::instance().setCapturePath(path, Settings::CapturePath::LeftStereo);
        Settings::instance().setCapturePath(path, Settings::CapturePath::RightStereo);
    }

    // init window opengl data
    getCurrentWindow().makeOpenGLContextCurrent(Window::Context::Shared);

    loadShaders();

    if (_initOpenGLFn) {
        MessageHandler::printInfo("Calling init callback");
        _initOpenGLFn();
        MessageHandler::printInfo("-------------------------------");
    }

    // create all textures, etc
    core::Node& thisNode = core::ClusterManager::instance().getThisNode();
    for (int i = 0; i < thisNode.getNumberOfWindows(); ++i) {
        _currentWindowIndex = i;
        // set context to shared
        getCurrentWindow().initOGL();
        
        if (_screenShotFn) {
            // set callback
            auto callback = [this](core::Image* img, size_t size,
                                   core::ScreenCapture::EyeIndex idx, GLenum type)
            {
                if (_screenShotFn) {
                    _screenShotFn(img, size, idx, type);
                }
            };
            
            Window& win = getCurrentWindow();
            // left channel (Mono and Stereo_Left)
            core::ScreenCapture* m = win.getScreenCapturePointer(Window::Eye::MonoOrLeft);
            if (m) {
                m->setCaptureCallback(callback);
            }
            // right channel (Stereo_Right)
            core::ScreenCapture* r = win.getScreenCapturePointer(Window::Eye::Right);
            if (r) {
                r->setCaptureCallback(callback);
            }
        }
    }

    // link all users to their viewports
    for (int w = 0; w < thisNode.getNumberOfWindows(); w++) {
        Window& win = thisNode.getWindow(w);
        for (int i = 0; i < win.getNumberOfViewports(); ++i) {
            win.getViewport(i).linkUserName();
        }
    }

    updateFrustums();

    // Add fonts
#ifdef SGCT_HAS_TEXT
    if (Settings::instance().getOSDTextFontPath().empty()) {
        const bool success = text::FontManager::instance().addFont(
            "SGCTFont",
            Settings::instance().getOSDTextFontName()
        );
        if (!success) {
            text::FontManager::instance().getFont(
                "SGCTFont",
                Settings::instance().getOSDTextFontSize()
            );
        }
    }
    else {
        std::string tmpPath = Settings::instance().getOSDTextFontPath() +
                              Settings::instance().getOSDTextFontName();
        const bool success = text::FontManager::instance().addFont(
            "SGCTFont",
            tmpPath,
            text::FontManager::Path::Local
        );
        if (!success) {
            text::FontManager::instance().getFont(
                "SGCTFont",
                Settings::instance().getOSDTextFontSize()
            );
        }
    }
#endif // SGCT_HAS_TEXT

    // init swap barrier is swap groups are active
    Window::setBarrier(true);
    Window::resetSwapGroupFrameNumber();

    for (int i = 0; i < thisNode.getNumberOfWindows(); ++i) {
        thisNode.getWindow(i).initContextSpecificOGL();
    }

    // check for errors
    MessageHandler::printInfo("Ready to render");
}

void Engine::frameLockPreStage() {
    using namespace core;

    const double ts = glfwGetTime();
    // from server to clients
    using P = std::pair<double, double>;
    std::optional<P> minMax = core::NetworkManager::instance().sync(
        NetworkManager::SyncMode::SendDataToClients
    );
    if (minMax) {
        addValue(_statistics.loopTimeMin, minMax->first);
        addValue(_statistics.loopTimeMax, minMax->second);
    }
    addValue(_statistics.syncTimes, static_cast<float>(glfwGetTime() - ts));

    // run only on clients/slaves
    if (ClusterManager::instance().getIgnoreSync() ||
        core::NetworkManager::instance().isComputerServer())
    {
        return;
    }

    // not server
    const double t0 = glfwGetTime();
    while (core::NetworkManager::instance().isRunning() && _isRunning) {
        if (core::NetworkManager::instance().isSyncComplete()) {
            break;
        }

        if (UseSleepToWaitForNodes) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        else {
            std::unique_lock lk(core::mutex::FrameSync);
            core::NetworkManager::cond.wait(lk);
        }

        // for debugging
        core::Network* conn;
        if (glfwGetTime() - t0 <= 1.0) {
            continue;
        }

        // more than a second
        conn = core::NetworkManager::instance().getSyncConnectionByIndex(0);
        if (_printSyncMessage && !conn->isUpdated()) {
            MessageHandler::printInfo(
                "Slave: waiting for master... send frame %d != previous recv frame "
                "%d\n\tNvidia swap groups: %s\n\tNvidia swap barrier: %s\n\tNvidia "
                "universal frame number: %u\n\tSGCT frame number: %u",
                conn->getSendFrameCurrent(), conn->getRecvFramePrevious(),
                Window::isUsingSwapGroups() ? "enabled" : "disabled",
                Window::isBarrierActive() ? "enabled" : "disabled",
                Window::getSwapGroupFrameNumber(), _frameCounter
            );
        }

        if (glfwGetTime() - t0 > _syncTimeout) {
            throw Error(
                3006,
                "No sync signal from master after " + std::to_string(_syncTimeout) + " s"
            );
        }
    }

    // A this point all data needed for rendering a frame is received.
    // Let's signal that back to the master/server.
    core::NetworkManager::instance().sync(NetworkManager::SyncMode::AcknowledgeData);
    addValue(_statistics.syncTimes, glfwGetTime() - t0);
}

void Engine::frameLockPostStage() {
    // post stage
    if (core::ClusterManager::instance().getIgnoreSync() ||
        !core::NetworkManager::instance().isComputerServer())
    {
        return;
    }

    const double t0 = glfwGetTime();
    while (core::NetworkManager::instance().isRunning() && _isRunning &&
        core::NetworkManager::instance().getActiveConnectionsCount() > 0)
    {
        if (core::NetworkManager::instance().isSyncComplete()) {
            break;
        }

        if (UseSleepToWaitForNodes) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        else {
            std::unique_lock lk(core::mutex::FrameSync);
            core::NetworkManager::cond.wait(lk);
        }

        // for debugging
        if (glfwGetTime() - t0 <= 1.0) {
            continue;
        }
        // more than a second
        core::NetworkManager& nm = core::NetworkManager::instance();
        for (int i = 0; i < nm.getSyncConnectionsCount(); ++i) {
            if (_printSyncMessage && !nm.getConnectionByIndex(i).isUpdated()) {
                MessageHandler::printInfo(
                    "Waiting for slave%d: send frame %d != recv frame %d\n\t"
                    "Nvidia swap groups: %s\n\tNvidia swap barrier: %s\n\t"
                    "Nvidia universal frame number: %u\n\tSGCT frame number: %u",
                    i, nm.getConnectionByIndex(i).getSendFrameCurrent(),
                    nm.getConnectionByIndex(i).getRecvFrameCurrent(),
                    Window::isUsingSwapGroups() ? "enabled" : "disabled",
                    Window::isBarrierActive() ? "enabled" : "disabled",
                    Window::getSwapGroupFrameNumber(), _frameCounter
                );
            }
        }

        if (glfwGetTime() - t0 > _syncTimeout) {
            // more than a minute
            throw Error(
                3007,
                "No sync signal from slaves after " + std::to_string(_syncTimeout) + " s"
            );
        }
    }

    addValue(_statistics.syncTimes, glfwGetTime() - t0);
}

void Engine::render() {
    _isRunning = true;

    getCurrentWindow().makeOpenGLContextCurrent(Window::Context::Shared);
    glGenQueries(1, &_timeQueryBegin);
    glGenQueries(1, &_timeQueryEnd);

    core::Node& thisNode = core::ClusterManager::instance().getThisNode();
    while (_isRunning) {
        _renderingOffScreen = false;

        // update tracking data
        if (isMaster()) {
            TrackingManager::instance().updateTrackingDevices();
        }

        if (_preSyncFn) {
            _preSyncFn();
        }

        if (core::NetworkManager::instance().isComputerServer()) {
            SharedData::instance().encode();
        }
        else if (!core::NetworkManager::instance().isRunning()) {
            // exit if not running
            MessageHandler::printError("Network disconnected. Exiting");
            break;
        }

        frameLockPreStage();

        bool buffersNeedUpdate = false;
        for (int i = 0; i < thisNode.getNumberOfWindows(); ++i) {
            const bool bufUpdate = thisNode.getWindow(i).update();
            buffersNeedUpdate |= bufUpdate;
        }

        if (buffersNeedUpdate) {
            updateDrawBufferResolutions();
        }

        _renderingOffScreen = true;
        getCurrentWindow().makeOpenGLContextCurrent(Window::Context::Shared);

        // Make sure correct context is current
        if (_postSyncPreDrawFn) {
            _postSyncPreDrawFn();
        }

        const double startFrameTime = glfwGetTime();
        const double ft = static_cast<float>(startFrameTime - stats.prevTimestamp);
        addValue(_statistics.frametimes, ft);
        stats.prevTimestamp = startFrameTime;

        if (_showGraph) {
            glQueryCounter(_timeQueryBegin, GL_TIMESTAMP);
        }

        // Render Viewports / Draw
        _currentDrawBufferIndex = 0;
        size_t firstDrawBufferIndexInWindow = 0;

        for (int i = 0; i < thisNode.getNumberOfWindows(); ++i) {
            if (!(thisNode.getWindow(i).isVisible() ||
                  thisNode.getWindow(i).isRenderingWhileHidden()))
            {
                continue;
            }

            // store the first buffer index for each window
            firstDrawBufferIndexInWindow = _currentDrawBufferIndex;

            // @TODO (abock, 2019-09-02): This is kinda weird; I tried commenting this
            // part out and only use _thisNode->getWindow(i) directly, but then it failed
            // to have two separate rendering windows. So some hidden state somewhere, I
            // guess?!
            _currentWindowIndex = i;
            Window& win = getCurrentWindow();

            if (!_renderingOffScreen) {
                win.makeOpenGLContextCurrent(Window::Context::Window);
            }

            Window::StereoMode sm = win.getStereoMode();

            // Render Left/Mono non-linear projection viewports to cubemap
            _currentRenderTarget = RenderTarget::NonLinearBuffer;

            for (int j = 0; j < win.getNumberOfViewports(); ++j) {
                core::Viewport& vp = win.getViewport(j);
                _currentViewportIndex.main = j;
                if (!vp.hasSubViewports()) {
                    continue;
                }

                core::NonLinearProjection* nonLinearProj = vp.getNonLinearProjection();

                nonLinearProj->setAlpha(getCurrentWindow().hasAlpha() ? 0.f : 1.f);
                if (sm == Window::StereoMode::NoStereo) {
                    // for mono viewports frustum mode can be selected by user or xml
                    _currentFrustumMode = win.getViewport(j).getEye();
                    nonLinearProj->renderCubemap();
                }
                else {
                    _currentFrustumMode = core::Frustum::Mode::StereoLeftEye;
                    nonLinearProj->renderCubemap();
                }

                // FBO index, every window and every nonlinear projection has it's own FBO
                _currentDrawBufferIndex++;
            }

            // Render left/mono regular viewports to FBO
            _currentRenderTarget = RenderTarget::WindowBuffer;

            // if any stereo type (except passive) then set frustum mode to left eye
            if (sm == Window::StereoMode::NoStereo) {
                _currentFrustumMode = core::Frustum::Mode::MonoEye;
                renderViewports(TextureIndex::LeftEye);
            }
            else {
                _currentFrustumMode = core::Frustum::Mode::StereoLeftEye;
                renderViewports(TextureIndex::LeftEye);
            }

            // FBO index, every window and every non-linear projection has it's own FBO
            _currentDrawBufferIndex++;

            // if we are not rendering in stereo, we are done
            if (sm == Window::StereoMode::NoStereo) {
                continue;
            }

            // jump back counter to the first buffer index for current window
            _currentDrawBufferIndex = firstDrawBufferIndexInWindow;

            // Render right non-linear projection viewports to cubemap
            _currentRenderTarget = RenderTarget::NonLinearBuffer;
            for (int j = 0; j < win.getNumberOfViewports(); ++j) {
                _currentViewportIndex.main = j;
                core::Viewport& vp = win.getViewport(j);

                if (!vp.hasSubViewports()) {
                    continue;
                }
                core::NonLinearProjection* p = vp.getNonLinearProjection();

                p->setAlpha(getCurrentWindow().hasAlpha() ? 0.f : 1.f);
                _currentFrustumMode = core::Frustum::Mode::StereoRightEye;
                p->renderCubemap();

                // FBO index, every window and every nonlinear projection has it's own
                _currentDrawBufferIndex++;
            }

            // Render right regular viewports to FBO
            _currentRenderTarget = RenderTarget::WindowBuffer;

            _currentFrustumMode = core::Frustum::Mode::StereoRightEye;
            // use a single texture for side-by-side and top-bottom stereo modes
            if (sm >= Window::StereoMode::SideBySide) {
                renderViewports(TextureIndex::LeftEye);
            }
            else {
                renderViewports(TextureIndex::RightEye);
            }

            // FBO index, every window and every non-linear projection has their own
            _currentDrawBufferIndex++;
        }

        // Render to screen
        for (int i = 0; i < thisNode.getNumberOfWindows(); ++i) {
            if (thisNode.getWindow(i).isVisible()) {
                _currentWindowIndex = i;

                _renderingOffScreen = false;
                renderFBOTexture();
            }
        }
        getCurrentWindow().makeOpenGLContextCurrent(Window::Context::Shared);

        if (_showGraph) {
            glQueryCounter(_timeQueryEnd, GL_TIMESTAMP);
        }

        if (_postDrawFn) {
            _postDrawFn();
        }

        if (_showGraph) {
            // wait until the query results are available
            GLboolean done = GL_FALSE;
            while (!done) {
                glGetQueryObjectiv(_timeQueryEnd, GL_QUERY_RESULT_AVAILABLE, &done);
            }

            // get the query results
            GLuint64 timerStart;
            glGetQueryObjectui64v(_timeQueryBegin, GL_QUERY_RESULT, &timerStart);
            GLuint64 timerEnd;
            glGetQueryObjectui64v(_timeQueryEnd, GL_QUERY_RESULT, &timerEnd);

            const double t = static_cast<double>(timerEnd - timerStart) / 1000000000.0;
            addValue(_statistics.drawTimes, t);
        }

        if (_showGraph) {
            _statisticsRenderer->update();
        }
        
        // master will wait for nodes render before swapping
        frameLockPostStage();

        // Swap front and back rendering buffers
        for (int i = 0; i < thisNode.getNumberOfWindows(); ++i) {
            thisNode.getWindow(i).swap(_takeScreenshot);
        }

        glfwPollEvents();
        for (int i = 0; i < thisNode.getNumberOfWindows(); ++i) {
            thisNode.getWindow(i).updateResolutions();
        }

        // Check if exit key was pressed or window was closed
        _isRunning = !(thisNode.getKeyPressed(_exitKey) ||
                     thisNode.closeAllWindows() || _shouldTerminate ||
                     !core::NetworkManager::instance().isRunning());

        // for all windows
        _frameCounter++;
        if (_takeScreenshot) {
            _shotCounter++;
        }
        _takeScreenshot = false;
    }

    getCurrentWindow().makeOpenGLContextCurrent(Window::Context::Shared);
    glDeleteQueries(1, &_timeQueryBegin);
    glDeleteQueries(1, &_timeQueryEnd);
}

void Engine::renderDisplayInfo() {
#ifdef SGCT_HAS_TEXT
    const unsigned int lFrameNumber = Window::getSwapGroupFrameNumber();

    unsigned int fontSize = Settings::instance().getOSDTextFontSize();
    fontSize = static_cast<unsigned int>(
        static_cast<float>(fontSize) * getCurrentWindow().getScale().x
    );

    sgct::text::Font* font = text::FontManager::instance().getFont("SGCTFont", fontSize);

    if (font) {
        const float lineHeight = font->getHeight() * 1.59f;
        const glm::vec2 pos = glm::vec2(getCurrentWindow().getResolution()) *
                              Settings::instance().getOSDTextOffset();
        
        const core::Node& thisNode = core::ClusterManager::instance().getThisNode();
        text::print(
            *font,
            text::TextAlignMode::TopLeft,
            pos.x,
            lineHeight * 6.f + pos.y,
            glm::vec4(0.8f, 0.8f, 0.8f, 1.f),
            "Node ip: %s (%s)",
            thisNode.getAddress().c_str(),
            core::NetworkManager::instance().isComputerServer() ? "master" : "slave"
        );

        const double accFrameTime = std::accumulate(
            _statistics.frametimes.begin(),
            _statistics.frametimes.end(),
            0.0
        );
        const double avgFrametime = accFrameTime / Statistics::HistoryLength;
        const double avgFPS = 1.f / avgFrametime;
        
        text::print(
            *font,
            text::TextAlignMode::TopLeft,
            pos.x,
            lineHeight * 5.f + pos.y,
            glm::vec4(0.8f, 0.8f, 0.f, 1.f),
            "Frame rate: %.2f Hz, frame: %u", avgFPS, _frameCounter
        );

        const double accDrawtime = std::accumulate(
            _statistics.drawTimes.begin(),
            _statistics.drawTimes.end(),
            0.0
        );
        const double avgDrawtime = accDrawtime / Statistics::HistoryLength;
        text::print(
            *font,
            text::TextAlignMode::TopLeft,
            pos.x,
            lineHeight * 4.f + pos.y,
            glm::vec4(0.8f, 0.f, 0.8f, 1.f),
            "Avg. draw time: %.2f ms", avgDrawtime * 1000.f
        );

        const double accSynctime = std::accumulate(
            _statistics.syncTimes.begin(),
            _statistics.syncTimes.end(),
            0.0
        );
        const double avgSynctime = accSynctime / Statistics::HistoryLength;

        if (isMaster()) {
            text::print(
                *font,
                text::TextAlignMode::TopLeft,
                pos.x,
                lineHeight * 3.f + pos.y,
                glm::vec4(0.f, 0.8f, 0.8f, 1.f),
                "Avg. sync time: %.2f ms (%d bytes, comp: %.3f)", avgSynctime * 1000.0, 
                SharedData::instance().getUserDataSize(),
                SharedData::instance().getCompressionRatio()
            );
        }
        else {
            text::print(
                *font,
                text::TextAlignMode::TopLeft,
                pos.x,
                lineHeight * 3.f + pos.y,
                glm::vec4(0.f, 0.8f, 0.8f, 1.f),
                "Avg. sync time: %.2f ms", avgSynctime * 1000.0
            );
        }

        const bool usingSwapGroups = Window::isUsingSwapGroups();
        if (usingSwapGroups) {
            text::print(
                *font,
                text::TextAlignMode::TopLeft,
                pos.x,
                lineHeight * 2.f + pos.y,
                glm::vec4(0.8f, 0.8f, 0.8f, 1.f),
                "Swap groups: %s and barrier is %s (%s) | Frame: %d",
                Window::isUsingSwapGroups() ? "Enabled" : "Disabled",
                Window::isBarrierActive() ? "active" : "inactive",
                Window::isSwapGroupMaster() ? "master" : "slave", lFrameNumber
            );
        }
        else {
            text::print(
                *font,
                text::TextAlignMode::TopLeft,
                pos.x,
                lineHeight * 2.f + pos.y,
                glm::vec4(0.8f, 0.8f, 0.8f, 1.f),
                "%s", "Swap groups: Disabled"
            );
        }

        text::print(
            *font,
            text::TextAlignMode::TopLeft,
            pos.x,
            lineHeight * 1.f + pos.y,
            glm::vec4(0.8f, 0.8f, 0.8f, 1.f),
            "Frame buffer resolution: %d x %d",
            getCurrentWindow().getFramebufferResolution().x,
            getCurrentWindow().getFramebufferResolution().y
        );

        // if active stereoscopic rendering
        if (_currentFrustumMode == core::Frustum::Mode::StereoLeftEye) {
            sgct::text::print(
                *font,
                text::TextAlignMode::TopLeft,
                pos.x,
                lineHeight * 8.f + pos.y,
                glm::vec4(0.8f, 0.8f, 0.8f, 1.f),
                "Stereo type: %s\nCurrent eye: Left",
                getStereoString(getCurrentWindow().getStereoMode()).c_str()
            );
        }
        else if (_currentFrustumMode == core::Frustum::Mode::StereoRightEye) {
            sgct::text::print(
                *font,
                text::TextAlignMode::TopLeft,
                pos.x,
                lineHeight * 8.f + pos.y,
                glm::vec4(0.8f, 0.8f, 0.8f, 1.f),
                "Stereo type: %s\nCurrent eye:          Right",
                getStereoString(getCurrentWindow().getStereoMode()).c_str()
            );
        }
    }
#endif // SGCT_HAS_TEXT
}

void Engine::draw() {
    // run scissor test to prevent clearing of entire buffer
    glEnable(GL_SCISSOR_TEST);
    
    enterCurrentViewport();
    setAndClearBuffer(BufferMode::RenderToTexture);
    glDisable(GL_SCISSOR_TEST);

    if (_drawFn) {
        _drawFn();
    }
}

void Engine::drawOverlays() {
    for (int i = 0; i < getCurrentWindow().getNumberOfViewports(); ++i) {
        getCurrentWindow().setCurrentViewport(i);
        const core::Viewport& vp = getCurrentWindow().getViewport(i);
        
        // if viewport has overlay
        if (!vp.hasOverlayTexture() || !vp.isEnabled()) {
            continue;
        }

        enterCurrentViewport();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, vp.getOverlayTextureIndex());
        _shader.overlay.bind();
        glUniform1i(_shaderLoc.overlayTex, 0);
        getCurrentWindow().bindVAO();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        getCurrentWindow().unbindVAO();
        ShaderProgram::unbind();
    }
}

void Engine::prepareBuffer(TextureIndex ti) {
    if (getCurrentWindow().usePostFX()) {
        ti = TextureIndex::Intermediate;
    }

    core::OffScreenBuffer* fbo = getCurrentWindow().getFBO();
    fbo->bind();
    if (fbo->isMultiSampled()) {
        return;
    }

    // update attachments
    fbo->attachColorTexture(getCurrentWindow().getFrameBufferTexture(ti));

    if (Settings::instance().useDepthTexture()) {
        fbo->attachDepthTexture(
            getCurrentWindow().getFrameBufferTexture(TextureIndex::Depth)
        );
    }

    if (Settings::instance().useNormalTexture()) {
        fbo->attachColorTexture(
            getCurrentWindow().getFrameBufferTexture(TextureIndex::Normals),
            GL_COLOR_ATTACHMENT1
        );
    }

    if (Settings::instance().usePositionTexture()) {
        fbo->attachColorTexture(
            getCurrentWindow().getFrameBufferTexture(TextureIndex::Positions),
            GL_COLOR_ATTACHMENT2
        );
    }
}

void Engine::renderFBOTexture() {
    core::OffScreenBuffer::unbind();

    Window& win = getCurrentWindow();
    win.makeOpenGLContextCurrent(Window::Context::Window);

    glDisable(GL_BLEND);
    // needed for shaders
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    _currentFrustumMode = (win.getStereoMode() == Window::StereoMode::Active) ?
        core::Frustum::Mode::StereoLeftEye :
        core::Frustum::Mode::MonoEye;

    const glm::ivec2 size = glm::ivec2(
        glm::ceil(win.getScale() * glm::vec2(win.getResolution()))
    );

    glViewport(0, 0, size.x, size.y);
    setAndClearBuffer(BufferMode::BackBufferBlack);
   
    Window::StereoMode sm = win.getStereoMode();
    bool maskShaderSet = false;
    if (sm > Window::StereoMode::Active && sm < Window::StereoMode::SideBySide) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, win.getFrameBufferTexture(TextureIndex::LeftEye));

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, win.getFrameBufferTexture(TextureIndex::RightEye));

        win.bindStereoShaderProgram();

        glUniform1i(win.getStereoShaderLeftTexLoc(), 0);
        glUniform1i(win.getStereoShaderRightTexLoc(), 1);

        for (int i = 0; i < win.getNumberOfViewports(); i++) {
            if (Settings::instance().getUseWarping()) {
                win.getViewport(i).renderWarpMesh();
            }
            else {
                win.getViewport(i).renderQuadMesh();
            }
        }
    }
    else {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, win.getFrameBufferTexture(TextureIndex::LeftEye));

        _shader.fboQuad.bind();
        glUniform1i(_shaderLoc.monoTex, 0);
        maskShaderSet = true;

        for (int i = 0; i < win.getNumberOfViewports(); ++i) {
            if (Settings::instance().getUseWarping()) {
                win.getViewport(i).renderWarpMesh();
            }
            else {
                win.getViewport(i).renderQuadMesh();
            }
        }

        // render right eye in active stereo mode
        if (win.getStereoMode() == Window::StereoMode::Active) {
            glViewport(0, 0, size.x, size.y);
            
            //clear buffers
            _currentFrustumMode = core::Frustum::Mode::StereoRightEye;
            setAndClearBuffer(BufferMode::BackBufferBlack);

            glBindTexture(
                GL_TEXTURE_2D,
                win.getFrameBufferTexture(TextureIndex::RightEye)
            );
            for (int i = 0; i < win.getNumberOfViewports(); ++i) {
                if (Settings::instance().getUseWarping()) {
                    win.getViewport(i).renderWarpMesh();
                }
                else {
                    win.getViewport(i).renderQuadMesh();
                }
            }
        }
    }

    // render mask (mono)
    if (win.hasAnyMasks()) {
        if (!maskShaderSet) {
            _shader.fboQuad.bind();
            glUniform1i(_shaderLoc.monoTex, 0);
        }
        
        glDrawBuffer(win.isDoubleBuffered() ? GL_BACK : GL_FRONT);
        glReadBuffer(win.isDoubleBuffered() ? GL_BACK : GL_FRONT);
        glActiveTexture(GL_TEXTURE0);
        glEnable(GL_BLEND);

        // Result = (Color * BlendMask) * (1-BlackLevel) + BlackLevel
        // render blend masks
        glBlendFunc(GL_ZERO, GL_SRC_COLOR);
        for (int i = 0; i < win.getNumberOfViewports(); ++i) {
            const core::Viewport& vp = win.getViewport(i);
            if (vp.hasBlendMaskTexture() && vp.isEnabled()) {
                glBindTexture(GL_TEXTURE_2D, vp.getBlendMaskTextureIndex());
                vp.renderMaskMesh();
            }
        }

        // render black level masks
        for (int i = 0; i < win.getNumberOfViewports(); ++i) {
            const core::Viewport& vp = win.getViewport(i);
            if (vp.hasBlackLevelMaskTexture() && vp.isEnabled()) {
                glBindTexture(GL_TEXTURE_2D, vp.getBlackLevelMaskTextureIndex());

                // inverse multiply
                glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
                vp.renderMaskMesh();

                // add
                glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                vp.renderMaskMesh();
            }
        }

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    ShaderProgram::unbind();
    glDisable(GL_BLEND);
}

void Engine::renderViewports(TextureIndex ti) {
    prepareBuffer(ti);

    Window::StereoMode sm = getCurrentWindow().getStereoMode();
    // render all viewports for selected eye
    for (int i = 0; i < getCurrentWindow().getNumberOfViewports(); ++i) {
        getCurrentWindow().setCurrentViewport(i);
        _currentViewportIndex.main = i;
        core::Viewport& vp = getCurrentWindow().getViewport(i);

        if (!vp.isEnabled()) {
            continue;
        }

        // if passive stereo or mono
        if (sm == Window::StereoMode::NoStereo) {
            _currentFrustumMode = vp.getEye();
        }

        if (vp.hasSubViewports()) {
            if (vp.isTracked()) {
                vp.getNonLinearProjection()->updateFrustums(
                    _currentFrustumMode,
                    _nearClippingPlaneDist,
                    _farClippingPlaneDist
                );
            }

            if (getCurrentWindow().shouldCallDraw3DFunction()) {
                vp.getNonLinearProjection()->render();
            }
        }
        else {
            // no subviewports
            if (vp.isTracked()) {
                vp.calculateFrustum(
                    _currentFrustumMode,
                    _nearClippingPlaneDist,
                    _farClippingPlaneDist
                );
            }

            // check if we want to blit the previous window before we do anything else
            if (getCurrentWindow().shouldBlitPreviousWindow()) {
                blitPreviousWindowViewport(_currentFrustumMode);
            }

            if (getCurrentWindow().shouldCallDraw3DFunction()) {
                draw();
            }
        }
    }

    // If we did not render anything, make sure we clear the screen at least
    if (!getCurrentWindow().shouldCallDraw3DFunction() &&
        !getCurrentWindow().shouldBlitPreviousWindow())
    {
        setAndClearBuffer(BufferMode::RenderToTexture);
    }

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    // for side-by-side or top-bottom mode, do postfx/blit only after rendering right eye
    const bool isSplitScreen = (sm >= Window::StereoMode::SideBySide);
    if (!(isSplitScreen && _currentFrustumMode == core::Frustum::Mode::StereoLeftEye)) {
        if (getCurrentWindow().usePostFX()) {
            // blit buffers
            updateRenderingTargets(ti); // only used if multisampled FBOs
            renderPostFX(ti);
            render2D();
            if (isSplitScreen) {
                // render left eye info and graph to render 2D items after post fx
                _currentFrustumMode = core::Frustum::Mode::StereoLeftEye;
                render2D();
            }
        }
        else {
            render2D();
            if (isSplitScreen) {
                // render left eye info and graph to render 2D items after post fx
                _currentFrustumMode = core::Frustum::Mode::StereoLeftEye;
                render2D();
            }

            updateRenderingTargets(ti); // only used if multisampled FBOs
        }
    }

    glDisable(GL_BLEND);
}

void Engine::render2D() {
    if (_showGraph && _statisticsRenderer == nullptr) {
        // User desired the graph
        _statisticsRenderer = std::make_unique<core::StatisticsRenderer>(_statistics);
    }
    if (!_showGraph && _statisticsRenderer != nullptr) {
        // User desired to remove the graph
        _statisticsRenderer = nullptr;
    }

    // draw viewport overlays if any
    drawOverlays();

    // draw info & stats
    // the cubemap viewports are all the same so it makes no sense to render everything
    // several times therefore just loop one iteration in that case.
    if (!(_showGraph || _showInfo || _draw2DFn)) {
        return;
    }

    for (int i = 0; i < getCurrentWindow().getNumberOfViewports(); ++i) {
        getCurrentWindow().setCurrentViewport(i);
        _currentViewportIndex.main = i;

        if (!getCurrentWindow().getCurrentViewport()->isEnabled()) {
            continue;
        }
        enterCurrentViewport();

        if (_showGraph) {
            _statisticsRenderer->render();
        }
        // The text renderer enters automatically the correct viewport
        if (_showInfo) {
            // choose specified eye from config
            if (getCurrentWindow().getStereoMode() == Window::StereoMode::NoStereo) {
                _currentFrustumMode = getCurrentWindow().getCurrentViewport()->getEye();
            }
            renderDisplayInfo();
        }

        // Check if we should call the use defined draw2D function
        if (_draw2DFn && getCurrentWindow().shouldCallDraw2DFunction()) {
            _draw2DFn();
        }
    }
}

void Engine::renderPostFX(TextureIndex targetIndex) {
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    int numberOfPasses = getCurrentWindow().getNumberOfPostFXs();
    for (int i = 0; i < numberOfPasses; i++) {
        PostFX& fx = getCurrentWindow().getPostFX(i);

        // set output
        if (i == (numberOfPasses - 1) && !getCurrentWindow().useFXAA()) {
            // if last
            fx.setOutputTexture(
                getCurrentWindow().getFrameBufferTexture(targetIndex)
            );
        }
        else {
            // ping pong between the two FX buffers
            fx.setOutputTexture(
                getCurrentWindow().getFrameBufferTexture(
                    (i % 2 == 0) ? TextureIndex::FX1 : TextureIndex::FX2
                )
            ); 
        }

        // set input (dependent on output)
        if (i == 0) {
            fx.setInputTexture(
                getCurrentWindow().getFrameBufferTexture(TextureIndex::Intermediate)
            );
        }
        else {
            PostFX& fxPrevious = getCurrentWindow().getPostFX(i - 1);
            fx.setInputTexture(fxPrevious.getOutputTexture());
        }

        fx.render(getCurrentWindow());
    }

    if (getCurrentWindow().useFXAA()) {
        PostFX* lastFx = (numberOfPasses > 0) ?
            &getCurrentWindow().getPostFX(numberOfPasses - 1) :
            nullptr;

        // bind target FBO
        getCurrentWindow().getFBO()->attachColorTexture(
            getCurrentWindow().getFrameBufferTexture(targetIndex)
        );

        glm::ivec2 framebufferSize = getCurrentWindow().getFramebufferResolution();
        glViewport(0, 0, framebufferSize.x, framebufferSize.y);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);

        glActiveTexture(GL_TEXTURE0);

        if (lastFx) {
            glBindTexture(GL_TEXTURE_2D, lastFx->getOutputTexture());
        }
        else {
            glBindTexture(
                GL_TEXTURE_2D,
                getCurrentWindow().getFrameBufferTexture(TextureIndex::Intermediate)
            );
        }

        _shader.fxaa.bind();
        glUniform1f(_shaderLoc.sizeX, static_cast<float>(framebufferSize.x));
        glUniform1f(_shaderLoc.sizeY, static_cast<float>(framebufferSize.y));
        glUniform1i(_shaderLoc.fxaaTexture, 0);
        glUniform1f(_shaderLoc.fxaaSubPixTrim, Settings::instance().getFXAASubPixTrim());
        glUniform1f(
            _shaderLoc.fxaaSubPixOffset,
            Settings::instance().getFXAASubPixOffset()
        );

        getCurrentWindow().bindVAO();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        getCurrentWindow().unbindVAO();

        ShaderProgram::unbind();
    }
}

void Engine::updateRenderingTargets(TextureIndex ti) {
    // copy AA-buffer to "regular" / non-AA buffer
    core::OffScreenBuffer* fbo = getCurrentWindow().getFBO();
    if (!fbo->isMultiSampled()) {
        return;
    }

    if (getCurrentWindow().usePostFX()) {
        ti = TextureIndex::Intermediate;
    }

    // bind separate read and draw buffers to prepare blit operation
    fbo->bindBlit();

    // update attachments
    fbo->attachColorTexture(getCurrentWindow().getFrameBufferTexture(ti));

    if (Settings::instance().useDepthTexture()) {
        fbo->attachDepthTexture(
            getCurrentWindow().getFrameBufferTexture(TextureIndex::Depth)
        );
    }

    if (Settings::instance().useNormalTexture()) {
        fbo->attachColorTexture(
            getCurrentWindow().getFrameBufferTexture(TextureIndex::Normals),
            GL_COLOR_ATTACHMENT1
        );
    }

    if (Settings::instance().usePositionTexture()) {
        fbo->attachColorTexture(
            getCurrentWindow().getFrameBufferTexture(TextureIndex::Positions),
            GL_COLOR_ATTACHMENT2
        );
    }

    fbo->blit();
}

void Engine::loadShaders() {
    _shader.fxaa = ShaderProgram("FXAAShader");
    _shader.fxaa.addShaderSource(core::shaders::FXAAVert, core::shaders::FXAAFrag);
    _shader.fxaa.createAndLinkProgram();
    _shader.fxaa.bind();

    _shaderLoc.sizeX = _shader.fxaa.getUniformLocation("rt_w");
    const glm::ivec2 framebufferSize = getCurrentWindow().getFramebufferResolution();
    glUniform1f(_shaderLoc.sizeX, static_cast<float>(framebufferSize.x));

    _shaderLoc.sizeY = _shader.fxaa.getUniformLocation("rt_h");
    glUniform1f(_shaderLoc.sizeY, static_cast<float>(framebufferSize.y));

    _shaderLoc.fxaaSubPixTrim = _shader.fxaa.getUniformLocation("FXAA_SUBPIX_TRIM");
    glUniform1f(_shaderLoc.fxaaSubPixTrim, Settings::instance().getFXAASubPixTrim());

    _shaderLoc.fxaaSubPixOffset = _shader.fxaa.getUniformLocation("FXAA_SUBPIX_OFFSET");
    glUniform1f(_shaderLoc.fxaaSubPixOffset, Settings::instance().getFXAASubPixOffset());

    _shaderLoc.fxaaTexture = _shader.fxaa.getUniformLocation("tex");
    glUniform1i(_shaderLoc.fxaaTexture, 0);
    ShaderProgram::unbind();

    // Used for overlays & mono.
    _shader.fboQuad = ShaderProgram("FBOQuadShader");
    _shader.fboQuad.addShaderSource(core::shaders::BaseVert, core::shaders::BaseFrag);
    _shader.fboQuad.createAndLinkProgram();
    _shader.fboQuad.bind();
    _shaderLoc.monoTex = _shader.fboQuad.getUniformLocation("Tex");
    glUniform1i(_shaderLoc.monoTex, 0);
    ShaderProgram::unbind();

    _shader.overlay = ShaderProgram("OverlayShader");
    _shader.overlay.addShaderSource(
        core::shaders::OverlayVert,
        core::shaders::OverlayFrag
    );
    _shader.overlay.createAndLinkProgram();
    _shader.overlay.bind();
    _shaderLoc.overlayTex = _shader.overlay.getUniformLocation("Tex");
    glUniform1i(_shaderLoc.overlayTex, 0);
    ShaderProgram::unbind();
}

void Engine::setAndClearBuffer(BufferMode mode) {
    if (mode < BufferMode::RenderToTexture) {
        const bool doubleBuffered = getCurrentWindow().isDoubleBuffered();
        // Set buffer
        if (getCurrentWindow().getStereoMode() != Window::StereoMode::Active) {
            glDrawBuffer(doubleBuffered ? GL_BACK : GL_FRONT);
            glReadBuffer(doubleBuffered ? GL_BACK : GL_FRONT);
        }
        else if (_currentFrustumMode == core::Frustum::Mode::StereoLeftEye) {
            // if active left
            glDrawBuffer(doubleBuffered ? GL_BACK_LEFT : GL_FRONT_LEFT);
            glReadBuffer(doubleBuffered ? GL_BACK_LEFT : GL_FRONT_LEFT);
        }
        else if (_currentFrustumMode == core::Frustum::Mode::StereoRightEye) {
            // if active right
            glDrawBuffer(doubleBuffered ? GL_BACK_RIGHT : GL_FRONT_RIGHT);
            glReadBuffer(doubleBuffered ? GL_BACK_RIGHT : GL_FRONT_RIGHT);
        }
    }

    // clear
    if (mode != BufferMode::BackBufferBlack) {
        clearBuffer();
    }
    else {
        // when rendering textures to backbuffer (using fbo)
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
}

bool Engine::isMaster() const {
    return core::NetworkManager::instance().isComputerServer();
}

core::Frustum::Mode Engine::getCurrentFrustumMode() const {
    return _currentFrustumMode;
}

const glm::mat4& Engine::getCurrentProjectionMatrix() const {
    const core::BaseViewport& vp = *getCurrentWindow().getCurrentViewport();
    return vp.getProjection(_currentFrustumMode).getProjectionMatrix();
}

const glm::mat4& Engine::getCurrentViewMatrix() const {
    const core::BaseViewport& vp = *getCurrentWindow().getCurrentViewport();
    return vp.getProjection(_currentFrustumMode).getViewMatrix();
}

const glm::mat4& Engine::getModelMatrix() const {
    return core::ClusterManager::instance().getSceneTransform();
}

const glm::mat4& Engine::getCurrentViewProjectionMatrix() const {
    const core::BaseViewport& vp = *getCurrentWindow().getCurrentViewport();
    return vp.getProjection(_currentFrustumMode).getViewProjectionMatrix();
}

glm::mat4 Engine::getCurrentModelViewProjectionMatrix() const {
    const core::BaseViewport& vp = *getCurrentWindow().getCurrentViewport();
    return vp.getProjection(_currentFrustumMode).getViewProjectionMatrix() * 
           core::ClusterManager::instance().getSceneTransform();
}

glm::mat4 Engine::getCurrentModelViewMatrix() const {
    const core::BaseViewport& vp = *getCurrentWindow().getCurrentViewport();
    return vp.getProjection(_currentFrustumMode).getViewMatrix() *
           core::ClusterManager::instance().getSceneTransform();
}

unsigned int Engine::getCurrentFrameNumber() const {
    return _frameCounter;
}

void Engine::waitForAllWindowsInSwapGroupToOpen() {
    core::Node& thisNode = core::ClusterManager::instance().getThisNode();

    // clear the buffers initially
    for (int i = 0; i < thisNode.getNumberOfWindows(); ++i) {
        thisNode.getWindow(i).makeOpenGLContextCurrent(Window::Context::Window);
        glDrawBuffer(getCurrentWindow().isDoubleBuffered() ? GL_BACK : GL_FRONT);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (thisNode.getWindow(i).isDoubleBuffered()) {
            glfwSwapBuffers(thisNode.getWindow(i).getWindowHandle());
        }
        else {
            glFinish();
        }
    }
    glfwPollEvents();
    
    // Must wait until all nodes are running if using swap barrier
    if (core::ClusterManager::instance().getIgnoreSync() ||
        core::ClusterManager::instance().getNumberOfNodes() <= 1)
    {
        return;
    }

    // check if swapgroups are supported
#ifdef WIN32
    const bool hasSwapGroup = glfwExtensionSupported("WGL_NV_swap_group");
#else
    const bool hasSwapGroup = false;
#endif
    if (hasSwapGroup) {
        MessageHandler::printInfo("Swap groups are supported by hardware");
    }
    else {
        MessageHandler::printInfo("Swap groups are not supported by hardware");
    }
    MessageHandler::printInfo("Waiting for all nodes to connect");

    while (core::NetworkManager::instance().isRunning() &&
           !thisNode.getKeyPressed(_exitKey) && !thisNode.closeAllWindows() &&
           !_shouldTerminate)
    {
        // Swap front and back rendering buffers
        for (int i = 0; i < thisNode.getNumberOfWindows(); i++) {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (thisNode.getWindow(i).isDoubleBuffered()) {
                glfwSwapBuffers(thisNode.getWindow(i).getWindowHandle());
            }
            else {
                glFinish();
            }
        }
        glfwPollEvents();

        if (core::NetworkManager::instance().areAllNodesConnected()) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // wait for user to release exit key
    while (thisNode.getKeyPressed(_exitKey)) {
        // Swap front and back rendering buffers; key buffers also swapped
        for (int i = 0; i < thisNode.getNumberOfWindows(); i++) {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (thisNode.getWindow(i).isDoubleBuffered()) {
                glfwSwapBuffers(thisNode.getWindow(i).getWindowHandle());
            }
            else {
                glFinish();
            }
        }
        glfwPollEvents();

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void Engine::updateFrustums() {
    core::Node& thisNode = core::ClusterManager::instance().getThisNode();
    for (int w = 0; w < thisNode.getNumberOfWindows(); w++) {
        Window& win = thisNode.getWindow(w);
        for (int i = 0; i < win.getNumberOfViewports(); i++) {
            core::Viewport& vp = win.getViewport(i);
            if (vp.isTracked()) {
                // if not tracked update, otherwise this is done on the fly
                continue;
            }

            if (vp.hasSubViewports()) {
                core::NonLinearProjection& proj = *vp.getNonLinearProjection();
                proj.updateFrustums(
                    core::Frustum::Mode::MonoEye,
                    _nearClippingPlaneDist,
                    _farClippingPlaneDist
                );

                proj.updateFrustums(
                    core::Frustum::Mode::StereoLeftEye,
                    _nearClippingPlaneDist,
                    _farClippingPlaneDist
                );

                proj.updateFrustums(
                    core::Frustum::Mode::StereoRightEye,
                    _nearClippingPlaneDist,
                    _farClippingPlaneDist
                );
            }
            else {
                vp.calculateFrustum(
                    core::Frustum::Mode::MonoEye,
                    _nearClippingPlaneDist,
                    _farClippingPlaneDist
                );

                vp.calculateFrustum(
                    core::Frustum::Mode::StereoLeftEye,
                    _nearClippingPlaneDist,
                    _farClippingPlaneDist
                );

                vp.calculateFrustum(
                    core::Frustum::Mode::StereoRightEye,
                    _nearClippingPlaneDist,
                    _farClippingPlaneDist
                );
            }
        }
    }
}

void Engine::blitPreviousWindowViewport(core::Frustum::Mode mode) {
    // Check that we have a previous window
    if (getCurrentWindowIndex() < 1) {
        MessageHandler::printWarning("Cannot blit previous window, as this is the first");
        return;
    }

    Window& previousWindow = getWindow(getCurrentWindowIndex() - 1);

    // run scissor test to prevent clearing of entire buffer
    glEnable(GL_SCISSOR_TEST);
    enterCurrentViewport();
    setAndClearBuffer(BufferMode::RenderToTexture);
    glDisable(GL_SCISSOR_TEST);

    _shader.overlay.bind();
    glUniform1i(_shaderLoc.overlayTex, 0);

    glActiveTexture(GL_TEXTURE0);
    TextureIndex m = [](core::Frustum::Mode m) {
        switch (m) {
            // abock (2019-09-27) Yep, I'm confused about this mapping, too. But I just
            // took the enumerations values as they were and I assume that it was an
            // undetected bug
            default:
            case core::Frustum::Mode::MonoEye: return TextureIndex::LeftEye;
            case core::Frustum::Mode::StereoLeftEye: return TextureIndex::RightEye;
            case core::Frustum::Mode::StereoRightEye: return TextureIndex::Intermediate;
        }
    }(mode);
    glBindTexture(GL_TEXTURE_2D, previousWindow.getFrameBufferTexture(m));

    getCurrentWindow().bindVAO();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    getCurrentWindow().unbindVAO();
    ShaderProgram::unbind();
}

void Engine::setDrawFunction(std::function<void()> fn) {
    _drawFn = std::move(fn);
}

void Engine::setDraw2DFunction(std::function<void()> fn) {
    _draw2DFn = std::move(fn);
}

void Engine::setPreSyncFunction(std::function<void()> fn) {
    _preSyncFn = std::move(fn);
}

void Engine::setPostSyncPreDrawFunction(std::function<void()> fn) {
    _postSyncPreDrawFn = std::move(fn);
}

void Engine::setPostDrawFunction(std::function<void()> fn) {
    _postDrawFn = std::move(fn);
}

void Engine::setInitOGLFunction(std::function<void()> fn) {
    _initOpenGLFn = std::move(fn);
}

void Engine::setPreWindowFunction(std::function<void()> fn) {
    _preWindowFn = std::move(fn);
}

void Engine::setCleanUpFunction(std::function<void()> fn) {
    _cleanUpFn = std::move(fn);
}

void Engine::setEncodeFunction(std::function<void()> fn) {
    SharedData::instance().setEncodeFunction(std::move(fn));
}

void Engine::setDecodeFunction(std::function<void()> fn) {
    SharedData::instance().setDecodeFunction(std::move(fn));
}

void Engine::setExternalControlCallback(std::function<void(const char*, int)> fn) {
    _externalDecodeCallbackFn = std::move(fn);
}

void Engine::setExternalControlStatusCallback(std::function<void(bool)> fn) {
    _externalStatusCallbackFn = std::move(fn);
}

void Engine::setDataTransferCallback(std::function<void(void*, int, int, int)> fn) {
    _dataTransferDecodeCallbackFn = std::move(fn);
}

void Engine::setDataTransferStatusCallback(std::function<void(bool, int)> fn) {
    _dataTransferStatusCallbackFn = std::move(fn);
}

void Engine::setDataAcknowledgeCallback(std::function<void(int, int)> fn) {
    _dataTransferAcknowledgeCallbackFn = std::move(fn);
}

void Engine::setContextCreationCallback(std::function<void(GLFWwindow*)> fn) {
    _contextCreationFn = std::move(fn);
}

void Engine::setScreenShotCallback(std::function<void(core::Image*, size_t,
                                   core::ScreenCapture::EyeIndex, GLenum type)> fn)
{
    _screenShotFn = std::move(fn);
}

void Engine::setKeyboardCallbackFunction(std::function<void(int, int, int, int)> fn) {
    gKeyboardCallbackFn = std::move(fn);
}

void Engine::setCharCallbackFunction(std::function<void(unsigned int, int)> fn) {
    gCharCallbackFn = std::move(fn);
}

void Engine::setMouseButtonCallbackFunction(std::function<void(int, int, int)> fn) {
    gMouseButtonCallbackFn = std::move(fn);
}

void Engine::setMousePosCallbackFunction(std::function<void(double, double)> fn) {
    gMousePosCallbackFn = std::move(fn);
}

void Engine::setMouseScrollCallbackFunction(std::function<void(double, double)> fn) {
    gMouseScrollCallbackFn = std::move(fn);
}

void Engine::setDropCallbackFunction(std::function<void(int, const char**)> fn) {
    gDropCallbackFn = std::move(fn);
}

 void Engine::setTouchCallbackFunction(std::function<void(const core::Touch*)> fn) {
     gTouchCallbackFn = std::move(fn);
 }

void Engine::clearBuffer() {
    const glm::vec4 color = Engine::instance().getClearColor();

    const float alpha = instance().getCurrentWindow().hasAlpha() ? 0.f : color.a;
    glClearColor(color.r, color.g, color.b, alpha);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Engine::enterCurrentViewport() {
    core::BaseViewport* vp = getCurrentWindow().getCurrentViewport();
    
    const glm::vec2 res = glm::vec2(getCurrentWindow().getFramebufferResolution());
    const glm::vec2 p = vp->getPosition() * res;
    const glm::vec2 s = vp->getSize() * res;
    _currentViewportCoords = glm::ivec4(glm::ivec2(p), glm::ivec2(s));

    Window::StereoMode sm = getCurrentWindow().getStereoMode();
    if (_currentFrustumMode == core::Frustum::Mode::StereoLeftEye) {
        switch (sm) {
            case Window::StereoMode::SideBySide:
                _currentViewportCoords.x /= 2;
                _currentViewportCoords.z /= 2;
                break;
            case Window::StereoMode::SideBySideInverted:
                _currentViewportCoords.x =
                    (_currentViewportCoords.x / 2) + (_currentViewportCoords.z / 2);
                _currentViewportCoords.z = _currentViewportCoords.z / 2;
                break;
            case Window::StereoMode::TopBottom:
                _currentViewportCoords.y =
                    (_currentViewportCoords.y / 2) + (_currentViewportCoords.w / 2);
                _currentViewportCoords.w /= 2;
                break;
            case Window::StereoMode::TopBottomInverted:
                _currentViewportCoords.y /= 2;
                _currentViewportCoords.w /= 2;
                break;
            default:
                break;
        }
    }
    else {
        switch (sm) {
            case Window::StereoMode::SideBySide:
                _currentViewportCoords.x =
                    (_currentViewportCoords.x / 2) + (_currentViewportCoords.z / 2);
                _currentViewportCoords.z /= 2;
                break;
            case Window::StereoMode::SideBySideInverted:
                _currentViewportCoords.x /= 2;
                _currentViewportCoords.z /= 2;
                break;
            case Window::StereoMode::TopBottom:
                _currentViewportCoords.y /= 2;
                _currentViewportCoords.w /= 2;
                break;
            case Window::StereoMode::TopBottomInverted:
                _currentViewportCoords.y =
                    (_currentViewportCoords.y / 2) + (_currentViewportCoords.w / 2);
                _currentViewportCoords.w /= 2;
                break;
            default:
                break;
        }
    }

    glViewport(
        _currentViewportCoords.x,
        _currentViewportCoords.y,
        _currentViewportCoords.z,
        _currentViewportCoords.w
    );
    
    glScissor(
        _currentViewportCoords.x,
        _currentViewportCoords.y,
        _currentViewportCoords.z,
        _currentViewportCoords.w
    );
}

const Engine::Statistics& Engine::getStatistics() const {
    return _statistics;
}

double Engine::getDt() const {
    return _statistics.frametimes[0];
}

double Engine::getAvgFPS() const {
    return 1.0 / getAvgDt();
}

double Engine::getAvgDt() const {
    const double accFrameTime = std::accumulate(
        _statistics.frametimes.begin(),
        _statistics.frametimes.end(),
        0.0
    );
    const double avgFrametime = accFrameTime / Statistics::HistoryLength;
    return avgFrametime;
}

double Engine::getMinDt() const {
    return *std::min_element(
        _statistics.frametimes.begin(),
        _statistics.frametimes.end()
    );
}

double Engine::getMaxDt() const {
    return *std::max_element(
        _statistics.frametimes.begin(),
        _statistics.frametimes.end()
    );
}

double Engine::getDtStandardDeviation() const {
    const double avg = getAvgDt();
    const double sumSquare = std::accumulate(
        _statistics.frametimes.begin(),
        _statistics.frametimes.end(),
        0.0,
        [avg](double cur, double rhs) { return cur + pow(rhs - avg, 2.0); }
    );
    return sumSquare / Statistics::HistoryLength;
}

glm::vec4 Engine::getClearColor() const {
    return _clearColor;
}

float Engine::getNearClippingPlane() const {
    return _nearClippingPlaneDist;
}

float Engine::getFarClippingPlane() const {
    return _farClippingPlaneDist;
}

void Engine::setNearAndFarClippingPlanes(float nearClip, float farClip) {
    _nearClippingPlaneDist = nearClip;
    _farClippingPlaneDist = farClip;
    updateFrustums();
}

void Engine::setEyeSeparation(float eyeSeparation) {
    core::Node& thisNode = core::ClusterManager::instance().getThisNode();
    for (int w = 0; w < thisNode.getNumberOfWindows(); w++) {
        Window& window = thisNode.getWindow(w);

        for (int i = 0; i < window.getNumberOfViewports(); i++) {
            window.getViewport(i).getUser().setEyeSeparation(eyeSeparation);
        }
    }
    updateFrustums();
}

void Engine::setClearColor(glm::vec4 color) {
    _clearColor = std::move(color);
}

void Engine::setExitKey(int key) {
    _exitKey = key;
}

unsigned int Engine::getCurrentDrawTexture() const {
    if (getCurrentWindow().usePostFX()) {
        return getCurrentWindow().getFrameBufferTexture(TextureIndex::Intermediate);
    }
    else {
        return getCurrentWindow().getFrameBufferTexture(
            (_currentFrustumMode == core::Frustum::Mode::StereoRightEye) ?
                TextureIndex::RightEye : TextureIndex::LeftEye
        );
    }
}

// @TODO (abock, 2019-11-01) Remove the following three functions if possible
unsigned int Engine::getCurrentDepthTexture() const {
    return getCurrentWindow().getFrameBufferTexture(TextureIndex::Depth);
}

unsigned int Engine::getCurrentNormalTexture() const {
    return getCurrentWindow().getFrameBufferTexture(TextureIndex::Normals);
}

unsigned int Engine::getCurrentPositionTexture() const {
    return getCurrentWindow().getFrameBufferTexture(TextureIndex::Positions);
}

glm::ivec2 Engine::getCurrentResolution() const {
    return getCurrentWindow().getFramebufferResolution();
}

int Engine::getFocusedWindowIndex() const {
    core::Node& thisNode = core::ClusterManager::instance().getThisNode();
    for (int i = 0; i < thisNode.getNumberOfWindows(); i++) {
        if (thisNode.getWindow(i).isFocused()) {
            return i;
        }
    }
    return 0; // no window has focus
}

void Engine::setDisplayInfoVisibility(bool state) {
    _showInfo = state;
}

void Engine::setStatsGraphVisibility(bool state) {
    _showGraph = state;
}

void Engine::takeScreenshot() {
    _takeScreenshot = true;
}

void Engine::invokeDecodeCallbackForExternalControl(const char* data, int length, int) {
    if (_externalDecodeCallbackFn && length > 0) {
        _externalDecodeCallbackFn(data, length);
    }
}

void Engine::invokeUpdateCallbackForExternalControl(bool connected) {
    if (_externalStatusCallbackFn) {
        _externalStatusCallbackFn(connected);
    }
}

void Engine::invokeDecodeCallbackForDataTransfer(void* data, int length, int package,
                                                 int client)
{
    if (_dataTransferDecodeCallbackFn && length > 0) {
        _dataTransferDecodeCallbackFn(data, length, package, client);
    }
}

void Engine::invokeUpdateCallbackForDataTransfer(bool connected, int clientId) {
    if (_dataTransferStatusCallbackFn) {
        _dataTransferStatusCallbackFn(connected, clientId);
    }
}

void Engine::invokeAcknowledgeCallbackForDataTransfer(int packageId, int clientId) {
    if (_dataTransferAcknowledgeCallbackFn) {
        _dataTransferAcknowledgeCallbackFn(packageId, clientId);
    }
}

void Engine::sendMessageToExternalControl(const void* data, int length) {
    if (core::NetworkManager::instance().getExternalControlConnection()) {
        core::NetworkManager::instance().getExternalControlConnection()->sendData(
            data,
            length
        );
    }
}

void Engine::setDataTransferCompression(bool state, int level) {
    core::NetworkManager::instance().setDataTransferCompression(state, level);
}

void Engine::transferDataBetweenNodes(const void* data, int length, int packageId) {
    core::NetworkManager::instance().transferData(data, length, packageId);
}

void Engine::transferDataToNode(const void* data, int length, int package, size_t node) {
    core::NetworkManager::instance().transferData(data, length, package, node);
}

void Engine::sendMessageToExternalControl(const std::string& msg) {
    if (core::NetworkManager::instance().getExternalControlConnection()) {
        core::NetworkManager::instance().getExternalControlConnection()->sendData(
            msg.c_str(),
            static_cast<int>(msg.size())
        );
    }
}

bool Engine::isExternalControlConnected() const {
    return (core::NetworkManager::instance().getExternalControlConnection() &&
        core::NetworkManager::instance().getExternalControlConnection()->isConnected());
}

void Engine::setExternalControlBufferSize(unsigned int newSize) {
    if (core::NetworkManager::instance().getExternalControlConnection()) {
        core::NetworkManager::instance().getExternalControlConnection()->setBufferSize(
            newSize
        );
    }
}

void Engine::updateDrawBufferResolutions() {
    core::Node& thisNode = core::ClusterManager::instance().getThisNode();
    _drawBufferResolutions.clear();

    for (int i = 0; i < thisNode.getNumberOfWindows(); ++i) {
        Window& win = getWindow(i);

        // first add cubemap resolutions if any
        for (int j = 0; j < win.getNumberOfViewports(); ++j) {
            const core::Viewport& vp = win.getViewport(j);
            if (vp.hasSubViewports()) {
                int cubeRes = vp.getNonLinearProjection()->getCubemapResolution();
                _drawBufferResolutions.emplace_back(cubeRes, cubeRes);
            }
        }

        // second add window resolution
        const glm::ivec2 size = win.getFinalFBODimensions();
        _drawBufferResolutions.push_back(size);
    }
}

int Engine::getKey(int winIndex, int key) {
    return glfwGetKey(_instance->getWindow(winIndex).getWindowHandle(), key);
}

int Engine::getMouseButton(int winIndex, int button) {
    return glfwGetMouseButton(_instance->getWindow(winIndex).getWindowHandle(), button);
}

void Engine::getMousePos(int winIndex, double* xPos, double* yPos) {
    glfwGetCursorPos(_instance->getWindow(winIndex).getWindowHandle(), xPos, yPos);
}

void Engine::setMousePos(int winIndex, double xPos, double yPos) {
    glfwSetCursorPos(_instance->getWindow(winIndex).getWindowHandle(), xPos, yPos);
}

void Engine::setMouseCursorVisibility(int winIndex, bool state) {
    GLFWwindow* win = _instance->getWindow(winIndex).getWindowHandle();
    glfwSetInputMode(win, GLFW_CURSOR, state ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
}

const char* Engine::getJoystickName(int joystick) {
    return glfwGetJoystickName(joystick);
}

const float* Engine::getJoystickAxes(int joystick, int* numOfValues) {
    return glfwGetJoystickAxes(joystick, numOfValues);
}

const unsigned char* Engine::getJoystickButtons(int joystick, int* numOfValues) {
    return glfwGetJoystickButtons(joystick, numOfValues);
}

const core::Node& Engine::getThisNode() const {
    return core::ClusterManager::instance().getThisNode();
}

Window& Engine::getWindow(int index) const {
    return core::ClusterManager::instance().getThisNode().getWindow(index);
}

int Engine::getNumberOfWindows() const {
    return core::ClusterManager::instance().getThisNode().getNumberOfWindows();
}

Window& Engine::getCurrentWindow() const {
    return core::ClusterManager::instance().getThisNode().getWindow(_currentWindowIndex);
}

int Engine::getCurrentWindowIndex() const {
    return _currentWindowIndex;
}

core::User& Engine::getDefaultUser() {
    return core::ClusterManager::instance().getDefaultUser();
}

double Engine::getTime() {
    return glfwGetTime();
}

glm::ivec2 Engine::getCurrentViewportSize() const {
    return { _currentViewportCoords.z, _currentViewportCoords.w };
}

glm::ivec2 Engine::getCurrentDrawBufferSize() const {
    return _drawBufferResolutions[_currentDrawBufferIndex];
}

const std::vector<glm::ivec2>& Engine::getDrawBufferResolutions() const {
    return _drawBufferResolutions;
}

Engine::RenderTarget Engine::getCurrentRenderTarget() const {
    return _currentRenderTarget;
}

glm::ivec4 Engine::getCurrentViewportPixelCoords() const {
    const core::Viewport& vp = getCurrentWindow().getViewport(_currentViewportIndex.main);
    if (vp.hasSubViewports()) {
        return vp.getNonLinearProjection()->getViewportCoords();
    }
    else {
        return _currentViewportCoords;
    }
}

void Engine::setSyncParameters(bool printMessage, float timeout) {
    _printSyncMessage = printMessage;
    _syncTimeout = timeout;
}

void Engine::setScreenShotNumber(unsigned int number) {
    _shotCounter = number;
}

unsigned int Engine::getScreenShotNumber() const {
    return _shotCounter;
}

} // namespace sgct
