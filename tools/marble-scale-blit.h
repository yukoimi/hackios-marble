/* Shared production blit, exercised with GLES3 on host and IOSurface on device.
 * Each caller owns a private context and two FBOs. No display MMIO is involved. */
#ifndef MARBLE_SCALE_BLIT_H
#define MARBLE_SCALE_BLIT_H
static unsigned marble_scale_blit(unsigned read_fbo,unsigned draw_fbo,
    unsigned source,unsigned output,int sw,int sh,int dw,int dh)
{
    if(!source || !output || sw<=0 || sh<=0 || dw<=0 || dh<=0)return 1;
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,read_fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,source,0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
    if(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE ||
       glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return 2;
    /* Both IOSurfaces use the same row origin. A second Y inversion would
     * turn the displayed frame upside down. Colour and alpha remain intact. */
    glBlitFramebuffer(0,0,sw,sh,0,0,dw,dh,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    glFinish();
    return glGetError();
}
#endif
