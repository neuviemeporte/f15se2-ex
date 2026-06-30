#include "eg3drast.h"
#include "egcode.h"
#include "egdata.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

extern int16 g_dirtyRectMinY;
extern int16 g_dirtyRectMaxY;
void storeObjTransformByOpcode(void);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum Eg3dRastOriginalConstant : int {
    kUntouchedRow = 1,
    kDirtyStartRow = 2,
    kDirtyEndRow = 4,
    kResetMinX = -1,
    kResetMaxX = 0,
    kDirtyMinAfterReset = -1,
    kDirtyMaxAfterReset = 0,
    kDirtyMinSeed = 12,
    kDirtyMaxSeed = 34,
    kZeroAngle = 0,
    kQ15CosineZeroProduct = 32766,
    kLodCommandKeepScanning = 0x80,
    kLodCommandIndex1 = 0x81,
    kLodOffsetToFinal = 6,
    kFinalDisplayOpcode = 0x05,
    kNearDistance = 10,
    kFarDistance = 100,
    kLodThreshold = 50,
    kNoExtraScale = 0,
    kTransformOpcode = 0x82,
    kTransformIndex = 2,
    kSpinAngle = 0x1234,
    kTestFailureExitCode = 1,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetSpanRows() {
    for (int i = 0; i < 220; ++i) {
        g_spanBuf.minX[i] = kDirtyMinSeed;
        g_spanBuf.maxX[i] = kDirtyMaxSeed;
    }
}

void setLe16(uint8 *p, int value) {
    p[0] = static_cast<uint8>(value & 0xff);
    p[1] = static_cast<uint8>((value >> 8) & 0xff);
}

} // namespace

int main() {
    int16 matrix[9] = {};
    uint8 modelStream[16] = {};

    resetSpanRows();
    g_dirtyRectMinY = kDirtyStartRow;
    g_dirtyRectMaxY = kDirtyEndRow;
    require(resetScanlineSpans() == 0,
            "resetScanlineSpans preserves the original zero return");
    for (int y = kDirtyStartRow; y <= kDirtyEndRow; ++y) {
        require(g_spanBuf.minX[y] == kResetMinX &&
                    g_spanBuf.maxX[y] == kResetMaxX,
                "resetScanlineSpans resets every row in the dirty range");
    }
    require(g_spanBuf.minX[kUntouchedRow] == kDirtyMinSeed &&
                g_spanBuf.maxX[kUntouchedRow] == kDirtyMaxSeed &&
                g_dirtyRectMinY == kDirtyMinAfterReset &&
                g_dirtyRectMaxY == kDirtyMaxAfterReset,
            "resetScanlineSpans leaves outside rows untouched and clears dirty bounds");

    require(buildRotationMatrixFar(matrix, kZeroAngle, kZeroAngle, kZeroAngle) == 0,
            "buildRotationMatrixFar preserves the original zero return");
    require(matrix[0] == kQ15CosineZeroProduct &&
                matrix[1] == 0 &&
                matrix[2] == 0 &&
                matrix[3] == 0 &&
                matrix[4] == kQ15CosineZeroProduct &&
                matrix[5] == 0 &&
                matrix[6] == 0 &&
                matrix[7] == 0 &&
                matrix[8] == kQ15CosineZeroProduct,
            "buildRotationMatrixFar builds the original identity-ish Q15 zero-angle matrix");

    modelStream[0] = kLodCommandKeepScanning;
    modelStream[3] = kFinalDisplayOpcode;
    g_modelStreamPtr = reinterpret_cast<char *>(modelStream);
    g_extraScaleShift = kNoExtraScale;
    g_objDistance = kNearDistance;
    setLe16(colorLut + 0x10, kLodThreshold);
    require(advanceModelPointerLod() == 0 &&
                g_modelStreamPtr == reinterpret_cast<char *>(modelStream + 3),
            "advanceModelPointerLod skips short LOD records when distance is inside the threshold");

    modelStream[0] = kLodCommandIndex1;
    setLe16(modelStream + 1, kLodOffsetToFinal);
    modelStream[kLodOffsetToFinal] = kFinalDisplayOpcode;
    setLe16(colorLut + 0x12, kLodThreshold);
    g_modelStreamPtr = reinterpret_cast<char *>(modelStream);
    g_objDistance = kFarDistance;
    require(advanceModelPointerLod() == 0 &&
                g_modelStreamPtr == reinterpret_cast<char *>(modelStream + kLodOffsetToFinal),
            "advanceModelPointerLod jumps by the original record offset when distance exceeds the threshold");

    modelStream[0] = kTransformOpcode;
    g_modelStreamPtr = reinterpret_cast<char *>(modelStream);
    g_spinAngle = kSpinAngle;
    g_objTransform[kTransformIndex] = 0;
    storeObjTransformByOpcode();
    require(g_objTransform[kTransformIndex] == kSpinAngle,
            "storeObjTransformByOpcode stores spinAngle into opcode low-two-bit transform slot");

    std::cout << "eg3drast_behavior_tests passed\n";
    return 0;
}
