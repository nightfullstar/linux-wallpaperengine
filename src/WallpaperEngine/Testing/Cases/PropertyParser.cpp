#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Data/Parsers/DynamicValueParser.h"
#include "WallpaperEngine/Data/Parsers/PropertyParser.h"

using WallpaperEngine::Data::JSON::JSON;
using WallpaperEngine::Data::Model::DynamicValue;
using WallpaperEngine::Data::Parsers::PropertyParser;

TEST_CASE ("Bool properties without a value default to false") {
    const JSON propertyData = {
	{ "type", "bool" },
	{ "text", "Enabled" },
    };

    const auto property = PropertyParser::parse (propertyData, "enabled");

    REQUIRE (property != nullptr);
    CHECK (property->getType () == DynamicValue::Boolean);
    CHECK_FALSE (property->getBool ());
}

TEST_CASE ("Directory properties are parsed as file-like properties") {
    const JSON propertyData = {
	{ "type", "directory" },
	{ "text", "Folder" },
    };

    const auto property = PropertyParser::parse (propertyData, "folder");

    REQUIRE (property != nullptr);
    CHECK (property->dump ().find ("folder - file") != std::string::npos);
}

TEST_CASE ("Spaced joke text stays a string, not a vec4") {
    using WallpaperEngine::Data::Model::Properties;
    using WallpaperEngine::Data::Parsers::DynamicValueParser;

    const Properties none;
    const auto head = DynamicValueParser::parse (JSON (std::string ("(    )")), none, false);
    REQUIRE (head != nullptr);
    CHECK (head->getType () == DynamicValue::String);
    CHECK (head->getString () == "(    )");

    const auto joke
	= DynamicValueParser::parse (JSON (std::string ("I don´t know WHAT\nthe fuck is going on.")), none, false);
    REQUIRE (joke != nullptr);
    CHECK (joke->getType () == DynamicValue::String);
    CHECK (joke->getString ().find ("WHAT") != std::string::npos);

    const auto origin = DynamicValueParser::parse (JSON (std::string ("1.00000 2.00000 3.00000")), none, false);
    REQUIRE (origin != nullptr);
    CHECK (origin->getType () == DynamicValue::Vec3);
    CHECK (origin->getVec3 ().x == 1.0f);
}

TEST_CASE ("Workshop color properties store 0-1 white as '1 1 1'") {
    const JSON propertyData = {
	{ "type", "color" },
	{ "text", "Text Color" },
	{ "value", "1 1 1" },
    };

    const auto property = PropertyParser::parse (propertyData, "textcolor");

    REQUIRE (property != nullptr);
    CHECK (property->getVec4 ().r == 1.0f);
    CHECK (property->getVec4 ().g == 1.0f);
    CHECK (property->getVec4 ().b == 1.0f);
    CHECK (property->getVec4 ().a == 1.0f);
}

TEST_CASE ("Numeric optionals accept string numbers from workshop scenes") {
    const JSON textObject = {
	{ "padding", "32.00000 32.00000" },
	{ "width", "3840" },
    };

    CHECK (textObject.optional ("padding", 0) == 32);
    CHECK (textObject.require<int> ("width", "missing width") == 3840);
}