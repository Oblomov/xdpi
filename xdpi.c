#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <X11/Xlib.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrandr.h>

#include <xcb/xproto.h>
#include <xcb/xinerama.h>
#include <xcb/randr.h>

void print_dpi_screen(int i, int width, int height, int mmw, int mmh)
{
	int xdpi = (int)round(width*25.4/mmw);
	int ydpi = (int)round(height*25.4/mmh);

	int xdpcm = (int)round(width*10/mmw);
	int ydpcm = (int)round(height*10/mmh);

	printf("Screen %d: %dx%d pixels, %dx%d mm: %dx%d dpi, %dx%d dpcm\n", i,
		width, height, mmw, mmh,
		xdpi, ydpi, xdpcm, ydpcm);
}

void do_xlib_dpi(Display *disp)
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

			print_dpi_screen(i, width, height, mmw, mmh);
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

				printf("\t\t%s: %dx%d pixels, (%s) %lux%lu mm: %dx%d dpi, %dx%d dpcm\n", rro->name,
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

	/* Xft.dpi */

	for (int i = 0; i < num_screens; ++i) {

		/* Xft.dpi */
		const char *dpi = XGetDefault(disp, "Xft", "dpi");
		if (dpi) {
			puts("X resources:");
			printf("\tXft.dpi: %s\n", dpi);
		}
	}
}

int xlib_dpi(void)
{
	Display *disp = XOpenDisplay(getenv("DISPLAY"));
	if (!disp) {
		fputs("Could not open X display\n", stderr);
		return 1;
	}

	puts("** Xlib interfaces");

	do_xlib_dpi(disp);

	XCloseDisplay(disp);

	return 0;
}

void do_xcb_dpi(xcb_connection_t *conn)
{
	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(xcb_get_setup(conn));
	int count, i;

	/* Collect information first, then show. This should make the async
	 * requests such as those for RANDR faster
	 */
	xcb_screen_t *screen_data =
		malloc(iter.rem*sizeof(*screen_data));
	xcb_randr_get_screen_resources_cookie_t *rr_cookie =
		malloc(iter.rem*sizeof(*rr_cookie));
	xcb_randr_get_screen_resources_reply_t **rr_res =
		calloc(iter.rem, sizeof(*rr_res));

	if (!screen_data || !rr_cookie || !rr_res) {
		fputs("could not allocate memory for screen data\n", stderr);
		goto cleanup;
	}

	/* Query */
	for (count = 0 ; iter.rem; ++count, xcb_screen_next(&iter)) {
		screen_data[count] = *iter.data;
		rr_cookie[count] = xcb_randr_get_screen_resources(conn,
			iter.data->root);
	}

	/* Get the actual data */
	for (i = 0; i < count; ++i) {
		xcb_generic_error_t *err = NULL;
		rr_res[count] = xcb_randr_get_screen_resources_reply(conn,
			rr_cookie[count], &err);
		if (err) {
			fprintf(stderr, "error getting resources for screen %d -- %d\n", i,
				err->error_code);
			count = i;
			free(err);
		}
	}

	/* Show it */
	for (i = 0; i < count; ++i) {
		const xcb_screen_t *screen = screen_data + i;
		/* Standard X11 information */
		{
			print_dpi_screen(i,
				screen->width_in_pixels, screen->height_in_pixels,
				screen->width_in_millimeters, screen->height_in_millimeters);
		}
	}
cleanup:
	free(screen_data);
	free(rr_cookie);
	for (i = 0; i < count; ++i)
		free(rr_res[i]);
	free(rr_res);


}

int xcb_dpi(void)
{
	int ret = 0;
	xcb_connection_t *conn = xcb_connect(NULL, NULL);
	if ((ret = xcb_connection_has_error(conn))) {
		fputs("XCB connection error\n", stderr);
	}

	puts("** xcb interfaces");

	do_xcb_dpi(conn);

	xcb_disconnect(conn);
	return ret;
}

int main(int argc, char *argv[])
{
	/* TODO support CLI options for help or output format selection */
	(void)argc;
	(void)argv;

	puts("*** Resolution and dot pitch information exposed by X11 ***");

	xlib_dpi();

	xcb_dpi();

	puts("*** Done ***");
}
