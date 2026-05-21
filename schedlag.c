#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/*
 * schedlag - measure scheduler wakeup latency
 *
 * Repeatedly sleeps for TARGET_NS nanoseconds and measures how late
 * each wakeup is. Late wakeups mean the scheduler is under load or
 * the system is resource-starved.
 *
 * Usage: schedlag [duration_seconds]
 */

#define TARGET_NS   10000000LL  /* 10 ms target sleep */
#define MAX_SAMPLES 100000
#define NUM_BUCKETS 24

static double samples[MAX_SAMPLES];

static int cmp_dbl(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

static double percentile(double *sorted, int n, double pct) {
    int idx = (int)((n - 1) * pct / 100.0);
    if (idx < 0)     idx = 0;
    if (idx >= n)    idx = n - 1;
    return sorted[idx];
}

int main(int argc, char *argv[]) {
    int duration = 10;
    if (argc > 1) {
        duration = atoi(argv[1]);
        if (duration < 1) duration = 1;
    }

    int max_samples = duration * (int)(1e9 / TARGET_NS);
    if (max_samples > MAX_SAMPLES) max_samples = MAX_SAMPLES;

    printf("Measuring scheduler latency for %d second%s "
           "(%d samples at %.0f ms intervals)...\n",
           duration, duration == 1 ? "" : "s",
           max_samples, TARGET_NS / 1e6);

    struct timespec req = {.tv_sec = 0, .tv_nsec = TARGET_NS};
    int    count            = 0;
    double max_overshoot_us = 0.0;
    double sum_us           = 0.0;

    for (int i = 0; i < max_samples; i++) {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);
        clock_gettime(CLOCK_MONOTONIC, &t1);

        long long elapsed_ns =
            (t1.tv_sec  - t0.tv_sec)  * 1000000000LL +
            (t1.tv_nsec - t0.tv_nsec);
        long long overshoot_ns = elapsed_ns - TARGET_NS;
        if (overshoot_ns < 0) overshoot_ns = 0;

        double us = overshoot_ns / 1000.0;
        samples[count++] = us;
        sum_us += us;
        if (us > max_overshoot_us) max_overshoot_us = us;
    }

    qsort(samples, count, sizeof(double), cmp_dbl);

    double avg  = sum_us / count;
    double p50  = percentile(samples, count, 50.0);
    double p90  = percentile(samples, count, 90.0);
    double p99  = percentile(samples, count, 99.0);
    double p999 = percentile(samples, count, 99.9);

    printf("\n--- Scheduler Latency (overshoot beyond %g ms target) ---\n",
           TARGET_NS / 1e6);
    printf("Samples : %d\n", count);
    printf("Average : %8.1f us\n", avg);
    printf("Max     : %8.1f us\n", max_overshoot_us);
    printf("p50     : %8.1f us\n", p50);
    printf("p90     : %8.1f us\n", p90);
    printf("p99     : %8.1f us\n", p99);
    printf("p99.9   : %8.1f us\n", p999);

    /* ASCII histogram - range covers 0..p99*2 with overflow bucket */
    double range = p99 * 2.0;
    if (range < 500.0) range = 500.0; /* at least 500 us range */

    int    buckets[NUM_BUCKETS] = {0};
    int    overflow = 0;
    double bucket_width = range / NUM_BUCKETS;

    for (int i = 0; i < count; i++) {
        int b = (int)(samples[i] / bucket_width);
        if (b >= NUM_BUCKETS) overflow++;
        else                  buckets[b]++;
    }

    int max_b = 1;
    for (int i = 0; i < NUM_BUCKETS; i++)
        if (buckets[i] > max_b) max_b = buckets[i];

    printf("\n--- Distribution (us) ---\n");
    for (int i = 0; i < NUM_BUCKETS; i++) {
        double lo = i * bucket_width;
        double hi = (i + 1) * bucket_width;
        int    bar = buckets[i] * 50 / max_b;
        printf("%7.0f-%7.0f us |", lo, hi);
        for (int j = 0; j < bar; j++) putchar('#');
        printf(" %d\n", buckets[i]);
    }
    if (overflow > 0)
        printf("      > %7.0f us | %d samples\n", range, overflow);

    return EXIT_SUCCESS;
}
