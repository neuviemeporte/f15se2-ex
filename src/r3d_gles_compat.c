/*
 * OpenGL 1.x immediate-mode emulation for the GLES2/WebGL renderer.
 *
 * The legacy renderer emits small batches with glBegin/glEnd and uses only
 * projection/model-view matrices, vertex colors, texture coordinates, and fog.
 * Capturing those attributes and drawing them through one shader preserves that
 * behavior without introducing a second scene implementation.
 */
#include "r3d_gles_compat.h"

#include <SDL3/SDL.h>

/* The public compatibility header redirects renderer calls to these wrappers.
 * Inside the implementation, enable/disable must reach the real GLES entry
 * points rather than recursively expanding back to the wrappers. */
#undef glEnable
#undef glDisable

#define GLES_BATCH_VERTICES 16384

typedef struct {
    GLfloat x, y, z;
    GLfloat r, g, b, a;
    GLfloat u, v;
    GLfloat fog;
} GlesVertex;

static GlesVertex s_vertices[GLES_BATCH_VERTICES];
/* Quad strips emit six vertices per pair after the first pair, almost 3x
 * their input size. Independent quads need only 1.5x. */
static GlesVertex s_triangles[GLES_BATCH_VERTICES * 3];
static bool s_batchOverflow = false;
static int s_vertexCount;
static GLenum s_mode;
static GLfloat s_color[4] = {1, 1, 1, 1};
static GLfloat s_texCoord[2];
static GLfloat s_fogCoord;
static GLfloat s_projection[16];
static GLfloat s_modelView[16];
static GLfloat *s_matrix = s_modelView;
static int s_textureEnabled;
static int s_fogEnabled;
static GLint s_fogMode = GL_EXP;
static GLfloat s_fogDensity;
static GLfloat s_fogStart;
static GLfloat s_fogEnd = 1;
static GLfloat s_fogColor[4];
static GLfloat s_pointSize = 1;

static GLuint s_program;
static GLint s_posLoc;
static GLint s_colorLoc;
static GLint s_texLoc;
static GLint s_fogLoc;
static GLint s_mvpLoc;
static GLint s_samplerLoc;
static GLint s_useTextureLoc;
static GLint s_useFogLoc;
static GLint s_fogModeLoc;
static GLint s_fogDensityLoc;
static GLint s_fogRangeLoc;
static GLint s_fogColorLoc;
static GLint s_pointSizeLoc;

static void identity(GLfloat *matrix) {
    int i;
    for (i = 0; i < 16; i++) matrix[i] = 0;
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1;
}

/* Column-major result = left * right, matching OpenGL matrix convention. */
static void multiply(GLfloat *result, const GLfloat *left, const GLfloat *right) {
    GLfloat out[16];
    int row, column, k;
    for (column = 0; column < 4; column++) {
        for (row = 0; row < 4; row++) {
            GLfloat value = 0;
            for (k = 0; k < 4; k++)
                value += left[k * 4 + row] * right[column * 4 + k];
            out[column * 4 + row] = value;
        }
    }
    SDL_memcpy(result, out, sizeof out);
}

static GLuint compileShader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    GLint okay = 0;
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &okay);
    if (!okay) {
        char message[512];
        glGetShaderInfoLog(shader, sizeof message, NULL, message);
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "GLES shader compile failed: %s", message);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

int r3dgles_compatInit(void) {
    static const char *vertexSource =
        "attribute vec3 a_position;\n"
        "attribute vec4 a_color;\n"
        "attribute vec2 a_texcoord;\n"
        "attribute float a_fog;\n"
        "uniform mat4 u_mvp;\n"
        "uniform float u_point_size;\n"
        "varying vec4 v_color;\n"
        "varying vec2 v_texcoord;\n"
        "varying float v_fog;\n"
        "void main() {\n"
        "  gl_Position = u_mvp * vec4(a_position, 1.0);\n"
        "  v_color = a_color;\n"
        "  v_texcoord = a_texcoord;\n"
        "  v_fog = a_fog;\n"
        "  gl_PointSize = u_point_size;\n"
        "}\n";
    static const char *fragmentSource =
        "precision mediump float;\n"
        "varying vec4 v_color;\n"
        "varying vec2 v_texcoord;\n"
        "varying float v_fog;\n"
        "uniform sampler2D u_texture;\n"
        "uniform int u_use_texture;\n"
        "uniform int u_use_fog;\n"
        "uniform int u_fog_mode;\n"
        "uniform float u_fog_density;\n"
        "uniform vec2 u_fog_range;\n"
        "uniform vec4 u_fog_color;\n"
        "void main() {\n"
        "  vec4 color = v_color;\n"
        "  if (u_use_texture != 0) color *= texture2D(u_texture, v_texcoord);\n"
        "  if (u_use_fog != 0) {\n"
        "    float keep;\n"
        "    if (u_fog_mode == 1)\n"
        "      keep = exp(-u_fog_density * max(v_fog, 0.0));\n"
        "    else if (u_fog_mode == 2) {\n"
        "      float d = u_fog_density * max(v_fog, 0.0);\n"
        "      keep = exp(-(d * d));\n"
        "    } else\n"
        "      keep = (u_fog_range.y - v_fog) / max(u_fog_range.y - u_fog_range.x, 0.0001);\n"
        "    color.rgb = mix(u_fog_color.rgb, color.rgb, clamp(keep, 0.0, 1.0));\n"
        "  }\n"
        "  gl_FragColor = color;\n"
        "}\n";
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    GLint okay = 0;
    if (!vertexShader || !fragmentShader) return 0;
    s_program = glCreateProgram();
    glAttachShader(s_program, vertexShader);
    glAttachShader(s_program, fragmentShader);
    glLinkProgram(s_program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    glGetProgramiv(s_program, GL_LINK_STATUS, &okay);
    if (!okay) {
        char message[512];
        glGetProgramInfoLog(s_program, sizeof message, NULL, message);
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "GLES program link failed: %s", message);
        return 0;
    }
    s_posLoc = glGetAttribLocation(s_program, "a_position");
    s_colorLoc = glGetAttribLocation(s_program, "a_color");
    s_texLoc = glGetAttribLocation(s_program, "a_texcoord");
    s_fogLoc = glGetAttribLocation(s_program, "a_fog");
    s_mvpLoc = glGetUniformLocation(s_program, "u_mvp");
    s_samplerLoc = glGetUniformLocation(s_program, "u_texture");
    s_useTextureLoc = glGetUniformLocation(s_program, "u_use_texture");
    s_useFogLoc = glGetUniformLocation(s_program, "u_use_fog");
    s_fogModeLoc = glGetUniformLocation(s_program, "u_fog_mode");
    s_fogDensityLoc = glGetUniformLocation(s_program, "u_fog_density");
    s_fogRangeLoc = glGetUniformLocation(s_program, "u_fog_range");
    s_fogColorLoc = glGetUniformLocation(s_program, "u_fog_color");
    s_pointSizeLoc = glGetUniformLocation(s_program, "u_point_size");
    identity(s_projection);
    identity(s_modelView);
    return 1;
}

void r3dgles_begin(GLenum mode) {
    s_mode = mode;
    s_vertexCount = 0;
    s_batchOverflow = false;
}

void r3dgles_vertex3f(GLfloat x, GLfloat y, GLfloat z) {
    GlesVertex *vertex;
    if (s_vertexCount >= GLES_BATCH_VERTICES) {
        if (!s_batchOverflow)
            SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                         "GLES batch exceeds %d vertices; discarding batch (mode %u)",
                         GLES_BATCH_VERTICES, (unsigned)s_mode);
        s_batchOverflow = true;
        return;
    }
    vertex = &s_vertices[s_vertexCount++];
    vertex->x = x; vertex->y = y; vertex->z = z;
    vertex->r = s_color[0]; vertex->g = s_color[1];
    vertex->b = s_color[2]; vertex->a = s_color[3];
    vertex->u = s_texCoord[0]; vertex->v = s_texCoord[1];
    vertex->fog = s_fogCoord;
}

void r3dgles_vertex2f(GLfloat x, GLfloat y) {
    r3dgles_vertex3f(x, y, 0);
}

void r3dgles_texCoord2f(GLfloat u, GLfloat v) {
    s_texCoord[0] = u;
    s_texCoord[1] = v;
}

void r3dgles_color3ub(GLubyte r, GLubyte g, GLubyte b) {
    s_color[0] = r / 255.0f;
    s_color[1] = g / 255.0f;
    s_color[2] = b / 255.0f;
    s_color[3] = 1;
}

void r3dgles_color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    s_color[0] = r; s_color[1] = g; s_color[2] = b; s_color[3] = a;
}

void r3dgles_fogCoordf(GLfloat distance) {
    s_fogCoord = distance;
}

static int expandTriangles(GlesVertex **vertices, GLenum *mode) {
    int i, count = 0;
    if (s_mode == GL_QUADS) {
        for (i = 0; i + 3 < s_vertexCount; i += 4) {
            s_triangles[count++] = s_vertices[i];
            s_triangles[count++] = s_vertices[i + 1];
            s_triangles[count++] = s_vertices[i + 2];
            s_triangles[count++] = s_vertices[i];
            s_triangles[count++] = s_vertices[i + 2];
            s_triangles[count++] = s_vertices[i + 3];
        }
        *vertices = s_triangles;
        *mode = GL_TRIANGLES;
        return count;
    }
    if (s_mode == GL_QUAD_STRIP) {
        for (i = 0; i + 3 < s_vertexCount; i += 2) {
            s_triangles[count++] = s_vertices[i];
            s_triangles[count++] = s_vertices[i + 1];
            s_triangles[count++] = s_vertices[i + 3];
            s_triangles[count++] = s_vertices[i];
            s_triangles[count++] = s_vertices[i + 3];
            s_triangles[count++] = s_vertices[i + 2];
        }
        *vertices = s_triangles;
        *mode = GL_TRIANGLES;
        return count;
    }
    if (s_mode == GL_POLYGON) *mode = GL_TRIANGLE_FAN;
    return s_vertexCount;
}

void r3dgles_end(void) {
    /* Never draw an incomplete primitive stream after exceeding capacity. */
    if (s_batchOverflow) return;
    GlesVertex *vertices = s_vertices;
    GLenum mode = s_mode;
    GLfloat mvp[16];
    int count = expandTriangles(&vertices, &mode);
    if (count <= 0) return;
    multiply(mvp, s_projection, s_modelView);
    glUseProgram(s_program);
    glUniformMatrix4fv(s_mvpLoc, 1, GL_FALSE, mvp);
    glUniform1i(s_samplerLoc, 0);
    glUniform1i(s_useTextureLoc, s_textureEnabled);
    glUniform1i(s_useFogLoc, s_fogEnabled);
    glUniform1i(s_fogModeLoc, s_fogMode == GL_EXP ? 1 : s_fogMode == GL_EXP2 ? 2 : 0);
    glUniform1f(s_fogDensityLoc, s_fogDensity);
    glUniform2f(s_fogRangeLoc, s_fogStart, s_fogEnd);
    glUniform4fv(s_fogColorLoc, 1, s_fogColor);
    glUniform1f(s_pointSizeLoc, s_pointSize);

    glEnableVertexAttribArray((GLuint)s_posLoc);
    glEnableVertexAttribArray((GLuint)s_colorLoc);
    glEnableVertexAttribArray((GLuint)s_texLoc);
    glEnableVertexAttribArray((GLuint)s_fogLoc);
    glVertexAttribPointer((GLuint)s_posLoc, 3, GL_FLOAT, GL_FALSE,
                          sizeof(GlesVertex), &vertices[0].x);
    glVertexAttribPointer((GLuint)s_colorLoc, 4, GL_FLOAT, GL_FALSE,
                          sizeof(GlesVertex), &vertices[0].r);
    glVertexAttribPointer((GLuint)s_texLoc, 2, GL_FLOAT, GL_FALSE,
                          sizeof(GlesVertex), &vertices[0].u);
    glVertexAttribPointer((GLuint)s_fogLoc, 1, GL_FLOAT, GL_FALSE,
                          sizeof(GlesVertex), &vertices[0].fog);
    glDrawArrays(mode, 0, count);
    glDisableVertexAttribArray((GLuint)s_posLoc);
    glDisableVertexAttribArray((GLuint)s_colorLoc);
    glDisableVertexAttribArray((GLuint)s_texLoc);
    glDisableVertexAttribArray((GLuint)s_fogLoc);
}

void r3dgles_matrixMode(GLenum mode) {
    s_matrix = mode == GL_PROJECTION ? s_projection : s_modelView;
}

void r3dgles_loadIdentity(void) {
    identity(s_matrix);
}

void r3dgles_loadMatrixf(const GLfloat *matrix) {
    SDL_memcpy(s_matrix, matrix, sizeof(GLfloat) * 16);
}

void r3dgles_ortho(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top,
                   GLfloat nearValue, GLfloat farValue) {
    GLfloat ortho[16];
    identity(ortho);
    ortho[0] = 2 / (right - left);
    ortho[5] = 2 / (top - bottom);
    ortho[10] = -2 / (farValue - nearValue);
    ortho[12] = -(right + left) / (right - left);
    ortho[13] = -(top + bottom) / (top - bottom);
    ortho[14] = -(farValue + nearValue) / (farValue - nearValue);
    multiply(s_matrix, s_matrix, ortho);
}

void r3dgles_enable(GLenum capability) {
    if (capability == GL_TEXTURE_2D) s_textureEnabled = 1;
    else if (capability == GL_FOG) s_fogEnabled = 1;
    else if (capability != GL_LIGHTING && capability != GL_MULTISAMPLE &&
             capability != GL_POINT_SMOOTH &&
             capability != GL_POLYGON_OFFSET_LINE &&
             capability != GL_POLYGON_OFFSET_POINT)
        glEnable(capability);
}

void r3dgles_disable(GLenum capability) {
    if (capability == GL_TEXTURE_2D) s_textureEnabled = 0;
    else if (capability == GL_FOG) s_fogEnabled = 0;
    else if (capability != GL_LIGHTING && capability != GL_MULTISAMPLE &&
             capability != GL_POINT_SMOOTH &&
             capability != GL_POLYGON_OFFSET_LINE &&
             capability != GL_POLYGON_OFFSET_POINT)
        glDisable(capability);
}

void r3dgles_fogi(GLenum name, GLint value) {
    if (name == GL_FOG_MODE) s_fogMode = value;
}

void r3dgles_fogf(GLenum name, GLfloat value) {
    if (name == GL_FOG_DENSITY) s_fogDensity = value;
    else if (name == GL_FOG_START) s_fogStart = value;
    else if (name == GL_FOG_END) s_fogEnd = value;
}

void r3dgles_fogfv(GLenum name, const GLfloat *value) {
    if (name == GL_FOG_COLOR) SDL_memcpy(s_fogColor, value, sizeof s_fogColor);
}

void r3dgles_shadeModel(GLenum mode) {
    (void)mode; /* Per-vertex colors are interpolated by the GLES2 shader. */
}

void r3dgles_pointSize(GLfloat size) {
    s_pointSize = size > 0 ? size : 1;
}
