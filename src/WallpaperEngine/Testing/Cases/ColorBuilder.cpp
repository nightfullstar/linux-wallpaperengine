#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Data/Builders/ColorBuilder.h"

using WallpaperEngine::Data::Builders::ColorBuilder;

TEST_CASE ("Integer 0-1 triples stay white, not 1/255") {
    const auto white = ColorBuilder::parse ("1 1 1");
    CHECK (white.r == 1.0f);
    CHECK (white.g == 1.0f);
    CHECK (white.b == 1.0f);
    CHECK (white.a == 1.0f);

    const auto black = ColorBuilder::parse ("0 0 0");
    CHECK (black.r == 0.0f);
    CHECK (black.g == 0.0f);
    CHECK (black.b == 0.0f);
    CHECK (black.a == 1.0f);
}

TEST_CASE ("0-255 triples still scale down") {
    const auto white = ColorBuilder::parse ("255 255 255");
    CHECK (white.r == 1.0f);
    CHECK (white.g == 1.0f);
    CHECK (white.b == 1.0f);

    const auto red = ColorBuilder::parse ("128 0 0");
    CHECK (red.r == Catch::Approx (128.0f / 255.0f));
    CHECK (red.g == 0.0f);
    CHECK (red.b == 0.0f);
}

TEST_CASE ("Dotted 0-1 triples stay in 0-1") {
    const auto white = ColorBuilder::parse ("1.00000 1.00000 1.00000");
    CHECK (white.r == Catch::Approx (1.0f));
    CHECK (white.g == Catch::Approx (1.0f));
    CHECK (white.b == Catch::Approx (1.0f));
    CHECK (white.a == Catch::Approx (1.0f));
}
