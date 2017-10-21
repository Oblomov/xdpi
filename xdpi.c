/* X11 DPI information retrieval.
 * Copyright (C) 2017 Giuseppe Bilotta <giuseppe.bilotta@gmail.com>
 * Licensed under the terms of the Mozilla Public License, version 2.
 * See LICENSE.txt for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <X11/Xlib.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrandr.h>

#if WITH_XCB
#include <xcb/xproto.h>
#include <xcb/xinerama.h>
#include <xcb/randr.h>
#endif

void print_dpi_common(int w, int h, int mmw, int mmh)
{
	double pitch = sqrt(mmw*mmw+mmh*mmh)/sqrt(w*w+h*h);

	int xdpi = 0, xdpcm = 0;
	int ydpi = 0, ydpcm = 0;

	if (mmw) {
		xdpi = (int)round(w*25.4/mmw);
		xdpcm = (int)round(w*10/mmw);
	}

	if (mmh) {
		ydpi = (int)round(h*25.4/mmh);
		ydpcm = (int)round(h*10/mmh);
	}

	printf("%dx%d dpi, %dx%d dpcm, dot pitch %.2gmm\n",
		xdpi, ydpi, xdpcm, ydpcm, pitch);
}

void print_dpi_screen(int i, int width, int height, int mmw, int mmh)
{
	printf("Screen %d: %dx%d pixels, %dx%d mm: ", i, width, height, mmw, mmh);
	print_dpi_common(width, height, mmw, mmh);
}

void print_dpi_randr(const char *name,
	unsigned long mmw, unsigned long mmh, int w, int h, int rotated, int connection)
{
	const char * connection_string = (connection == RR_Connected ?
		"connected" : (connection == RR_Disconnected ?
			"disconnected" : (connection == RR_UnknownConnection ?
				"unknown" : "?")));
	printf("\t\t%s: %dx%d pixels, (%s, %s) %lux%lu mm: ",
		name,
		w, h,
		(rotated ? "R" : "U"),
		connection_string,
		mmw, mmh);
	print_dpi_common(w, h, mmw, mmh);
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
		XRRScreenResources *xrr_res = XRRGetScreenResources(disp, RootWindowOfScreen(screen));

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

				unsigned long mmw = rotated ? rro->mm_height : rro->mm_width;
				unsigned long mmh = rotated ? rro->mm_width : rro->mm_height;

				print_dpi_randr(rro->name, mmw, mmh, w, h, rotated, rro->connection);

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

#if WITH_XCB
void do_xcb_dpi(xcb_connection_t *conn)
{
	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(xcb_get_setup(conn));

	const xcb_query_extension_reply_t *xine_query = xcb_get_extension_data(conn, &xcb_xinerama_id);
	const xcb_query_extension_reply_t *randr_query = xcb_get_extension_data(conn, &xcb_randr_id);

	int xine_active = xine_query->present;
	int randr_active = randr_query->present;

	int count = 0, i, j;

	/* Collect information first, then show. This should make the async
	 * requests such as those for RANDR faster.
	 */
	xcb_screen_t *screen_data =
		malloc(iter.rem*sizeof(*screen_data));

	xcb_xinerama_query_screens_cookie_t xine_cookie;
	xcb_xinerama_query_screens_reply_t *xine_reply = NULL;

	xcb_randr_get_screen_resources_cookie_t *rr_cookie = randr_active ?
		malloc(iter.rem*sizeof(*rr_cookie)) : NULL;
	xcb_randr_get_screen_resources_reply_t **rr_res = randr_active ?
		calloc(iter.rem, sizeof(*rr_res)) : NULL;
	xcb_randr_crtc_t **rr_crtc = randr_active ?
		calloc(iter.rem, sizeof(*rr_crtc)) : NULL;
	xcb_randr_get_crtc_info_reply_t ***rr_crtc_info = randr_active ?
		calloc(iter.rem, sizeof(*rr_crtc_info)) : NULL;
	xcb_randr_get_output_info_reply_t ***rr_out = randr_active ?
		calloc(iter.rem, sizeof(*rr_out)) : NULL;

	xcb_generic_error_t *err = NULL;

	if (!screen_data || !rr_cookie || !rr_res || !rr_crtc || !rr_crtc_info || !rr_out) {
		fputs("could not allocate memory for screen data\n", stderr);
		goto cleanup;
	}

	/* Find if Xinerama is actually enabled */
	if (xine_active) {
		xcb_xinerama_is_active_cookie_t xine_active_cookie = xcb_xinerama_is_active(conn);
		xcb_xinerama_is_active_reply_t *xine_active_reply = xcb_xinerama_is_active_reply
			(conn, xine_active_cookie, &err);
		if (err) {
			fprintf(stderr, "error getting Xinerama status -- %d\n", err->error_code);
			free(err);
			err = NULL;
		} else {
			xine_active = xine_active_reply->state;
		}
		free(xine_active_reply);
	}

	/** Query **/

	/* Collect core info and query RANDR */
	for (count = 0 ; iter.rem; ++count, xcb_screen_next(&iter)) {
		screen_data[count] = *iter.data;
		if (randr_active)
			rr_cookie[count] = xcb_randr_get_screen_resources(conn,
				iter.data->root);
	}

	/* Xinerama */
	if (xine_active)
		xine_cookie = xcb_xinerama_query_screens(conn);

	/** Get the actual data **/
	/* RANDR */
	for (i = 0; i < count; ++i) {
		int num_crtcs = 0;
		int num_outputs = 0;
		const xcb_randr_output_t *output = NULL;

		xcb_randr_get_crtc_info_cookie_t *crtc_cookie = NULL;
		xcb_randr_get_output_info_cookie_t *output_cookie = NULL;

		rr_res[i] = xcb_randr_get_screen_resources_reply(conn,
			rr_cookie[i], &err);
		if (err) {
			fprintf(stderr, "error getting resources for screen %d -- %d\n", i,
				err->error_code);
			free(err);
			err = NULL;
			randr_active = 0;
		}

		if (!randr_active)
			break;

		num_crtcs = xcb_randr_get_screen_resources_crtcs_length(rr_res[i]);
		num_outputs = xcb_randr_get_screen_resources_outputs_length(rr_res[i]);

		/* Get the first crtc and output. We store the CRTC to match it to the output
		 * later on. NOTE that this is not for us to free. */
		rr_crtc[i] = xcb_randr_get_screen_resources_crtcs(rr_res[i]);
		output = xcb_randr_get_screen_resources_outputs(rr_res[i]);

		/* Cookies for the requests */
		crtc_cookie = calloc(num_crtcs, sizeof(xcb_randr_get_crtc_info_cookie_t));
		output_cookie = calloc(num_outputs, sizeof(xcb_randr_get_output_info_cookie_t));

		if (!crtc_cookie || !output_cookie) {
			fputs("could not allocate memory for RANDR request cookies\n", stderr);
			break;
		}

		/* CRTC requests */
		for (j = 0; j < num_crtcs; ++j)
			crtc_cookie[j] = xcb_randr_get_crtc_info(conn, rr_crtc[i][j],  0);

		/* Output requests */
		for (j = 0; j < num_outputs; ++j)
			output_cookie[j] = xcb_randr_get_output_info(conn, output[j],  0);

		/* Room for the replies */
		rr_crtc_info[i] = calloc(num_crtcs, sizeof(xcb_randr_get_crtc_info_reply_t*));
		rr_out[i] = calloc(num_outputs, sizeof(xcb_randr_get_output_info_reply_t*));

		if (!rr_crtc_info[i] || !rr_out[i]) {
			fputs("could not allocate memory for RANDR data\n", stderr);
			break;
		}

		/* Actually get the replies. */
		for (j = 0; j < num_crtcs; ++j) {
			rr_crtc_info[i][j] = xcb_randr_get_crtc_info_reply(conn, crtc_cookie[j], &err);
			if (err) {
				fprintf(stderr, "error getting info for CRTC %d on screen %d -- %d\n", j, i,
					err->error_code);
				free(err);
				err = NULL;
				continue;
			}
		}

		for (j = 0; j < num_outputs; ++j) {
			rr_out[i][j] = xcb_randr_get_output_info_reply(conn, output_cookie[j], &err);
			if (err) {
				fprintf(stderr, "error getting info for output %d on screen %d -- %d\n", j, i,
					err->error_code);
				free(err);
				err = NULL;
				continue;
			}
		}

		free(output_cookie);
		free(crtc_cookie);
	}
	/* Xinerama */
	if (xine_active) {
		xine_reply = xcb_xinerama_query_screens_reply(conn, xine_cookie, &err);
		if (err) {
			fprintf(stderr, "error getting info about Xinerama screens -- %d \n",
				err->error_code);
			free(err);
			err = NULL;
			xine_active = 0;
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
		/* XRANDR information */
		if (rr_res[i]) {
			puts("\tXRandR:");
			const xcb_randr_get_screen_resources_reply_t *rr = rr_res[i];
			for (int c = 0; c < rr->num_crtcs; ++c) {
				const xcb_randr_get_crtc_info_reply_t *rrc = rr_crtc_info[i][c];
				if (rrc->num_outputs < 1)
					continue;

				uint16_t w = rrc->width;
				uint16_t h = rrc->height;

				uint16_t rot = (rrc->rotation & 0x0f);
				int rotated = ((rot == XCB_RANDR_ROTATION_ROTATE_90) || (rot == XCB_RANDR_ROTATION_ROTATE_270));

				/* Note that we proceed here differently than with Xlib: instead of getting
				 * per-CRTC output info, we got per-Screen outputs, so now we iterate over all
				 * Screen outputs and find the ones that match this CRTC
				 */
				for (int o = 0; o < rr->num_outputs; ++o) {
					const xcb_randr_get_output_info_reply_t *rro = rr_out[i][o];
					if (rro->crtc != rr_crtc[i][c])
						continue;

					uint32_t mmw = rotated ? rro->mm_height : rro->mm_width;
					uint32_t mmh = rotated ? rro->mm_width : rro->mm_height;

					/* NOTE: rr_name is NOT for us to free. It's also not guaranteed to be
					 * NULL-terminated, so we copy it to our own string */
					const uint8_t *rr_name = xcb_randr_get_output_info_name(rro);
					char *name = calloc(rro->name_len + 1, sizeof(char));
					memcpy(name, rr_name, rro->name_len);
					print_dpi_randr(name, mmw, mmh, w, h, rotated, rro->connection);
					free(name);
				}
			}
		}
	}

	if (xine_active) {
		/* Xinerama info */
		xcb_xinerama_screen_info_iterator_t iter = xcb_xinerama_query_screens_screen_info_iterator(xine_reply);
		int num_xines = iter.rem;
		if (num_xines > 0)
			puts("Xinerama screens:");
		for (int i = 0; i < num_xines; ++i, xcb_xinerama_screen_info_next(&iter)) {
			const xcb_xinerama_screen_info_t *xi = iter.data;
			printf("\t%u: %ux%u pixels, no dpi information\n",
				i,
				xi->width,
				xi->height);
		}
	}

cleanup:
	free(screen_data);
	free(rr_cookie);
	for (i = 0; i < count; ++i) {
		const xcb_randr_get_screen_resources_reply_t *rr = rr_res[i];
		for (int o = 0; o < rr->num_outputs; ++o)
			free(rr_out[i][o]);
		for (int c = 0; c < rr->num_crtcs; ++c)
			free(rr_crtc_info[i][c]);
		free(rr_out[i]);
		free(rr_crtc_info[i]);
		free(rr_res[i]);
	}
	free(rr_out);
	free(rr_crtc_info);
	free(rr_crtc);
	free(rr_res);
	free(xine_reply);
}

int xcb_dpi(void)
{
	int ret = 0;
	xcb_connection_t *conn = xcb_connect(NULL, NULL);
	if ((ret = xcb_connection_has_error(conn))) {
		fputs("XCB connection error\n", stderr);
		return 1;
	}

	puts("** xcb interfaces");

	do_xcb_dpi(conn);

	xcb_disconnect(conn);
	return ret;
}
#endif

int main(int argc, char *argv[])
{
	/* TODO support CLI options for help or output format selection */
	(void)argc;
	(void)argv;

	puts("*** Resolution and dot pitch information exposed by X11 ***");

	xlib_dpi();

#if WITH_XCB
	xcb_dpi();
#endif

	puts("*** Done ***");
}
