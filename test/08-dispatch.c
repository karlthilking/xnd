/* 07_dispatch.c */
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "common/types.h"
#include "common/xalloc.h"

static double
apply(void)
{
	const size_t N = 16;
	double *results;
	dispatch_queue_t wq;

	results = xmalloc(N * sizeof(*results));
	wq = dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0);

	dispatch_apply(N, wq, ^(size_t i) {
		double local = 0.0f;
		srand48(time(NULL));
		const int rounds = 1 << 24;

		for (int k = 0; k < rounds; k++) {
			local += (double)((i * k) % 97);
			local += (double)(lrand48() % (64 >> 2));
			local -= (double)(lrand48() % (1 << 4));
			local /= 2.0f;
			local -= __builtin_bswap64(lrand48()) % 1024ul;
		}

		results[i] = local;
	});

	double global = 0.0f;
	for (size_t i = 0; i < N; i++)
		global += results[i];

	free(results);
	return global;
}

int
main(int argc, char *argv[])
{
	double result, total = 0.0f;
	const int iterations = 25;

	for (int i = 0; i < iterations; i++) {
		result = apply();
		total += result;
		printf("Global sum #%d: %f\n", i, result);
	}

	printf("Total sum: %f\n", total);
	exit(0);
}
