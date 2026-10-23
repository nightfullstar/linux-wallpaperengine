#pragma once

#include <string>
#include <vector>

#include <GL/glew.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"
#include "WallpaperEngine/Scripting/ScriptableObject.h"

// Forward-declare FreeType types to avoid leaking the header into users.
struct FT_LibraryRec_;
struct FT_FaceRec_;
typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Render::Objects {
using namespace WallpaperEngine::Data::Model;

// Same scene-space origin as CImage: WE is Y-down, the scene FBO is Y-up.
// CText used to use (origin.y - sceneH/2), which mirrored text off-screen
// so white glyphs never landed on the bluescreen.
inline glm::vec3 textSceneOrigin (const glm::vec3& origin, float sceneW, float sceneH) {
    return {origin.x - sceneW * 0.5f, sceneH * 0.5f - origin.y, origin.z};
}

// Quad verts are centered on 0. WE origin is the aligned edge of the box:
// left/center → shift +width/2 so the left edge sits on origin (Honest
// Bluescreen Head). Applied in unscaled local space, after scale.
inline glm::vec2 textAlignOffset (
    const std::string& horizontal, const std::string& vertical, float quadW, float quadH
) {
    glm::vec2 out {0.0f, 0.0f};
    if (horizontal == "left") {
	out.x = quadW * 0.5f;
    } else if (horizontal == "right") {
	out.x = -quadW * 0.5f;
    }
    if (vertical == "top") {
	out.y = -quadH * 0.5f;
    } else if (vertical == "bottom") {
	out.y = quadH * 0.5f;
    }
    return out;
}

/**
 * Phase 1 text renderer.
 *
 * Renders static text objects as a single FreeType-rasterized RGBA texture
 * drawn on a textured quad with its own minimal GLSL shader. Does NOT go
 * through CRenderable / materials / passes — Phase 1 does not need effects.
 *
 * Phase 2 (scripted/dynamic text, alignment from properties, effect passes)
 * is intentionally not implemented here. When the scene provides a dynamic
 * `text: { script: "..." }` object this class captures the script source in
 * the data model but renders an empty string — the Wallpaper Engine JS
 * runtime required to evaluate it is out of scope for Phase 1.
 */
class CText final : virtual public CObject, public Scripting::ScriptableObject {
public:
    CText (Wallpapers::CScene& scene, const Text& text);
    ~CText () override;

    void setup () override;
    void render () override;

private:
    // Rebuilds the glyph texture (and matching quad VBO) from the given string.
    // Reuses existing GL handles if already allocated, so this is safe to call
    // every time the rendered text changes.
    void rebuildTextureFrom (const std::string& text);
    void buildShader ();
    void uploadQuadVertices ();

    // setup() helpers (kept small to keep the setup flow linear).
    bool initFreeType ();
    bool loadEmbeddedFont ();
    bool loadSystemFont ();
    unsigned int computeEffectivePixelSize () const;
    void initScriptLayer ();

    const Text& m_text;
    std::string m_lastRenderedText;
    unsigned int m_lastPixelSize = 0;
    Scripting::ScriptLayerHandle m_layerHandle = Scripting::kInvalidLayerHandle;

    FT_Library m_ftLibrary = nullptr;
    FT_Face m_ftFace = nullptr;
    std::vector<uint8_t> m_fontData;

    GLuint m_texture = 0;
    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    GLint m_uMVP = -1;
    GLint m_uColor = -1;
    GLint m_uTexture = -1;

    glm::ivec2 m_textureSize = { 0, 0 };
    glm::vec2 m_quadSize = { 0.0f, 0.0f };

    bool m_valid = false;
};
} // namespace WallpaperEngine::Render::Objects
