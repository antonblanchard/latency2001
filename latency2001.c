/*
 * latency benchmark
 *
 * Copyright (C) 2014 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <signal.h>
#include <malloc.h>
#include <sys/mman.h>
#include "support.h"

static unsigned long cacheline_size = 128;

/*
 * POWER7:
 * L1-D 32 kB
 * L2 256 kB
 * L3 4 MB
 * 64 entry DERAT - 4MB with 64kB pages
 * 512 entry TLB - 32MB with 64kB pages, 8GB with 16M pages
 *
 * POWER8:
 * L1-D 64 kB
 * L2 512 kB
 * L3 8 MB
 * 48 entry DERAT - 3MB with 64kB pages
 * 2048 entry TLB - 128MB with 64kB pages, 32GB with 16M pages
 */

volatile bool finished;
void sigalrm_handler(int junk)
{
	finished = true;
}

static long end = -1UL;

static void *prepare_cache(char *c, unsigned long slots, unsigned long skip,
			   int endofline)
{
	unsigned long lfsr_bits;
	unsigned int offset = 0;
	unsigned long i;
	long *prev_ptr = &end;
	unsigned long lfsr = 0x1;

	lfsr_bits = 1;
	while ((1 << lfsr_bits) < slots)
		lfsr_bits++;

	if (endofline)
		offset = skip - sizeof(long);

	for (i = 0; i < (slots-1); i++) {
		long *d;

again:
		lfsr = mylfsr(lfsr_bits, lfsr);
		if (lfsr >= slots)
			goto again;

		d = (long *)&c[lfsr * skip + offset];

		assert(*d == 0);
		*d = (long)prev_ptr;
		prev_ptr = d;
	}

	return prev_ptr;
}

static void *prepare_tlb(char *c, unsigned long slots, unsigned long skip,
			 int endofline)
{
	unsigned int offset = 0;
	unsigned long i;
	long *prev_ptr = &end;

	if (endofline)
		offset = skip - sizeof(long);

	for (i = 0; i < (slots-1); i++) {
		long *d;

		d = (long *)&c[i * skip + (i * cacheline_size) % skip + offset];

		assert(*d == 0);
		*d = (long)prev_ptr;
		prev_ptr = d;
	}

	return prev_ptr;
}

static void *prepare_sequential(char *c, unsigned long slots,
				unsigned long skip, int endofline)
{
	unsigned int offset = 0;
	unsigned long i;
	long *prev_ptr = &end;

	if (endofline)
		offset = skip - sizeof(long);

	for (i = 0; i < (slots-1); i++) {
		long *d;

		d = (long *)&c[i * skip + offset];

		assert(*d == 0);
		*d = (long)prev_ptr;
		prev_ptr = d;
	}

	return prev_ptr;
}

enum type {
	TLB,
	SEQUENTIAL,
	LFSR,
};

static void doit(unsigned long size, unsigned long skip, enum type type,
		 unsigned long time, int endofline, char *c, int csv)
{
	unsigned long slots;
	long *start;
	struct timespec before, after;
	unsigned long elapsed;
	unsigned long before_tb, elapsed_tb;
	unsigned long iterations = 0;
	float cycles, ns;

	slots = size / skip;

	switch (type) {
	case TLB:
		start = prepare_tlb(c, slots, skip, endofline);
		break;

	case SEQUENTIAL:
		start = prepare_sequential(c, slots, skip, endofline);
		break;

	case LFSR:
		start = prepare_cache(c, slots, skip, endofline);
		break;
	}

	finished = false;

	signal(SIGALRM, sigalrm_handler);
	alarm(time);

	clock_gettime(CLOCK_MONOTONIC, &before);
	before_tb = mftb();

	while (!finished) {
		long *p = start;

		do {
			p = (long *)*p;
			p = (long *)*p;
			p = (long *)*p;
			p = (long *)*p;
			p = (long *)*p;
			p = (long *)*p;
			p = (long *)*p;
			p = (long *)*p;
		} while ((long)p != -1);

		asm volatile("" : "=&r"(p)); /* Don't optimise loop away */

		iterations++;
	}

	elapsed_tb = mftb() - before_tb;
	clock_gettime(CLOCK_MONOTONIC, &after);

	elapsed = (after.tv_sec - before.tv_sec) * 1000000000UL +
			(after.tv_nsec - before.tv_nsec);

	if (elapsed < 100 * 1000)
		fprintf(stderr, "WARNING, test ran for under 1us\n");

	cycles = (float)elapsed_tb * timebase_multiplier / iterations / slots;
	ns = (float)elapsed / iterations / slots;

	if (csv)
		printf("%ld,%.2f,%.2f\n", size, cycles, ns);
	else
		printf("%11ld %9.2f cycles %9.2f ns\n", size, cycles, ns);

	signal(SIGALRM, SIG_DFL);
}

void *alloc_small_mem(size_t size)
{
	void *addr;
	unsigned long length;

        length = ALIGN_UP(size, getpagesize());

        addr = mmap(NULL, length, PROT_READ|PROT_WRITE,
                    MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);

        if (addr == MAP_FAILED) {
                perror("mmap");
                exit(1);
        }

	if (madvise(addr, length, MADV_NOHUGEPAGE)) {
                perror("madvise(MADV_NOHUGEPAGE)");
                exit(1);
	}

	return addr;
}

void free_small_mem(void *addr, size_t size)
{
	munmap(addr, size);
}

static void usage(void)
{
	printf("latency2001 [opts] <size>\n\n");
	printf("\t-a <cpu>\t\tcpu to allocate memory on\n");
	printf("\t-c <cpu>\t\tcpu to run on\n");
	printf("\t-C\t\t\toutput in CSV format\n");
	printf("\t-l\t\t\tuse large pages\n");
	printf("\t-t <seconds>\t\tminimum time in seconds to run for\n");
	printf("\t-T\t\t\tTLB test\n");
	printf("\t-S\t\t\tSequential test\n");
#ifdef __powerpc__
	printf("\t-p\t\t\tDon't disable prefetch via DSCR\n");
#endif
	printf("\t-s <stride>\t\tstride size\n");
	printf("\t-e\t\t\ttouch end of cachelines\n\n");
}

int main(int argc, char *argv[])
{
	int allocate_cpu = -1;
	int run_cpu = -1;
	bool largepage = false;
	unsigned long time = 1;
	unsigned long skip = 0;
	unsigned long size;
	bool csv = false;
	bool end = false;
	enum type type = LFSR;
	bool verbose = false;
	bool prefetch = false;
	char *c;
	unsigned long pagesize = getpagesize();

	while (1) {
		signed char c = getopt(argc, argv, "a:c:Clt:TSs:evph");
		if (c < 0)
			break;

		switch (c) {
			case 'a':
				allocate_cpu = atoi(optarg);
				break;

			case 'c':
				run_cpu = atoi(optarg);
				break;

			case 'C':
				csv = true;
				break;

			case 'l':
				largepage = true;
				pagesize = get_hugepage_size();
				break;

			case 't':
				time = atoi(optarg);
				break;

			case 'T':
				type = TLB;
				break;

			case 'S':
				type = SEQUENTIAL;
				break;

			case 's':
				skip = parse_size(optarg);
				break;

			case 'e':
				end = true;
				break;

			case 'v':
				verbose = true;
				break;

#ifdef __powerpc__
			case 'p':
				prefetch = true;
				break;
#endif

			case 'h':
				usage();
				exit(1);
				break;
		}
	}

	if ((argc - optind) == 0) {
		usage();
		exit(1);
	}

	get_proc_frequency();

	if (csv)
		printf("size,cycles,ns\n");

#ifdef __powerpc__
	if (!prefetch)
		set_dscr(1);
#endif

	while ((argc - optind) > 0) {
		size = parse_size(argv[optind]);

		if (!skip) {
			if (type == TLB) {
				skip = pagesize;
			} else {
				/*
				 * For small sizes, we need enough pointer
				 * dereferences to avoid multiple iterations
				 * from overlapping.
				 */
				if (size < 16*1024)
					skip = 8;
				else
					skip = cacheline_size;
			}
		}

		if (size % (skip * 8)) {
			fprintf(stderr, "Size must be a multiple of 8x skip\n\n");
			exit(1);
		}

		if (allocate_cpu != -1)
			runon(allocate_cpu);

		if (largepage) {
			c = alloc_large_mem(size);
		} else {
			c = alloc_small_mem(size);
			if (!c) {
				perror("memalign");
				exit(1);
			}
		}

		memset(c, 0, size);

		if (verbose)
			print_real_addresses((unsigned long)c, size, pagesize);

		if (run_cpu != -1)
			runon(run_cpu);

		doit(size, skip, type, time, end, c, csv);

		if (largepage)
			free_large_mem(c, size);
		else
			free_small_mem(c, size);

		optind++;
	}

	return 0;
}
