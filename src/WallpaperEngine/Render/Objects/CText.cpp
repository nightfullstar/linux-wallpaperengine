#include "CText.h"

#include <algorithm>
#include <filesystem>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "WallpaperEngine/Data/Model/DynamicValue.h"
#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Data/Model/UserSetting.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Camera.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"

using namespace WallpaperEngine::Render::Objects;

namespace {
// TODO: Phase 2 – load font from wallpaper's materials/fonts/ using AssetLocator
// Phase 1 uses a system font instead of the font shipped by the wallpaper.
// Wallpaper Engine bundles .ttf files in `materials/fonts/`; wiring those in
// is deferred to Phase 2 along with dynamic/scripted text.
const std::vector<std::string> kFontCandidates = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
};

const char* kVertexShader = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
    vUV = aUV;
    // z=-1 sits inside the ortho frustum (nearz 0.01); z=0 clips on some drivers
    gl_Position = uMVP * vec4(aPos, -1.0, 1.0);
}
)glsl";

const char* kFragmentShader = R"glsl(
#version 330 core
in vec2 vUV;
uniform sampler2D uTexture;
uniform vec4 uColor;
out vec4 FragColor;
void main() {
    float coverage = texture(uTexture, vUV).r;
    FragColor = vec4(uColor.rgb, uColor.a * coverage);
}
)glsl";

GLuint compileShader (GLenum type, const char* source) {
    GLuint shader = glCreateShader (type);
    glShaderSource (shader, 1, &source, nullptr);
    glCompileShader (shader);

    GLint status = GL_FALSE;
    glGetShaderiv (shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
	char log[1024];
	glGetShaderInfoLog (shader, sizeof (log), nullptr, log);
	sLog.error ("CText shader compile failed: ", log);
	glDeleteShader (shader);
	return 0;
    }
    return shader;
}
} // namespace

CText::CText (Wallpapers::CScene& scene, const Text& text) :
    CObject (scene, text), ScriptableObject (scene, text), m_text (text) {
    this->registerProperty ("color", *text.color->value);
    this->registerProperty ("alpha", *text.alpha->value);
    this->registerProperty ("origin", *text.origin->value);
    this->registerProperty ("scale", *text.scale->value);
    this->registerProperty ("visible", *text.visible->value);
    this->registerProperty ("pointSize", *text.pointSize->value);
    this->registerProperty ("text", *text.text->value);
    this->registerProperty ("pointSize", *text.pointSize->value);
}

CText::~CText () {
    if (m_layerHandle != Scripting::kInvalidLayerHandle) {
	this->getScene ().getScriptEngine ().destroyLayer (m_layerHandle);
	m_layerHandle = Scripting::kInvalidLayerHandle;
    }
    if (m_vbo != 0) {
	glDeleteBuffers (1, &m_vbo);
    }
    if (m_vao != 0) {
	glDeleteVertexArrays (1, &m_vao);
    }
    if (m_program != 0) {
	glDeleteProgram (m_program);
    }
    if (m_texture != 0) {
	glDeleteTextures (1, &m_texture);
    }
    if (m_ftFace != nullptr) {
	FT_Done_Face (m_ftFace);
    }
    if (m_ftLibrary != nullptr) {
	FT_Done_FreeType (m_ftLibrary);
    }
}

void CText::setup () {
    const bool scripted = m_text.text->value->getScriptSource ().has_value ();
    const auto& text = m_text.text->value->getString ();

    // Nothing to render and no script to produce text later → bail.
    if (text.empty () && !scripted) {
	return;
    }

    if (!initFreeType ()) {
	return;
    }

    if (!loadEmbeddedFont () && !loadSystemFont ()) {
	return;
    }

    m_lastPixelSize = computeEffectivePixelSize ();
    FT_Set_Pixel_Sizes (m_ftFace, 0, static_cast<FT_UInt> (m_lastPixelSize));

    buildShader ();
    // Scripted text may have an empty placeholder; use a single space so the
    // glyph texture has non-zero dimensions until the script produces a value.
    rebuildTextureFrom (text.empty () ? std::string (" ") : text);

    if (scripted) {
	initScriptLayer ();
    }

    m_valid = m_texture != 0 && m_program != 0 && m_vao != 0;
    sLog.out (
	"CText setup '", m_text.name, "' valid=", m_valid, " tex=", m_textureSize.x, "x", m_textureSize.y,
	" font='", m_text.font, "'"
    );
}

bool CText::initFreeType () {
    if (FT_Init_FreeType (&m_ftLibrary) == 0) {
	return true;
    }
    sLog.error ("CText: FT_Init_FreeType failed for object ", m_text.name);
    return false;
}

bool CText::loadEmbeddedFont () {
    // Wallpapers packed in .pkg don't expose physical paths, so we read the font
    // into memory and use FT_New_Memory_Face. m_fontData must outlive the face.
    // `systemfont_*` references signal "use a system font"; let the fallback handle them.
    if (m_text.font.empty () || m_text.font.rfind ("systemfont_", 0) == 0) {
	return false;
    }

    try {
	auto stream = getAssetLocator ().read (m_text.font);
	stream->seekg (0, std::ios::end);
	const auto size = stream->tellg ();
	stream->seekg (0, std::ios::beg);
	m_fontData.resize (static_cast<size_t> (size));
	stream->read (reinterpret_cast<char*> (m_fontData.data ()), size);

	if (FT_New_Memory_Face (
		m_ftLibrary, m_fontData.data (), static_cast<FT_Long> (m_fontData.size ()), 0, &m_ftFace
	    )
	    == 0) {
	    return true;
	}

	sLog.error ("CText: FT_New_Memory_Face failed for '", m_text.font, "', falling back to system font");
    } catch (const std::exception& e) {
	sLog.error ("CText: cannot read font '", m_text.font, "': ", e.what (), ", falling back to system font");
    }

    m_fontData.clear ();
    return false;
}

bool CText::loadSystemFont () {
    std::string fontPath;
    for (const auto& candidate : kFontCandidates) {
	if (std::filesystem::exists (candidate)) {
	    fontPath = candidate;
	    break;
	}
    }
    if (fontPath.empty ()) {
	sLog.error ("CText: no usable system font found");
	return false;
    }
    if (FT_New_Face (m_ftLibrary, fontPath.c_str (), 0, &m_ftFace) != 0) {
	sLog.error ("CText: FT_New_Face failed for ", fontPath);
	return false;
    }
    return true;
}

unsigned int CText::computeEffectivePixelSize () const {
    // WE text objects often come with scale ~0.09 that, combined with a modest
    // pointsize, would rasterize glyphs to ~2px on screen (invisible). Rasterize
    // at higher resolution so that after the model scale is applied in render()
    // the on-screen size matches the intended pointsize. The layout `size` box
    // is NOT a font size — using it turned 12pt Small Text into 96px and
    // crushed word spaces.
    const glm::vec3 initialScale = m_text.scale->value->getVec3 ();
    const float avgScale = (initialScale.x + initialScale.y) * 0.5f;
    const float compensate = (avgScale > 0.0f && avgScale < 1.0f) ? std::min (1.0f / avgScale, 32.0f) : 1.0f;
    const float sceneH = getScene ().getCamera ().getHeight ();
    const float sceneScale = sceneH > 0.0f ? std::min (std::max (sceneH / 1080.0f, 1.0f), 2.0f) : 1.0f;
    // WE `pointsize` is typographic points (1/72"). FT_Set_Pixel_Sizes wants
    // pixels; 24pt at 96 DPI is 32px, which is what the official client draws
    // on a 1080p display. Treating points as pixels made word spaces ~25% tight.
    const float dpi = 96.0f / 72.0f;
    const float desired = m_text.pointSize->value->getFloat () * dpi * compensate * sceneScale;
    return std::max<unsigned int> (1u, static_cast<unsigned int> (desired));
}

void CText::initScriptLayer () {
    const auto& script = m_text.text->value->getScriptSource ();

    if (!script.has_value ()) {
	return;
    }

    m_layerHandle = this->getScene ().getScriptEngine ().createLayerScript (
	*script, m_text.text->value->getProperties (), m_text.text->value->getString ()
    );

    if (m_layerHandle == Scripting::kInvalidLayerHandle) {
	sLog.error ("CText: createLayerScript failed for '", m_text.name, "'");
    }
}

namespace {
std::vector<FT_ULong> decodeUtf8 (const std::string& text) {
    std::vector<FT_ULong> out;
    out.reserve (text.size ());
    for (size_t i = 0; i < text.size ();) {
	const auto c = static_cast<unsigned char> (text[i]);
	FT_ULong cp = c;
	size_t n = 1;
	if (c >= 0xF0 && i + 3 < text.size ()) {
	    cp = static_cast<FT_ULong> ((c & 0x07) << 18) | static_cast<FT_ULong> ((text[i + 1] & 0x3F) << 12)
		| static_cast<FT_ULong> ((text[i + 2] & 0x3F) << 6) | static_cast<FT_ULong> (text[i + 3] & 0x3F);
	    n = 4;
	} else if (c >= 0xE0 && i + 2 < text.size ()) {
	    cp = static_cast<FT_ULong> ((c & 0x0F) << 12) | static_cast<FT_ULong> ((text[i + 1] & 0x3F) << 6)
		| static_cast<FT_ULong> (text[i + 2] & 0x3F);
	    n = 3;
	} else if (c >= 0xC0 && i + 1 < text.size ()) {
	    cp = static_cast<FT_ULong> ((c & 0x1F) << 6) | static_cast<FT_ULong> (text[i + 1] & 0x3F);
	    n = 2;
	}
	out.push_back (cp);
	i += n;
    }
    return out;
}

int glyphAdvance (FT_Face face, FT_GlyphSlot slot, FT_ULong cp, unsigned int pixelSize) {
    if (FT_Load_Char (face, cp, FT_LOAD_RENDER) == 0 && (slot->advance.x >> 6) > 0) {
	return slot->advance.x >> 6;
    }
    if (FT_Load_Char (face, cp, FT_LOAD_DEFAULT) == 0 && (slot->advance.x >> 6) > 0) {
	return slot->advance.x >> 6;
    }
    if (cp == ' ' || cp == '\t' || cp == 0x00A0) {
	const int fallback = static_cast<int> (std::max (1u, pixelSize / 3));
	return std::max (slot->advance.x > 0 ? static_cast<int> (slot->advance.x >> 6) : 0, fallback);
    }
    return 0;
}

void blitGlyph (
    std::vector<uint8_t>& pixels, int width, int height, const FT_GlyphSlot slot, int originX, int originY
) {
    const auto& bmp = slot->bitmap;
    for (unsigned int row = 0; row < bmp.rows; ++row) {
	for (unsigned int col = 0; col < bmp.width; ++col) {
	    const int dstX = originX + static_cast<int> (col);
	    const int dstY = originY + static_cast<int> (row);
	    if (dstX < 0 || dstX >= width || dstY < 0 || dstY >= height) {
		continue;
	    }
	    pixels[static_cast<size_t> (dstY) * width + dstX] = bmp.buffer[row * bmp.pitch + col];
	}
    }
}
} // namespace

void CText::rebuildTextureFrom (const std::string& text) {
    // Measure then rasterize. Split on U+000A so Honest Bluescreen's two-line
    // joke is not smashed into one overflowing row (Phase 1 used to be 1-line).
    FT_GlyphSlot slot = m_ftFace->glyph;
    const auto cps = decodeUtf8 (text);

    std::vector<std::vector<FT_ULong>> lines (1);
    for (FT_ULong cp : cps) {
	if (cp == '\n') {
	    lines.emplace_back ();
	} else {
	    lines.back ().push_back (cp);
	}
    }

    int maxAscent = 0;
    int maxDescent = 0;
    int maxWidth = 1;
    std::vector<int> lineWidths;
    lineWidths.reserve (lines.size ());
    for (const auto& line : lines) {
	int penX = 0;
	for (FT_ULong cp : line) {
	    const int adv = glyphAdvance (m_ftFace, slot, cp, m_lastPixelSize);
	    if (adv <= 0 && slot->bitmap.rows == 0) {
		continue;
	    }
	    penX += adv > 0 ? adv : (slot->advance.x >> 6);
	    maxAscent = std::max (maxAscent, slot->bitmap_top);
	    maxDescent = std::max (maxDescent, static_cast<int> (slot->bitmap.rows) - slot->bitmap_top);
	}
	lineWidths.push_back (penX);
	maxWidth = std::max (maxWidth, penX);
    }

    const int lineHeight = std::max (1, maxAscent + maxDescent);
    const int width = std::max (1, maxWidth);
    const int height = std::max (1, lineHeight * static_cast<int> (lines.size ()));
    std::vector<uint8_t> pixels (static_cast<size_t> (width) * height, 0);

    for (size_t lineIndex = 0; lineIndex < lines.size (); ++lineIndex) {
	int penX = 0;
	const int lineTop = static_cast<int> (lineIndex) * lineHeight;
	for (FT_ULong cp : lines[lineIndex]) {
	    const int adv = glyphAdvance (m_ftFace, slot, cp, m_lastPixelSize);
	    if (slot->bitmap.width > 0 && slot->bitmap.rows > 0) {
		blitGlyph (
		    pixels, width, height, slot, penX + slot->bitmap_left, lineTop + maxAscent - slot->bitmap_top
		);
	    }
	    penX += adv > 0 ? adv : std::max (0, static_cast<int> (slot->advance.x >> 6));
	}
    }

    const bool firstUpload = (m_texture == 0);
    if (firstUpload) {
	glGenTextures (1, &m_texture);
    }
    glBindTexture (GL_TEXTURE_2D, m_texture);
    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
    // GL_R8 + swizzle: some 3.3 drivers sample a GL_RED texture as 0,0,0,1 so
    // coverage never reaches the shader and white glyphs vanish.
    glTexImage2D (GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data ());
    if (firstUpload) {
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);
    }

    m_textureSize = { width, height };
    // `size` is a bounding box, not a stretch target. Stretching prose to that
    // box (Big/Small Text 886×266 / 1670×201) fattened glyphs and hid word
    // spaces. Only fill the box for sparse runs like Head "(    )" so the
    // parens still wrap the eyes.
    bool fillBox = false;
    for (const auto& line : lines) {
	int spaceRun = 0;
	for (FT_ULong cp : line) {
	    if (cp == ' ' || cp == 0x00A0) {
		if (++spaceRun >= 3) {
		    fillBox = true;
		}
	    } else {
		spaceRun = 0;
	    }
	}
    }
    m_quadSize = { static_cast<float> (width), static_cast<float> (height) };
    if (fillBox) {
	if (m_text.size.x > m_quadSize.x) {
	    m_quadSize.x = m_text.size.x;
	}
	if (m_text.size.y > m_quadSize.y) {
	    m_quadSize.y = m_text.size.y;
	}
    }
    m_lastRenderedText = text;

    uploadQuadVertices ();
}

void CText::buildShader () {
    GLuint vs = compileShader (GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compileShader (GL_FRAGMENT_SHADER, kFragmentShader);
    if (vs == 0 || fs == 0) {
	if (vs) {
	    glDeleteShader (vs);
	}
	if (fs) {
	    glDeleteShader (fs);
	}
	return;
    }

    m_program = glCreateProgram ();
    glAttachShader (m_program, vs);
    glAttachShader (m_program, fs);
    glLinkProgram (m_program);
    glDeleteShader (vs);
    glDeleteShader (fs);

    GLint status = GL_FALSE;
    glGetProgramiv (m_program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
	char log[1024];
	glGetProgramInfoLog (m_program, sizeof (log), nullptr, log);
	sLog.error ("CText program link failed: ", log);
	glDeleteProgram (m_program);
	m_program = 0;
	return;
    }

    m_uMVP = glGetUniformLocation (m_program, "uMVP");
    m_uColor = glGetUniformLocation (m_program, "uColor");
    m_uTexture = glGetUniformLocation (m_program, "uTexture");
}

void CText::uploadQuadVertices () {
    // Quad centered at the origin, sized in pixels. Scene-space placement is
    // done via the model matrix using the object's origin/scale. VBO contents
    // are re-uploaded whenever the glyph bitmap is rebuilt so the quad always
    // matches the current texture dimensions.
    const float hx = m_quadSize.x * 0.5f;
    const float hy = m_quadSize.y * 0.5f;
    // With vflip=true (Wayland/GLFW), GL y- = screen top. So the quad bottom (y=-hy,
    // lower GL y) appears at screen top. UV.v=0 here = FT glyph top → shows at screen top ✓
    const float verts[] = {
	// pos        // uv
	-hx, -hy, 0.0f, 0.0f, hx, -hy, 1.0f, 0.0f, hx,  hy, 1.0f, 1.0f,
	-hx, -hy, 0.0f, 0.0f, hx, hy,  1.0f, 1.0f, -hx, hy, 0.0f, 1.0f,
    };

    const bool firstUpload = (m_vao == 0);
    if (firstUpload) {
	glGenVertexArrays (1, &m_vao);
	glGenBuffers (1, &m_vbo);
    }
    glBindVertexArray (m_vao);
    glBindBuffer (GL_ARRAY_BUFFER, m_vbo);
    glBufferData (GL_ARRAY_BUFFER, sizeof (verts), verts, GL_DYNAMIC_DRAW);
    if (firstUpload) {
	glEnableVertexAttribArray (0);
	glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float), reinterpret_cast<void*> (0));
	glEnableVertexAttribArray (1);
	glVertexAttribPointer (
	    1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float), reinterpret_cast<void*> (2 * sizeof (float))
	);
    }
    glBindVertexArray (0);
}

void CText::render () {
    if (!m_valid) {
	return;
    }
    if (!m_text.visible->value->getBool ()) {
	return;
    }

#if !NDEBUG
    std::string str = "Text " + this->getObject ().name + " (" + std::to_string (this->getObject ().id) + ")";
    glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, str.c_str ());
#endif /* DEBUG */
    std::string renderedText = m_lastRenderedText;
    if (m_layerHandle != Scripting::kInvalidLayerHandle) {
	auto& se = this->getScene ().getScriptEngine ();
	se.tickLayer (
	    m_layerHandle, static_cast<double> (getScene ().getTime ()),
	    static_cast<double> (getScene ().getDeltaTime ()), static_cast<double> (getScene ().getFps ())
	);
	const std::string current = se.layerText (m_layerHandle);
	renderedText = current.empty () ? std::string (" ") : current;
    }

    const unsigned int pixelSize = computeEffectivePixelSize ();
    if (pixelSize != m_lastPixelSize) {
	m_lastPixelSize = pixelSize;
	FT_Set_Pixel_Sizes (m_ftFace, 0, static_cast<FT_UInt> (m_lastPixelSize));
	rebuildTextureFrom (renderedText);
    } else if (renderedText != m_lastRenderedText) {
	rebuildTextureFrom (renderedText);
    }

    const glm::vec4 color = m_text.color->value->getVec4 ();
    const float alpha = m_text.alpha->value->getFloat ();
    const glm::vec3 scale = m_text.scale->value->getVec3 ();
    const glm::vec3 origin = m_text.origin->value->getVec3 ();

    // Same origin as CImage scene verts. The previous (origin.y - sceneH/2)
    // path mirrored text off the bluescreen so white glyphs never showed.
    const float scene_w = getScene ().getCamera ().getWidth ();
    const float scene_h = getScene ().getCamera ().getHeight ();
    const glm::vec3 gl_origin = textSceneOrigin (origin, scene_w, scene_h);

    const glm::vec2 align = textAlignOffset (m_text.alignment, m_text.verticalalign, m_quadSize.x, m_quadSize.y);

    glm::mat4 model = glm::translate (glm::mat4 (1.0f), gl_origin);
    model = glm::scale (model, scale);
    model = glm::translate (model, glm::vec3 (align, 0.0f));

    const glm::mat4 mvp = getScene ().getCamera ().getProjection () * getScene ().getCamera ().getLookAt () * model;

    // Images leave their last-pass FBO bound. Draw onto the scene or glyphs vanish.
    glBindFramebuffer (GL_FRAMEBUFFER, getScene ().getWallpaperFramebuffer ());
    glViewport (0, 0, static_cast<GLsizei> (scene_w), static_cast<GLsizei> (scene_h));
    glDisable (GL_DEPTH_TEST);
    glDepthMask (GL_FALSE);
    glEnable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glUseProgram (m_program);
    glUniformMatrix4fv (m_uMVP, 1, GL_FALSE, glm::value_ptr (mvp));
    glUniform4f (m_uColor, color.r, color.g, color.b, color.a * alpha);

    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, m_texture);
    glUniform1i (m_uTexture, 0);

    glBindVertexArray (m_vao);
    glDrawArrays (GL_TRIANGLES, 0, 6);
    glBindVertexArray (0);
#if !NDEBUG
    glPopDebugGroup ();
#endif /* DEBUG */
}
