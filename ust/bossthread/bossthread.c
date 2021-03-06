/*
 * bossthread.c
 *
 *	Creates 4 threads with increasing amount of computation. Start and
 *	join is done in increasing order of computation amount. One UST event
 *	is recorded at the beginning and the end of the processing.
 *
 *  Created on: 2011-02-18
 *      Author: francis
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "calibrate.h"

#define TRACEPOINT_DEFINE
#include "bossthread_tp_provider.h"

#define MAX_THREAD 4

struct worker_data {
	unsigned long int count;
	unsigned long int res;
};

void *worker(void *data) {
	struct worker_data *info = (struct worker_data *) data;
	int i;
	pthread_t self = pthread_self();
	tracepoint(bossthread, id, self);

	for(i=0; i<info->count; i++) {
		info->res++;
	}

	tracepoint(bossthread, id, self);
	return 0;
}

int main(int argc, char **argv) {
	pthread_t threads[MAX_THREAD];
	void *status;
	int i = 0;
	struct worker_data *data[MAX_THREAD];
	unsigned long max;

	// count up to 10ms
	max = calibrate(10000);

	for(i=0;i<MAX_THREAD;i++) {
		data[i] = calloc(1, sizeof(struct worker_data));
		data[i]->count = max * (i + 1);
	}

	for(i=0; i<MAX_THREAD; i++) {
		pthread_create(&threads[i], NULL, worker, (void *) data[i]);
	}

	for(i=0; i<MAX_THREAD; i++) {
		pthread_join(threads[i], &status);
	}

	return 0;
}
