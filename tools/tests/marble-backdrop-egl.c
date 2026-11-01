/* Exercise the actual EAGL helper and rewritten shaders on host Mesa, without
 * heating the phone. This is not an Adreno or optical-display qualification. */
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <time.h>
#define say printf
static int backdrop_copy=1;
static int backdrop_grow;
static int output_timing=1;
static uint64_t now_us(void) {
    struct timespec t;assert(clock_gettime(CLOCK_MONOTONIC,&t)==0);
    return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}
struct eagl {
    GLuint backdrop;
    GLint backdrop_binding;
    GLsizei backdrop_width,backdrop_height;
    bool backdrop_bound;
    uint64_t backdrop_stats[4],output_backdrop_stats[4];
    unsigned unit,errors;
};
#include "marble-backdrop-production.h"

static const GLfloat identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
static const GLfloat colours[20]={.6,0,0,0, 0,.7,0,0, 0,0,.8,0, 0,0,0,.9, .05,.03,.02,.1};
static const GLfloat flat[4]={.2,.4,.6,.8};
static unsigned checked;
/* Compile the actual generic CA translation preamble/end, with a representative
 * legacy fetch body. The host checks its pixels through the production copy helper. */
static GLuint generic_fetch_program(void)
{
    const char *body =
        "uniform lowp sampler2D texture0;\n"
        "uniform mediump vec4 colours[5];\n"
        "void main() {\n"
        "  highp vec4 behind=gl_LastFragData[0], s=behind;\n"
        "  s.rgb/=max(s.a,.005);\n"
        "  s=s.r*colours[0]+s.g*colours[1]+s.b*colours[2]+s.a*colours[3]+colours[4];\n"
        "  s.a=clamp(s.a,0.,1.); s.rgb=clamp(s.rgb*s.a,0.,1.);\n"
        "  gl_FragColor=mix(behind,s,texture2D(texture0,vec2(.5)).a);\n"
        "}\n";
    GLuint program=glCreateProgram(), vertex=glCreateShader(GL_VERTEX_SHADER), fragment=glCreateShader(GL_FRAGMENT_SHADER);
    const char *parts[]={"#version 300 es\n",fragment_start,fragment_fetch_copy,body,fragment_fetch_copy_end};
    const char *vertex_source=plain_vertex;
    glShaderSource(vertex,1,&vertex_source,NULL);glCompileShader(vertex);
    glShaderSource(fragment,5,parts,NULL);glCompileShader(fragment);
    GLint good=0;glGetShaderiv(fragment,GL_COMPILE_STATUS,&good);
    if(!good) { char log[2048];glGetShaderInfoLog(fragment,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log); }
    assert(good);glAttachShader(program,vertex);glAttachShader(program,fragment);
    glBindAttribLocation(program,0,"vertex_position");glBindAttribLocation(program,1,"vertex_color");
    glBindAttribLocation(program,2,"vertex_texcoord0");glLinkProgram(program);
    glDeleteShader(vertex);glDeleteShader(fragment);return program;
}
static GLuint texture(unsigned w,unsigned h,const void *pixels)
{
    GLuint t;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);return t;
}
static float clamp(float f){return f<0?0:f>1?1:f;}
static void run_case(struct eagl *e,GLuint program,unsigned which,unsigned w,unsigned h,
                     bool scissored,const GLint box[4])
{
    unsigned char initial[16*16*4],actual[sizeof(initial)];
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
        unsigned char *p=initial+4*(y*w+x);
        p[3]=64+(x*17+y*11)%192;p[0]=(x*13+y*3)%(p[3]+1);
        p[1]=(x*5+y*19)%(p[3]+1);p[2]=(x*7+y*23)%(p[3]+1);
    }
    GLuint draw,read,target,mask,sentinel;
    glActiveTexture(GL_TEXTURE0);target=texture(w,h,initial);
    const unsigned char one[4]={255,255,255,128};mask=texture(1,1,one);
    glGenFramebuffers(1,&draw);glBindFramebuffer(GL_FRAMEBUFFER,draw);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target,0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glReadBuffer(GL_NONE); /* helper must restore the source FBO's own read selection */
    glGenFramebuffers(1,&read);glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
    glActiveTexture(GL_TEXTURE7);sentinel=texture(1,1,one);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,mask);
    glActiveTexture(GL_TEXTURE0+e->unit);
    glViewport(0,0,w,h);glDisable(GL_BLEND);
    (scissored?glEnable:glDisable)(GL_SCISSOR_TEST);
    glScissor(box[0],box[1],box[2],box[3]);
    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program,"vertex_matrix"),1,GL_FALSE,identity);
    glUniform4fv(glGetUniformLocation(program,"colours"),5,colours);
    glUniform4fv(glGetUniformLocation(program,"flat_colour"),1,flat);
    glUniform4f(glGetUniformLocation(program,"texmat"),1,1,0,0);
    const GLfloat corners[8]={-1,-1,1,-1,-1,1,1,1};
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,0,0,corners);
    glDisableVertexAttribArray(1);glVertexAttrib4f(1,1,1,1,1);
    glDisableVertexAttribArray(2);glVertexAttrib2f(2,.5,.5);
    /* A tiny PBO makes allocation with a bound unpack buffer fail, rather than
     * silently treating NULL as offset zero into application-owned memory. */
    GLuint unpack;glGenBuffers(1,&unpack);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,1,NULL,GL_STATIC_DRAW);
    bool visible=!scissored || (box[0]<(GLint)w && box[1]<(GLint)h &&
        box[0]+box[2]>0 && box[1]+box[3]>0 && box[2]>0 && box[3]>0);
    bool copied=backdrop_begin(e,target,program);assert(copied==visible);
    if(copied) {
        GLint capacity_w,capacity_h;
        glActiveTexture(GL_TEXTURE7);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&capacity_w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&capacity_h);
        assert(capacity_w==e->backdrop_width && capacity_h==e->backdrop_height);
        assert(backdrop_grow ? (capacity_w>=(GLint)w && capacity_h>=(GLint)h) :
                              (capacity_w==(GLint)w && capacity_h==(GLint)h));
        glActiveTexture(GL_TEXTURE0+e->unit);
    }
    GLint got;
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&got);assert((GLuint)got==unpack);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&got);assert((GLuint)got==read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&got);assert((GLuint)got==draw);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&got);assert(got==GL_TEXTURE0+(GLint)e->unit);
    if(copied)glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    backdrop_end(e);
    glActiveTexture(GL_TEXTURE7);glGetIntegerv(GL_TEXTURE_BINDING_2D,&got);assert((GLuint)got==sentinel);
    glActiveTexture(GL_TEXTURE0+e->unit);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,draw);glGetIntegerv(GL_READ_BUFFER,&got);assert(got==GL_NONE);
    glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,actual);
    assert(glGetError()==GL_NO_ERROR && e->errors==0 && !e->backdrop_bound);
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
        bool inside=!scissored || ((GLint)x>=box[0] && (GLint)y>=box[1] &&
            (GLint)x<box[0]+box[2] && (GLint)y<box[1]+box[3]);
        const unsigned char *p=initial+4*(y*w+x),*gotp=actual+4*(y*w+x);
        float a=p[3]/255.f;
        for(unsigned c=0;c<4;c++) {
            float want=p[c]/255.f;
            if(inside && which==2)want=flat[c]*a;
            else if(inside) {
                float s=c==3?clamp(a*.9f+.1f):clamp((p[c]/255.f/fmaxf(a,.005f)*colours[5*c]+colours[16+c])*clamp(a*.9f+.1f));
                want=want*(1-128/255.f)+s*(128/255.f);
            }
            if(abs((int)gotp[c]-(int)lroundf(want*255))>2) {
                fprintf(stderr,"pixel mismatch shader=%u size=%ux%u scissor=%d at %u,%u channel=%u got=%u want=%.3f\n",which,w,h,scissored,x,y,c,gotp[c],want*255);exit(1);
            }
            checked++;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&draw);glDeleteFramebuffers(1,&read);
    GLuint textures[]={target,mask,sentinel};glDeleteTextures(3,textures);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glDeleteBuffers(1,&unpack);
}
int main(void)
{
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);EGLint major,minor;
    assert(eglInitialize(display,&major,&minor));assert(eglBindAPI(EGL_OPENGL_ES_API));
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count;assert(eglChooseConfig(display,attrs,&config,1,&count)&&count==1);
    EGLint pbuffer[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE},ctxattrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,pbuffer);
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctxattrs);
    assert(eglMakeCurrent(display,surface,surface,context));
    printf("host renderer: %s\n",glGetString(GL_RENDERER));
    struct eagl e={.unit=3};
    const char *shaders[]={vibrant_fragment,group_over_fragment,group_flat_fragment};
    const GLint boxes[][4]={{0,0,8,8},{2,3,3,2},{-2,-3,5,6},{6,5,9,10},{9,9,2,2},{0,0,0,8}};
    unsigned cases=0;
    for(backdrop_grow=0;backdrop_grow<2;backdrop_grow++) {
      for(unsigned shader=0;shader<4;shader++) {
        GLuint program=shader==3?generic_fetch_program():plain_program(shaders[shader],"host production shader test");GLint good=0;
        glGetProgramiv(program,GL_LINK_STATUS,&good);assert(good);
        assert(glGetUniformLocation(program,"marble_behind")>=0);
        run_case(&e,program,shader,8,8,false,boxes[0]);cases++;
        for(unsigned pass=0;pass<2;pass++)for(unsigned b=0;b<6;b++) {
            run_case(&e,program,shader,8,8,true,boxes[b]);cases++;
        }
        run_case(&e,program,shader,11,7,true,boxes[1]);cases++;
        run_case(&e,program,shader,7,13,true,boxes[1]);cases++;
        run_case(&e,program,shader,8,8,false,boxes[0]);cases++;
        glDeleteProgram(program);
      }
      glDeleteTextures(1,&e.backdrop);memset(&e,0,sizeof(e));e.unit=3;
    }
    printf("marble-backdrop-host: PASS cases=%u channels=%u GL_errors=%u (host Mesa only)\n",cases,checked,e.errors);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);return 0;
}
