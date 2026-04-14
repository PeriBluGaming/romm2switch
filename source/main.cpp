#include "ui/app.hpp"

// libnx entry point
extern "C" void userAppInit() {}
extern "C" void userAppExit() {}

int main(int /*argc*/, char* /*argv*/[]) {
    App app;
    app.run();
    return 0;
}
