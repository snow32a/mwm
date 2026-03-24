#include <X11/X.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include "freetype/freetype.h"
#include <glib-2.0/glib.h>
#include "fonts.h"
#include "bitmaps.h"
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
FrameData frames[MAX_FRAMES];
int nframes = 0;
Display *dpy;
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
static inline void RenderFrameKF(Window wnd, int focusval){
	for (int i = 0; i < nframes; i++) {
		if (frames[i].frame == wnd) {
			// render duh title
			int textw;
			int texth;
			if (focusval == 1) {
				XPutImage(dpy, frames[i].frame, frames[i].gc,
						  frames[i].label, 0, 0,
			  frames[i].width / 2 - frames[i].textw / 2, 10,
			  frames[i].textw, frames[i].texth);
			} else {
				XPutImage(dpy, frames[i].frame, frames[i].gc,
						  frames[i].labeli, 0, 0,
			  frames[i].width / 2 - frames[i].textw / 2, 10,
			  frames[i].textw, frames[i].texth);
			}
			XFlush(dpy);
		}
	}
}
static inline void RenderFrame(Window wnd){
	Window focused;
	int revert;
	XGetInputFocus(dpy, &focused, &revert);
			if(wnd == focused){
				RenderFrameKF(wnd,1);
			}else{
				RenderFrameKF(wnd,0);
			}

}
int main() {
	int screen;
	dpy = XOpenDisplay(NULL);
	screen = DefaultScreen(dpy);
	if (!dpy) {
		perror("XOpenDisplay");
		exit(1);
	}

	InitFreetype();
	LoadFontFromBytes(URWGothic_Book, URWGothic_Book_Len);
	SetFontSize(12);

	printf("WM started\n");

	root = DefaultRootWindow(dpy);

	XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask);

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
		}
		if (ev.type == MapRequest) {
			Window client = ev.xmaprequest.window;

			XWindowAttributes attr;
			XGetWindowAttributes(dpy, client, &attr);

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
			                 ButtonPressMask | ExposureMask |
			                 PointerMotionMask | FocusChangeMask);
			XSelectInput(dpy, client,
						 FocusChangeMask);

			// reparent client into frame
			XReparentWindow(dpy, client, frame, 5, 29);
			XGrabButton(dpy, Button1, AnyModifier, client, False,
			            ButtonPressMask, GrabModeSync, GrabModeAsync, None,
			            None);
			// map both
			XMapWindow(dpy, frame);
			XMapWindow(dpy, client);

			XFlush(dpy);
			frames[nframes++] = (FrameData){frame,
			                                client,
			                                wndname,
			                                gc,
			                                labelimg,
											labeliimg,
			                                textw,
			                                texth,
			                                640 - attr.width / 2,
			                                400 - attr.height / 2,
			                                640 - attr.width / 2,
			                                400 - attr.height / 2,
			                                attr.width,
			                                attr.height,
			                                0};
		}
		else if (ev.type == FocusIn) {
			RenderFrameKF(ev.xfocus.window,1);
		}
		else if (ev.type == FocusOut) {
			RenderFrameKF(ev.xfocus.window,0);
		}
		if (ev.type == Expose) {
			RenderFrame(ev.xfocus.window);
		}
		if (ev.type == ButtonRelease) {
			for (int i = 0; i < nframes; i++) {
				if (frames[i].beingDragged == 1) {
					frames[i].beingDragged = 0;
					XUngrabPointer(dpy, CurrentTime);
					break;
				}
			}
		}
		if (ev.type == ButtonPress) {
			Window w = ev.xbutton.window;
			for (int i = 0; i < nframes; i++) {
				if (frames[i].beingDragged == 1) {
					break;
				}
				if (frames[i].frame == w) {
					XRaiseWindow(dpy, frames[i].frame);
					XSetInputFocus(dpy, frames[i].client, RevertToPointerRoot, CurrentTime);
					if (ev.xbutton.y <= 29) {
						XGrabPointer(dpy, root, True,
						             PointerMotionMask | ButtonReleaseMask,
						             GrabModeAsync, GrabModeAsync, None, None,
						             CurrentTime);
						for (int i = 0; i < nframes; i++) {
							if (frames[i].frame == ev.xbutton.window) {
								frames[i].src_left = ev.xbutton.x;
								frames[i].src_top = ev.xbutton.y;
								frames[i].beingDragged = 1;
							}
						}
					}
				} else if (frames[i].client == w) {
					XRaiseWindow(dpy, frames[i].frame);
					XSetInputFocus(dpy, frames[i].client, RevertToPointerRoot, CurrentTime);
					printf("clicked client area\n");
					XAllowEvents(dpy, ReplayPointer, CurrentTime);
					XUngrabPointer(dpy, CurrentTime);
				}
			}
		}
		if (ev.type == MotionNotify) {
			if (ev.xmotion.state & Button1Mask) {

				for (int i = 0; i < nframes; i++) {
					if (frames[i].beingDragged == 1) {
						XMoveWindow(dpy, frames[i].frame,
						            ev.xmotion.x_root - frames[i].src_left,
						            ev.xmotion.y_root - frames[i].src_top);
						frames[i].left = ev.xmotion.x_root - frames[i].src_left;
						frames[i].top = ev.xmotion.y_root - frames[i].src_top;
					}
				}
			}
		}
		if (ev.type == ConfigureRequest) {
			XConfigureRequestEvent *e = &ev.xconfigurerequest;

			int frame_idx = -1;
			for (int i = 0; i < nframes; i++) {
				Window parent, root_ret, *children;
				unsigned int nc;
				XQueryTree(dpy, e->window, &root_ret, &parent, &children,
				           &nc); // i stole this qwq
				if (children)
					XFree(children);
				if (parent == frames[i].frame) {
					frame_idx = i;
					break;
				}
			}

			if (frame_idx >= 0) {
				if (e->value_mask & (CWX | CWY)) {
					XMoveWindow(dpy, frames[frame_idx].frame, e->x, e->y);
				}
				if (e->value_mask & (CWWidth | CWHeight)) {
					XResizeWindow(dpy, frames[frame_idx].frame, e->width,
					              e->height);
				}
				XMoveWindow(dpy, e->window, 5, 29);
				XWindowChanges wc = {.width = e->width - 10,
				                     .height = e->height - 29 - 5,
				                     .border_width = e->border_width};
				XConfigureWindow(dpy, e->window, e->value_mask & ~(CWX | CWY),
				                 &wc);
			} else {
				XWindowChanges wc = {.x = e->x,
				                     .y = e->y,
				                     .width = e->width,
				                     .height = e->height,
				                     .border_width = e->border_width,
				                     .sibling = e->above,
				                     .stack_mode = e->detail};
				XConfigureWindow(dpy, e->window, e->value_mask, &wc);
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
