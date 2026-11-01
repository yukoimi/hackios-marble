#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../marble-scale-blit.h"
#include "marble-scale-pixels.h"
int main(void)
{
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(display!=EGL_NO_DISPLAY);
    assert(eglInitialize(display,NULL,NULL));assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint config_attributes[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_NONE};
    EGLConfig config;EGLint count;assert(eglChooseConfig(display,config_attributes,&config,1,&count) && count==1);
    const EGLint context_attributes[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attributes);assert(context!=EGL_NO_CONTEXT);
    assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context));
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    GLuint read_fbo,draw_fbo;glGenFramebuffers(1,&read_fbo);glGenFramebuffers(1,&draw_fbo);
    const int sizes[][4]={{17,9,31,15},{63,31,19,7},{1,1,7,3},{19,7,63,31},{32,16,32,16}};
    unsigned cases=0,channels=0,bad=0;
    for(unsigned test=0;test<sizeof(sizes)/sizeof(sizes[0]);test++) {
        int sw=sizes[test][0],sh=sizes[test][1],dw=sizes[test][2],dh=sizes[test][3];
        int pitch=(sw*4+63)&~63,opitch=(dw*4+63)&~63;
        unsigned char *p=malloc((size_t)pitch*sh),*q=malloc((size_t)opitch*dh);assert(p && q);
        GLuint textures[2];glGenTextures(2,textures);
        for(unsigned i=0;i<2;i++) {
            glBindTexture(GL_TEXTURE_2D,textures[i]);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,i?dw:sw,i?dh:sh,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        }
        for(int frame=0;frame<3;frame++) {
            scale_pattern(p,sw,sh,pitch,0,frame);
            glBindTexture(GL_TEXTURE_2D,textures[0]);glPixelStorei(GL_UNPACK_ROW_LENGTH,pitch/4);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,sw,sh,GL_RGBA,GL_UNSIGNED_BYTE,p);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
            /* The production helper must disable an inherited scissor. */
            glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);
            unsigned error=marble_scale_blit(read_fbo,draw_fbo,textures[0],textures[1],sw,sh,dw,dh);assert(!error);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,draw_fbo);glPixelStorei(GL_PACK_ROW_LENGTH,opitch/4);
            memset(q,0xad,(size_t)opitch*dh);glReadPixels(0,0,dw,dh,GL_RGBA,GL_UNSIGNED_BYTE,q);glPixelStorei(GL_PACK_ROW_LENGTH,0);
            assert(!glGetError());unsigned wrong=scale_verify(q,sw,sh,dw,dh,opitch,0,frame);
            printf("case=%u frame=%d wrong=%u\n",test,frame,wrong);bad+=wrong;cases++;channels+=dw*dh*4;
            /* Check test readback respects row padding. */
            for(int y=0;y<dh;y++)for(int x=dw*4;x<opitch;x++)assert(q[y*opitch+x]==0xad);
        }
        glDeleteTextures(2,textures);free(p);free(q);
    }
    glDeleteFramebuffers(1,&read_fbo);glDeleteFramebuffers(1,&draw_fbo);
    assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
    eglDestroyContext(display,context);eglTerminate(display);
    printf("%s cases=%u channels=%u bad=%u\n",bad?"FAIL":"PASS",cases,channels,bad);
    return bad!=0;
}
