/*
    voltlogger_oscilloscope
    
    Copyright (C) 2015 Dmitry Yu Okunev <dyokunev@ut.mephi.ru> 0x8E30679C
    
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 
*/

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>	/* free()	*/
#include <string.h>
#include <unistd.h>
//#include <fcntl.h>
#include <gtk/gtk.h>
#include <pthread.h>
#include <errno.h>

#include "configuration.h"
#include "binary.h"
#include "malloc.h"

FILE *sensor;
FILE *dump;

#define HISTORY_SIZE (1 << 20)
#define MAX_CHANNELS 1
#define Y_BITS 12

typedef struct {
	uint64_t timestamp;
	uint32_t value;
} history_t;

int running = 1;

history_t  *history       [MAX_CHANNELS];
uint32_t    history_length[MAX_CHANNELS] = {0};
uint64_t    ts_global = 0;

pthread_mutex_t history_mutex;

enum trigger_mode {
	TG_RISE,
	TG_FALL,
};

double x_userdiv    = 1.9E-3;
double x_useroffset = 0;
double y_userscale  = 4;
double y_useroffset = -0.14;

int trigger_start_mode = TG_RISE;
int trigger_start_y    = 695;
int trigger_end_mode   = TG_RISE;
int trigger_end_y      = 695;

void
sensor_open()
{
	sensor = stdin;

	return;
}

int
sensor_fetch(history_t *p)
{
	uint16_t ts_local;
	uint16_t ts_local_old;
	uint16_t value;

	ts_local = get_uint16(sensor);
	value    = get_uint16(sensor);

	ts_local_old = ts_global & 0xffff;
	if (ts_local <= ts_local_old) {
		ts_global += 1<<16;
	}

	ts_global = (ts_global & ~0xffff) | ts_local;

	p->timestamp = ts_global;
	p->value     = value;

	return 1;
}

void
sensor_close()
{
	return;
}

void
dump_open(char *dumppath, char tailonly)
{
	dump = stdin;

	if (dumppath == NULL)
		return;

	if (*dumppath == 0)
		return;

	if (!strcmp(dumppath, "-"))
		return;

	dump = fopen(dumppath, "r");
	if (dump == NULL) {
		fprintf(stderr, "Cannot open file \"%s\": %s", dumppath, strerror(errno));
		abort ();
	}

	if (tailonly) {
		fseek(dump, 0, SEEK_END);
	}

	return;
}

int
dump_fetch(history_t *p)
{
	uint64_t ts_parse;
	uint64_t ts_device;
	uint32_t value;

	ts_parse  = get_uint64(dump);
	while (ts_parse < 1437900000000000000 || ts_parse > 1537900000000000000) {
		printf("dump_fetch() correction 0\n");
		get_uint8(dump);
		ts_parse  = get_uint64(dump);
	}

	ts_device = get_uint64(dump);

	value     = get_uint32(dump);
	while (value < 0 || value > 1024) {
		printf("dump_fetch() correction 1\n");
		value     = get_uint32(dump);
	}

	(void)ts_parse;
	p->timestamp = ts_device;
	p->value     = value;

	//printf("V: %lu %lu %u\n", ts_parse, ts_device, value);
	//sleep(1);

	return 1;
}

void
dump_close()
{
	return;
}

void
history_flush()
{
	//sleep(3600);
	pthread_mutex_lock(&history_mutex);
	memcpy(history[0], &history[0][HISTORY_SIZE], sizeof(*history[0])*HISTORY_SIZE);
	history_length[0] = HISTORY_SIZE;

	printf("history_flush()\n");
	pthread_mutex_unlock(&history_mutex);
	return;
}

void *
history_fetcher(void *arg)
{
	while (running) {
		//sensor_fetch(&history[0][ history_length[0]++ ]);
		dump_fetch(&history[0][ history_length[0]++ ]);
//		printf("%lu; %u\n", history[0][ history_length[0]-1].timestamp, history[0][ history_length[0]-1].value);
		if (history_length[0] >= HISTORY_SIZE * 2) 
			history_flush();
	}

	return NULL;
}

static gboolean
cb_expose (GtkWidget      *area,
           GdkEventExpose *event,
           gpointer       *data)
{
	int width, height;
	cairo_t *cr;

	cr	= gdk_cairo_create (event->window);
	width	= gdk_window_get_width (event->window);
	height	= gdk_window_get_height (event->window);

	cairo_rectangle(cr, 0, 0, width, height);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_fill(cr);

	cairo_set_line_width (cr, 2);

	int history_end   = history_length[0]-2;

	if (history_end >= (double)HISTORY_SIZE*x_userdiv) {
		//printf("%u %u\n", HISTORY_SIZE, history_end);
		pthread_mutex_lock(&history_mutex);
		//cairo_set_source_rgba (cr, 0, 0, 0.3, 0.8);
		//cairo_set_source_rgba (cr, 0.8, 0.8, 1, 0.8);
		cairo_set_source_rgba (cr, 0.1, 1, 0.1, 0.8);
		cairo_move_to(cr, -1, height/2);
		int x;
		int y;

		//int history_start = history_end - HISTORY_SIZE;
		int history_start_initial = history_end - (double)HISTORY_SIZE*x_userdiv;
		int history_start = history_start_initial;

		if (history_start == 0)
			history_start++;

		//printf("h: %u %u\n", history_start, history_end);

		char found;

		found = 0;
		while (history_start < history_end && !found) {
			history_t *cur  = &history[0][history_start];
			history_t *prev = &history[0][history_start-1];
			//printf("1 %u %u\n", cur->value, prev->value);
			switch (trigger_start_mode) {
				case TG_FALL:
					found = (cur->value >= trigger_start_y && prev->value <  trigger_start_y);
					break;
				case TG_RISE:
					found = (cur->value <  trigger_start_y && prev->value >= trigger_start_y);
					break;
			}

			if (found)
				break;

			history_start++;
		}

		if (history_start >= history_end) {
			printf("Unable to sync start\n");
			history_start = history_start_initial;
		}

		int history_end_initial = history_end;
		history_end--;

		found = 0;
		while (history_start < history_end && !found) {
			history_t *cur  = &history[0][history_end];
			history_t *prev = &history[0][history_end+1];
			//printf("2 %u %u\n", cur->value, prev->value);
			switch (trigger_end_mode) {
				case TG_RISE:
					found = (cur->value >= trigger_end_y && prev->value <  trigger_end_y);
					break;
				case TG_FALL:
					found = (cur->value <  trigger_end_y && prev->value >= trigger_end_y);
					break;
			}

			if (found)
				break;

			history_end--;
		}

		if (history_start >= history_end) {
			printf("Unable to sync end\n");
			history_end = history_end_initial;
		}

		//printf("H: %u %u\n", history_start, history_end);

		uint64_t timestamp_start = history[0][history_start].timestamp;
		uint64_t timestamp_end   = history[0][history_end  ].timestamp;

		int history_cur = history_start;

		if (timestamp_start == timestamp_end) {
			printf("%lu %lu %u %u %u %u\n", timestamp_start, timestamp_end, history_start, history_end, history[0][history_start].value, history[0][history_end].value);
		}
		assert (timestamp_end != timestamp_start);

		double x_scale = (double)width  / (timestamp_end - timestamp_start);
		double y_scale = (double)height / (1 << Y_BITS);

		while (history_cur < history_end) {
			history_t *p = &history[0][ history_cur++ ];

			x = (double)x_useroffset*width              + (double)x_scale               * (double)(p->timestamp - timestamp_start);
			y = (double)height/2 + (double)y_useroffset*y_userscale*height + (double)y_scale * y_userscale * (double) p->value;
			cairo_line_to(cr, x, y);
			//printf("xy: %u (%lu %e) %u (%u %e %e)\n", x, p->timestamp - timestamp_start, x_scale, y, p->value, y_scale, y_userscale);
		}
		cairo_stroke(cr);
		pthread_mutex_unlock(&history_mutex);
	}

	//cairo_set_source_rgba (cr, 0, 0, 0, 0.2);
	cairo_set_source_rgba (cr, 1, 1, 1, 0.2);
	cairo_move_to(cr, 0,	 height/2);
	cairo_line_to(cr, width, height/2);
	cairo_stroke(cr);

	cairo_destroy (cr);
	return TRUE;
}

void *
update (void *arg)
{
	GtkWidget *area = arg;

	while (running) {
		gtk_widget_queue_draw(area);
		usleep(AUTOUPDATE_USECS);
	}

	return NULL;
}

int
main (int    argc,
      char **argv)
{
	int i;
	pthread_t thread_fetcher;
	pthread_t thread_autoupdate;
	char *dumppath = NULL;
	char tailonly = 0;
	//sensor_open();

	// Parsing arguments
	char c;
	while ((c = getopt (argc, argv, "i:tf")) != -1) {
		char *arg;
		arg = optarg;

		switch (c)
		{
			case 'i':
				dumppath = arg;
				break;
			case 't':
				tailonly = 1;
				break;
			default:
				abort ();
		}
	}


	i = 0;
	while (i < MAX_CHANNELS)
		history[i++] = xcalloc(HISTORY_SIZE * 2 + 1, sizeof(history_t));


	dump_open(dumppath, tailonly);

	if (pthread_create(&thread_fetcher, NULL, history_fetcher, NULL)) {
		fprintf(stderr, "Error creating thread\n");
		return 1;
	}

	GtkWidget *main_window,
	          *vbox,
	          *button,
	          *area;

	gtk_init (&argc, &argv);
	main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (main_window), 800, 600);
	g_signal_connect (main_window, "destroy", gtk_main_quit, NULL);
	vbox = gtk_vbox_new (FALSE, 5);
	gtk_container_add (GTK_CONTAINER (main_window), vbox);
	button = gtk_button_new_from_stock (GTK_STOCK_REFRESH);
	gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
	area = gtk_drawing_area_new ();
	g_signal_connect (area, "expose-event", G_CALLBACK (cb_expose), NULL);
	gtk_box_pack_start (GTK_BOX (vbox), area, TRUE, TRUE, 0);
	g_signal_connect_swapped (button, "clicked",
	                          G_CALLBACK (gtk_widget_queue_draw), area);
	gtk_widget_show_all (main_window);

	if (pthread_create(&thread_autoupdate, NULL, update, area)) {
		fprintf(stderr, "Error creating thread\n");
		return 1;
	}

	gtk_main ();

	running = 0;

	if (pthread_join(thread_autoupdate, NULL)) {
		fprintf(stderr, "Error joining thread\n");
		return 2;
	}
	if (pthread_join(thread_fetcher, NULL)) {
		fprintf(stderr, "Error joining thread\n");
		return 2;
	}

//	sensor_close();
	dump_close();
	i = 0;
	while (i < MAX_CHANNELS)
		free(history[i++]);
	return 0;
}

