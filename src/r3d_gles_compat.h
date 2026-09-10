#ifndef R3D_GLES_COMPAT_H
#define R3D_GLES_COMPAT_H

#include <SDL3/SDL_opengles2.h>

/* Constants removed from GLES2 but still used as renderer intent. */
#ifndef GL_QUADS
#define GL_QUADS 0x0007
#endif
#ifndef GL_POLYGON
#define GL_POLYGON 0x0009
#endif
#ifndef GL_QUAD_STRIP
#define GL_QUAD_STRIP 0x0008
#endif
#ifndef GL_MODELVIEW
#define GL_MODELVIEW 0x1700
#endif
#ifndef GL_PROJECTION
#define GL_PROJECTION 0x1701
#endif
#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D 0x0DE1
#endif
#ifndef GL_LIGHTING
#define GL_LIGHTING 0x0B50
#endif
#ifndef GL_FOG
#define GL_FOG 0x0B60
#endif
#ifndef GL_FOG_MODE
#define GL_FOG_MODE 0x0B65
#endif
#ifndef GL_FOG_DENSITY
#define GL_FOG_DENSITY 0x0B62
#endif
#ifndef GL_FOG_START
#define GL_FOG_START 0x0B63
#endif
#ifndef GL_FOG_END
#define GL_FOG_END 0x0B64
#endif
#ifndef GL_FOG_COLOR
#define GL_FOG_COLOR 0x0B66
#endif
#ifndef GL_EXP
#define GL_EXP 0x0800
#endif
#ifndef GL_EXP2
#define GL_EXP2 0x0801
#endif
#ifndef GL_FLAT
#define GL_FLAT 0x1D00
#endif
#ifndef GL_SMOOTH
#define GL_SMOOTH 0x1D01
#endif
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif
#ifndef GL_POINT_SMOOTH
#define GL_POINT_SMOOTH 0x0B10
#endif
#ifndef GL_POLYGON_OFFSET_POINT
#define GL_POLYGON_OFFSET_POINT 0x2A01
#endif
#ifndef GL_POLYGON_OFFSET_LINE
#define GL_POLYGON_OFFSET_LINE 0x2A02
#endif

int r3dgles_compatInit(void);
void r3dgles_fogCoordf(GLfloat distance);
void r3dgles_begin(GLenum mode);
void r3dgles_end(void);
void r3dgles_vertex2f(GLfloat x, GLfloat y);
void r3dgles_vertex3f(GLfloat x, GLfloat y, GLfloat z);
void r3dgles_texCoord2f(GLfloat u, GLfloat v);
void r3dgles_color3ub(GLubyte r, GLubyte g, GLubyte b);
void r3dgles_color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void r3dgles_matrixMode(GLenum mode);
void r3dgles_loadIdentity(void);
void r3dgles_loadMatrixf(const GLfloat *matrix);
void r3dgles_ortho(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top,
                   GLfloat nearValue, GLfloat farValue);
void r3dgles_enable(GLenum capability);
void r3dgles_disable(GLenum capability);
void r3dgles_fogi(GLenum name, GLint value);
void r3dgles_fogf(GLenum name, GLfloat value);
void r3dgles_fogfv(GLenum name, const GLfloat *value);
void r3dgles_shadeModel(GLenum mode);
void r3dgles_pointSize(GLfloat size);

#define glBegin r3dgles_begin
#define glEnd r3dgles_end
#define glVertex2f r3dgles_vertex2f
#define glVertex3f r3dgles_vertex3f
#define glTexCoord2f r3dgles_texCoord2f
#define glColor3ub r3dgles_color3ub
#define glColor4f r3dgles_color4f
#define glMatrixMode r3dgles_matrixMode
#define glLoadIdentity r3dgles_loadIdentity
#define glLoadMatrixf r3dgles_loadMatrixf
#define glOrtho r3dgles_ortho
#define glEnable r3dgles_enable
#define glDisable r3dgles_disable
#define glFogi r3dgles_fogi
#define glFogf r3dgles_fogf
#define glFogfv r3dgles_fogfv
#define glShadeModel r3dgles_shadeModel
#define glPointSize r3dgles_pointSize

#endif
