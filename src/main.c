#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include "freetype/freetype.h"
#include <glib-2.0/glib.h>
#include "fonts.h"
#include "bitmaps.h"
#include <X11/Xutil.h>
FT_Library ftlib;
FT_Face curFace;
typedef struct {
	unsigned char r;
	unsigned char g;
	unsigned char b;
	unsigned char a;
} rgba;

typedef struct {
	unsigned char r;
	unsigned char g;
	unsigned char b;
} rgb;
typedef struct {
	Window frame;
	Window client;
	char *title;
	GC gc;
	XImage *label;
	XImage *labeli;
	int textw, texth;
	int left, top;
	int src_left, src_top;
	int width, height;
	char beingDragged;
} FrameData;

#define MAX_FRAMES 64
const unsigned int TitlebarHeight = 29;
FrameData frames[MAX_FRAMES];
int nframes = 0;
Display *dpy;
Atom _NET_WM_WINDOW_TYPE;
Atom _NET_WM_WINDOW_TYPE_DOCK;
Atom _NET_WM_WINDOW_TYPE_DESKTOP;
Atom _NET_WM_WINDOW_TYPE_MENU;
Atom _NET_WM_STATE;
Atom _NET_WM_STATE_STICKY;
Atom _NET_WM_STRUT;
Atom _NET_WM_STRUT_PARTIAL;
Window root;
XEvent ev;

void InitFreetype() { FT_Init_FreeType(&ftlib); }
void LoadFontFromPath(char *path) { FT_New_Face(ftlib, path, 0, &curFace); }
void LoadFontFromBytes(const char *bytes, size_t arrlen) {
	FT_New_Memory_Face(ftlib, (const FT_Byte *)bytes, arrlen, 0, &curFace);
}
void SetFontSize(unsigned int size) { FT_Set_Pixel_Sizes(curFace, 0, size); }

rgba *RenderRGBXText(char *text, int *w, int *h, rgb color) {
	int pen_x = 0;
	int max_ascent = 0;
	int max_descent = 0;

	for (char *p = text; *p; p++) {
		FT_Load_Char(curFace, *p, FT_LOAD_RENDER);

		FT_GlyphSlot g = curFace->glyph;

		int ascent = g->bitmap_top;
		int descent = g->bitmap.rows - g->bitmap_top;

		if (ascent > max_ascent)
			max_ascent = ascent;

		if (descent > max_descent)
			max_descent = descent;

		pen_x += g->advance.x >> 6;
	}

	*w = pen_x;
	*h = max_ascent + max_descent;
	pen_x = 0;
	rgba *buffer = calloc(*w * *h, sizeof(rgba));
	for (char *p = text; *p; p++) {
		FT_Load_Char(curFace, *p, FT_LOAD_RENDER);

		FT_GlyphSlot g = curFace->glyph;

		int x = pen_x + g->bitmap_left;
		int y = max_ascent - g->bitmap_top;

		for (int row = 0; row < g->bitmap.rows; row++) {
			for (int col = 0; col < g->bitmap.width; col++) {
				int dst_x = x + col;
				int dst_y = y + row;

				int dst_index = dst_y * (*w) + dst_x;
				int src_index = row * g->bitmap.pitch + col;

				buffer[dst_index].r =
				    g->bitmap.buffer[src_index] * color.r / 255;
				buffer[dst_index].g =
				    g->bitmap.buffer[src_index] * color.g / 255;
				buffer[dst_index].b =
				    g->bitmap.buffer[src_index] * color.b / 255;
				buffer[dst_index].a = 255;
			}
		}

		pen_x += g->advance.x >> 6;
	}
	return buffer;
}
XImage *xbtn;
static inline void RenderDecorButtons(FrameData frame, int clsstat){
	XColor color;
	color.red = 65536/8;
	color.green = 65536/8;
	color.blue = 65536/8;
	XAllocColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), &color);


	if(clsstat){
		XSetForeground(dpy, frame.gc, color.pixel);
	}else{
		XSetForeground(dpy, frame.gc, 0x000a0b11);
	}
	XFillRectangle(dpy, frame.frame, frame.gc, frame.width+10 - TitlebarHeight, 0, TitlebarHeight, TitlebarHeight);
	XPutImage(dpy, frame.frame, frame.gc, xbtn, 0, 0,
			  frame.width - (TitlebarHeight - xbtnicon_h) / 2,
			  (TitlebarHeight - xbtnicon_h) / 2, xbtnicon_w,
			  xbtnicon_h);
}
static inline void RenderFrameKF(Window wnd, int focusval) {
	for (int i = 0; i < nframes; i++) {
		if (frames[i].frame == wnd) {
			// render duh title
			int textw;
			int texth;
			if(frames[i].title){
			if (focusval == 1) {
				XPutImage(dpy, frames[i].frame, frames[i].gc, frames[i].label,
				          0, 0, frames[i].width / 2 - frames[i].textw / 2, 10,
				          frames[i].textw, frames[i].texth);
			} else {
				XPutImage(dpy, frames[i].frame, frames[i].gc, frames[i].labeli,
				          0, 0, frames[i].width / 2 - frames[i].textw / 2, 10,
				          frames[i].textw, frames[i].texth);
			}
			}
			RenderDecorButtons(frames[i],0);
			XFlush(dpy);
		}
	}
}
static inline void RenderFrame(Window wnd) {
	Window focused;
	int revert;
	XGetInputFocus(dpy, &focused, &revert);
	if (wnd == focused) {
		RenderFrameKF(wnd, 1);
	} else {
		RenderFrameKF(wnd, 0);
	}
}
int IsDockWindow(Window w) {
	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	Atom *prop = NULL;

	if (XGetWindowProperty(dpy, w, _NET_WM_WINDOW_TYPE, 0, 1024, False,
		XA_ATOM, &actual_type, &actual_format,
		&nitems, &bytes_after, (unsigned char**)&prop) == Success) {
		for (unsigned long i = 0; i < nitems; i++) {
			if (prop[i] == _NET_WM_WINDOW_TYPE_DOCK) {
				XFree(prop);
				return 1;
			}
		}
		XFree(prop);
		}
		return 0;
}
int IsMenuWindow(Window w) {
	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	Atom *prop = NULL;

	if (XGetWindowProperty(dpy, w, _NET_WM_WINDOW_TYPE, 0, 1024, False,
		XA_ATOM, &actual_type, &actual_format,
		&nitems, &bytes_after, (unsigned char**)&prop) == Success) {
		for (unsigned long i = 0; i < nitems; i++) {
			if (prop[i] == _NET_WM_WINDOW_TYPE_MENU) {
				XFree(prop);
				return 1;
			}
		}
		XFree(prop);
		}
		return 0;
}
int IsDskWindow(Window w) {
	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	Atom *prop = NULL;

	if (XGetWindowProperty(dpy, w, _NET_WM_WINDOW_TYPE, 0, 1024, False,
		XA_ATOM, &actual_type, &actual_format,
		&nitems, &bytes_after, (unsigned char**)&prop) == Success) {
		for (unsigned long i = 0; i < nitems; i++) {
			if (prop[i] == _NET_WM_WINDOW_TYPE_DESKTOP) {
				XFree(prop);
				return 1;
			}
		}
		XFree(prop);
		}
		return 0;
}
int usableX;
int usableY;
int main() {
	int screen;
	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		perror("XOpenDisplay");
		exit(1);
	}
	screen = DefaultScreen(dpy);
	_NET_WM_WINDOW_TYPE = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	_NET_WM_WINDOW_TYPE_DOCK = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
	_NET_WM_WINDOW_TYPE_DESKTOP = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
	_NET_WM_STATE = XInternAtom(dpy, "_NET_WM_STATE", False);
	_NET_WM_STATE_STICKY = XInternAtom(dpy, "_NET_WM_STATE_STICKY", False);
	_NET_WM_STRUT = XInternAtom(dpy, "_NET_WM_STRUT", False);
	_NET_WM_STRUT_PARTIAL = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False);
	_NET_WM_WINDOW_TYPE_MENU = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_MENU", False);
	InitFreetype();
	LoadFontFromBytes(URWGothic_Book, URWGothic_Book_Len);
	SetFontSize(12);

	printf("WM started\n");

	root = DefaultRootWindow(dpy);

	XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask);

	xbtn = XCreateImage(dpy, DefaultVisual(dpy, screen),
	                    DefaultDepth(dpy, screen), ZPixmap, 0, (char *)xbtnicon,
	                    xbtnicon_w, xbtnicon_h, 32, 0);
	XSetErrorHandler((XErrorHandler)(int[]){0});
	while (1) {
		XNextEvent(dpy, &ev);
		printf("Event: %d\n", ev.type);
		if (ev.type == ConfigureRequest) {
			XConfigureRequestEvent *e = &ev.xconfigurerequest;

			XWindowChanges wc;
			wc.x = e->x;
			wc.y = e->y;
			wc.width = e->width;
			wc.height = e->height;
			wc.border_width = e->border_width;
			wc.sibling = e->above;
			wc.stack_mode = e->detail;
			XConfigureWindow(dpy, e->window, e->value_mask, &wc);
		} else if (ev.type == MapRequest) {
			Window client = ev.xmaprequest.window;

			XWindowAttributes attr;
			XGetWindowAttributes(dpy, client, &attr);
			if(!(IsDockWindow(client) || IsMenuWindow(client) || IsDskWindow(client))){

			// Create frame window
			Window frame = XCreateSimpleWindow(
			    dpy, root, attr.x, attr.y, attr.width + 10,
			    attr.height + 29 + 5, // extra space for titlebar
			    0, 0x000a0b11, 0x000a0b11);
			GC gc = XCreateGC(dpy, frame, 0, NULL);
			int textw, texth;
			char *wndname;
			XImage *labelimg;
			XImage *labeliimg;
			XFetchName(dpy, client, &wndname);

			if (wndname) {
				rgba *textbuf = RenderRGBXText(wndname, &textw, &texth,
				                               (rgb){255, 255, 255});
				labelimg = XCreateImage(
				    dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen),
				    ZPixmap, 0, (char *)textbuf, textw, texth, 32, 4 * textw);
				rgba *textbufi = RenderRGBXText(wndname, &textw, &texth,
				                                (rgb){128, 128, 128});
				labeliimg = XCreateImage(
				    dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen),
				    ZPixmap, 0, (char *)textbufi, textw, texth, 32, 4 * textw);
			} else {
				labelimg = NULL;
			}
			XSelectInput(dpy, frame,
			             SubstructureRedirectMask | SubstructureNotifyMask |
			                 ButtonPressMask | ButtonReleaseMask | ExposureMask |
			                 PointerMotionMask | LeaveWindowMask| FocusChangeMask);
			XSelectInput(dpy, client, FocusChangeMask | PropertyChangeMask);

			// reparent client into frame
			XReparentWindow(dpy, client, frame, 5, 29);
			XGrabButton(dpy, Button1, AnyModifier, client, False,
			            ButtonPressMask, GrabModeSync, GrabModeAsync, None,
			            None);
			// map both
			XMapWindow(dpy, frame);
			frames[nframes++] = (FrameData){frame,
				client,
				wndname,
				gc,
				labelimg,
				labeliimg,
				textw,
				texth,
				DisplayWidth(dpy, screen) - attr.width / 2,
				DisplayHeight(dpy, screen) - attr.height / 2,
				DisplayWidth(dpy, screen) - attr.width / 2,
				DisplayHeight(dpy, screen) - attr.height / 2,
				attr.width,
				attr.height,
				0};

			}
			XMapWindow(dpy, client);

			XFlush(dpy);
		} else if (ev.type == FocusIn) {
			RenderFrameKF(ev.xfocus.window, 1);
		} else if (ev.type == FocusOut) {
			if (ev.xfocus.mode == NotifyGrab || ev.xfocus.mode == NotifyUngrab)
				continue;
			if (ev.xfocus.detail == NotifyInferior  ||
				ev.xfocus.detail == NotifyVirtual   ||
				ev.xfocus.detail == NotifyNonlinearVirtual)
				continue;
			RenderFrameKF(ev.xfocus.window, 0);
			for (int i = 0; i < nframes; i++) {
				if (frames[i].client == ev.xfocus.window) {
					RenderFrameKF(frames[i].frame, 0);
					break;
				}
			}
		}
		if (ev.type == Expose) {
			RenderFrame(ev.xexpose.window);
		}
		if (ev.type == ButtonRelease) {
			for (int i = 0; i < nframes; i++) {
				if (frames[i].beingDragged == 1) {
					frames[i].beingDragged = 0;
					XUngrabPointer(dpy, CurrentTime);
					break;
				}
			}
			for (int i = 0; i < nframes; i++) {
				if (frames[i].frame == ev.xbutton.window &&
					ev.xbutton.y < TitlebarHeight &&
					ev.xbutton.x > frames[i].width - TitlebarHeight) {

				Atom wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
				Atom wm_delete    = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

				// check if client supports WM_DELETE_WINDOW
				Atom *protocols;
				int n;
				int supports_delete = 0;
				if (XGetWMProtocols(dpy, frames[i].client, &protocols, &n)) {
					for (int j = 0; j < n; j++) {
						if (protocols[j] == wm_delete) {
							supports_delete = 1;
							break;
						}
					}
					XFree(protocols);
				}

				if (supports_delete) {
					XEvent msg = {0};
					msg.type                 = ClientMessage;
					msg.xclient.window       = frames[i].client;
					msg.xclient.message_type = wm_protocols;
					msg.xclient.format       = 32;
					msg.xclient.data.l[0]    = wm_delete;
					msg.xclient.data.l[1]    = CurrentTime;
					XSendEvent(dpy, frames[i].client, False, NoEventMask, &msg);
				} else {
					XKillClient(dpy, frames[i].client);
				}
				break;
					}
			}
		} else if (ev.type == ButtonPress) {
			Window w = ev.xbutton.window;
			for (int i = 0; i < nframes; i++) {
				if (frames[i].beingDragged == 1) {
					break;
				}
				if (frames[i].frame == w) {
					XRaiseWindow(dpy, frames[i].frame);
					XSetInputFocus(dpy, frames[i].client, RevertToPointerRoot,
					               CurrentTime);
					if (ev.xbutton.y <= 29) {
						XGrabPointer(dpy, root, True,
						             PointerMotionMask | ButtonReleaseMask,
						             GrabModeAsync, GrabModeAsync, None, None,
						             CurrentTime);
						for (int i = 0; i < nframes; i++) {
							if (frames[i].frame == ev.xbutton.window && (ev.xbutton.x<frames[i].width-TitlebarHeight || ev.xbutton.y>TitlebarHeight)) {
								frames[i].src_left = ev.xbutton.x;
								frames[i].src_top = ev.xbutton.y;
								frames[i].beingDragged = 1;
							}
						}
					}
				} else if (frames[i].client == w) {
					if(!(IsDockWindow(frames[i].client) || IsDskWindow(frames[i].client))){
					XRaiseWindow(dpy, frames[i].frame);
					}
					XSetInputFocus(dpy, frames[i].client, RevertToPointerRoot,
					               CurrentTime);
					printf("clicked client area\n");
					XAllowEvents(dpy, ReplayPointer, CurrentTime);
					XUngrabPointer(dpy, CurrentTime);
				}
			}
		} else if (ev.type == MotionNotify) {
			for (int i = 0; i < nframes; i++) {
			if (ev.xmotion.state & Button1Mask) {

					if (frames[i].beingDragged == 1) {
						if ((ev.xmotion.x_root - frames[i].src_left >= 0) ||
						    (ev.xmotion.y_root - frames[i].src_top >= 0)) {
							XMoveWindow(dpy, frames[i].frame,
							            ev.xmotion.x_root - frames[i].src_left,
							            ev.xmotion.y_root - frames[i].src_top);
							frames[i].left =
							    ev.xmotion.x_root - frames[i].src_left;
							frames[i].top =
							    ev.xmotion.y_root - frames[i].src_top;
						}
					}
			}else{
				if(ev.xmotion.x>frames[i].width+10-TitlebarHeight && ev.xmotion.y <TitlebarHeight){
					RenderDecorButtons(frames[i],1);
				}else{
					RenderDecorButtons(frames[i],0);
				}
			}
			}
		}
		else if (ev.type == LeaveNotify){
			for (int i = 0; i < nframes; i++) {
				RenderDecorButtons(frames[i],0);
			}
		}
		if (ev.type == PropertyNotify) {
			if (ev.xproperty.atom == XInternAtom(dpy, "WM_NAME", False)) {
				char *wndname = NULL;
				int frame_idx = -1;
				for (int i = 0; i < nframes; i++) {
					if (frames[i].client == ev.xproperty.window) {
						frame_idx = i;
						break;
					}
				}
				if (frame_idx < 0)
					continue;

				XFetchName(dpy, ev.xproperty.window, &wndname);
				if (!wndname)
					continue;

				int textw, texth;
				rgba *textbuf = RenderRGBXText(wndname, &textw, &texth,
				                               (rgb){255, 255, 255});
				XImage *labelimg = XCreateImage(
				    dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen),
				    ZPixmap, 0, (char *)textbuf, textw, texth, 32, 4 * textw);
				rgba *textbufi = RenderRGBXText(wndname, &textw, &texth,
				                                (rgb){128, 128, 128});
				XImage *labeliimg = XCreateImage(
				    dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen),
				    ZPixmap, 0, (char *)textbufi, textw, texth, 32, 4 * textw);

				XDestroyImage(frames[frame_idx].label);
				XDestroyImage(frames[frame_idx].labeli);
				frames[frame_idx].label = labelimg;
				frames[frame_idx].labeli = labeliimg;
				frames[frame_idx].title = wndname;
				frames[frame_idx].textw = textw;
				frames[frame_idx].texth = texth;
			}
		}
		if (ev.type == UnmapNotify) {
			for (int i = 0; i < nframes; i++) {
				if (frames[i].client == ev.xunmap.window) {
					XDestroyWindow(dpy, frames[i].frame);
					for (int j = i; j < nframes; j++) {
						frames[j] = frames[j + 1];
					}
					nframes -= 1;
					break;
				}
			}
		}
	}
}
