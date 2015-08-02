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
#define MAX_REAL_CHANNELS 7
#define MAX_MATH_CHANNELS 3
#define Y_BITS 12

#define GLADE_PATH "oscilloscope.glade"

typedef struct {
	uint64_t timestamp;
	uint32_t value[MAX_REAL_CHANNELS];
} history_t;

int running = 1;


GtkBuilder *builder;
history_t  *history;
uint32_t    history_length = 0;
uint64_t    ts_global = 0;

pthread_mutex_t history_mutex;

enum trigger_mode {
	TG_RISE,
	TG_FALL,
};

double x_userdiv    = 0.95E-3;
double x_useroffset = 0;
double y_userscale [MAX_REAL_CHANNELS + MAX_MATH_CHANNELS];
double y_useroffset[MAX_REAL_CHANNELS + MAX_MATH_CHANNELS];

int trigger_channel	= 0;
int trigger_start_mode	= TG_RISE;
int trigger_start_y	= 675;
int trigger_end_mode	= TG_RISE;
int trigger_end_y	= 675;

int channelsNum		= 1;
int mathChannelsNum	= 0;

double line_colors[MAX_REAL_CHANNELS + MAX_MATH_CHANNELS][3] = {{1}};

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

	ts_local = get_uint16(sensor);

	ts_local_old = ts_global & 0xffff;
	if (ts_local <= ts_local_old) {
		ts_global += 1<<16;
	}

	ts_global = (ts_global & ~0xffff) | ts_local;

	p->timestamp = ts_global;

	int i = 0;
	while (i < channelsNum)
		p->value[i++] = get_uint16(sensor);

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
	} else {
		fseek(dump, 0, SEEK_SET);
	}
	//fprintf(stderr, "Pos: %li\n", ftell(dump));

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
		printf("dump_fetch() correction 0: %lu %li\n", ts_parse, ftell(dump));
		get_uint8(dump);
		ts_parse = get_uint64(dump);
	}

	ts_device = get_uint64(dump);

	int i = 0;
	while (i < channelsNum) {
		p->value[i] = get_uint32(dump);
		while (value < 0 || value > 1024) {
			printf("dump_fetch() correction 1\n");
			p->value[i] = get_uint32(dump);
		}
		i++;
	}

	(void)ts_parse;
	p->timestamp = ts_device;

	//fprintf(stderr, "%u\n", p->value[0]);

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
	memcpy(history, &history[HISTORY_SIZE], sizeof(*history)*HISTORY_SIZE);
	history_length = HISTORY_SIZE;

	printf("history_flush()\n");
	pthread_mutex_unlock(&history_mutex);
	return;
}

void *
history_fetcher(void *arg)
{
	//fprintf(stderr, "history_fetcher\n");

	while (running) {
		//sensor_fetch(&history[0][ history_length[0]++ ]);
		dump_fetch(&history[ history_length++ ]);
		//printf("%lu; %u\n", history[ history_length-1].timestamp, history[ history_length-1].value[0]);
		if (history_length >= HISTORY_SIZE * 2) 
			history_flush();
	}

	return NULL;
}

void
cb_scale_changed (
		GtkRange *range,
		gpointer  user_data)
{
	double *scale_value = user_data;

	*scale_value = gtk_range_get_value (range);
	return;
}

void
cb_offset_changed (
		GtkRange *range,
		gpointer  user_data)
{
	double *offset_value = user_data;

	*offset_value = gtk_range_get_value (range);
	return;
}

void
cb_resize (
		GtkWindow *window, 
		GdkEvent *event,
		gpointer data)
{
	int width, height;
	char offsetwidgetname[] = "offset_chanX";

	width  = event->configure.width;
	height = event->configure.height;

	GtkWidget *offset;

	int chan = 0;
	while (chan < MAX_REAL_CHANNELS + MAX_MATH_CHANNELS) {
		offsetwidgetname[11] = chan++ + '0';

		offset = GTK_WIDGET ( gtk_builder_get_object(builder, offsetwidgetname) );
		if (offset == NULL)
			continue;

		gtk_widget_set_size_request (offset, 10, height);
	}

	printf("%d, %d\n", width, height);
}

static gboolean
cb_draw (GtkWidget	*area,
         cairo_t	*cr,
         gpointer	*data)
{
	int width, height;

	GdkWindow *areaGdkWindow = gtk_widget_get_window(area);

	assert (areaGdkWindow != NULL);

	//gtk_window_get_size (gtk_widget_get_window(area), &width, &height);
	width	= gdk_window_get_width  (areaGdkWindow);
	height	= gdk_window_get_height (areaGdkWindow);

	//fprintf(stderr, "%i %i\n", width, height);

	cairo_rectangle(cr, 0, 0, width, height);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_fill(cr);

	cairo_set_line_width (cr, 2);

	int history_end   = history_length-2;

	if (history_end >= (double)HISTORY_SIZE*x_userdiv) {
		//printf("%u %u\n", HISTORY_SIZE, history_end);
		pthread_mutex_lock(&history_mutex);
		//cairo_set_source_rgba (cr, 0, 0, 0.3, 0.8);
		//cairo_set_source_rgba (cr, 0.8, 0.8, 1, 0.8);
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
			history_t *cur  = &history[history_start];
			history_t *prev = &history[history_start-1];
			//printf("1 %u %u\n", cur->value, prev->value);
			switch (trigger_start_mode) {
				case TG_FALL:
					found = (cur->value[trigger_channel] >= trigger_start_y && prev->value[trigger_channel] <  trigger_start_y);
					break;
				case TG_RISE:
					found = (cur->value[trigger_channel] <  trigger_start_y && prev->value[trigger_channel] >= trigger_start_y);
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
			history_t *cur  = &history[history_end];
			history_t *prev = &history[history_end+1];
			//printf("2 %u %u\n", cur->value, prev->value);
			switch (trigger_end_mode) {
				case TG_RISE:
					found = (cur->value[trigger_channel] >= trigger_end_y && prev->value[trigger_channel] <  trigger_end_y);
					break;
				case TG_FALL:
					found = (cur->value[trigger_channel] <  trigger_end_y && prev->value[trigger_channel] >= trigger_end_y);
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

		uint64_t timestamp_start = history[history_start].timestamp;
		uint64_t timestamp_end   = history[history_end  ].timestamp;

		if (timestamp_start == timestamp_end) {
			printf("%lu %lu %u %u %u %u\n", timestamp_start, timestamp_end, history_start, history_end, history[history_start].value[0], history[history_end].value[0]);
		}
		assert (timestamp_end != timestamp_start);

		double x_scale = (double)width  / (timestamp_end - timestamp_start);
		double y_scale = (double)height / (1 << Y_BITS);

		int chan = 0;
		while (chan < channelsNum) {
			int history_cur = history_start;
			cairo_set_source_rgba (cr, line_colors[chan][0], line_colors[chan][1], line_colors[chan][2], 0.8);
			cairo_move_to(cr, -1, height/2);
			while (history_cur < history_end) {
				history_t *p = &history[ history_cur++ ];

				x = (double)x_useroffset*width              + (double)x_scale               * (double)(p->timestamp - timestamp_start);
				y = (double)height/2 + (double)y_useroffset[chan]*y_userscale[chan]*height - (double)y_scale * y_userscale[chan] * (double) p->value[chan];
				cairo_line_to(cr, x, y);
				//printf("xy: %u (%lu %e) %u (%u %e %e)\n", x, p->timestamp - timestamp_start, x_scale, y, p->value, y_scale, y_userscale);
			}
			cairo_stroke(cr);

			chan++;
		}

		chan = 0;
		while (chan < mathChannelsNum) {
			int history_cur = history_start;
			cairo_set_source_rgba (cr, line_colors[MAX_REAL_CHANNELS + chan][0], line_colors[MAX_REAL_CHANNELS + chan][1], line_colors[MAX_REAL_CHANNELS + chan][2], 0.5);
			cairo_move_to(cr, -1, height/2);
			while (history_cur < history_end) {
				history_t *p = &history[ history_cur++ ];

				x = (double)x_useroffset*width              + (double)x_scale               * (double)(p->timestamp - timestamp_start);
				y = (double)height/2 + (double)y_useroffset[chan]*y_userscale[chan]*height - (double)y_scale * y_userscale[chan] * (double) p->value[chan*2] * (p->value[chan*2] + 1);
				cairo_line_to(cr, x, y);
				//printf("xy: %u (%lu %e) %u (%u %e %e)\n", x, p->timestamp - timestamp_start, x_scale, y, p->value, y_scale, y_userscale);
			}
			cairo_stroke(cr);

			chan++;
		}
		pthread_mutex_unlock(&history_mutex);
	}

	//cairo_set_source_rgba (cr, 0, 0, 0, 0.2);
	cairo_set_source_rgba (cr, 1, 1, 1, 0.2);
	cairo_move_to(cr, 0,	 height/2);
	cairo_line_to(cr, width, height/2);
	cairo_stroke(cr);

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
	pthread_t thread_fetcher;
	pthread_t thread_autoupdate;
	char *dumppath = NULL;
	char tailonly = 0;
	//sensor_open();

	history = xcalloc(HISTORY_SIZE * 2 + 1, sizeof(history_t));


	GtkWidget *main_window,
	          *button,
	          *area;

	gtk_init (&argc, &argv);

	// Parsing arguments
	char c;
	while ((c = getopt (argc, argv, "i:tfC:M:")) != -1) {
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
			case 'C':
				channelsNum = atoi(arg);
				break;
			case 'M':
				mathChannelsNum = atoi(arg);
				break;
			default:
				abort ();
		}
	}


	assert ( channelsNum     < MAX_REAL_CHANNELS );
	assert ( mathChannelsNum < MAX_MATH_CHANNELS );

	dump_open(dumppath, tailonly);

	if (pthread_create(&thread_fetcher, NULL, history_fetcher, NULL)) {
		fprintf(stderr, "Error creating thread\n");
		return 1;
	}

	builder = gtk_builder_new();

	GError *gerr = NULL;

	if ( gtk_builder_add_from_file (builder, GLADE_PATH, &gerr) <= 0 ) {
		fprintf(stderr, "Cannot parse file \""GLADE_PATH"\": %s\n", gerr->message);
		return 3;
	}
	main_window = GTK_WIDGET ( gtk_builder_get_object (builder, "mainwindow") );
	assert ( main_window != NULL );

	//main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	//gtk_window_set_default_size (GTK_WINDOW (main_window), 800, 600);

	g_signal_connect (main_window, "destroy", gtk_main_quit, NULL);
	//button = gtk_button_new_from_stock (GTK_STOCK_REFRESH);
	button = GTK_WIDGET ( gtk_builder_get_object(builder, "refresh") );

	assert (button != NULL);

	area = GTK_WIDGET ( gtk_builder_get_object(builder, "oscillogram") );

	assert (area != NULL);

	g_signal_connect (area, "draw",   G_CALLBACK (cb_draw), NULL);
	//g_signal_connect (area, "resize", G_CALLBACK (cb_resize), main_window);
	g_signal_connect (area, "configure-event", G_CALLBACK(cb_resize), NULL);

	g_signal_connect_swapped (button, "clicked",
	                          G_CALLBACK (gtk_widget_queue_draw), area);
	gtk_widget_show_all (main_window);

	if (pthread_create(&thread_autoupdate, NULL, update, area)) {
		fprintf(stderr, "Error creating thread\n");
		return 1;
	}

	line_colors[0][0] = 1;
	line_colors[0][1] = 0;
	line_colors[0][2] = 0;

	line_colors[1][0] = 0;
	line_colors[1][1] = 1;
	line_colors[1][2] = 0;

	line_colors[2][0] = 0;
	line_colors[2][1] = 0;
	line_colors[2][2] = 1;

	line_colors[3][0] = 1;
	line_colors[3][1] = 1;
	line_colors[3][2] = 0;

	line_colors[4][0] = 1;
	line_colors[4][1] = 0;
	line_colors[4][2] = 1;

	line_colors[5][0] = 0;
	line_colors[5][1] = 1;
	line_colors[5][2] = 1;

	line_colors[6][0] = 0.5;
	line_colors[6][1] = 0.5;
	line_colors[6][2] = 0.5;

	line_colors[7][0] = 1;
	line_colors[7][1] = 1;
	line_colors[7][2] = 1;

	line_colors[8][0] = 1;
	line_colors[8][1] = 1;
	line_colors[9][2] = 1;

	line_colors[9][0] = 1;
	line_colors[9][1] = 1;
	line_colors[9][2] = 1;

#if MAX_REAL_CHANNELS + MAX_MATH_CHANNELS < 10
	#error MAX_REAL_CHANNELS + MAX_MATH_CHANNELS < 10
#endif

	{
		char offsetwidgetname[] = "offset_chanX";
		char scalewidgetname[]  = "scale_chanX";
		int chan = 0;
		while (chan < MAX_REAL_CHANNELS + MAX_MATH_CHANNELS) {

			y_userscale[chan]  = 2;
			y_useroffset[chan] = 0.14;

			offsetwidgetname[11] = chan + '0';
			GtkRange *offset = GTK_RANGE ( gtk_builder_get_object(builder, offsetwidgetname) );
			scalewidgetname [10] = chan + '0';
			GtkRange *scale  = GTK_RANGE ( gtk_builder_get_object(builder, scalewidgetname) );

			if (offset == NULL) {
				chan++;
				continue;
			}

			gtk_range_set_range(offset, -1 , 1);
			gtk_range_set_value(offset, 0.14);
			gtk_range_set_increments(offset, 0.05, 0.5);

			gtk_range_set_range(scale, 1 , 10);
			gtk_range_set_value(scale, 2);

			g_signal_connect (offset, "value-changed", G_CALLBACK (cb_offset_changed), &y_useroffset[chan]);
			g_signal_connect (scale,  "value-changed", G_CALLBACK (cb_scale_changed),  &y_userscale[chan]);
			chan++;
		}
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
	free(history);
	return 0;
}

