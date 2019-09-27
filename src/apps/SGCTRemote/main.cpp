#include <sgct.h>

namespace {
    sgct::Engine* gEngine;

    sgct::SharedDouble currentTime(0.0);

    sgct::SharedBool showStats(false);
    sgct::SharedBool showGraph(false);
    sgct::SharedBool showWireframe(false);
    sgct::SharedFloat sizeFactor(0.5f);
} // namespace

using namespace sgct;

void drawFun() {
    constexpr const float Speed = 50.0f;
    glRotatef(static_cast<float>(currentTime.getVal()) * Speed, 0.f, 1.f, 0.f);

    const float size = sizeFactor.getVal();

    glBegin(GL_TRIANGLES);
    glColor3f(1.f, 0.f, 0.f);
    glVertex3f(-0.5f * size, -0.5f * size, 0.f);

    glColor3f(0.f, 1.f, 0.f);
    glVertex3f(0.f, 0.5f * size, 0.f);

    glColor3f(0.f, 0.f, 1.f);
    glVertex3f(0.5f * size, -0.5f * size, 0.f);
    glEnd();
}

void preSyncFun() {
    // set the time only on the master
    if (gEngine->isMaster()) {
        currentTime.setVal(Engine::getTime());
    }
}

void postSyncPreDrawFun() {
    gEngine->setDisplayInfoVisibility(showStats.getVal());
    gEngine->setStatsGraphVisibility(showGraph.getVal());
    gEngine->setWireframe(showWireframe.getVal());
}

void encodeFun() {
    SharedData::instance()->writeDouble(currentTime);
    SharedData::instance()->writeFloat(sizeFactor);
    SharedData::instance()->writeBool(showStats);
    SharedData::instance()->writeBool(showGraph);
    SharedData::instance()->writeBool(showWireframe);
}

void decodeFun() {
    SharedData::instance()->readDouble(currentTime);
    SharedData::instance()->readFloat(sizeFactor);
    SharedData::instance()->readBool(showStats);
    SharedData::instance()->readBool(showGraph);
    SharedData::instance()->readBool(showWireframe);
}

void externalControlMessageCallback(const char* receivedChars, int size) {
    if (gEngine->isMaster()) {
        std::string_view msg(receivedChars, size);
        if (size == 7 && msg.substr(0, 5) == "stats") {
            showStats.setVal(msg.substr(6, 1) == "1");
        }
        else if (size == 7 && msg.substr(0, 5) == "graph") {
            showGraph.setVal(msg.substr(6, 1) == "1");
        }
        else if (size == 6 && msg.substr(0, 4) == "wire") {
            showWireframe.setVal(msg.substr(5, 1) == "1");
        }
        else if (size >= 6 && msg.substr(0, 4) == "size") {
            // parse string to int
            int tmpVal = std::stoi(std::string(msg.substr(5)));
            // recalc percent to float
            sizeFactor.setVal(static_cast<float>(tmpVal) / 100.f);
        }

        MessageHandler::instance()->print(
            "Message: '%s', size: %d\n", receivedChars, size
        );
    }
}

void externalControlStatusCallback(bool connected) {
    if (connected) {
        MessageHandler::instance()->print("External control connected\n");
    }
    else {
        MessageHandler::instance()->print("External control disconnected\n");
    }
}

int main(int argc, char* argv[]) {
    std::vector<std::string> arg(argv + 1, argv + argc);
    Configuration config = parseArguments(arg);
    gEngine = new Engine(config);

    // Bind your functions
    gEngine->setDrawFunction(drawFun);
    gEngine->setPreSyncFunction(preSyncFun);
    gEngine->setPostSyncPreDrawFunction(postSyncPreDrawFun);
    gEngine->setExternalControlCallback(externalControlMessageCallback);
    gEngine->setExternalControlStatusCallback(externalControlStatusCallback);

    SharedData::instance()->setEncodeFunction(encodeFun);
    SharedData::instance()->setDecodeFunction(decodeFun);

    // Init the engine
    if (!gEngine->init()) {
        delete gEngine;
        return EXIT_FAILURE;
    }

    gEngine->render();
    delete gEngine;
    exit(EXIT_SUCCESS);
}
