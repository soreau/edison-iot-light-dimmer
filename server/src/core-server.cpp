/*
 * Copyright (c) 2016 Scott Moreau
 */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <mraa/gpio.h>
#include "ttp223.hpp"

using namespace std;

extern "C" {
#define SOCK_PATH "/tmp/socket"
#define MAX_BRIGHTNESS 0x7F

enum {
	OFF = 0,
	LOW,
	MEDIUM,
	HIGH
};

static int levels[4] = {0, 75, 96, 127};

struct dimmer_data {
	mraa_gpio_context m_gpio[8];
	pthread_mutex_t mcu_write_mutex;
	int update_touch;
	int enabled;
	int done;
	int v;
};

static void *
write_mcu_data(struct dimmer_data *pd, int v)
{
	pthread_mutex_lock(&pd->mcu_write_mutex);
	mraa_gpio_write(pd->m_gpio[0], v & 0x01 ? 1 : 0);
	mraa_gpio_write(pd->m_gpio[1], v & 0x02 ? 1 : 0);
	mraa_gpio_write(pd->m_gpio[2], v & 0x04 ? 1 : 0);
	mraa_gpio_write(pd->m_gpio[3], v & 0x08 ? 1 : 0);
	mraa_gpio_write(pd->m_gpio[4], v & 0x10 ? 1 : 0);
	mraa_gpio_write(pd->m_gpio[5], v & 0x20 ? 1 : 0);
	mraa_gpio_write(pd->m_gpio[6], v & 0x40 ? 1 : 0);
	mraa_gpio_write(pd->m_gpio[7], 1);
	pthread_mutex_unlock(&pd->mcu_write_mutex);
	usleep(20000);
	mraa_gpio_write(pd->m_gpio[7], 0);
}

static void *
set_value(void *data)
{
	struct dimmer_data *pd = (struct dimmer_data *) data;
	if (pd->enabled)
		write_mcu_data(pd, pd->v);
	else
		write_mcu_data(pd, 0);
}
} /* extern "C" */

static void *
monitor_touch_sensor(void *data)
{
	struct dimmer_data *pd = (struct dimmer_data *) data;
	upm::TTP223* touch = new upm::TTP223(4);
	struct timespec t1, t2, t3, t4;
	pthread_t write_thread = -1;
	long int elapsed;
	int touched = 0;

	while(!pd->done) {
		if (touch->isPressed()) {
			if (!touched) {
				int i;
				int level = OFF;
				if (!pd->enabled)
					level = LOW;
				else if (pd->v == levels[HIGH])
					level = OFF;
				else if (pd->v == 0)
					level = LOW;
				else {
					for (i = 0; i < 4; i++) {
						if (levels[i] > pd->v) {
							level = i;
							break;
						}
					}
				}
				touched = 1;
				pd->update_touch = 1;
				pd->v = levels[level];
				pd->enabled = (level != OFF);
				if (write_thread != -1)
					pthread_join(write_thread, NULL);
				pthread_create(&write_thread, 0, &set_value, pd);
				cout << "touched: " << level << ", " << pd->v << endl;
				memset(&t1, 0, sizeof(struct timespec));
				clock_gettime(CLOCK_MONOTONIC, &t1);
				memcpy(&t2, &t1, sizeof(struct timespec));
			} else {
				elapsed = (t2.tv_sec - t1.tv_sec) * 1000000;
				elapsed += (t2.tv_nsec - t1.tv_nsec) / 1000;
				if (elapsed > 500000) {
					memset(&t4, 0, sizeof(struct timespec));
					clock_gettime(CLOCK_MONOTONIC, &t4);
					elapsed = (t4.tv_sec - t3.tv_sec) * 1000000;
					elapsed += (t4.tv_nsec - t3.tv_nsec) / 1000;
					if (elapsed > 100000 && pd->enabled && pd->v > 0) {
						pd->v--;
						cout << "decremented: " << elapsed << endl;
						pd->update_touch = 1;
						if (write_thread != -1)
							pthread_join(write_thread, NULL);
						pthread_create(&write_thread, 0, &set_value, pd);
						memset(&t3, 0, sizeof(struct timespec));
						clock_gettime(CLOCK_MONOTONIC, &t3);
					}
				} else {
					memset(&t2, 0, sizeof(struct timespec));
					clock_gettime(CLOCK_MONOTONIC, &t2);
					memcpy(&t3, &t2, sizeof(struct timespec));
				}
			}
		} else {
			if (touched) {
				touched = 0;
				cout << "released" << endl;
			}
		}
		usleep(5000);
	}

	delete touch;
}

extern "C" {
int
main(void)
{
	struct dimmer_data data;
	pthread_t write_thread = -1;
	pthread_t touch_thread;
	int i, s, s2, t, len;
	struct sockaddr_un local, remote;
	fd_set sock;
	char buf[100], *tmp;

	mraa_init();

	/* initialize D6-13 for use as digital pin outputs */
	for (i = 0; i < 8; i++) {
		data.m_gpio[i] = mraa_gpio_init(i + 6);
		mraa_gpio_use_mmaped(data.m_gpio[i], 1);
		mraa_gpio_dir(data.m_gpio[i], MRAA_GPIO_OUT);
		mraa_gpio_write(data.m_gpio[i], 0);
	}

	pthread_mutex_init(&data.mcu_write_mutex, NULL);
	data.update_touch = 0;
	data.enabled = 0;
	data.done = 0;
	data.v = 0;

	pthread_create(&touch_thread, 0, &monitor_touch_sensor, &data);

	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}

	local.sun_family = AF_UNIX;
	strcpy(local.sun_path, SOCK_PATH);
	unlink(local.sun_path);
	len = strlen(local.sun_path) + sizeof(local.sun_family);
	if (bind(s, (struct sockaddr *)&local, len) == -1) {
		perror("bind");
		exit(1);
	}

	if (listen(s, 5) == -1) {
		perror("listen");
		exit(1);
	}

	while (1) {
		int n;
		printf("Waiting for a connection...\n");
		t = sizeof(remote);
		if ((s2 = accept(s, (struct sockaddr *)&remote, (socklen_t *)&t)) == -1) {
			perror("accept");
			exit(1);
		}

		printf("Client connected.\n");

		while (1) {
			int len, v, i, ready;
			memset(buf, 0, sizeof(buf));
			struct timeval tv;
			tv.tv_sec = 0;
			tv.tv_usec = 5000;
			FD_ZERO(&sock);
			FD_SET(s2, &sock);
			ready = select(s2 + 1, &sock, NULL, NULL, &tv);
			if (ready == -1) {
				perror("select");
				break;
			}
			if (ready) {
				n = recv(s2, buf, 100, 0);
				if (n <= 0) {
					perror("recv");
					break;
				}
				if (!strncmp(buf, "Hello", 5)) {
					printf("%s", buf);
					char out[15];
					sprintf(out, "toggle:t0:%d\n", data.enabled);
					if (send(s2, out, strlen(out), 0) < 0) {
						perror("send");
					}
					sprintf(out, "range:r0:%d\n", data.v);
					if (send(s2, out, strlen(out), 0) < 0) {
						perror("send");
					}
				}
				len = strlen(buf);
				for (i = len - 2; buf[i] != '\n' && i >= 0; i--);
				tmp = &buf[i + 1];
				len = strlen(tmp);
				if (len > 3 && tmp[2] == ':') {
					int u = 0;
					if (!strncmp(tmp, "t0", 2)) {
						v = strtol(&tmp[3], NULL, 10);
						data.enabled = v;
						pthread_create(&write_thread, 0, &set_value, &data);
					} else if (!strncmp(tmp, "r0", 2)) {
						v = strtol(&tmp[3], NULL, 10);
						data.v = v;
						if (data.enabled)
							pthread_create(&write_thread, 0, &set_value, &data);
					}
				}
			} else if (data.update_touch) {
				char out[15];
				sprintf(out, "toggle:t0:%d\n", data.enabled);
				if (send(s2, out, strlen(out), 0) < 0) {
					perror("send");
				}
				sprintf(out, "range:r0:%d\n", data.v);
				if (send(s2, out, strlen(out), 0) < 0) {
					perror("send");
				}
				data.update_touch = 0;
			}
			if (write_thread != -1)
				pthread_join(write_thread, NULL);
		}

		close(s2);
	}

	close(s);
	close(s2);

	for (i = 0; i < 5; i++) {
		mraa_gpio_write(data.m_gpio[i], 0);
		mraa_gpio_close(data.m_gpio[i]);
	}

	data.done = 0;
	pthread_join(touch_thread, NULL);
	pthread_mutex_destroy(&data.mcu_write_mutex);

	return 0;
}
} /* extern "C" */
