#include "ColorBuilder.h"
#include "VectorBuilder.h"

#include <algorithm>
#include <format>
#include <glm/vec3.hpp>

const WallpaperEngine::Data::Model::Color WallpaperEngine::Data::Builders::ColorBuilder::White
    = WallpaperEngine::Data::Model::Color (1.0f, 1.0f, 1.0f, 1.0f);
const WallpaperEngine::Data::Model::Color WallpaperEngine::Data::Builders::ColorBuilder::Black
    = WallpaperEngine::Data::Model::Color (0.0f, 0.0f, 0.0f, 1.0f);

WallpaperEngine::Data::Model::Color
WallpaperEngine::Data::Builders::ColorBuilder::parse (const std::string& value, float alpha) {
    auto copy = value;

    // replace the actual separators with spaces to normalize them
    if (copy.find (',') != std::string::npos) {
	// replace comma separator with spaces so it's
	std::ranges::replace (copy, ',', ' ');
    }

    // hex colors should be converted to int colors
    if (copy.find ('#') == 0) {
	auto number = copy.substr (1);

	// expand short css notation into the right one
	// support for css notation
	if (number.size () == 3) {
	    number = std::format (
		"{}{}{}{}{}{}{:02x}", number.at (0), number.at (0), number.at (1), number.at (1), number.at (2),
		number.at (2), static_cast<int> (alpha * 255)
	    );
	} else if (number.size () == 4) {
	    number = std::format (
		"{}{}{}{}{}{}{}{}", number.at (0), number.at (0), number.at (1), number.at (1), number.at (2),
		number.at (2), number.at (3), number.at (3)
	    );
	} else if (number.size () != 6 && number.size () != 8) {
	    sLog.exception ("Invalid CSS color notation for ", value);
	}

	// parse hex color
	const auto color = std::stoi (number, nullptr, 16);

	return WallpaperEngine::Data::Model::Color (
	    (color >> 24 & 0xFF) / 255.0f, (color >> 16 & 0xFF) / 255.0f, (color >> 8 & 0xFF) / 255.0f,
	    (color & 0xFF) / 255.0f
	);
    }

    int vectorSize = VectorBuilder::preparseSize (copy);

    if (vectorSize != 3 && vectorSize != 4) {
	throw std::invalid_argument ("Invalid color value");
    }

    // Workshop color properties write white as "1 1 1" (already 0-1). The old
    // "no decimal point means 0-255" heuristic turned that into 1/255 ≈ black,
    // so Honest Bluescreen's white face and text disappeared into the blue.
    // A channel > 1 is the 0-255 form ("255 255 255"); otherwise keep 0-1.
    if (vectorSize == 3) {
	const auto rgb = VectorBuilder::parse<glm::vec3> (copy);
	if (rgb.r > 1.0f || rgb.g > 1.0f || rgb.b > 1.0f) {
	    return { rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f, alpha };
	}
	return { rgb.r, rgb.g, rgb.b, alpha };
    }

    const auto rgba = VectorBuilder::parse<glm::vec4> (copy);
    if (rgba.r > 1.0f || rgba.g > 1.0f || rgba.b > 1.0f || rgba.a > 1.0f) {
	return { rgba.r / 255.0f, rgba.g / 255.0f, rgba.b / 255.0f, rgba.a / 255.0f };
    }
    return Model::Color (rgba);
}