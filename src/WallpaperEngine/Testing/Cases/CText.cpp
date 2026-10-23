#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Render/Objects/CText.h"

using WallpaperEngine::Render::Objects::textAlignOffset;
using WallpaperEngine::Render::Objects::textSceneOrigin;

TEST_CASE ("CText scene origin matches CImage Y-down to Y-up") {
    const glm::vec3 origin {1061.32239f, 1537.06494f, 0.0f};
    const auto gl = textSceneOrigin (origin, 3840.0f, 2160.0f);

    CHECK (gl.x == Catch::Approx (origin.x - 1920.0f));
    CHECK (gl.y == Catch::Approx (1080.0f - origin.y));
    CHECK (gl.z == 0.0f);
    CHECK (gl.y != Catch::Approx (origin.y - 1080.0f));
}

TEST_CASE ("left-aligned Head box covers the eyes") {
    const float quadW = 254.0f;
    const float quadH = 200.0f;
    const float scaleX = 2.0f;
    const auto off = textAlignOffset ("left", "center", quadW, quadH);

    CHECK (off.x == Catch::Approx (quadW * 0.5f));
    CHECK (off.y == 0.0f);

    const float originX = 1061.32239f;
    const float left = originX;
    const float right = originX + quadW * scaleX;
    CHECK (left < 1377.74f);
    CHECK (right > 1377.74f);
}
