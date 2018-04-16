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
#include <xcb/xcb_xrm.h>
#endif

/* The DPI reported by the core protocol, possibly with an Xft.dpi
 * override. One per screen.
 */
float *reference_dpi;

#define STRMAX 1024
struct named_dpi
{
	int dpi;
	char name[STRMAX+1];
};

/* The DPI reported by RANDR for each output or monitor.
 * One per output per screen
 */
int *noutput;
struct named_dpi **output_dpi;
int *nmon;
struct named_dpi **monitor_dpi;

static int print_dpi_common(int w, int h, int mmw, int mmh)
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

	return ydpi ? ydpi : xdpi;
}

static int print_dpi_screen(int i, int width, int height, int mmw, int mmh)
{
	printf("Screen %d: %dx%d pixels, %dx%d mm: ", i, width, height, mmw, mmh);
	return print_dpi_common(width, height, mmw, mmh);
}

static int print_dpi_randr(const char *name,
	unsigned long mmw, unsigned long mmh, int w, int h,
	int rotated, int primary, int connection)
{
	const char * connection_string = (connection == RR_Connected ?
		"connected" : (connection == RR_Disconnected ?
			"disconnected" : (connection == RR_UnknownConnection ?
				"unknown" : "?")));
	printf("\t\t%s (%s%s, %s): %dx%d pixels, %lux%lu mm: ",
		name ? name : "<error>",
		(rotated ? "R" : "U"),
		(primary ? ", primary" : ""),
		connection_string,
		w, h,
		mmw, mmh);
	return print_dpi_common(w, h, mmw, mmh);
}

static int print_dpi_monitor(const char *name, int width, int height, int mmw, int mmh, Bool prim, Bool automatic)
{
	/* TODO FIXME the monitor interface does not provide a way to tell if
	 * the monitor is rotated or not. A possible ways to determine this
	 * would be to fetch the associated outputs and check if any/all are
	 * rotated. This requires multiple roundtrips, and one is left to
	 * wonder what should be done if one of the outputs is rotated
	 * and the other is not. Pending further clarifications on the matter,
	 * we determine if the output is rotated or not simply by comparing
	 * the relative magnitude of width/height with that of mmw/mmh.
	 */

	const int rotated = ((width > height) != (mmw > mmh));
	if (rotated) {
		int t = mmw;
		mmw = mmh;
		mmh = t;
	}

	char info[STRMAX+1] = {0};
	snprintf(info, STRMAX, " (%s%s%s)",
		(rotated ? "R" : "U"),
		(prim ? ", primary" : ""),
		(automatic ? ", automatic" : ""));

	printf("\t\t%s%s: %dx%d pixels, %dx%d mm: ",
		name ? name : "<error>",
		info, width, height, mmw, mmh);
	return print_dpi_common(width, height, mmw, mmh);
}


static int do_xlib_dpi(Display *disp)
{
	int num_screens = ScreenCount(disp);

	reference_dpi = calloc(num_screens, sizeof(*reference_dpi));
	noutput = calloc(num_screens, sizeof(*noutput));
	nmon = calloc(num_screens, sizeof(*nmon));
	output_dpi = calloc(num_screens, sizeof(*output_dpi));
	monitor_dpi = calloc(num_screens, sizeof(*monitor_dpi));

	int scratch = 0;
	const Bool has_randr = XRRQueryExtension(disp, &scratch, &scratch);
	int rr_major = 0, rr_minor = 0;
	Bool has_randr_primary = False;
	Bool has_randr_monitor = True;
	if (has_randr) {
		XRRQueryVersion(disp, &rr_major, &rr_minor);
		has_randr_primary = (rr_major > 1 || rr_minor >= 3);
		has_randr_monitor = (rr_major > 1 || rr_minor >= 5);
	}

	/* Iterate over all screens, and show X11 and XRandR information */
	for (int i = 0 ; i < num_screens ; ++i) {
		Screen *screen = ScreenOfDisplay(disp, i);
		Window root_win = RootWindowOfScreen(screen);

		/* Standard X11 information */
		{
			int width = WidthOfScreen(screen);
			int height = HeightOfScreen(screen);

			int mmw = WidthMMOfScreen(screen);
			int mmh = HeightMMOfScreen(screen);

			reference_dpi[i] = print_dpi_screen(i, width, height, mmw, mmh);
		}

		if (!has_randr)
			continue;

		/* XRandR information */
		XRRScreenResources *xrr_res = XRRGetScreenResources(disp, root_win);

		if (!xrr_res)
			continue; /* no XRR resources */

		printf("\tXRandR (%d.%d):\n", rr_major, rr_minor);

		RROutput primary = -1;
		if (has_randr_primary)
			primary = XRRGetOutputPrimary(disp, root_win);

		output_dpi[i] = calloc(
			(noutput[i] = xrr_res->noutput),
			sizeof(**output_dpi));

		/* iterate over all outputs, and compute the DPIs from the connected CRTC */
		for (int o = 0; o < xrr_res->noutput; ++o) {
			XRROutputInfo *rro = XRRGetOutputInfo(disp, xrr_res, xrr_res->outputs[o]);

			/* Use negative dpi to mark the output as disconnected --will be overwritten
			 * if it turns out to be connected. An output with negative dpi will
			 * be skipped when printing scaling factors */
			output_dpi[i][o].dpi = -1;

			if (rro->crtc) {
				XRRCrtcInfo *rrc = XRRGetCrtcInfo(disp, xrr_res, rro->crtc);

				unsigned int w = rrc->width;
				unsigned int h = rrc->height;

				Rotation rot = (rrc->rotation & 0x0f);
				int rotated = ((rot == RR_Rotate_90) || (rot == RR_Rotate_270));

				unsigned long mmw = rotated ? rro->mm_height : rro->mm_width;
				unsigned long mmh = rotated ? rro->mm_width : rro->mm_height;

				strncpy(output_dpi[i][o].name, rro->name, STRMAX);
				output_dpi[i][o].dpi = print_dpi_randr(rro->name, mmw, mmh, w, h,
					rotated, xrr_res->outputs[o] == primary,
					rro->connection);

				XRRFreeCrtcInfo(rrc);
			}
			XRRFreeOutputInfo(rro);
		}
		XRRFreeScreenResources(xrr_res);

		/* Monitors were introduced in RANDR 1.5 */
		if (has_randr_monitor) {
			XRRMonitorInfo *monitors = XRRGetMonitors(disp, root_win, True, nmon + i);
			if (nmon[i] > 0) {
				puts("\tMonitors:");
				monitor_dpi[i] = calloc(nmon[i], sizeof(**monitor_dpi));

				XRRMonitorInfo *mon = monitors;
				for (int m = 0; m < nmon[i]; ++m, ++mon) {
					/* Note that width/height follow the monitor rotation,
					 * but mwidth/mheight don't!
					 */
					char *name = XGetAtomName(disp, mon->name);
					strncpy(monitor_dpi[i][m].name, name, STRMAX);
					monitor_dpi[i][m].dpi = print_dpi_monitor(name,
						mon->width, mon->height,
						mon->mwidth, mon->mheight,
						mon->primary, mon->automatic);
					free(name);
				}
			}
			XRRFreeMonitors(monitors);
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
			float xft_dpi;
			puts("X resources:");
			printf("\tXft.dpi: %s\n", dpi);
			xft_dpi = strtof(dpi, NULL);
			/* Override core DPI only if valid */
			if (xft_dpi > 0)
				reference_dpi[i] = xft_dpi;
		}
	}
	return num_screens;
}

static int xlib_dpi(void)
{
	puts("** Xlib interfaces");

	Display *disp = XOpenDisplay(getenv("DISPLAY"));
	if (!disp) {
		fputs("Could not open X display\n", stderr);
		return 0;
	}

	int num_screens = do_xlib_dpi(disp);

	XCloseDisplay(disp);

	return num_screens;
}

#if WITH_XCB
static void do_xcb_dpi(xcb_connection_t *conn)
{
	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(xcb_get_setup(conn));
	xcb_generic_error_t *err = NULL;

	const xcb_query_extension_reply_t *xine_query = xcb_get_extension_data(conn, &xcb_xinerama_id);
	const xcb_query_extension_reply_t *randr_query = xcb_get_extension_data(conn, &xcb_randr_id);

	int xine_active = xine_query->present;
	int randr_active = randr_query->present;
	int has_randr_primary = 0;
	int has_randr_monitors = 0;

	int count = 0, i, j;

	/* Collect information first, then show. This should make the async
	 * requests such as those for RANDR faster.
	 */
	xcb_screen_t *screen_data =
		malloc(iter.rem*sizeof(*screen_data));

	xcb_xinerama_query_screens_cookie_t xine_cookie;
	xcb_xinerama_query_screens_reply_t *xine_reply = NULL;

	uint32_t rr_major = 0, rr_minor = 0;

	xcb_randr_query_version_cookie_t rr_ver_cookie;
	xcb_randr_query_version_reply_t *rr_ver_rep = NULL;
	if (randr_active)
		rr_ver_cookie = xcb_randr_query_version(conn, 1, 5);

	xcb_randr_get_screen_resources_cookie_t *rr_cookie = randr_active ?
		malloc(iter.rem*sizeof(*rr_cookie)) : NULL;
	xcb_randr_get_screen_resources_reply_t **rr_res = randr_active ?
		calloc(iter.rem, sizeof(*rr_res)) : NULL;

	xcb_randr_get_output_primary_cookie_t *rr_primary_cookie = randr_active ?
		malloc(iter.rem*sizeof(*rr_primary_cookie)) : NULL;
	xcb_randr_get_output_primary_reply_t **rr_primary_reply = randr_active ?
		calloc(iter.rem, sizeof(*rr_primary_reply)) : NULL;

	xcb_randr_crtc_t **rr_crtc = randr_active ?
		calloc(iter.rem, sizeof(*rr_crtc)) : NULL;
	xcb_randr_output_t **rr_output = randr_active ?
		calloc(iter.rem, sizeof(*rr_output)) : NULL;
	xcb_randr_get_crtc_info_reply_t ***rr_crtc_info = randr_active ?
		calloc(iter.rem, sizeof(*rr_crtc_info)) : NULL;
	xcb_randr_get_output_info_reply_t ***rr_out = randr_active ?
		calloc(iter.rem, sizeof(*rr_out)) : NULL;

	/* Monitors require RANDR 1.5 */
	if (randr_active) {
		rr_ver_rep = xcb_randr_query_version_reply(conn, rr_ver_cookie, &err);
		if (err) {
			fprintf(stderr, "error getting Xinerama status -- %d\n", err->error_code);
			free(err);
			randr_active = 0;
		} else {
			rr_major = rr_ver_rep->major_version;
			rr_minor = rr_ver_rep->minor_version;
			if (rr_major > 1 || rr_minor >= 3)
				has_randr_primary = 1;
			if (rr_major > 1 || rr_minor >= 5)
				has_randr_monitors = 1;
		}
	}

	xcb_randr_get_monitors_cookie_t *rr_mon_cookie = has_randr_monitors ?
		malloc(iter.rem*sizeof(*rr_cookie)) : NULL;
	xcb_randr_get_monitors_reply_t **rr_mon = has_randr_monitors ?
		malloc(iter.rem*sizeof(*rr_mon)) : NULL;

	if (!screen_data) {
		fputs("could not allocate memory for screen data\n", stderr);
		goto cleanup;
	}
	if (randr_active && !(rr_cookie && rr_res && rr_crtc && rr_crtc_info && rr_out)) {
		fputs("could not allocate memory for RANDR data\n", stderr);
		goto cleanup;
	}
	if (has_randr_primary && !(rr_primary_cookie && rr_primary_reply)) {
		fputs("could not allocate memory for RANDR primary output\n", stderr);
		goto cleanup;
	}
	if (has_randr_monitors && !(rr_mon_cookie && rr_mon)) {
		fputs("could not allocate memory for RANDR monitor data\n", stderr);
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
		if (has_randr_primary)
			rr_primary_cookie[count] = xcb_randr_get_output_primary(conn,
				iter.data->root);
		if (has_randr_monitors)
			rr_mon_cookie[count] = xcb_randr_get_monitors(conn, iter.data->root, 1);
	}

	/* Xinerama */
	if (xine_active)
		xine_cookie = xcb_xinerama_query_screens(conn);

	/** Get the actual data **/
	/* RANDR */
	if (randr_active) for (i = 0; i < count; ++i) {
		int num_crtcs = 0;
		int num_outputs = 0;

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

		if (has_randr_primary) {
			rr_primary_reply[i] = xcb_randr_get_output_primary_reply(conn,
				rr_primary_cookie[i], &err);
			if (err) {
				fprintf(stderr, "error getting primary output for screen %d -- %d\n", i,
					err->error_code);
				free(err);
				err = NULL;
				randr_active = 0;
			}
		}

		if (!randr_active)
			break;


		num_crtcs = xcb_randr_get_screen_resources_crtcs_length(rr_res[i]);
		num_outputs = xcb_randr_get_screen_resources_outputs_length(rr_res[i]);

		/* Get the first crtc and output. We store the CRTC to match it to the output
		 * later on. NOTE that this is not for us to free. */
		rr_crtc[i] = xcb_randr_get_screen_resources_crtcs(rr_res[i]);
		rr_output[i] = xcb_randr_get_screen_resources_outputs(rr_res[i]);

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
			output_cookie[j] = xcb_randr_get_output_info(conn, rr_output[i][j],  0);

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

		if (has_randr_monitors) {
			rr_mon[i] = xcb_randr_get_monitors_reply(conn, rr_mon_cookie[i], &err);
			if (err) {
				fprintf(stderr, "error getting monitors list on screen %d -- %d\n", i,
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
		xcb_randr_output_t primary = -1;
		if (has_randr_primary)
			primary = rr_primary_reply[i]->output;

		const xcb_screen_t *screen = screen_data + i;
		/* Standard X11 information */
		{
			print_dpi_screen(i,
				screen->width_in_pixels, screen->height_in_pixels,
				screen->width_in_millimeters, screen->height_in_millimeters);
		}
		/* XRANDR information */
		if (randr_active && rr_res[i]) {
			printf("\tXRandR (%d.%d):\n", rr_major, rr_minor);
			const xcb_randr_get_screen_resources_reply_t *rr = rr_res[i];
			for (int o = 0; o < rr->num_outputs; ++o) {
				const xcb_randr_get_output_info_reply_t *rro = rr_out[i][o];
				if (rro->crtc) {
					int c = 0;
					while (c < rr->num_crtcs) {
						if (rr_crtc[i][c] == rro->crtc)
							break;
						++c;
					}
					if (c < rr->num_crtcs) {
						const xcb_randr_get_crtc_info_reply_t *rrc = rr_crtc_info[i][c];
						uint16_t w = rrc->width;
						uint16_t h = rrc->height;

						uint16_t rot = (rrc->rotation & 0x0f);
						int rotated = ((rot == XCB_RANDR_ROTATION_ROTATE_90) || (rot == XCB_RANDR_ROTATION_ROTATE_270));

						uint32_t mmw = rotated ? rro->mm_height : rro->mm_width;
						uint32_t mmh = rotated ? rro->mm_width : rro->mm_height;

						/* NOTE: rr_name is NOT for us to free. It's also not guaranteed to be
						 * NULL-terminated, so we copy it to our own string */
						const uint8_t *rr_name = xcb_randr_get_output_info_name(rro);
						char *name = calloc(rro->name_len + 1, sizeof(char));
						if (name) memcpy(name, rr_name, rro->name_len);
						print_dpi_randr(name, mmw, mmh, w, h,
							rotated,
							primary == rr_output[i][o],
							rro->connection);
						free(name);
					}
				}
			}

			if (has_randr_monitors) {
				puts("\tMonitors:");
				xcb_randr_monitor_info_iterator_t rr_mon_iter =
					xcb_randr_get_monitors_monitors_iterator(rr_mon[i]);
				while (rr_mon_iter.rem) {
					const xcb_randr_monitor_info_t *mon = rr_mon_iter.data;
					/* TODO get atom names all at once? */
					const xcb_get_atom_name_cookie_t name_cookie = xcb_get_atom_name(conn, mon->name);
					xcb_get_atom_name_reply_t *name_rep = xcb_get_atom_name_reply(conn, name_cookie, &err);
					char *name = NULL;
					if (err) {
						fprintf(stderr, "error getting atom name -- %d \n",
							err->error_code);
						free(err);
						err = NULL;
					} else {
						size_t name_l = xcb_get_atom_name_name_length(name_rep);
						name = calloc(name_l+1, sizeof(char));
						if (name) memcpy(name, xcb_get_atom_name_name(name_rep), name_l);
					}
					print_dpi_monitor(name,
						mon->width, mon->height,
						mon->width_in_millimeters, mon->height_in_millimeters,
						mon->primary, mon->automatic);
					free(name);
					free(name_rep);
					xcb_randr_monitor_info_next(&rr_mon_iter);
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

	/* Xft.dpi */
	xcb_xrm_database_t *xrmdb = xcb_xrm_database_from_default(conn);
	if (xrmdb) {
		char *dpi = NULL;
		xcb_xrm_resource_get_string(xrmdb, "Xft.dpi", NULL, &dpi);
		if (dpi) {
			puts("X resources:");
			printf("\tXft.dpi: %s\n", dpi);
		}
		free(dpi);
		xcb_xrm_database_free(xrmdb);
	}

cleanup:
	free(screen_data);
	free(rr_cookie);
	if (randr_active) for (i = 0; i < count; ++i) {
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
	free(rr_output);
	free(rr_crtc);
	free(rr_res);
	free(rr_ver_rep);
	free(rr_primary_cookie);
	if (has_randr_primary) for (i = 0; i < count; ++i) {
		free(rr_primary_reply[i]);
	}
	free(rr_primary_reply);
	if (has_randr_monitors) for (i = 0; i < count; ++i) {
		free(rr_mon[i]);
	}
	free(rr_mon);
	free(rr_mon_cookie);
	free(xine_reply);
}

/* TODO FIXME this returns an error value, while xlib_dpi returns the number of screens
 * (for use by print_scaling_factors)
 */
static int xcb_dpi(void)
{
	puts("** xcb interfaces");
	int ret = 0;
	xcb_connection_t *conn = xcb_connect(NULL, NULL);
	if ((ret = xcb_connection_has_error(conn))) {
		fputs("XCB connection error\n", stderr);
	} else {
		do_xcb_dpi(conn);

		xcb_disconnect(conn);
	}
	return ret;
}
#endif

struct scaling_factor
{
	int min;
	float actual;
	int round;
	int max;
};

static inline
struct scaling_factor calc_scaling(float actual)
{
	struct scaling_factor ret = {
		.min = (int)floor(actual),
		.actual = actual,
		.round = (int)round(actual),
		.max = (int)ceil(actual)
	};

	if (ret.min < 1) ret.min = 1;
	if (ret.round < 1) ret.round = 1;
	if (ret.max < 1) ret.max = 1;

	return ret;
}

static inline
void print_scaling_factor(struct scaling_factor scaling)
{
	printf("%d %.2g %d %d",
		scaling.min, scaling.actual, scaling.round, scaling.max);
}

void print_scaling_factors(int num_screens)
{
	for (int i = 0; i < num_screens; ++i) {
		printf("Screen %d:\n", i);
		float reference = reference_dpi[i]/96.0f;
		printf("\treference scaling: ");
		print_scaling_factor(calc_scaling(reference));
		puts("");

		if (nmon[i]) {
			/* TODO FIXME we assume that the first enumerated monitor is the primary one,
			 * we should keep its index around */
			int primary_dpi = monitor_dpi[i][0].dpi;
			printf("\tmonitors:\n");
			for (int m = 0; m < nmon[i]; ++m) {
				printf("\t\t%s:\n", monitor_dpi[i][m].name);
				int dpi = monitor_dpi[i][m].dpi;
				float native = dpi/96.0f;
				float rated = (reference*dpi)/primary_dpi;
				printf("\t\t\tnative: ");
				print_scaling_factor(calc_scaling(native));
				printf("\n\t\t\tprorated: ");
				print_scaling_factor(calc_scaling(rated));
				puts("");
			}
		}
		free(monitor_dpi[i]);

		if (noutput[i]) {
			/* TODO FIXME we assume that the first enumerated output is the primary one,
			 * we should keep its index around */
			int primary_dpi = output_dpi[i][0].dpi;
			printf("\toutputs:\n");
			for (int o = 0; o < noutput[i]; ++o) {
				int dpi = output_dpi[i][o].dpi;
				if (dpi < 0) continue; /* output is not connected */
				printf("\t\t%s:\n", output_dpi[i][o].name);
				float native = dpi/96.0f;
				float rated = (reference*dpi)/primary_dpi;
				printf("\t\t\tnative: ");
				print_scaling_factor(calc_scaling(native));
				printf("\n\t\t\tprorated: ");
				print_scaling_factor(calc_scaling(rated));
				puts("");
			}
		}
		free(output_dpi[i]);
	}
	free(monitor_dpi);
	free(output_dpi);
	free(reference_dpi);
}

static const char* dpi_related_vars[] = {
	"CLUTTER_SCALE",
	"GDK_SCALE",
	"GDK_DPI_SCALE",
	"QT_AUTO_SCREEN_SCALE_FACTOR",
	"QT_SCALE_FACTOR",
	"QT_SCREEN_SCALE_FACTORS",
	"QT_DEVICE_PIXEL_RATIO", /* obsolete */
	NULL
};

void print_relevant_env()
{
	for (const char * const*var = dpi_related_vars; *var; ++var) {
		char *v = getenv(*var);
		if (v)
			printf("%s=%s\n", *var, v);
	}
}


int main(int argc, char *argv[])
{
	/* TODO support CLI options for help or output format selection */
	(void)argc;
	(void)argv;

	puts("*** Resolution and dot pitch information exposed by X11 ***");

	int num_screens = xlib_dpi();

#if WITH_XCB
	xcb_dpi();
#endif

	puts("*** Auto-computed per-output scaling ***");

	print_scaling_factors(num_screens);

	puts("*** Environment variables ***");

	print_relevant_env();

	puts("*** Done ***");
}
