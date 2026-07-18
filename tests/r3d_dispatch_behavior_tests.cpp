#include "r3d.h"
#include "shared/blackbox_diag.h"

#include <SDL3/SDL.h>

#include <cstdarg>
#include <cstdlib>
#include <iostream>

namespace {

int gBeginCalls = 0;
int gSubmitCalls = 0;
int gLineCalls = 0;
int gEndCalls = 0;
int gDiagBeginCalls = 0;
int gDiagSubmitCalls = 0;
int gDiagLineCalls = 0;
int gDiagEndCalls = 0;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

const char *glName() { return "opengl1"; }
const char *softwareName() { return "software"; }
int rejectBackend() { return 0; }
int acceptBackend() { return 1; }
void noShutdown() {}
R3DMesh registerMesh(R3DMesh mesh) { return mesh; }
void releaseMesh(R3DMesh) {}
void beginScene(const R3DScene *) { ++gBeginCalls; }
void submit(const R3DSubmit *) { ++gSubmitCalls; }
void submitLine(const R3DLine *) { ++gLineCalls; }
void endScene() { ++gEndCalls; }

} // namespace

const R3DBackend r3d_glBackend = {
    glName, rejectBackend, noShutdown, registerMesh, releaseMesh,
    beginScene, submit, submitLine, endScene};
const R3DBackend r3d_softwareBackend = {
    softwareName, acceptBackend, noShutdown, registerMesh, releaseMesh,
    beginScene, submit, submitLine, endScene};

void log_info(const char *, ...) {}
void log_warn(const char *, ...) {}
extern "C" void blackbox_diagRenderBeginScene(const R3DScene *) { ++gDiagBeginCalls; }
extern "C" void blackbox_diagRenderSubmit(const R3DSubmit *) { ++gDiagSubmitCalls; }
extern "C" void blackbox_diagRenderLine(const R3DLine *) { ++gDiagLineCalls; }
extern "C" void blackbox_diagRenderEndScene(void) { ++gDiagEndCalls; }

int main() {
    SDL_setenv_unsafe("F15_RENDER", "software", 1);
    r3d_init();
    require(std::string(r3d_backendName()) == "software",
            "forced software renderer is selected");

    R3DScene scene = {nullptr, 1, 2, 3, 4, 5, 6, 1};
    R3DSubmit object = {nullptr, 7, 8, 9, 10, 11, 12, 0};
    R3DLine line = {1, 2, 3, 4, 5, 6, 15};
    r3d_beginScene(&scene);
    r3d_submit(&object);
    r3d_submitLine(&line);
    r3d_endScene();
    require(gBeginCalls == 1 && gSubmitCalls == 1 && gLineCalls == 1 && gEndCalls == 1,
            "renderer dispatch forwards every scene command to the backend");
    require(gDiagBeginCalls == 1 && gDiagSubmitCalls == 1 && gDiagLineCalls == 1 &&
                gDiagEndCalls == 1,
            "renderer dispatch mirrors every scene command to blackbox diagnostics");
    r3d_shutdown();
    std::cout << "r3d_dispatch_behavior_tests passed\n";
    return 0;
}
