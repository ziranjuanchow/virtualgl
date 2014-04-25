/* Copyright (C)2004 Landmark Graphics Corporation
 * Copyright (C)2005 Sun Microsystems, Inc.
 * Copyright (C)2010-2011, 2013-2014 D. R. Commander
 *
 * This library is free software and may be redistributed and/or modified under
 * the terms of the wxWindows Library License, Version 3.1 or (at your option)
 * any later version.  The full license is in the LICENSE.txt file included
 * with this distribution.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * wxWindows Library License for more details.
 */

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "vglutil.h"
#include "Timer.h"
#include "Error.h"
#include <errno.h>
#define GL_GLEXT_PROTOTYPES
#include "../common/glx.h"
#include <GL/glu.h>
#include "x11err.h"

using namespace vglutil;


static int ALIGN=1;
#define PAD(w) (((w)+(ALIGN-1))&(~(ALIGN-1)))
#define BMPPAD(pitch) ((pitch+(sizeof(int)-1))&(~(sizeof(int)-1)))


typedef struct _PixelFormat
{
	unsigned long roffset, goffset, boffset;
	int pixelSize;
	int glFormat;
	int bgr;
	const char *name;
} PixelFormat;

static int FORMATS=3
	#ifdef GL_BGRA_EXT
	+1
	#endif
	#ifdef GL_BGR_EXT
	+1
	#endif
	#ifdef GL_ABGR_EXT
	+1
	#endif
	;

PixelFormat pf[4
	#ifdef GL_BGRA_EXT
	+1
	#endif
	#ifdef GL_BGR_EXT
	+1
	#endif
	#ifdef GL_ABGR_EXT
	+1
	#endif
	]={
		{0, 0, 0, 1, GL_RED, 0, "RED"},
		#ifdef GL_BGRA_EXT
		{2, 1, 0, 4, GL_BGRA_EXT, 1, "BGRA"},
		#endif
		#ifdef GL_ABGR_EXT
		{3, 2, 1, 4, GL_ABGR_EXT, 0, "ABGR"},
		#endif
		#ifdef GL_BGR_EXT
		{2, 1, 0, 3, GL_BGR_EXT, 1, "BGR"},
		#endif
		{0, 1, 2, 4, GL_RGBA, 0, "RGBA"},
		{0, 1, 2, 3, GL_RGB, 0, "RGB"},
	};


#define BENCH_NAME       "GLreadtest"

#define WIDTH            701
#define HEIGHT           701

int width=WIDTH, height=HEIGHT;
Display *dpy=NULL;  Window win=0;  Pixmap pm=0;  GLXPixmap glxpm=0;
XVisualInfo *v=NULL;  GLXFBConfig c=0;
GLXContext ctx=0;
#ifndef GLX11
GLXPbuffer pbuffer=0;
#endif

Timer timer;
int useWindow=0, usePixmap=0, useFBO=0, visualID=0, loops=1, useAlpha=0;
#ifdef GL_EXT_framebuffer_object
GLuint fb=0, rb=0;
#endif
#ifdef GL_VERSION_1_5
int pbo=0;
#endif
double benchTime=1.0;

#define STRLEN 256


char *sigFig(int fig, char *string, double value)
{
	char format[80];
	double _l=(value==0.0)? 0.0:log10(fabs(value));  int l;
	if(_l<0.)
	{
		l=(int)fabs(floor(_l));
		snprintf(format, 79, "%%%d.%df", fig+l+1, fig+l-1);
	}
	else
	{
		l=(int)_l+1;
		if(fig<=l) snprintf(format, 79, "%%.0f");
		else snprintf(format, 79, "%%%d.%df", fig+1, fig-l);
	}
	snprintf(string, STRLEN-1, format, value);
	return(string);
}


extern "C" {
int xhandler(Display *dpy, XErrorEvent *xe)
{
	fprintf(stderr, "X11 Error: %s\n", x11error(xe->error_code));
	return 0;
}
} // extern "C"


void findVisual(void)
{
	int winattribs[]={GLX_RGBA, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
		GLX_BLUE_SIZE, 8, None, None, None};
	int winattribsdb[]={GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8,
		GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None, None, None};

	#ifndef GLX11
	int pbattribs[]={GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
		GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT, None,
		None, None};
	int pbattribsdb[]={GLX_DOUBLEBUFFER, 1, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
		GLX_BLUE_SIZE, 8, GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_DRAWABLE_TYPE,
		GLX_PBUFFER_BIT, None, None, None};
	GLXFBConfig *fbconfigs=NULL;  int nelements=0;
	#endif

	// Use GLX 1.1 functions here in case we're remotely displaying to
	// something that doesn't support GLX 1.3
	if(useWindow || usePixmap || useFBO)
	{
		try
		{
			if(visualID)
			{
				XVisualInfo vtemp;  int n=0;
				vtemp.visualid=visualID;
				v=XGetVisualInfo(dpy, VisualIDMask, &vtemp, &n);
				if(!v || !n) _throw("Could not obtain visual");
				printf("Visual = 0x%.2x\n", (unsigned int)v->visualid);
				return;
			}
			if(useAlpha)
			{
				winattribs[7]=GLX_ALPHA_SIZE;  winattribs[8]=8;
				winattribsdb[8]=GLX_ALPHA_SIZE;  winattribsdb[9]=8;
			}
			if(!(v=glXChooseVisual(dpy, DefaultScreen(dpy), winattribs))
				&& !(v=glXChooseVisual(dpy, DefaultScreen(dpy), winattribsdb)))
				_throw("Could not obtain Visual");
			printf("Visual = 0x%.2x\n", (unsigned int)v->visualid);
			return;
		}
		catch(...)
		{
			if(v) { XFree(v);  v=NULL; }  throw;
		}
	}

	#ifndef GLX11
	if(useAlpha)
	{
		pbattribs[10]=GLX_ALPHA_SIZE;  pbattribs[11]=8;
		pbattribsdb[12]=GLX_ALPHA_SIZE;  pbattribsdb[13]=8;
	}
	fbconfigs=glXChooseFBConfig(dpy, DefaultScreen(dpy), pbattribs, &nelements);
	if(!fbconfigs) fbconfigs=glXChooseFBConfig(dpy, DefaultScreen(dpy),
		pbattribsdb, &nelements);
	if(!nelements || !fbconfigs)
	{
		if(fbconfigs) XFree(fbconfigs);
		_throw("Could not obtain Visual");
	}
	c=fbconfigs[0];  XFree(fbconfigs);

	int fbcid=-1;
	glXGetFBConfigAttrib(dpy, c, GLX_FBCONFIG_ID, &fbcid);
	printf("FB Config = 0x%.2x\n", fbcid);
	#endif
}


void drawableInit(void)
{
	#ifndef	GLX11
	int pbattribs[]={GLX_PBUFFER_WIDTH, 0, GLX_PBUFFER_HEIGHT, 0, None};
	#endif

	// Use GLX 1.1 functions here in case we're remotely displaying to
	// something that doesn't support GLX 1.3
	if(useWindow || usePixmap || useFBO)
	{
		if(!(ctx=glXCreateContext(dpy, v, NULL, True)))
			_throw("Could not create GL context");

		if(usePixmap)
		{
			if(!(glxpm=glXCreateGLXPixmap(dpy, v, pm)))
				_throw("Could not creaate GLX pixmap");
		}
		glXMakeCurrent(dpy, usePixmap? glxpm:win, ctx);

		#ifdef GL_EXT_framebuffer_object
		if(useFBO)
		{
			glGenFramebuffersEXT(1, &fb);
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
			glGenRenderbuffersEXT(1, &rb);
			glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb);
			glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, useAlpha? GL_RGBA8:GL_RGB8,
				width, height);
			glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,
				GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
			if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)
				!=GL_FRAMEBUFFER_COMPLETE_EXT)
				_throw("Could not create FBO");
		}
		#endif
		return;
	}

	#ifndef GLX11
	ctx=glXCreateNewContext(dpy, c, GLX_RGBA_TYPE, NULL, True);
	if(!ctx) _throw("Could not create GL context");

	pbattribs[1]=width;  pbattribs[3]=height;
	pbuffer=glXCreatePbuffer(dpy, c, pbattribs);
	if(!pbuffer) _throw("Could not create Pbuffer");

	glXMakeContextCurrent(dpy, pbuffer, pbuffer, ctx);
	#endif
}


char glerrstr[STRLEN]="No error";

static void check_errors(const char *tag)
{
	int i, error=0;  char *s;
	i=glGetError();
	if(i!=GL_NO_ERROR) error=1;
	while(i!=GL_NO_ERROR)
	{
		s=(char *)gluErrorString(i);
		if(s) snprintf(glerrstr, STRLEN-1, "OpenGL ERROR in %s: %s\n", tag, s);
		else snprintf(glerrstr, STRLEN-1, "OpenGL ERROR #%d in %s\n", i, tag);
		i=glGetError();
	}
	if(error) _throw(glerrstr);
}


void initBuf(int x, int y, int width, int height, int format,
	unsigned char *buf)
{
	int i, j, ps=pf[format].pixelSize;
	for(i=0; i<height; i++)
	{
		for(j=0; j<width; j++)
		{
			if(pf[format].glFormat==GL_RED)
				buf[(i*width+j)*ps]=((i+y)*(j+x))%256;
			else
			{
				buf[(i*width+j)*ps+pf[format].roffset]=((i+y)*(j+x))%256;
				buf[(i*width+j)*ps+pf[format].goffset]=((i+y)*(j+x)*2)%256;
				buf[(i*width+j)*ps+pf[format].boffset]=((i+y)*(j+x)*3)%256;
			}
		}
	}
}


int cmpBuf(int x, int y, int width, int height, int format, unsigned char *buf,
	int bassackwards)
{
	int i, j, l, ps=pf[format].pixelSize;
	for(i=0; i<height; i++)
	{
		l=bassackwards? height-i-1:i;
		for(j=0; j<width; j++)
		{
			if(pf[format].glFormat==GL_RED)
			{
				if(buf[l*PAD(width*ps)+j*ps]!=((i+y)*(j+x))%256) return 0;
			}
			else
			{
				if(buf[l*PAD(width*ps)+j*ps+pf[format].roffset]!=((i+y)*(j+x))%256)
					return 0;
				if(buf[l*PAD(width*ps)+j*ps+pf[format].goffset]!=((i+y)*(j+x)*2)%256)
					return 0;
				if(buf[l*PAD(width*ps)+j*ps+pf[format].boffset]!=((i+y)*(j+x)*3)%256)
					return 0;
			}
		}
	}
	return 1;
}


// Makes sure the frame buffer has been cleared prior to a write
void clearfb(int format)
{
	unsigned char *buf=NULL;  int ps=3, glFormat=GL_RGB;

	if((buf=(unsigned char *)malloc(width*height*ps))==NULL)
		_throw("Could not allocate buffer");
	memset(buf, 0xFF, width*height*ps);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	#ifdef GL_EXT_framebuffer_object
	if(useFBO)
	{
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
		glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
	}
	else
	#endif
	{
		glDrawBuffer(GL_FRONT);
		glReadBuffer(GL_FRONT);
	}
	glClearColor(0., 0., 0., 0.);
	glClear(GL_COLOR_BUFFER_BIT);
	glReadPixels(0, 0, width, height, glFormat, GL_UNSIGNED_BYTE, buf);
	check_errors("frame buffer read");
	for(int i=0; i<width*height*ps; i++)
	{
		if(buf[i]!=0)
		{
			fprintf(stderr, "Buffer was not cleared\n");  break;
		}
	}
	if(buf) free(buf);
}


// Generic GL write test
void writeTest(int format)
{
	unsigned char *rgbaBuffer=NULL;  int n, ps=pf[format].pixelSize;
	double rbtime;
	char temps[STRLEN];

	try
	{
		fprintf(stderr, "glDrawPixels():   ");
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glShadeModel(GL_FLAT);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_LIGHTING);
		clearfb(format);
		if((rgbaBuffer=(unsigned char *)malloc(width*height*ps))==NULL)
			_throw("Could not allocate buffer");
		initBuf(0, 0, width, height, format, rgbaBuffer);
		n=0;
		timer.start();
		do
		{
			glDrawPixels(width, height, pf[format].glFormat, GL_UNSIGNED_BYTE,
				rgbaBuffer);
			glFinish();
			n++;
		} while((rbtime=timer.elapsed())<benchTime || n<2);

		double avgmps=(double)n*(double)(width*height)/((double)1000000.*rbtime);
		check_errors("frame buffer write");
		fprintf(stderr, "%s Mpixels/sec\n", sigFig(4, temps, avgmps));

	} catch(Error &e) { fprintf(stderr, "%s\n", e.getMessage()); }

	if(rgbaBuffer) free(rgbaBuffer);
}


// Generic OpenGL readback test
void readTest(int format)
{
	unsigned char *rgbaBuffer=NULL;  int n, ps=pf[format].pixelSize;
	double rbtime, readpixelstime;  Timer timer2;
	#ifdef GL_VERSION_1_5
	GLuint bufferid=0;
	#endif
	char temps[STRLEN];

	try
	{
		fprintf(stderr, "glReadPixels():   ");

		glPixelStorei(GL_UNPACK_ALIGNMENT, ALIGN);
		glPixelStorei(GL_PACK_ALIGNMENT, ALIGN);

		#ifdef GL_EXT_framebuffer_object
		if(useFBO) glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
		else
		#endif
		glReadBuffer(GL_FRONT);

		#ifdef GL_VERSION_1_5
		if(pbo)
		{
			const char *ext=(const char *)glGetString(GL_EXTENSIONS);
			if(!ext || !strstr(ext, "GL_ARB_pixel_buffer_object"))
				_throw("GL_ARB_pixel_buffer_object extension not available");
			glGenBuffers(1, &bufferid);
			if(!bufferid) _throw("Could not generate PBO buffer");
			glBindBuffer(GL_PIXEL_PACK_BUFFER_EXT, bufferid);
			glBufferData(GL_PIXEL_PACK_BUFFER_EXT, PAD(width*ps)*height, NULL,
				GL_STREAM_READ);
			check_errors("PBO initialization");
			int temp=0;
			glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER_EXT, GL_BUFFER_SIZE,
				&temp);
			if(temp!=PAD(width*ps)*height) _throw("Could not generate PBO buffer");
			temp=0;
			glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING_EXT, &temp);
			if(temp!=pbo) _throw("Could not bind PBO buffer");
		}
		#endif

		if((rgbaBuffer=(unsigned char *)malloc(PAD(width*ps)*height))==NULL)
			_throw("Could not allocate buffer");
		memset(rgbaBuffer, 0, PAD(width*ps)*height);
		n=0;  rbtime=readpixelstime=0.;
		double tmin=0., tmax=0., ssq=0., sum=0.;  int first=1;
		do
		{
			timer.start();
			if(pf[format].glFormat==GL_LUMINANCE)
			{
				glPushAttrib(GL_PIXEL_MODE_BIT);
				glPixelTransferf(GL_RED_SCALE, (GLfloat)0.299);
				glPixelTransferf(GL_GREEN_SCALE, (GLfloat)0.587);
				glPixelTransferf(GL_BLUE_SCALE, (GLfloat)0.114);
			}
			#ifdef GL_VERSION_1_5
			if(pbo)
			{
				unsigned char *pixels=NULL;
				glBindBuffer(GL_PIXEL_PACK_BUFFER_EXT, bufferid);
				timer2.start();
				glReadPixels(0, 0, width, height, pf[format].glFormat, GL_UNSIGNED_BYTE,
					NULL);
				readpixelstime+=timer2.elapsed();
				pixels=(unsigned char *)glMapBuffer(GL_PIXEL_PACK_BUFFER_EXT,
					GL_READ_ONLY);
				if(!pixels) _throw("Could not map buffer");
				memcpy(rgbaBuffer, pixels, PAD(width*ps)*height);
				glUnmapBuffer(GL_PIXEL_PACK_BUFFER_EXT);
			}
			else
			#endif
			{
				timer2.start();
				glReadPixels(0, 0, width, height, pf[format].glFormat, GL_UNSIGNED_BYTE,
					rgbaBuffer);
				readpixelstime+=timer2.elapsed();
			}

			if(pf[format].glFormat==GL_LUMINANCE)
			{
				glPopAttrib();
			}
			double elapsed=timer.elapsed();
			if(first)
			{
				tmin=tmax=elapsed;  first=0;
			}
			else
			{
				if(elapsed<tmin) tmin=elapsed;
				if(elapsed>tmax) tmax=elapsed;
			}
			n++;
			rbtime+=elapsed;
			ssq+=pow((double)(width*height)/((double)1000000.*elapsed), 2.0);
			sum+=(double)(width*height)/((double)1000000.*elapsed);
		} while(rbtime<benchTime || n<2);

		if(!cmpBuf(0, 0, width, height, format, rgbaBuffer, 0))
			_throw("ERROR: Bogus data read back.");

		double mean=sum/(double)n;
		double stddev=sqrt((ssq - 2.0*mean*sum + mean*mean*(double)n)/(double)n);
		double avgmps=(double)n*(double)(width*height)/((double)1000000.*rbtime);
		double minmps=(double)(width*height)/((double)1000000.*tmax);
		double maxmps=(double)(width*height)/((double)1000000.*tmin);
		check_errors("frame buffer read");

		fprintf(stderr, "%s Mpixels/sec ", sigFig(4, temps, avgmps));
		fprintf(stderr, "(min = %s, ", sigFig(4, temps, minmps));
		fprintf(stderr, "max = %s, ", sigFig(4, temps, maxmps));
		fprintf(stderr, "sdev = %s)\n", sigFig(4, temps, stddev));
		fprintf(stderr, "glReadPixels() accounted for %s%% of total readback time\n",
			sigFig(4, temps, readpixelstime/rbtime*100.0));

	} catch(Error &e) { fprintf(stderr, "%s\n", e.getMessage()); }

	if(rgbaBuffer) free(rgbaBuffer);
	#ifdef GL_VERSION_1_5
	if(pbo && bufferid>0)
	{
		glBindBuffer(GL_PIXEL_PACK_BUFFER_EXT, 0);
		glDeleteBuffers(1, &bufferid);
	}
	#endif
}


void display(void)
{
	int format;

	for(format=0; format<FORMATS; format++)
	{
		fprintf(stderr, ">>>>>>>>>>  PIXEL FORMAT:  %s  <<<<<<<<<<\n",
			pf[format].name);

		#if defined(GL_ABGR_EXT) || defined(GL_BGRA_EXT) || defined(GL_BGR_EXT)
		const char *ext=(const char *)glGetString(GL_EXTENSIONS), *compext=NULL;
		#ifdef GL_ABGR_EXT
		if(pf[format].glFormat==GL_ABGR_EXT) compext="GL_EXT_abgr";
		#endif
		#ifdef GL_BGRA_EXT
		if(pf[format].glFormat==GL_BGRA_EXT) compext="GL_EXT_bgra";
		#endif
		#ifdef GL_BGR_EXT
		if(pf[format].glFormat==GL_BGR_EXT) compext="GL_EXT_bgra";
		#endif
		if(compext && (!ext || !strstr(ext, compext)))
		{
			fprintf(stderr, "%s extension not available.  Skipping ...\n\n",
				compext);
			continue;
		}
		#endif

		writeTest(format);
		for(int i=0; i<loops; i++) readTest(format);
		fprintf(stderr, "\n");
	}

	exit(0);
}

void usage(char **argv)
{
	fprintf(stderr, "\nUSAGE: %s [-h|-?] [-window] [-pm]", argv[0]);
	#ifdef GL_EXT_framebuffer_object
	fprintf(stderr, " [-fbo]");
	#endif
	#ifdef GL_VERSION_1_5
	fprintf(stderr, " [-pbo]\n");
	#else
	fprintf(stderr, "\n");
	#endif
	fprintf(stderr, "       [-width <n>] [-height <n>] [-align <n>] [-visualid <xx>] [-alpha]\n");
	fprintf(stderr, "       [-rgb] [-rgba] [-bgr] [-bgra] [-abgr] [-time <t>] [-loop <l>]");
	fprintf(stderr, "\n-h or -? = This screen\n");
	fprintf(stderr, "-window = Render to a window instead of a Pbuffer\n");
	fprintf(stderr, "-pm = Render to a pixmap instead of a Pbuffer\n");
	#ifdef GL_EXT_framebuffer_object
	fprintf(stderr, "-fbo = Render to a framebuffer object (FBO) instead of a Pbuffer\n");
	#endif
	#ifdef GL_VERSION_1_5
	fprintf(stderr, "-pbo = Use pixel buffer objects to perform readback\n");
	#endif
	fprintf(stderr, "-width = Set drawable width to n pixels (default: %d)\n", WIDTH);
	fprintf(stderr, "-height = Set drawable height to n pixels (default: %d)\n", HEIGHT);
	fprintf(stderr, "-align = Set row alignment to n bytes (default: %d)\n", ALIGN);
	fprintf(stderr, "-visualid = Ignore visual selection and use this visual ID (hex) instead\n");
	fprintf(stderr, "-alpha = Create Pbuffer/window using 32-bit instead of 24-bit visual\n");
	fprintf(stderr, "-rgb = Test only RGB pixel format\n");
	fprintf(stderr, "-rgba = Test only RGBA pixel format\n");
	fprintf(stderr, "-bgr = Test only BGR pixel format\n");
	fprintf(stderr, "-bgra = Test only BGRA pixel format\n");
	fprintf(stderr, "-abgr = Test only ABGR pixel format\n");
	fprintf(stderr, "-time <t> = Run each test for <t> seconds\n");
	fprintf(stderr, "-loop <l> = Run readback test <l> times in a row\n");
	fprintf(stderr, "\n");
	exit(0);
}


int main(int argc, char **argv)
{
	fprintf(stderr, "\n%s v%s (Build %s)\n", BENCH_NAME, __VERSION, __BUILD);

	#ifdef GLX11
	useWindow=1;
	#endif

	for(int i=0; i<argc; i++)
	{
		if(!stricmp(argv[i], "-h")) usage(argv);
		if(!stricmp(argv[i], "-?")) usage(argv);
		if(!stricmp(argv[i], "-window")) useWindow=1;
		if(!stricmp(argv[i], "-pm")) usePixmap=1;
		#ifdef GL_EXT_framebuffer_object
		if(!stricmp(argv[i], "-fbo")) useFBO=1;
		#endif
		#ifdef GL_VERSION_1_5
		if(!stricmp(argv[i], "-pbo")) pbo=1;
		#endif
		if(!stricmp(argv[i], "-alpha")) useAlpha=1;
		if(!stricmp(argv[i], "-rgb"))
		{
			PixelFormat pftemp={0, 1, 2, 3, GL_RGB, 0, "RGB"};
			pf[0]=pftemp;
			FORMATS=1;
		}
		if(!stricmp(argv[i], "-rgba"))
		{
			PixelFormat pftemp={0, 1, 2, 4, GL_RGBA, 0, "RGBA"};
			pf[0]=pftemp;
			FORMATS=1;
		}
		#ifdef GL_BGR_EXT
		if(!stricmp(argv[i], "-bgr"))
		{
			PixelFormat pftemp={2, 1, 0, 3, GL_BGR_EXT, 1, "BGR"};
			pf[0]=pftemp;
			FORMATS=1;
		}
		#endif
		#ifdef GL_BGRA_EXT
		if(!stricmp(argv[i], "-bgra"))
		{
			PixelFormat pftemp={2, 1, 0, 4, GL_BGRA_EXT, 1, "BGRA"};
			pf[0]=pftemp;
			FORMATS=1;
		}
		#endif
		#ifdef GL_ABGR_EXT
		if(!stricmp(argv[i], "-abgr"))
		{
			PixelFormat pftemp={3, 2, 1, 4, GL_ABGR_EXT, 0, "ABGR"};
			pf[0]=pftemp;
			FORMATS=1;
		}
		#endif
		if(!stricmp(argv[i], "-loop") && i<argc-1)
		{
			int temp=atoi(argv[i+1]);  i++;
			if(temp>1) loops=temp;
		}
		if(!stricmp(argv[i], "-align") && i<argc-1)
		{
			int temp=atoi(argv[i+1]);  i++;
			if(temp>=1 && (temp&(temp-1))==0) ALIGN=temp;
		}
		if(!stricmp(argv[i], "-visualid") && i<argc-1)
		{
			int temp=0;
			sscanf(argv[i+1], "%x", &temp);
			if(temp>0) visualID=temp;  i++;
		}
		if(!stricmp(argv[i], "-width") && i<argc-1)
		{
			int temp=atoi(argv[i+1]);  i++;
			if(temp>=1) width=temp;
		}
		if(!stricmp(argv[i], "-height") && i<argc-1)
		{
			int temp=atoi(argv[i+1]);  i++;
			if(temp>=1) height=temp;
		}
		if(!stricmp(argv[i], "-time") && i<argc-1)
		{
			double temp=atof(argv[i+1]);  i++;
			if(temp>0.0) benchTime=temp;
		}
	}

	if(argc<2) fprintf(stderr, "\n%s -h for advanced usage.\n", argv[0]);

	try
	{
		XSetErrorHandler(xhandler);
		if(!(dpy=XOpenDisplay(0)))
		{
			fprintf(stderr, "Could not open display %s\n", XDisplayName(0));
			exit(1);
		}
		#ifdef GL_VERSION_1_5
		if(pbo) fprintf(stderr, "Using PBO's for readback\n");
		#endif

		if((DisplayWidth(dpy, DefaultScreen(dpy))<width ||
			DisplayHeight(dpy, DefaultScreen(dpy))<height) && useWindow)
		{
			fprintf(stderr,
				"ERROR: Please switch to a screen resolution of at least %d x %d.\n",
				width, height);
			exit(1);
		}

		findVisual();

		if(useWindow || usePixmap || useFBO)
		{
			XSetWindowAttributes swa;
			Window root=DefaultRootWindow(dpy);
			swa.border_pixel=0;
			swa.event_mask=0;

			swa.colormap=XCreateColormap(dpy, root, v->visual, AllocNone);
			errifnot(win=XCreateWindow(dpy, root, 0, 0,
				(usePixmap || useFBO)? 1:width, (usePixmap || useFBO)? 1:height,
				0, v->depth, InputOutput, v->visual,
				CWBorderPixel|CWColormap|CWEventMask, &swa));
			if(usePixmap)
			{
				errifnot(pm=XCreatePixmap(dpy, win, width, height, v->depth));
			}
			else if(!useFBO) XMapWindow(dpy, win);
			XSync(dpy, False);
		}
		fprintf(stderr, "%s size = %d x %d pixels\n",
			usePixmap? "Pixmap" : useWindow? "Window" : useFBO? "FBO" : "Pbuffer",
			width, height);
		fprintf(stderr, "Using %d-byte row alignment\n\n", ALIGN);

		drawableInit();
		display();
		return 0;

	} catch(Error &e) { fprintf(stderr, "%s\n", e.getMessage()); }

	if(dpy)
	{
		#ifdef GL_EXT_framebuffer_object
		if(useFBO)
		{
			glDeleteRenderbuffersEXT(1, &rb);
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
			glDeleteFramebuffersEXT(1, &fb);
		}
		#endif
		glXMakeCurrent(dpy, 0, 0);
		if(ctx) glXDestroyContext(dpy, ctx);
		#ifndef GLX11
		if(pbuffer) glXDestroyPbuffer(dpy, pbuffer);
		#endif
		if(glxpm) glXDestroyGLXPixmap(dpy, glxpm);
		if(pm) XFreePixmap(dpy, pm);
		if(win) XDestroyWindow(dpy, win);
		XCloseDisplay(dpy);
	}
	if(v) XFree(v);
}
