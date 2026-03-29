//
// gl3_Shaders.c
//
// OpenGL 3.3 shader compilation and management.
//

#include "gl3_Shaders.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================
// Shader source code (embedded).
// ============================================================

// --- 2D shader (UI, HUD, console) ---
static const char* vertexSource2D =
	"#version 330 core\n"
	"layout(location = 0) in vec2 aPos;\n"
	"layout(location = 1) in vec2 aTexCoord;\n"
	"layout(location = 2) in vec4 aColor;\n"
	"uniform mat4 uProjection;\n"
	"out vec2 vTexCoord;\n"
	"out vec4 vColor;\n"
	"void main() {\n"
	"    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);\n"
	"    vTexCoord = aTexCoord;\n"
	"    vColor = aColor;\n"
	"}\n";

static const char* fragmentSource2D =
	"#version 330 core\n"
	"in vec2 vTexCoord;\n"
	"in vec4 vColor;\n"
	"uniform sampler2D uTexture;\n"
	"uniform vec4 uColor;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec4 texColor = texture(uTexture, vTexCoord);\n"
	"    FragColor = texColor * vColor * uColor;\n"
	"    if (FragColor.a < 0.01) discard;\n"
	"}\n";

// --- 3D textured shader ---
static const char* vertexSource3D =
	"#version 330 core\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aTexCoord;\n"
	"layout(location = 2) in vec4 aColor;\n"
	"uniform mat4 uProjection;\n"
	"uniform mat4 uModelview;\n"
	"out vec2 vTexCoord;\n"
	"out vec4 vColor;\n"
	"out vec3 vViewPos;\n"
	"void main() {\n"
	"    vec4 viewPos4 = uModelview * vec4(aPos, 1.0);\n"
	"    gl_Position = uProjection * viewPos4;\n"
	"    vViewPos = viewPos4.xyz;\n"
	"    vTexCoord = aTexCoord;\n"
	"    vColor = aColor;\n"
	"}\n";

static const char* fragmentSource3D =
	"#version 330 core\n"
	"in vec2 vTexCoord;\n"
	"in vec4 vColor;\n"
	"in vec3 vViewPos;\n"
	"uniform sampler2D uTexture;\n"
	"uniform vec4 uColor;\n"
	"uniform int  uNumDlights;\n"
	"uniform vec4 uDlightPosRad[8];\n"	// xyz = view-space pos, w = intensity (radius)
	"uniform vec4 uDlightColor[8];\n"	// xyz = rgb (0..1), w = unused
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec4 base = texture(uTexture, vTexCoord) * vColor * uColor;\n"
	"    if (base.a < 0.01) discard;\n"
	"    vec3 dlightSum = vec3(0.0);\n"
	"    for (int i = 0; i < uNumDlights; i++) {\n"
	"        float dist = length(uDlightPosRad[i].xyz - vViewPos);\n"
	"        float atten = max(0.0, (uDlightPosRad[i].w - dist) / 256.0);\n"
	"        dlightSum += uDlightColor[i].rgb * atten;\n"
	"    }\n"
	"    vec3 lit = base.rgb + dlightSum;\n"
	"    FragColor = vec4(lit, base.a);\n"
	"}\n";

// --- 3D color-only shader (no texture) ---
static const char* vertexSource3DColor =
	"#version 330 core\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec4 aColor;\n"
	"uniform mat4 uProjection;\n"
	"uniform mat4 uModelview;\n"
	"out vec4 vColor;\n"
	"void main() {\n"
	"    gl_Position = uProjection * uModelview * vec4(aPos, 1.0);\n"
	"    vColor = aColor;\n"
	"}\n";

static const char* fragmentSource3DColor =
	"#version 330 core\n"
	"in vec4 vColor;\n"
	"uniform vec4 uColor;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    FragColor = vColor * uColor;\n"
	"    if (FragColor.a < 0.01) discard;\n"
	"}\n";

// --- 3D lightmapped shader (world surfaces: diffuse * lightmap) ---
// Vertex layout: pos3 (loc 0) + tc2 (loc 1) + lmtc2 (loc 2) = VERTEXSIZE=7 floats, matching glpoly_t verts[i][0..6].
static const char* vertexSource3DLM =
	"#version 330 core\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aTexCoord;\n"
	"layout(location = 2) in vec2 aLMCoord;\n"
	"uniform mat4 uProjection;\n"
	"uniform mat4 uModelview;\n"
	"out vec2 vTexCoord;\n"
	"out vec2 vLMCoord;\n"
	"void main() {\n"
	"    gl_Position = uProjection * uModelview * vec4(aPos, 1.0);\n"
	"    vTexCoord = aTexCoord;\n"
	"    vLMCoord = aLMCoord;\n"
	"}\n";

static const char* fragmentSource3DLM =
	"#version 330 core\n"
	"in vec2 vTexCoord;\n"
	"in vec2 vLMCoord;\n"
	"uniform sampler2D uDiffuse;\n"
	"uniform sampler2D uLightmap;\n"
	"uniform vec4 uColor;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec4 diffuse = texture(uDiffuse, vTexCoord);\n"
	"    vec4 lm = texture(uLightmap, vLMCoord);\n"
	"    vec3 lit = diffuse.rgb * lm.rgb;\n"
	"    FragColor = vec4(lit, diffuse.a) * uColor;\n"
	"    if (FragColor.a < 0.01) discard;\n"
	"}\n";

// --- Post-process shader (HDR composite + exposure) ---
// Vertex layout: vec2 pos (NDC) + vec2 texcoord = 4 floats per vertex.
static const char* vertexSourcePost =
	"#version 330 core\n"
	"layout(location = 0) in vec2 aPos;\n"
	"layout(location = 1) in vec2 aTexCoord;\n"
	"out vec2 vTexCoord;\n"
	"void main() {\n"
	"    gl_Position = vec4(aPos, 0.0, 1.0);\n"
	"    vTexCoord = aTexCoord;\n"
	"}\n";

static const char* fragmentSourcePost =
	"#version 330 core\n"
	"in vec2 vTexCoord;\n"
	"uniform sampler2D uHDRBuffer;\n"
	"uniform sampler2D uBloomBuffer;\n"
	"uniform sampler2D uAOBuffer;\n"
	"uniform sampler2D uDepthMap;\n"
	"uniform float uExposure;\n"
	"uniform float uBloomStrength;\n"
	"uniform float uAOStrength;\n"
	"uniform int  uFogEnabled;\n"
	"uniform int  uFogMode;\n"
	"uniform vec3 uFogColor;\n"
	"uniform float uFogDensity;\n"
	"uniform float uFogStart;\n"
	"uniform float uFogEnd;\n"
	"uniform vec2 uFogNearFar;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec3  hdr   = texture(uHDRBuffer,   vTexCoord).rgb;\n"
	"    vec3  bloom = texture(uBloomBuffer, vTexCoord).rgb;\n"
	"    float ao    = mix(1.0, texture(uAOBuffer, vTexCoord).r, uAOStrength);\n"
	"    vec3  color = hdr * ao + bloom * uBloomStrength;\n"
	"    if (uFogEnabled != 0) {\n"
	"        float depth_ndc = texture(uDepthMap, vTexCoord).r * 2.0 - 1.0;\n"
	"        float near = uFogNearFar.x;\n"
	"        float far  = uFogNearFar.y;\n"
	"        float linearDist = (2.0 * near * far) / (far + near - depth_ndc * (far - near));\n"
	"        float fogFactor = 1.0;\n"
	"        if (uFogMode == 0) {\n"
	"            fogFactor = clamp((uFogEnd - linearDist) / (uFogEnd - uFogStart), 0.0, 1.0);\n"
	"        } else if (uFogMode == 1) {\n"
	"            fogFactor = exp(-uFogDensity * 0.5 * linearDist);\n"
	"        } else {\n"
	"            float d = uFogDensity * 0.5 * linearDist;\n"
	"            fogFactor = exp(-d * d);\n"
	"        }\n"
	"        color = mix(uFogColor, color, fogFactor);\n"
	"    }\n"
	"    FragColor = vec4(color * uExposure, 1.0);\n"
	"}\n";

// --- Bloom bright-pass extract shader ---
// Outputs pixels whose perceived luminance exceeds uThreshold, scaled by a smooth knee.
static const char* fragmentSourceBloomExtract =
	"#version 330 core\n"
	"in vec2 vTexCoord;\n"
	"uniform sampler2D uHDRBuffer;\n"
	"uniform float uThreshold;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec3  color      = texture(uHDRBuffer, vTexCoord).rgb;\n"
	"    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));\n"
	"    float rcp        = 1.0 / max(1.0 - uThreshold, 0.0001);\n"
	"    float weight     = clamp((brightness - uThreshold) * rcp, 0.0, 1.0);\n"
	"    FragColor = vec4(color * weight, 1.0);\n"
	"}\n";

// --- Bloom separable Gaussian blur shader (9-tap, horizontal or vertical) ---
static const char* fragmentSourceBloomBlur =
	"#version 330 core\n"
	"in vec2 vTexCoord;\n"
	"uniform sampler2D uImage;\n"
	"uniform bool      uHorizontal;\n"
	"uniform vec2      uTexelSize;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);\n"
	"    vec2 offset = uHorizontal ? vec2(uTexelSize.x, 0.0) : vec2(0.0, uTexelSize.y);\n"
	"    vec3 result = texture(uImage, vTexCoord).rgb * weight[0];\n"
	"    for (int i = 1; i < 5; i++) {\n"
	"        result += texture(uImage, vTexCoord + offset * float(i)).rgb * weight[i];\n"
	"        result += texture(uImage, vTexCoord - offset * float(i)).rgb * weight[i];\n"
	"    }\n"
	"    FragColor = vec4(result, 1.0);\n"
	"}\n";

// --- SSAO shader: reconstructs view-space position from depth and samples a hemisphere kernel ---
static const char* fragmentSourceSSAO =
	"#version 330 core\n"
	"in vec2 vTexCoord;\n"
	"uniform sampler2D uDepthMap;\n"
	"uniform sampler2D uNoiseTex;\n"
	"uniform vec3  uKernel[16];\n"
	"uniform vec4  uProjParams;\n"			// x=P[0], y=P[5], z=P[10], w=P[14]
	"uniform float uRadius;\n"
	"uniform float uBias;\n"
	"uniform vec2  uScreenSize;\n"
	"out float FragColor;\n"
	"vec3 ReconstructViewPos(vec2 tc) {\n"
	"    float depth  = texture(uDepthMap, tc).r;\n"
	"    float ndc_z  = depth * 2.0 - 1.0;\n"
	"    float view_z = -uProjParams.w / (ndc_z + uProjParams.z);\n"
	"    float view_x = (tc.x * 2.0 - 1.0) * (-view_z) / uProjParams.x;\n"
	"    float view_y = (tc.y * 2.0 - 1.0) * (-view_z) / uProjParams.y;\n"
	"    return vec3(view_x, view_y, view_z);\n"
	"}\n"
	"void main() {\n"
	"    float depth = texture(uDepthMap, vTexCoord).r;\n"
	"    if (depth >= 0.9999) { FragColor = 1.0; return; }\n"
	"    vec3 fragPos  = ReconstructViewPos(vTexCoord);\n"
	// Best-neighbor normal: pick the neighbor pair with the smaller depth difference
	// to avoid discontinuity artifacts at geometry edges.
	"    vec2 ts = 1.0 / uScreenSize;\n"
	"    vec3 pr = ReconstructViewPos(vTexCoord + vec2(ts.x,  0.0));\n"
	"    vec3 pl = ReconstructViewPos(vTexCoord - vec2(ts.x,  0.0));\n"
	"    vec3 pu = ReconstructViewPos(vTexCoord + vec2(0.0,  ts.y));\n"
	"    vec3 pd = ReconstructViewPos(vTexCoord - vec2(0.0,  ts.y));\n"
	"    vec3 dx = (abs(pr.z - fragPos.z) < abs(pl.z - fragPos.z)) ? pr - fragPos : fragPos - pl;\n"
	"    vec3 dy = (abs(pu.z - fragPos.z) < abs(pd.z - fragPos.z)) ? pu - fragPos : fragPos - pd;\n"
	"    vec3 N  = normalize(cross(dx, dy));\n"
	// Camera-facing check: flip N if it points away from the viewer in view-space.
	"    if (dot(N, -normalize(fragPos)) < 0.0) N = -N;\n"
	"    vec2 noiseScale = uScreenSize / 4.0;\n"
	"    vec3 randomVec  = normalize(texture(uNoiseTex, vTexCoord * noiseScale).rgb);\n"
	// Gram-Schmidt with degeneracy guard: skip if randomVec is nearly parallel to N.
	"    vec3 tangent = randomVec - N * dot(randomVec, N);\n"
	"    if (length(tangent) < 0.001) { FragColor = 1.0; return; }\n"
	"    tangent        = normalize(tangent);\n"
	"    vec3 bitangent = cross(N, tangent);\n"
	"    mat3 TBN       = mat3(tangent, bitangent, N);\n"
	"    float occlusion = 0.0;\n"
	"    for (int i = 0; i < 16; i++) {\n"
	"        vec3 samplePos = fragPos + TBN * uKernel[i] * uRadius;\n"
	// Skip samples that landed behind the camera (view-space z >= 0).
	"        if (samplePos.z >= 0.0) continue;\n"
	"        float rcp_w   = -1.0 / samplePos.z;\n"
	"        vec2  sampleTC = clamp(vec2(\n"
	"            samplePos.x * uProjParams.x * rcp_w * 0.5 + 0.5,\n"
	"            samplePos.y * uProjParams.y * rcp_w * 0.5 + 0.5), 0.0, 1.0);\n"
	"        vec3  scenePos   = ReconstructViewPos(sampleTC);\n"
	// Range check: linear falloff within uRadius; avoids division by near-zero.
	"        float rangeCheck = smoothstep(0.0, 1.0, 1.0 - abs(fragPos.z - scenePos.z) / uRadius);\n"
	"        occlusion += (scenePos.z >= samplePos.z + uBias ? 1.0 : 0.0) * rangeCheck;\n"
	"    }\n"
	"    FragColor = 1.0 - (occlusion / 16.0);\n"
	"}\n";

// --- SSAO box blur (4x4, 16-tap) ---
static const char* fragmentSourceSSAOBlur =
	"#version 330 core\n"
	"in vec2 vTexCoord;\n"
	"uniform sampler2D uSSAOInput;\n"
	"uniform vec2 uTexelSize;\n"
	"out float FragColor;\n"
	"void main() {\n"
	"    float result = 0.0;\n"
	"    for (int x = 0; x < 4; x++)\n"
	"        for (int y = 0; y < 4; y++)\n"
	"            result += texture(uSSAOInput, vTexCoord + (vec2(float(x), float(y)) - 1.5) * uTexelSize).r;\n"
	"    FragColor = result / 16.0;\n"
	"}\n";

// --- Water surface shader (9-float vertex layout: pos3+tc2+col4, adds projective reflection) ---
static const char* vertexSourceWater =
	"#version 330 core\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aTexCoord;\n"
	"layout(location = 2) in vec4 aColor;\n"
	"uniform mat4 uProjection;\n"
	"uniform mat4 uModelview;\n"
	"out vec2 vTexCoord;\n"
	"out vec4 vColor;\n"
	"out vec4 vClipPos;\n"
	"void main() {\n"
	"    vec4 viewPos = uModelview * vec4(aPos, 1.0);\n"
	"    gl_Position = uProjection * viewPos;\n"
	"    vClipPos    = gl_Position;\n"
	"    vTexCoord   = aTexCoord;\n"
	"    vColor      = aColor;\n"
	"}\n";

static const char* fragmentSourceWater =
	"#version 330 core\n"
	"in vec2 vTexCoord;\n"
	"in vec4 vColor;\n"
	"in vec4 vClipPos;\n"
	"uniform sampler2D uTexture;\n"
	"uniform sampler2D uReflectTex;\n"
	"uniform vec4  uColor;\n"
	"uniform float uReflectAmt;\n"
	"uniform float uTime;\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    vec4 waterColor = texture(uTexture, vTexCoord) * vColor * uColor;\n"
	"    if (waterColor.a < 0.01) discard;\n"
	"    float dx = sin(vTexCoord.y * 25.0 + uTime * 1.5) * 0.010\n"
	"             + sin(vTexCoord.x * 17.0 + uTime * 2.3) * 0.006;\n"
	"    float dy = cos(vTexCoord.x * 25.0 + uTime * 1.5) * 0.010\n"
	"             + cos(vTexCoord.y * 17.0 + uTime * 2.3) * 0.006;\n"
	"    vec2 screenUV = clamp(vClipPos.xy / vClipPos.w * 0.5 + 0.5 + vec2(dx, dy), 0.0, 1.0);\n"
	"    vec3 reflectColor = texture(uReflectTex, screenUV).rgb;\n"
	"    vec3 blended = mix(waterColor.rgb, reflectColor, uReflectAmt);\n"
	"    FragColor = vec4(blended, waterColor.a);\n"
	"}\n";

static GLuint CompileShader(GLenum type, const char* source)
{
	const GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);

	GLint success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

	if (!success)
	{
		char info_log[512];
		glGetShaderInfoLog(shader, sizeof(info_log), NULL, info_log);
		ri.Con_Printf(PRINT_ALL, "GL3 Shader compile error: %s\n", info_log);
		glDeleteShader(shader);
		return 0;
	}

	return shader;
}

static GLuint CreateProgram(const char* vert_src, const char* frag_src)
{
	const GLuint vert = CompileShader(GL_VERTEX_SHADER, vert_src);
	if (vert == 0)
		return 0;

	const GLuint frag = CompileShader(GL_FRAGMENT_SHADER, frag_src);
	if (frag == 0)
	{
		glDeleteShader(vert);
		return 0;
	}

	const GLuint program = glCreateProgram();
	glAttachShader(program, vert);
	glAttachShader(program, frag);
	glLinkProgram(program);

	GLint success;
	glGetProgramiv(program, GL_LINK_STATUS, &success);

	if (!success)
	{
		char info_log[512];
		glGetProgramInfoLog(program, sizeof(info_log), NULL, info_log);
		ri.Con_Printf(PRINT_ALL, "GL3 Program link error: %s\n", info_log);
		glDeleteProgram(program);
		glDeleteShader(vert);
		glDeleteShader(frag);
		return 0;
	}

	// Shaders can be detached/deleted after linking.
	glDetachShader(program, vert);
	glDetachShader(program, frag);
	glDeleteShader(vert);
	glDeleteShader(frag);

	return program;
}

// ============================================================
// Public API.
// ============================================================

static GLuint currentProgram = 0;

void GL3_UseShader(const GLuint program)
{
	if (currentProgram != program)
	{
		glUseProgram(program);
		currentProgram = program;
	}
}

qboolean GL3_InitShaders(void)
{
	// --- 2D shader ---
	gl3state.shader2D = CreateProgram(vertexSource2D, fragmentSource2D);
	if (gl3state.shader2D == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitShaders: failed to create 2D shader program\n");
		return false;
	}

	gl3state.uni2D_projection = glGetUniformLocation(gl3state.shader2D, "uProjection");
	gl3state.uni2D_texture = glGetUniformLocation(gl3state.shader2D, "uTexture");
	gl3state.uni2D_color = glGetUniformLocation(gl3state.shader2D, "uColor");

	// --- 3D textured shader ---
	gl3state.shader3D = CreateProgram(vertexSource3D, fragmentSource3D);
	if (gl3state.shader3D == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitShaders: failed to create 3D shader program\n");
		return false;
	}

	gl3state.uni3D_projection = glGetUniformLocation(gl3state.shader3D, "uProjection");
	gl3state.uni3D_modelview = glGetUniformLocation(gl3state.shader3D, "uModelview");
	gl3state.uni3D_texture = glGetUniformLocation(gl3state.shader3D, "uTexture");
	gl3state.uni3D_color = glGetUniformLocation(gl3state.shader3D, "uColor");

	gl3state.uni3D_numDlights   = glGetUniformLocation(gl3state.shader3D, "uNumDlights");
	gl3state.uni3D_dlightPosRad = glGetUniformLocation(gl3state.shader3D, "uDlightPosRad");
	gl3state.uni3D_dlightColor  = glGetUniformLocation(gl3state.shader3D, "uDlightColor");

	// --- 3D color-only shader ---
	gl3state.shader3DColor = CreateProgram(vertexSource3DColor, fragmentSource3DColor);
	if (gl3state.shader3DColor == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitShaders: failed to create 3D color shader program\n");
		return false;
	}

	gl3state.uni3DColor_projection = glGetUniformLocation(gl3state.shader3DColor, "uProjection");
	gl3state.uni3DColor_modelview = glGetUniformLocation(gl3state.shader3DColor, "uModelview");
	gl3state.uni3DColor_color = glGetUniformLocation(gl3state.shader3DColor, "uColor");

	// --- 3D lightmapped shader ---
	gl3state.shader3DLightmap = CreateProgram(vertexSource3DLM, fragmentSource3DLM);
	if (gl3state.shader3DLightmap == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitShaders: failed to create 3D lightmap shader program\n");
		return false;
	}

	gl3state.uni3DLM_projection = glGetUniformLocation(gl3state.shader3DLightmap, "uProjection");
	gl3state.uni3DLM_modelview  = glGetUniformLocation(gl3state.shader3DLightmap, "uModelview");
	gl3state.uni3DLM_diffuse    = glGetUniformLocation(gl3state.shader3DLightmap, "uDiffuse");
	gl3state.uni3DLM_lightmap   = glGetUniformLocation(gl3state.shader3DLightmap, "uLightmap");
	gl3state.uni3DLM_color      = glGetUniformLocation(gl3state.shader3DLightmap, "uColor");

	// Bind sampler units once
	GL3_UseShader(gl3state.shader2D);
	glUniform1i(gl3state.uni2D_texture, 0);
	glUniform4f(gl3state.uni2D_color, 1.0f, 1.0f, 1.0f, 1.0f);

	GL3_UseShader(gl3state.shader3D);
	glUniform1i(gl3state.uni3D_texture, 0);
	glUniform4f(gl3state.uni3D_color, 1.0f, 1.0f, 1.0f, 1.0f);
	glUniform1i(gl3state.uni3D_numDlights, 0);

	GL3_UseShader(gl3state.shader3DLightmap);
	glUniform1i(gl3state.uni3DLM_diffuse, 0);
	glUniform1i(gl3state.uni3DLM_lightmap, 1);
	glUniform4f(gl3state.uni3DLM_color, 1.0f, 1.0f, 1.0f, 1.0f);

	// --- Create shared 2D VAO/VBO ---
	glGenVertexArrays(1, &gl3state.vao2D);
	glGenBuffers(1, &gl3state.vbo2D);

	glBindVertexArray(gl3state.vao2D);
	glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo2D);

	// 2D vertex layout: vec2 pos, vec2 texcoord, vec4 color = 8 floats per vertex.
	const GLsizei stride2D = 8 * sizeof(float);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride2D, (void*)0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride2D, (void*)(2 * sizeof(float)));
	glEnableVertexAttribArray(1);

	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride2D, (void*)(4 * sizeof(float)));
	glEnableVertexAttribArray(2);

	glBindVertexArray(0);

	// --- Create lightmapped world VAO/VBO (VERTEXSIZE=7 floats: pos3+tc2+lmtc2) ---
	glGenVertexArrays(1, &gl3state.vao3DLM);
	glGenBuffers(1, &gl3state.vbo3DLM);

	glBindVertexArray(gl3state.vao3DLM);
	glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo3DLM);

	const GLsizei strideLM = 7 * sizeof(float);

	// pos3 (location 0).
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, strideLM, (void*)0);
	glEnableVertexAttribArray(0);

	// tc2 diffuse (location 1).
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, strideLM, (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	// tc2 lightmap (location 2).
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, strideLM, (void*)(5 * sizeof(float)));
	glEnableVertexAttribArray(2);

	glBindVertexArray(0);

	// --- Create generic 3D VAO/VBO (9 floats/vert: pos3+tc2+col4) ---
	glGenVertexArrays(1, &gl3state.vao3D);
	glGenBuffers(1, &gl3state.vbo3D);

	glBindVertexArray(gl3state.vao3D);
	glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo3D);

	const GLsizei stride3D = 9 * sizeof(float);

	// pos3 (location 0).
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride3D, (void*)0);
	glEnableVertexAttribArray(0);

	// tc2 (location 1).
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride3D, (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	// col4 (location 2).
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride3D, (void*)(5 * sizeof(float)));
	glEnableVertexAttribArray(2);

	glBindVertexArray(0);

	// --- Create full-screen quad VAO/VBO (4 floats/vert: NDC pos2 + tc2) ---
	{
		static const float fsq_verts[4 * 4] = {
			-1.0f,  1.0f,  0.0f, 1.0f,   // top-left
			-1.0f, -1.0f,  0.0f, 0.0f,   // bottom-left
			 1.0f,  1.0f,  1.0f, 1.0f,   // top-right
			 1.0f, -1.0f,  1.0f, 0.0f    // bottom-right
		};

		glGenVertexArrays(1, &gl3state.vaoFSQ);
		glGenBuffers(1, &gl3state.vboFSQ);

		glBindVertexArray(gl3state.vaoFSQ);
		glBindBuffer(GL_ARRAY_BUFFER, gl3state.vboFSQ);
		glBufferData(GL_ARRAY_BUFFER, sizeof(fsq_verts), fsq_verts, GL_STATIC_DRAW);

		const GLsizei strideFSQ = 4 * sizeof(float);

		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, strideFSQ, (void*)0);
		glEnableVertexAttribArray(0);

		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, strideFSQ, (void*)(2 * sizeof(float)));
		glEnableVertexAttribArray(1);

		glBindVertexArray(0);
	}

	// --- Post-process shader ---
	gl3state.shaderPost = CreateProgram(vertexSourcePost, fragmentSourcePost);
	if (gl3state.shaderPost == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitShaders: failed to create post-process shader program\n");
		return false;
	}

	gl3state.uniPost_hdrBuffer = glGetUniformLocation(gl3state.shaderPost, "uHDRBuffer");
	gl3state.uniPost_exposure  = glGetUniformLocation(gl3state.shaderPost, "uExposure");

	gl3state.uniPost_depthMap    = glGetUniformLocation(gl3state.shaderPost, "uDepthMap");
	gl3state.uniPost_fogEnabled  = glGetUniformLocation(gl3state.shaderPost, "uFogEnabled");
	gl3state.uniPost_fogMode     = glGetUniformLocation(gl3state.shaderPost, "uFogMode");
	gl3state.uniPost_fogColor    = glGetUniformLocation(gl3state.shaderPost, "uFogColor");
	gl3state.uniPost_fogDensity  = glGetUniformLocation(gl3state.shaderPost, "uFogDensity");
	gl3state.uniPost_fogStart    = glGetUniformLocation(gl3state.shaderPost, "uFogStart");
	gl3state.uniPost_fogEnd      = glGetUniformLocation(gl3state.shaderPost, "uFogEnd");
	gl3state.uniPost_fogNearFar  = glGetUniformLocation(gl3state.shaderPost, "uFogNearFar");

	GL3_UseShader(gl3state.shaderPost);
	glUniform1i(gl3state.uniPost_hdrBuffer, 0);
	glUniform1f(gl3state.uniPost_exposure, 1.0f);
	glUniform1i(gl3state.uniPost_depthMap, 3);
	glUniform1i(gl3state.uniPost_fogEnabled, 0);

	// --- Create 1x1 white texture for color-only 2D drawing ---
	{
		const GLubyte white_pixel[4] = { 255, 255, 255, 255 };
		glGenTextures(1, &gl3state.whiteTexture);
		glBindTexture(GL_TEXTURE_2D, gl3state.whiteTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white_pixel);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	// --- Water surface shader ---
	gl3state.shaderWater = CreateProgram(vertexSourceWater, fragmentSourceWater);
	if (gl3state.shaderWater == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitShaders: failed to create water shader program\n");
		return false;
	}

	gl3state.uniWater_projection = glGetUniformLocation(gl3state.shaderWater, "uProjection");
	gl3state.uniWater_modelview  = glGetUniformLocation(gl3state.shaderWater, "uModelview");
	gl3state.uniWater_color      = glGetUniformLocation(gl3state.shaderWater, "uColor");
	gl3state.uniWater_reflectTex = glGetUniformLocation(gl3state.shaderWater, "uReflectTex");
	gl3state.uniWater_reflectAmt = glGetUniformLocation(gl3state.shaderWater, "uReflectAmt");
	gl3state.uniWater_time       = glGetUniformLocation(gl3state.shaderWater, "uTime");

	GL3_UseShader(gl3state.shaderWater);
	glUniform1i(gl3state.uniWater_reflectTex, 1);      // TMU1: reflection texture.
	glUniform4f(gl3state.uniWater_color, 1.0f, 1.0f, 1.0f, 1.0f);
	glUniform1f(gl3state.uniWater_reflectAmt, 0.35f);
	glUniform1f(gl3state.uniWater_time, 0.0f);

	ri.Con_Printf(PRINT_ALL, "GL3 shaders initialized.\n");

	return true;
}

void GL3_ShutdownShaders(void)
{
	if (gl3state.vao2D != 0) { glDeleteVertexArrays(1, &gl3state.vao2D); gl3state.vao2D = 0; }
	if (gl3state.vbo2D != 0) { glDeleteBuffers(1, &gl3state.vbo2D); gl3state.vbo2D = 0; }

	if (gl3state.vao3DLM != 0) { glDeleteVertexArrays(1, &gl3state.vao3DLM); gl3state.vao3DLM = 0; }
	if (gl3state.vbo3DLM != 0) { glDeleteBuffers(1, &gl3state.vbo3DLM); gl3state.vbo3DLM = 0; }

	if (gl3state.vao3D != 0) { glDeleteVertexArrays(1, &gl3state.vao3D); gl3state.vao3D = 0; }
	if (gl3state.vbo3D != 0) { glDeleteBuffers(1, &gl3state.vbo3D); gl3state.vbo3D = 0; }

	if (gl3state.vaoFSQ != 0) { glDeleteVertexArrays(1, &gl3state.vaoFSQ); gl3state.vaoFSQ = 0; }
	if (gl3state.vboFSQ != 0) { glDeleteBuffers(1, &gl3state.vboFSQ); gl3state.vboFSQ = 0; }

	if (gl3state.shader2D != 0) { glDeleteProgram(gl3state.shader2D); gl3state.shader2D = 0; }
	if (gl3state.shader3D != 0) { glDeleteProgram(gl3state.shader3D); gl3state.shader3D = 0; }
	if (gl3state.shader3DColor != 0) { glDeleteProgram(gl3state.shader3DColor); gl3state.shader3DColor = 0; }
	if (gl3state.shader3DLightmap != 0) { glDeleteProgram(gl3state.shader3DLightmap); gl3state.shader3DLightmap = 0; }

	if (gl3state.shaderPost != 0) { glDeleteProgram(gl3state.shaderPost); gl3state.shaderPost = 0; }
	if (gl3state.shaderWater != 0) { glDeleteProgram(gl3state.shaderWater); gl3state.shaderWater = 0; }

	if (gl3state.whiteTexture != 0) { glDeleteTextures(1, &gl3state.whiteTexture); gl3state.whiteTexture = 0; }

	currentProgram = 0;
}

// ============================================================
// Matrix helpers.
// ============================================================

void GL3_UpdateProjection2D(const float width, const float height)
{
	// Orthographic projection: left=0, right=width, top=0, bottom=height, near=-99999, far=99999.
	const float l = 0.0f;
	const float r = width;
	const float t = 0.0f;
	const float b = height;
	const float n = -99999.0f;
	const float f = 99999.0f;

	const float proj[16] = {
		2.0f / (r - l),       0.0f,                 0.0f,                0.0f,
		0.0f,                 2.0f / (t - b),       0.0f,                0.0f,
		0.0f,                 0.0f,                 -2.0f / (f - n),     0.0f,
		-(r + l) / (r - l),   -(t + b) / (t - b),   -(f + n) / (f - n), 1.0f
	};

	GL3_UseShader(gl3state.shader2D);
	glUniformMatrix4fv(gl3state.uni2D_projection, 1, GL_FALSE, proj);
}

void GL3_UpdateProjection3D(const float fov_y, const float aspect, const float znear, const float zfar)
{
	const float f = 1.0f / tanf(fov_y * (float)M_PI / 360.0f);

	const float proj[16] = {
		f / aspect, 0.0f, 0.0f,                                  0.0f,
		0.0f,       f,    0.0f,                                  0.0f,
		0.0f,       0.0f, (zfar + znear) / (znear - zfar),      -1.0f,
		0.0f,       0.0f, (2.0f * zfar * znear) / (znear - zfar), 0.0f
	};

	GL3_UseShader(gl3state.shader3D);
	glUniformMatrix4fv(gl3state.uni3D_projection, 1, GL_FALSE, proj);

	GL3_UseShader(gl3state.shader3DColor);
	glUniformMatrix4fv(gl3state.uni3DColor_projection, 1, GL_FALSE, proj);

	GL3_UseShader(gl3state.shader3DLightmap);
	glUniformMatrix4fv(gl3state.uni3DLM_projection, 1, GL_FALSE, proj);

	if (gl3state.shaderWater != 0)
	{
		GL3_UseShader(gl3state.shaderWater);
		glUniformMatrix4fv(gl3state.uniWater_projection, 1, GL_FALSE, proj);
	}

	// Store projection matrix for software queries.
	memcpy(r_projection_matrix, proj, sizeof(proj));

	// Cache projection parameters for SSAO view-space reconstruction.
	gl3state.projParams[0] = proj[0];   // P[0]  = f / aspect
	gl3state.projParams[1] = proj[5];   // P[5]  = f
	gl3state.projParams[2] = proj[10];  // P[10] = (zfar+znear)/(znear-zfar)
	gl3state.projParams[3] = proj[14];  // P[14] = 2*zfar*znear/(znear-zfar)
}

void GL3_UpdateModelview3D(const float* matrix4x4)
{
	GL3_UseShader(gl3state.shader3D);
	glUniformMatrix4fv(gl3state.uni3D_modelview, 1, GL_FALSE, matrix4x4);

	GL3_UseShader(gl3state.shader3DColor);
	glUniformMatrix4fv(gl3state.uni3DColor_modelview, 1, GL_FALSE, matrix4x4);

	GL3_UseShader(gl3state.shader3DLightmap);
	glUniformMatrix4fv(gl3state.uni3DLM_modelview, 1, GL_FALSE, matrix4x4);

	if (gl3state.shaderWater != 0)
	{
		GL3_UseShader(gl3state.shaderWater);
		glUniformMatrix4fv(gl3state.uniWater_modelview, 1, GL_FALSE, matrix4x4);
	}
}

void GL3_UpdateModelviewLM(const float* matrix4x4)
{
	GL3_UseShader(gl3state.shader3DLightmap);
	glUniformMatrix4fv(gl3state.uni3DLM_modelview, 1, GL_FALSE, matrix4x4);
}

void GL3_SetLMColor(const float r, const float g, const float b, const float a)
{
	GL3_UseShader(gl3state.shader3DLightmap);
	glUniform4f(gl3state.uni3DLM_color, r, g, b, a);
}

void GL3_Set3DColor(const float r, const float g, const float b, const float a)
{
	GL3_UseShader(gl3state.shader3D);
	glUniform4f(gl3state.uni3D_color, r, g, b, a);

	if (gl3state.shaderWater != 0)
	{
		GL3_UseShader(gl3state.shaderWater);
		glUniform4f(gl3state.uniWater_color, r, g, b, a);
	}
}

// ============================================================
// Dynamic polygon drawing helpers.
// ============================================================

// Draw a polygon using shader3DLightmap (VERTEXSIZE=7 floats/vert: pos3+tc2+lmtc2).
void GL3_DrawLMPoly(const float* verts, const int numverts)
{
	GL3_UseShader(gl3state.shader3DLightmap);
	glBindVertexArray(gl3state.vao3DLM);
	glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo3DLM);
	glBufferData(GL_ARRAY_BUFFER, numverts * 7 * sizeof(float), verts, GL_STREAM_DRAW);
	glDrawArrays(GL_TRIANGLE_FAN, 0, numverts);
}

// Draw a polygon using shader3D (9 floats/vert: pos3+tc2+col4).
void GL3_Draw3DPoly(const GLenum mode, const float* verts, const int numverts)
{
	GL3_UseShader(gl3state.shader3D);
	glBindVertexArray(gl3state.vao3D);
	glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo3D);
	glBufferData(GL_ARRAY_BUFFER, numverts * 9 * sizeof(float), verts, GL_STREAM_DRAW);
	glDrawArrays(mode, 0, numverts);
}

// Draw a water polygon using shaderWater (same 9 floats/vert as GL3_Draw3DPoly).
// Binds the reflection texture to TMU1; TMU0 must already be bound to the water diffuse.
void GL3_DrawWaterPoly(const GLenum mode, const float* verts, const int numverts)
{
	GL3_UseShader(gl3state.shaderWater);

	// Update per-draw time uniform for animated distortion.
	glUniform1f(gl3state.uniWater_time, r_newrefdef.time);

	// Bind the reflection texture to TMU1 without disturbing the cached TMU0 state.
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboTexReflect);
	glActiveTexture(GL_TEXTURE0);

	glBindVertexArray(gl3state.vao3D);
	glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo3D);
	glBufferData(GL_ARRAY_BUFFER, numverts * 9 * sizeof(float), verts, GL_STREAM_DRAW);
	glDrawArrays(mode, 0, numverts);
}

// ============================================================
// Fog configuration helpers.
// ============================================================

void GL3_SetFog(const int enabled, const int mode, const float r, const float g, const float b, const float density, const float start, const float end)
{
	// Update post-process shader fog uniforms (depth-based fog applied after bloom).
	GL3_UseShader(gl3state.shaderPost);
	glUniform1i(gl3state.uniPost_fogEnabled, enabled);
	if (enabled)
	{
		glUniform1i(gl3state.uniPost_fogMode, mode);
		glUniform3f(gl3state.uniPost_fogColor, r, g, b);
		glUniform1f(gl3state.uniPost_fogDensity, density);
		glUniform1f(gl3state.uniPost_fogStart, start);
		glUniform1f(gl3state.uniPost_fogEnd, end);
		glUniform2f(gl3state.uniPost_fogNearFar, 1.0f, end);
	}
}

void GL3_DisableFog(void)
{
	GL3_SetFog(0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
}

// ============================================================
// FBO management.
// ============================================================

qboolean GL3_InitFBO(const int width, const int height)
{
	glGenFramebuffers(1, &gl3state.fbo3D);
	glBindFramebuffer(GL_FRAMEBUFFER, gl3state.fbo3D);

	// RGBA16F color texture.
	glGenTextures(1, &gl3state.fboTex3D);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboTex3D);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl3state.fboTex3D, 0);

	// Depth-stencil texture (depth samplable by SSAO shader, stencil used by shadow system).
	glGenTextures(1, &gl3state.fboDepth3D);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboDepth3D);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, gl3state.fboDepth3D, 0);

	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitFBO: framebuffer incomplete (status 0x%x)\n", (unsigned)status);
		GL3_ShutdownFBO();
		return false;
	}

	gl3state.fbo_width  = width;
	gl3state.fbo_height = height;

	ri.Con_Printf(PRINT_ALL, "GL3 HDR FBO initialized (%dx%d RGBA16F).\n", width, height);

	return true;
}

void GL3_ShutdownFBO(void)
{
	if (gl3state.fboTex3D   != 0) { glDeleteTextures(1, &gl3state.fboTex3D);         gl3state.fboTex3D   = 0; }
	if (gl3state.fboDepth3D != 0) { glDeleteTextures(1, &gl3state.fboDepth3D);        gl3state.fboDepth3D = 0; }
	if (gl3state.fbo3D      != 0) { glDeleteFramebuffers(1, &gl3state.fbo3D);        gl3state.fbo3D      = 0; }

	gl3state.fbo_width  = 0;
	gl3state.fbo_height = 0;
}

// ============================================================
// Reflection FBO management.
// ============================================================

qboolean GL3_InitReflect(const int width, const int height)
{
	gl3state.reflect_width  = (width  > 1) ? width  / 2 : 1;
	gl3state.reflect_height = (height > 1) ? height / 2 : 1;

	// RGBA16F color texture (sampled by the water shader).
	glGenTextures(1, &gl3state.fboTexReflect);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboTexReflect);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gl3state.reflect_width, gl3state.reflect_height, 0, GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	// Depth renderbuffer (not sampled; only needed for depth testing during reflection render).
	glGenRenderbuffers(1, &gl3state.rboReflectDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, gl3state.rboReflectDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, gl3state.reflect_width, gl3state.reflect_height);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	glGenFramebuffers(1, &gl3state.fboReflect);
	glBindFramebuffer(GL_FRAMEBUFFER, gl3state.fboReflect);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl3state.fboTexReflect, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, gl3state.rboReflectDepth);

	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitReflect: framebuffer incomplete (status 0x%x)\n", (unsigned)status);
		GL3_ShutdownReflect();
		return false;
	}

	ri.Con_Printf(PRINT_ALL, "GL3 reflection FBO initialized (%dx%d RGBA16F).\n", gl3state.reflect_width, gl3state.reflect_height);
	return true;
}

void GL3_ShutdownReflect(void)
{
	if (gl3state.fboTexReflect   != 0) { glDeleteTextures(1,      &gl3state.fboTexReflect);   gl3state.fboTexReflect   = 0; }
	if (gl3state.rboReflectDepth != 0) { glDeleteRenderbuffers(1, &gl3state.rboReflectDepth); gl3state.rboReflectDepth = 0; }
	if (gl3state.fboReflect      != 0) { glDeleteFramebuffers(1,  &gl3state.fboReflect);      gl3state.fboReflect      = 0; }

	gl3state.reflect_width  = 0;
	gl3state.reflect_height = 0;
}

// ============================================================
// Bloom FBO + shader management.
// ============================================================

// Creates a single-color-attachment framebuffer with a GL_RGB16F texture (no depth).
// Returns false on failure; on failure the partially-created resources are left for GL3_ShutdownBloom to clean up.
static qboolean CreateBloomFBO(GLuint* out_fbo, GLuint* out_tex, const int w, const int h)
{
	glGenTextures(1, out_tex);
	glBindTexture(GL_TEXTURE_2D, *out_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, out_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, *out_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *out_tex, 0);

	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		ri.Con_Printf(PRINT_ALL, "GL3 bloom FBO incomplete (status 0x%x)\n", (unsigned)status);
		return false;
	}

	return true;
}

qboolean GL3_InitBloom(const int width, const int height)
{
	gl3state.bloom_width  = width  > 1 ? width  / 2 : 1;
	gl3state.bloom_height = height > 1 ? height / 2 : 1;

	// --- Bloom extract shader ---
	gl3state.shaderBloomExtract = CreateProgram(vertexSourcePost, fragmentSourceBloomExtract);
	if (gl3state.shaderBloomExtract == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitBloom: failed to create bloom extract shader\n");
		return false;
	}

	gl3state.uniBloomExtract_hdrBuffer = glGetUniformLocation(gl3state.shaderBloomExtract, "uHDRBuffer");
	gl3state.uniBloomExtract_threshold = glGetUniformLocation(gl3state.shaderBloomExtract, "uThreshold");

	GL3_UseShader(gl3state.shaderBloomExtract);
	glUniform1i(gl3state.uniBloomExtract_hdrBuffer, 0);

	// --- Bloom blur shader ---
	gl3state.shaderBloomBlur = CreateProgram(vertexSourcePost, fragmentSourceBloomBlur);
	if (gl3state.shaderBloomBlur == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitBloom: failed to create bloom blur shader\n");
		return false;
	}

	gl3state.uniBloomBlur_image      = glGetUniformLocation(gl3state.shaderBloomBlur, "uImage");
	gl3state.uniBloomBlur_horizontal = glGetUniformLocation(gl3state.shaderBloomBlur, "uHorizontal");
	gl3state.uniBloomBlur_texelSize  = glGetUniformLocation(gl3state.shaderBloomBlur, "uTexelSize");

	GL3_UseShader(gl3state.shaderBloomBlur);
	glUniform1i(gl3state.uniBloomBlur_image, 0);

	// --- Query and initialise bloom uniforms in shaderPost ---
	gl3state.uniPost_bloomBuffer   = glGetUniformLocation(gl3state.shaderPost, "uBloomBuffer");
	gl3state.uniPost_bloomStrength = glGetUniformLocation(gl3state.shaderPost, "uBloomStrength");

	GL3_UseShader(gl3state.shaderPost);
	glUniform1i(gl3state.uniPost_bloomBuffer,   1);		// TMU1
	glUniform1f(gl3state.uniPost_bloomStrength, 0.0f);

	// --- Create bloom FBOs ---
	if (!CreateBloomFBO(&gl3state.fboBloomExtract, &gl3state.fboTexBloomExtract,
						gl3state.bloom_width, gl3state.bloom_height))
		return false;

	for (int i = 0; i < 2; i++)
	{
		if (!CreateBloomFBO(&gl3state.fboBloomPingPong[i], &gl3state.fboTexBloomPingPong[i],
							gl3state.bloom_width, gl3state.bloom_height))
			return false;
	}

	ri.Con_Printf(PRINT_ALL, "GL3 bloom FBOs initialized (%dx%d).\n", gl3state.bloom_width, gl3state.bloom_height);

	return true;
}

void GL3_ShutdownBloom(void)
{
	if (gl3state.fboTexBloomExtract != 0) { glDeleteTextures(1,     &gl3state.fboTexBloomExtract);  gl3state.fboTexBloomExtract = 0; }
	if (gl3state.fboBloomExtract    != 0) { glDeleteFramebuffers(1, &gl3state.fboBloomExtract);     gl3state.fboBloomExtract    = 0; }

	for (int i = 0; i < 2; i++)
	{
		if (gl3state.fboTexBloomPingPong[i] != 0) { glDeleteTextures(1,     &gl3state.fboTexBloomPingPong[i]);  gl3state.fboTexBloomPingPong[i] = 0; }
		if (gl3state.fboBloomPingPong[i]    != 0) { glDeleteFramebuffers(1, &gl3state.fboBloomPingPong[i]);     gl3state.fboBloomPingPong[i]    = 0; }
	}

	if (gl3state.shaderBloomExtract != 0) { glDeleteProgram(gl3state.shaderBloomExtract); gl3state.shaderBloomExtract = 0; }
	if (gl3state.shaderBloomBlur    != 0) { glDeleteProgram(gl3state.shaderBloomBlur);    gl3state.shaderBloomBlur    = 0; }

	gl3state.bloom_width  = 0;
	gl3state.bloom_height = 0;
}

void GL3_CompositeHDR(const int w, const int h, const float exposure, const float bloom_strength, const float ao_strength)
{
	if (gl3state.fbo3D == 0 || gl3state.shaderPost == 0)
		return;

	glViewport(0, 0, w, h);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);

	GL3_UseShader(gl3state.shaderPost);
	glUniform1f(gl3state.uniPost_exposure,      exposure);
	glUniform1f(gl3state.uniPost_bloomStrength, bloom_strength);
	glUniform1f(gl3state.uniPost_aoStrength,    ao_strength);

	// Bind the HDR color texture to TMU0 and keep the binding cache consistent.
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboTex3D);
	gl_state.currenttmu = 0;
	gl_state.currenttextures[0] = (int)gl3state.fboTex3D;

	// Bind the bloom result (or a 1x1 white fallback) to TMU1.
	glActiveTexture(GL_TEXTURE1);
	const GLuint bloom_tex = (gl3state.fboTexBloomPingPong[1] != 0)
		? gl3state.fboTexBloomPingPong[1]
		: gl3state.whiteTexture;
	glBindTexture(GL_TEXTURE_2D, bloom_tex);
	gl_state.currenttextures[1] = (int)bloom_tex;

	// Bind the AO result (or white fallback) to TMU2 — skips gl_state cache (MAX_TEXTURE_UNITS=2).
	glActiveTexture(GL_TEXTURE2);
	const GLuint ao_tex = (gl3state.fboTexSSAOBlur != 0)
		? gl3state.fboTexSSAOBlur
		: gl3state.whiteTexture;
	glBindTexture(GL_TEXTURE_2D, ao_tex);

	// Bind the depth texture to TMU3 for post-process fog.
	glActiveTexture(GL_TEXTURE3);
	if (gl3state.fboDepth3D != 0)
		glBindTexture(GL_TEXTURE_2D, gl3state.fboDepth3D);

	// Restore active unit to TMU0 so subsequent engine code isn't surprised.
	glActiveTexture(GL_TEXTURE0);

	glBindVertexArray(gl3state.vaoFSQ);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
}

// Number of separable Gaussian blur passes (must be even so the final result lands in pingpong[1]).
#define BLOOM_BLUR_PASSES 10

void GL3_RenderBloom(const float threshold, const float strength)
{
	if (strength <= 0.0f || gl3state.fboBloomExtract == 0)
		return;

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glViewport(0, 0, gl3state.bloom_width, gl3state.bloom_height);

	glBindVertexArray(gl3state.vaoFSQ);
	glActiveTexture(GL_TEXTURE0);

	// --- Bright-pass extract ---
	glBindFramebuffer(GL_FRAMEBUFFER, gl3state.fboBloomExtract);
	GL3_UseShader(gl3state.shaderBloomExtract);
	glUniform1f(gl3state.uniBloomExtract_threshold, threshold);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboTex3D);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	// --- Separable Gaussian blur: ping-pong between two half-res FBOs ---
	// Pass i even  → horizontal, writes to pingpong[0].
	// Pass i odd   → vertical,   writes to pingpong[1].
	// After BLOOM_BLUR_PASSES (even count) the result is in pingpong[1].
	GL3_UseShader(gl3state.shaderBloomBlur);
	const float texel_w = 1.0f / (float)gl3state.bloom_width;
	const float texel_h = 1.0f / (float)gl3state.bloom_height;
	glUniform2f(gl3state.uniBloomBlur_texelSize, texel_w, texel_h);

	for (int i = 0; i < BLOOM_BLUR_PASSES; i++)
	{
		const int horizontal = (i % 2 == 0) ? 1 : 0;
		const int dst = horizontal ? 0 : 1;
		const GLuint src_tex = (i == 0)
			? gl3state.fboTexBloomExtract
			: gl3state.fboTexBloomPingPong[1 - dst];

		glBindFramebuffer(GL_FRAMEBUFFER, gl3state.fboBloomPingPong[dst]);
		glUniform1i(gl3state.uniBloomBlur_horizontal, horizontal);
		glBindTexture(GL_TEXTURE_2D, src_tex);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindVertexArray(0);
	// Final bloom result is now in fboTexBloomPingPong[1].
}

// ============================================================
// Per-frame dynamic lighting.
// ============================================================

void GL3_UpdateDlights(void)
{
	const int n = (r_newrefdef.num_dlights < GL3_MAX_DLIGHTS)
				  ? r_newrefdef.num_dlights : GL3_MAX_DLIGHTS;

	GL3_UseShader(gl3state.shader3D);
	glUniform1i(gl3state.uni3D_numDlights, n);

	if (n == 0)
		return;

	float pos_rad[GL3_MAX_DLIGHTS * 4];
	float colors[GL3_MAX_DLIGHTS * 4];

	// r_world_matrix is the view matrix (world-space → view-space).
	const float* M = r_world_matrix;

	for (int i = 0; i < n; i++)
	{
		const dlight_t* dl = &r_newrefdef.dlights[i];
		const float x = dl->origin[0];
		const float y = dl->origin[1];
		const float z = dl->origin[2];

		// Column-major matrix-vector multiply: view_pos = M * [x, y, z, 1].
		pos_rad[i * 4 + 0] = M[0] * x + M[4] * y + M[8]  * z + M[12];
		pos_rad[i * 4 + 1] = M[1] * x + M[5] * y + M[9]  * z + M[13];
		pos_rad[i * 4 + 2] = M[2] * x + M[6] * y + M[10] * z + M[14];
		pos_rad[i * 4 + 3] = dl->intensity;

		colors[i * 4 + 0] = (float)dl->color.r / 255.0f;
		colors[i * 4 + 1] = (float)dl->color.g / 255.0f;
		colors[i * 4 + 2] = (float)dl->color.b / 255.0f;
		colors[i * 4 + 3] = 0.0f;
	}

	glUniform4fv(gl3state.uni3D_dlightPosRad, n, pos_rad);
	glUniform4fv(gl3state.uni3D_dlightColor,  n, colors);
}

// ============================================================
// SSAO post-process.
// ============================================================

void GL3_ShutdownSSAO(void)
{
	if (gl3state.fboTexSSAO     != 0) { glDeleteTextures(1,     &gl3state.fboTexSSAO);     gl3state.fboTexSSAO     = 0; }
	if (gl3state.fboSSAO        != 0) { glDeleteFramebuffers(1, &gl3state.fboSSAO);        gl3state.fboSSAO        = 0; }
	if (gl3state.fboTexSSAOBlur != 0) { glDeleteTextures(1,     &gl3state.fboTexSSAOBlur); gl3state.fboTexSSAOBlur = 0; }
	if (gl3state.fboSSAOBlur    != 0) { glDeleteFramebuffers(1, &gl3state.fboSSAOBlur);    gl3state.fboSSAOBlur    = 0; }
	if (gl3state.ssaoNoiseTex   != 0) { glDeleteTextures(1,     &gl3state.ssaoNoiseTex);   gl3state.ssaoNoiseTex   = 0; }
	if (gl3state.shaderSSAO     != 0) { glDeleteProgram(gl3state.shaderSSAO);              gl3state.shaderSSAO     = 0; }
	if (gl3state.shaderSSAOBlur != 0) { glDeleteProgram(gl3state.shaderSSAOBlur);          gl3state.shaderSSAOBlur = 0; }
}

qboolean GL3_InitSSAO(const int width, const int height)
{
	// --- SSAO shader ---
	gl3state.shaderSSAO = CreateProgram(vertexSourcePost, fragmentSourceSSAO);
	if (gl3state.shaderSSAO == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitSSAO: failed to create SSAO shader\n");
		return false;
	}

	gl3state.uniSSAO_depthMap   = glGetUniformLocation(gl3state.shaderSSAO, "uDepthMap");
	gl3state.uniSSAO_noiseTex   = glGetUniformLocation(gl3state.shaderSSAO, "uNoiseTex");
	gl3state.uniSSAO_kernel     = glGetUniformLocation(gl3state.shaderSSAO, "uKernel");
	gl3state.uniSSAO_projParams = glGetUniformLocation(gl3state.shaderSSAO, "uProjParams");
	gl3state.uniSSAO_radius     = glGetUniformLocation(gl3state.shaderSSAO, "uRadius");
	gl3state.uniSSAO_bias       = glGetUniformLocation(gl3state.shaderSSAO, "uBias");
	gl3state.uniSSAO_screenSize = glGetUniformLocation(gl3state.shaderSSAO, "uScreenSize");

	GL3_UseShader(gl3state.shaderSSAO);
	glUniform1i(gl3state.uniSSAO_depthMap, 0);	// TMU0
	glUniform1i(gl3state.uniSSAO_noiseTex, 1);	// TMU1

	// --- SSAO blur shader ---
	gl3state.shaderSSAOBlur = CreateProgram(vertexSourcePost, fragmentSourceSSAOBlur);
	if (gl3state.shaderSSAOBlur == 0)
	{
		ri.Con_Printf(PRINT_ALL, "GL3_InitSSAO: failed to create SSAO blur shader\n");
		GL3_ShutdownSSAO();
		return false;
	}

	gl3state.uniSSAOBlur_ssaoInput = glGetUniformLocation(gl3state.shaderSSAOBlur, "uSSAOInput");
	gl3state.uniSSAOBlur_texelSize = glGetUniformLocation(gl3state.shaderSSAOBlur, "uTexelSize");

	GL3_UseShader(gl3state.shaderSSAOBlur);
	glUniform1i(gl3state.uniSSAOBlur_ssaoInput, 0);	// TMU0

	// --- Wire up AO uniforms in shaderPost (queried here; sampler bound to TMU2 once) ---
	gl3state.uniPost_aoBuffer   = glGetUniformLocation(gl3state.shaderPost, "uAOBuffer");
	gl3state.uniPost_aoStrength = glGetUniformLocation(gl3state.shaderPost, "uAOStrength");

	GL3_UseShader(gl3state.shaderPost);
	glUniform1i(gl3state.uniPost_aoBuffer,   2);		// TMU2
	glUniform1f(gl3state.uniPost_aoStrength, 0.0f);

	// --- Generate SSAO hemisphere sample kernel (16 samples, deterministic LCG) ---
	{
		unsigned int state = 12345u;
		float kernel[16 * 3];

		for (int i = 0; i < 16; i++)
		{
			state = state * 1664525u + 1013904223u;
			const float rx = (float)state / 4294967295.0f;
			state = state * 1664525u + 1013904223u;
			const float ry = (float)state / 4294967295.0f;
			state = state * 1664525u + 1013904223u;
			const float rz = (float)state / 4294967295.0f;

			// Hemisphere sample: x,y in [-1,1], z in [0,1].
			float sx = rx * 2.0f - 1.0f;
			float sy = ry * 2.0f - 1.0f;
			float sz = rz;

			// Normalize.
			const float len = sqrtf(sx * sx + sy * sy + sz * sz);
			if (len > 0.0f) { sx /= len; sy /= len; sz /= len; }

			// Accelerate distribution toward origin: lerp(0.1, 1.0, (i/16)^2).
			float scale = (float)i / 16.0f;
			scale = 0.1f + scale * scale * 0.9f;

			kernel[i * 3 + 0] = sx * scale;
			kernel[i * 3 + 1] = sy * scale;
			kernel[i * 3 + 2] = sz * scale;
		}

		GL3_UseShader(gl3state.shaderSSAO);
		glUniform3fv(gl3state.uniSSAO_kernel, 16, kernel);
	}

	// --- Generate 4x4 noise texture (random XY rotation vectors, z=0) ---
	{
		unsigned int state = 98765u;
		float noise[4 * 4 * 3];

		for (int i = 0; i < 16; i++)
		{
			state = state * 1664525u + 1013904223u;
			const float nx = (float)state / 4294967295.0f * 2.0f - 1.0f;
			state = state * 1664525u + 1013904223u;
			const float ny = (float)state / 4294967295.0f * 2.0f - 1.0f;

			const float nlen = sqrtf(nx * nx + ny * ny);
			noise[i * 3 + 0] = (nlen > 0.0f) ? nx / nlen : 1.0f;
			noise[i * 3 + 1] = (nlen > 0.0f) ? ny / nlen : 0.0f;
			noise[i * 3 + 2] = 0.0f;
		}

		glGenTextures(1, &gl3state.ssaoNoiseTex);
		glBindTexture(GL_TEXTURE_2D, gl3state.ssaoNoiseTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, noise);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	// --- Create two full-res GL_R16F FBOs (raw SSAO + blurred result, no depth) ---
	{
		GLuint* fbos[2]     = { &gl3state.fboSSAO,    &gl3state.fboSSAOBlur    };
		GLuint* textures[2] = { &gl3state.fboTexSSAO, &gl3state.fboTexSSAOBlur };

		for (int i = 0; i < 2; i++)
		{
			glGenTextures(1, textures[i]);
			glBindTexture(GL_TEXTURE_2D, *textures[i]);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, width, height, 0, GL_RED, GL_FLOAT, NULL);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glBindTexture(GL_TEXTURE_2D, 0);

			glGenFramebuffers(1, fbos[i]);
			glBindFramebuffer(GL_FRAMEBUFFER, *fbos[i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *textures[i], 0);

			const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			if (status != GL_FRAMEBUFFER_COMPLETE)
			{
				ri.Con_Printf(PRINT_ALL, "GL3_InitSSAO: SSAO FBO %d incomplete (status 0x%x)\n", i, (unsigned)status);
				GL3_ShutdownSSAO();
				return false;
			}
		}
	}

	ri.Con_Printf(PRINT_ALL, "GL3 SSAO initialized (%dx%d).\n", width, height);

	return true;
}

void GL3_RenderSSAO(const float radius, const float bias)
{
	if (gl3state.fbo3D == 0 || gl3state.fboTexSSAOBlur == 0 || gl3state.shaderSSAO == 0)
		return;

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glViewport(0, 0, gl3state.fbo_width, gl3state.fbo_height);
	glBindVertexArray(gl3state.vaoFSQ);

	// --- SSAO pass: sample depth hemisphere → raw occlusion map ---
	glBindFramebuffer(GL_FRAMEBUFFER, gl3state.fboSSAO);
	GL3_UseShader(gl3state.shaderSSAO);
	glUniform1f(gl3state.uniSSAO_radius, radius);
	glUniform1f(gl3state.uniSSAO_bias, bias);
	glUniform2f(gl3state.uniSSAO_screenSize, (float)gl3state.fbo_width, (float)gl3state.fbo_height);
	glUniform4fv(gl3state.uniSSAO_projParams, 1, gl3state.projParams);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboDepth3D);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, gl3state.ssaoNoiseTex);
	glActiveTexture(GL_TEXTURE0);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	// --- Blur pass: 4x4 box blur smooths the raw occlusion map ---
	glBindFramebuffer(GL_FRAMEBUFFER, gl3state.fboSSAOBlur);
	GL3_UseShader(gl3state.shaderSSAOBlur);
	glUniform2f(gl3state.uniSSAOBlur_texelSize,
		1.0f / (float)gl3state.fbo_width,
		1.0f / (float)gl3state.fbo_height);
	glBindTexture(GL_TEXTURE_2D, gl3state.fboTexSSAO);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindVertexArray(0);

	// Restore TMU1 to idle so the engine's texture cache stays consistent.
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
}
