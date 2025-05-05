#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <errno.h>
#include <sys/timerfd.h>
#include <sys/inotify.h>
#include <sys/stat.h>

#define MAX_APPS 20
#define STR_MAX 200

struct app_time {
	char name[STR_MAX];
	struct timespec time;
	unsigned int time_ms;
};

struct mapping {
	char *appid;
	char *title;
	char *appname;
};

struct app_time times[MAX_APPS] = {0};
char curr_appid[STR_MAX], curr_title[STR_MAX];
struct timespec start = {0}, end = {0};
int quit = 0, write_now = 0, paused = 0;

#include "config.h"

void ms_to_str(char *str, unsigned int time_ms) {
	unsigned int time_s;
	int h, m, s;

	if (!str)
		return;

	time_s = time_ms / 1e3;
	if (time_ms - time_s * 1e3 >= 500)
		time_s++;

	h = time_s / 3600;
	m = (time_s - h * 3600) / 60;
	s = time_s - h * 3600 - m * 60;

	if (h == 0 && m == 0)
		sprintf(str, "%ds", s);
	else if (h == 0)
		sprintf(str, "%dm%02ds", m, s);
	else
		sprintf(str, "%dh%02dm%02ds", h, m, s);
}

unsigned int str_to_ms(const char *time) {
	unsigned int time_ms;
	int h, m, s;

	/* sscanf() does not clear the values after unsuccessful attempts */
	if (sscanf(time, "%dh%dm%ds", &h, &m, &s) == 3) {
	} else if (sscanf(time, "%dm%ds", &m, &s) == 2) {
		h = 0;
	} else if (sscanf(time, "%ds", &s) == 1) {
		h = 0;
		m = 0;
	}

	time_ms = (h * 3600 + m * 60 + s) * 1000;

	return time_ms;
}

const char *get_app_name(const char *appid, const char *title) {
	/* perform a match agains the mappings matrix */
	const struct mapping *m;
	int nitems = sizeof(mappings) / sizeof(mappings[0]);
	char *appname = NULL;

	for (m = mappings; m < mappings + nitems; m++) {
		if ((m->appid && strcmp(m->appid, appid) == 0 || !m->appid) &&
				(m->title && strcmp(m->title, title) == 0 || !m->title)) {
			appname = m->appname;
			break;
		}
	}

	return appname ? appname : appid;
}

void get_archive_path(char *path) {
	struct stat st;
	time_t now = time(NULL);
	struct tm lt = *localtime(&now);

	if (stat(archive_dir, &st) == -1)
		mkdir(archive_dir, 0755);

	lt.tm_mday--;
	mktime(&lt);
	sprintf(path, "%s/%04d-%02d-%02d", archive_dir,
			lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

void get_file_contents(char *str, int size, const char *path) {
	FILE *f = fopen(path, "r");

	if (!f || !str)
		return;

	fgets(str, size, f);
	fclose(f);
}

int get_max_appid_len(void) {
	int len, maxlen = 0;

	for (int i = 0; i < MAX_APPS; i++) {
		len = strlen(times[i].name);
		if (len > maxlen)
			maxlen = len;
	}

	return maxlen;
}

int get_next_midnight(void) {
	time_t now = time(NULL), next_midnight;
	struct tm nm = *localtime(&now);

	nm.tm_mday++;
	mktime(&nm); /* fix out of range values; see https://stackoverflow.com/a/4214968 */

	nm.tm_hour = 0;
	nm.tm_min = 0;
	nm.tm_sec = 0;
	next_midnight = mktime(&nm);

	return next_midnight;
}

void increase_app_time(const char *name, unsigned int time_ms) {
	if (paused)
		return;

	for (int i = 0; i < MAX_APPS; i++) {
		if (times[i].name[0] == '\0') {
			/* end of list reached; create new entry */
			strcpy(times[i].name, name);
			times[i].time_ms = time_ms;
			break;
		} else if (strcmp(times[i].name, name) == 0) {
			/* match found; add time to existing value */
			times[i].time_ms += time_ms;
			break;
		}
		/* Note: no new entries are added if the array is full and contains no match */
	}
}

int comp(const void *a, const void *b) {
	return ((struct app_time *) a)->time_ms < ((struct app_time *) b)->time_ms;
}

void write_times(int archive) {
	FILE *f;
	char archive_path[STR_MAX];
	int maxlen = get_max_appid_len();
	unsigned int total_time_ms = 0;
	char fmt_time[10];

	if (archive) {
		get_archive_path(archive_path);
		f = fopen(archive_path, "w");
	} else {
		f = fopen(scrtime_path, "w");
	}

	if (!f)
		return;

	if (maxlen < 5)
		maxlen = 5; /* because of "Total", which is also in the list */

	for (int i = 0; i < MAX_APPS; i++)
		total_time_ms += times[i].time_ms;
	ms_to_str(fmt_time, total_time_ms);
	fprintf(f, "%-*s %9s\n\n", maxlen, "Total", fmt_time);

	qsort(times, MAX_APPS, sizeof(struct app_time), comp);
	for (int i = 0; i < MAX_APPS; i++) {
		if (times[i].name[0] == '\0')
			break;

		ms_to_str(fmt_time, times[i].time_ms);
		fprintf(f, "%-*s %9s\n", maxlen, times[i].name, fmt_time);
	}

	if (!archive) {
		if (quit)
			fprintf(f, "\nProgram exited; screentime recording has stopped\n");
		else if (paused)
			fprintf(f, "\nScreentime recording is currently paused\n");
	}

	fclose(f);
}

void load_times(void) {
	FILE *scrtime_f = fopen(scrtime_path, "r");
	int matched, nnl = 0;
	char buf[STR_MAX], name[STR_MAX], timestr[10];
	unsigned int time_ms;

	if (!scrtime_f)
		return;

	/* discard everything after the second empty line */
	while (fgets(buf, sizeof(buf), scrtime_f) && nnl < 2) {
		if ((matched = sscanf(buf, "%s %s", name, timestr)) == 2) {
			if (strcmp(name, "Total") != 0) {
				time_ms = str_to_ms(timestr);
				increase_app_time(name, time_ms);
			}
		} else if (matched == -1) {
			nnl++;
		}
	}
	fclose(scrtime_f);
}

void start_recording(void) {
	clock_gettime(CLOCK_MONOTONIC_RAW, &start);
	get_file_contents(curr_appid, sizeof(curr_appid), appid_path);
	get_file_contents(curr_title, sizeof(curr_title), title_path);
}

void stop_recording(void) {
	struct timespec diff_ts;
	unsigned int diff_ms;
	const char *appname;

	clock_gettime(CLOCK_MONOTONIC_RAW, &end);
	diff_ts.tv_sec = end.tv_sec - start.tv_sec;
	diff_ts.tv_nsec = end.tv_nsec - start.tv_nsec;
	diff_ms = diff_ts.tv_sec * 1e3 + diff_ts.tv_nsec / 1e6;

	appname = get_app_name(curr_appid, curr_title);
	
	increase_app_time(appname, diff_ms);
}

void compute_timeout(struct itimerspec *it) {
	time_t now, next_midnight;
	int diff;

	now = time(NULL);
	next_midnight = get_next_midnight();
	diff = next_midnight - now + 1; /* +1s to be safe */

	it->it_interval = (struct timespec) { .tv_sec = 0, .tv_nsec = 0 };
	it->it_value = (struct timespec) { .tv_sec = diff, .tv_nsec = 0 };
}

void clear_history(void) {
	for (int i = 0; i < MAX_APPS; i++) {
		times[i].name[0] = '\0';
		times[i].time_ms = 0;
	}
}

void main_loop(void) {
	enum { TIMER, INOTIFY };
	struct pollfd pfds[2];
	struct itimerspec timeout;
	uint64_t expirations;
	int file_watch;
	struct inotify_event iev;
	int ret;

	pfds[TIMER].fd = timerfd_create(CLOCK_BOOTTIME, 0);
	pfds[TIMER].events = POLLIN;

	pfds[INOTIFY].fd = inotify_init();
	/* title gets updated last, so watch title instead of appid
	 * to make sure that both are up-to-date */
	file_watch = inotify_add_watch(pfds[INOTIFY].fd, title_path, IN_CLOSE_WRITE);
	pfds[TIMER].events = POLLIN;

	start_recording();

	while (!quit) {
		compute_timeout(&timeout);
		timerfd_settime(pfds[TIMER].fd, 0, &timeout, NULL);
		ret = poll(pfds, 2, -1);

		if (ret > 0) {
			if (pfds[TIMER].revents & POLLIN) { /* midnight has come */
				read(pfds[TIMER].fd, &expirations, sizeof(expirations));

				stop_recording();
				write_times(1);
				clear_history();
				start_recording();
			} else if (pfds[INOTIFY].revents & POLLIN) {
				read(pfds[INOTIFY].fd, &iev, sizeof(iev));
				if (iev.wd == file_watch && iev.mask == IN_CLOSE_WRITE) {
					/* window focus changed */
					stop_recording();
					start_recording();
				}
			}
		} else if (ret == -1 && errno == EINTR) { /* caught signal */
			if (write_now == 1) {
				stop_recording();
				write_times(0);
				start_recording();
				write_now = 0;
			} else if (paused) {
				stop_recording();
			} else if (!paused) {
				start_recording();
			}
		}
	}

	if (!paused)
		stop_recording();

	write_times(0);
	close(pfds[TIMER].fd);
	close(pfds[INOTIFY].fd);
}

void handler(int signal) {
	if (signal == SIGINT || signal == SIGTERM)
		quit = 1;
	else if (signal == SIGUSR1) /* force a write */
		write_now = 1;
	else if (signal == SIGUSR2) /* pause/resume */
		paused = !paused;
}

int main() {
	signal(SIGINT, handler);
	signal(SIGTERM, handler);
	signal(SIGUSR1, handler);
	signal(SIGUSR2, handler);

	load_times();
	main_loop();
}
