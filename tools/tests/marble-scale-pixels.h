/* CPU oracle for an asymmetric, changing RGBA ramp. Test code only. */
#ifndef MARBLE_SCALE_PIXELS_H
#define MARBLE_SCALE_PIXELS_H
static unsigned char scale_channel(int x,int y,int channel,int frame)
{
    switch(channel) {
    case 0:return (unsigned char)(13+2*x+frame*19);
    case 1:return (unsigned char)(7+3*y+frame*11);
    case 2:return (unsigned char)(20+x+2*y+frame*23);
    default:return (unsigned char)(80+x+y+frame*7);
    }
}
static void scale_pattern(unsigned char *p,int w,int h,int stride,int bgra,int frame)
{
    memset(p,0xad,(size_t)stride*h);
    for(int y=0;y<h;y++)for(int x=0;x<w;x++)for(int c=0;c<4;c++)
        p[y*stride+x*4+(bgra && c<3?2-c:c)]=scale_channel(x,y,c,frame);
}
static unsigned scale_verify(const unsigned char *p,int sw,int sh,int dw,int dh,int stride,int bgra,int frame)
{
    unsigned bad=0;
    for(int y=0;y<dh;y++)for(int x=0;x<dw;x++) {
        double sx=((x+.5)*sw/dw)-.5,sy=((y+.5)*sh/dh)-.5;
        if(sx<0)sx=0;if(sy<0)sy=0;if(sx>sw-1)sx=sw-1;if(sy>sh-1)sy=sh-1;
        int x0=(int)sx,y0=(int)sy,x1=x0+1<sw?x0+1:x0,y1=y0+1<sh?y0+1:y0;
        double fx=sx-x0,fy=sy-y0;
        for(int c=0;c<4;c++) {
            double a=scale_channel(x0,y0,c,frame)*(1-fx)+scale_channel(x1,y0,c,frame)*fx;
            double b=scale_channel(x0,y1,c,frame)*(1-fx)+scale_channel(x1,y1,c,frame)*fx;
            int want=(int)(a*(1-fy)+b*fy+.5);
            if(abs((int)p[y*stride+x*4+(bgra && c<3?2-c:c)]-want)>2)bad++;
        }
    }
    return bad;
}
#endif
