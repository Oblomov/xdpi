#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <X11/Xlib.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrandr.h>

void show_dpi_info(Display *disp)
{
	int num_screens = ScreenCount(disp);

	/* Iterate over all screens, and show X11 and XRandR information */
	for (int i = 0 ; i < num_screens ; ++i) {
		Screen *screen = ScreenOfDisplay(disp, i);

		/* Standard X11 information */
		{
			int width = WidthOfScreen(screen);
			int height = HeightOfScreen(screen);

			int mmw = WidthMMOfScreen(screen);
			int mmh = HeightMMOfScreen(screen);

			int xdpi = (int)round(width*25.4/mmw);
			int ydpi = (int)round(height*25.4/mmh);

			int xdpcm = (int)round(width*10/mmw);
			int ydpcm = (int)round(height*10/mmh);

			printf("Screen %d: %dx%d pixels, %dx%d mm: %dx%d dpi, %dx%d dpcm\n", i,
				width, height, mmw, mmh,
				xdpi, ydpi, xdpcm, ydpcm);
		}

		/* XRandR information */
		XRRScreenResources *xrr_res = XRRGetScreenResourcesCurrent(disp, RootWindowOfScreen(screen));

		if (!xrr_res)
			continue; /* no XRR resources */

		puts("\tXRandR:");

		/* iterate over all CRTCs, and compute the DPIs of the connected outputs */
		for (int c = 0; c < xrr_res->ncrtc; ++c) {
			XRRCrtcInfo *rrc = XRRGetCrtcInfo(disp, xrr_res, xrr_res->crtcs[c]);
			/* skip if nothing connected */
			if (rrc->noutput < 1)
				continue;

			unsigned int w = rrc->width;
			unsigned int h = rrc->height;

			Rotation rot = (rrc->rotation & 0x0f);
			int rotated = ((rot == RR_Rotate_90) || (rot == RR_Rotate_270));

			for (int o = 0; o < rrc->noutput; ++o) {
				XRROutputInfo *rro = XRRGetOutputInfo(disp, xrr_res, rrc->outputs[o]);
				/* Skip if not connected, or if reported width/height are 0 */
				if (rro->connection != RR_Connected || !rro->mm_width || !rro->mm_height) {
					XRRFreeOutputInfo(rro);
					continue;
				}

				unsigned long mmw = rotated ? rro ->mm_height : rro->mm_width;
				unsigned long mmh = rotated ? rro ->mm_width : rro->mm_height;

				int rrxdpi = (int)round(w*25.4/mmw);
				int rrydpi = (int)round(h*25.4/mmh);

				int rrxdpcm = (int)round(w*10/mmw);
				int rrydpcm = (int)round(h*10/mmh);

				printf("\t\t%s: %dx%d pixels, (%s) %dx%d mm: %dx%d dpi, %dx%d dpcm\n", rro->name,
					w, h,
					(rotated ? "R" : "U"), mmw, mmh,
					rrxdpi, rrydpi,
					rrxdpcm, rrydpcm);

				XRRFreeOutputInfo(rro);
			}
			XRRFreeCrtcInfo(rrc);
		}
	}

	/* Xinerama */

	Bool xine_p = XineramaIsActive(disp);
	if (xine_p) {
		int num_xines = 0;
		XineramaScreenInfo *xines = XineramaQueryScreens(disp, &num_xines);
		if (xines) {
			puts("Xinerama screens:");
			for (int i = 0; i < num_xines; ++i) {
				XineramaScreenInfo *xi = xines + i;
				int n = xi->screen_number;
				printf("\t%u: %ux%u pixels, no dpi information\n",
					n,
					xi->width,
					xi->height);
			}
		}
		XFree(xines);
	}

	/* XRandR */

	for (int i = 0; i < num_screens; ++i) {

		int shown = 0;

		/* Xft.dpi */
		const char *dpi = XGetDefault(disp, "Xft", "dpi");
		if (dpi) {
			puts("X resources:");
			printf("\tXft.dpi: %s\n", dpi);
		}
	}
}

int main(int argc, char *argv[])
{
	puts("*** Resolution and dot pitch information exposed by X11 ***");

	Display *disp = XOpenDisplay(getenv("DISPLAY"));
	if (!disp) {
		fputs("Could not open X display\n", stderr);
		return 1;
	}

	show_dpi_info(disp);

	return 0;
}
