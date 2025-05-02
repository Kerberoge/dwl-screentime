#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/stat.h>

#define MAX_APPS 20
#define STR_MAX 20


struct app_time {
	char name[STR_MAX];
	struct timespec time;
	unsigned int time_ms;
};

const char *appid_path = "/tmp/dwl/appid";
const char *scrtime_path = "/tmp/screentime";
const char *archive_path = "/home/" USER "/.local/share/screentime";
struct app_time times[MAX_APPS] = {0};
char curr_appid[STR_MAX];
struct timespec start = {0}, end = {0};
time_t next_midnight;


void increase_app_time(const char *appid, unsigned int time_ms) {
	for (int i = 0; i < MAX_APPS; i++) {
		if (times[i].name[0] == '\0') {
			/* end of list reached; create new entry */
			strcpy(times[i].name, appid);
			times[i].time_ms = time_ms;
			break;
		} else if (strcmp(times[i].name, appid) == 0) {
			/* match found; add time to existing value */
			times[i].time_ms += time_ms;
			break;
		}
		/* Note: no new entries are added if the array is full and contains no match */
	}
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

int comp(const void *a, const void *b) {
	return ((struct app_time *) a)->time_ms < ((struct app_time *) b)->time_ms;
}

void ms_to_str(char *str, const unsigned int time_ms) {
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

void get_file_contents(char *str, const char *path) {
	FILE *f = fopen(path, "r");

	if (!f || !str)
		return;

	fgets(str, STR_MAX, f);
	fclose(f);
}

void write_times(const char *path) {
	FILE *f = fopen(path, "w");
	int maxlen = get_max_appid_len();
	unsigned int total_time_ms = 0;
	char fmt_time[10];

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
	fclose(f);
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

void save_times(void) {
	/* saves yesterday's times in the archive */
	struct stat st;
	time_t now = time(NULL);
	struct tm lt = *localtime(&now);
	char path[100];

	if (stat(archive_path, &st) == -1)
		mkdir(archive_path, 0755);

	lt.tm_mday--;
	mktime(&lt);
	sprintf(path, "%s/%04d-%02d-%02d", archive_path,
			lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);

	write_times(path);
}

void app_switch(void) {
	char new_appid[STR_MAX];
	struct timespec diff_ts;
	unsigned int diff_ms;
	time_t now = time(NULL);

	get_file_contents(new_appid, appid_path);

	if (strcmp(new_appid, "none") == 0)
		strcpy(new_appid, "Desktop");

	clock_gettime(CLOCK_MONOTONIC_RAW, &end);

	if (start.tv_sec > 0 && start.tv_nsec > 0) { /* ignore first run */
		diff_ts.tv_sec = end.tv_sec - start.tv_sec;
		diff_ts.tv_nsec = end.tv_nsec - start.tv_nsec;
		diff_ms = diff_ts.tv_sec * 1e3 + diff_ts.tv_nsec / 1e6;
		
		increase_app_time(curr_appid, diff_ms);
	}

	clock_gettime(CLOCK_MONOTONIC_RAW, &start);
	strcpy(curr_appid, new_appid);
	
	if (now >= next_midnight) {
		next_midnight = get_next_midnight();
		save_times(); /* this implies a call to write_times() */

		for (int i = 0; i < MAX_APPS; i++) {
			times[i].name[0] = '\0';
			times[i].time_ms = 0;
		}
	} else {
		write_times(scrtime_path);
	}
}

void handler(int signal) {
	/* force a write */
	app_switch();
}

void load_times(void) {
	FILE *scrtime_f = fopen(scrtime_path, "r");
	char appid[STR_MAX], timestr[10];
	unsigned int time_ms;

	if (!scrtime_f)
		return;

	while (fscanf(scrtime_f, "%s %s", appid, timestr) > 0) {
		if (strcmp(appid, "Total") != 0) {
			time_ms = str_to_ms(timestr);
			increase_app_time(appid, time_ms);
		}
	}
	fclose(scrtime_f);
}

void watch_file(const char *path, void (*callback)()) {
	int fd = inotify_init();
	int file_watch = inotify_add_watch(fd, path, IN_CLOSE_WRITE);
	struct inotify_event event;

	while (read(fd, &event, sizeof(event))) {
		callback();
	}

	close(fd);
}

int main() {
	signal(SIGUSR1, handler);

	next_midnight = get_next_midnight();
	load_times();
	watch_file(appid_path, app_switch);
}
