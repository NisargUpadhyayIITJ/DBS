/* test_task3.c
 * Task 3: compare index construction methods using AM layer on student data
 * Methods:
 *  - unsorted build: scan input file in original order and insert into index
 *  - sorted bulk-build: read all keys, sort them, then insert (reduces splits)
 *  - random incremental build: shuffles input order and inserts (worst-case)
 *
 * For each method we measure build time and PF page-level statistics.
 * We also measure point-query performance (time & pages accessed) on a sample
 * of keys.
 */

#include "am.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Minimal PF prototypes and PFstats struct so this test can compile cleanly
    without depending on header ordering issues in the legacy am.h/pf.h */
typedef struct PFstats { int logical_reads; int logical_writes; int phys_reads; int phys_writes; int page_hits; int page_misses; } PFstats;
extern int PF_Init();
extern int PF_OpenFile(char *fname);
extern int PF_CloseFile(int fd);
extern int PF_UnfixPage(int fd, int pagenum, int dirty);
extern int PF_GetStats(struct PFstats *out);

/* AM layer functions used */
extern int AM_CreateIndex(char *fileName,int indexNo,char attrType,int attrLength);
extern int AM_DestroyIndex(char *fileName,int indexNo);
extern int AM_InsertEntry(int fileDesc,char attrType,int attrLength,char *value,int recId);
extern int AM_Search(int fileDesc,char attrType,int attrLength,char *value,int *pageNum,char **pageBuf,int *indexPtr);
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define INDEXNO 0
#define MAX_KEYS_SAMPLE 1000

static long elapsed_ms(struct timespec a, struct timespec b){
    return (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec)/1000000;
}

/* read up to max_recs lines from datafile; parse rollno (int) from start of line */
static int read_rollnos(const char *datafile, int max_recs, int **out_keys){
    FILE *f = fopen(datafile, "r");
    if(!f) return -1;
    int *keys = malloc(sizeof(int)*max_recs);
    char *line = NULL; size_t cap = 0;
    int cnt = 0;
    while(cnt < max_recs && getline(&line,&cap,f) > 0){
        /* parse integer before first semicolon */
        char *p = strchr(line,';');
        if(!p) continue;
        *p = '\0';
        int roll = atoi(line);
        keys[cnt++] = roll;
    }
    free(line); fclose(f);
    *out_keys = keys; return cnt;
}

/* wrapper to create, open index filename base (e.g., "student") */
static int create_and_open_index(const char *basename){
    /* destroy previous, ignore errors */
    AM_DestroyIndex((char*)basename, INDEXNO);
    if(AM_CreateIndex((char*)basename, INDEXNO, 'i', sizeof(int)) != AME_OK){
        fprintf(stderr,"AM_CreateIndex failed\n"); return -1;
    }
    char idxname[128]; sprintf(idxname, "%s.%d", basename, INDEXNO);
    int fd = PF_OpenFile(idxname);
    if(fd < 0) { fprintf(stderr,"PF_OpenFile(%s) failed\n", idxname); return -1; }
    return fd;
}

/* run a build: keys[0..n-1] inserted in given order; returns elapsed ms and stats deltas via pointers */
static long build_index_insert(int fd, int *keys, int n, PFstats *before, PFstats *after){
    struct timespec t0,t1; PF_GetStats(before);
    clock_gettime(CLOCK_MONOTONIC,&t0);
    for(int i=0;i<n;i++){
        int key = keys[i];
        if (AM_InsertEntry(fd, 'i', sizeof(int), (char*)&key, i) != AME_OK){
            fprintf(stderr,"AM_InsertEntry failed at i=%d key=%d\n", i, key);
        }
    }
    clock_gettime(CLOCK_MONOTONIC,&t1);
    PF_GetStats(after);
    return elapsed_ms(t0,t1);
}

/* measure point-query workload: sample m keys from array; returns elapsed ms and stats delta */
static long measure_point_queries(int fd, int *keys, int n, int m, PFstats *before, PFstats *after){
    if(m > n) m = n;
    /* choose m evenly spaced keys */
    PF_GetStats(before);
    struct timespec t0,t1; clock_gettime(CLOCK_MONOTONIC,&t0);
    for(int i=0;i<m;i++){
        int idx = (i * n) / m; int key = keys[idx];
        int pageNum, indexPtr; char *pageBuf;
        int status = AM_Search(fd, 'i', sizeof(int), (char*)&key, &pageNum, &pageBuf, &indexPtr);
        if(status < 0){ /* error */ }
        /* unfix the leaf page returned by AM_Search */
        PF_UnfixPage(fd, pageNum, FALSE);
    }
    clock_gettime(CLOCK_MONOTONIC,&t1);
    PF_GetStats(after);
    return elapsed_ms(t0,t1);
}

int main(int argc, char **argv){
    const char *datafile = "../data/student.txt"; /* default relative */
    int nrecs = 2000; /* default records to process */
    if(argc > 1) nrecs = atoi(argv[1]);
    if(argc > 2) datafile = argv[2];

    printf("Task 3: AM index-build experiments using '%s' (n=%d)\n", datafile, nrecs);
    PF_Init();

    int *keys = NULL;
    int count = read_rollnos(datafile, nrecs, &keys);
    if(count <= 0){ fprintf(stderr,"failed to read rollnos\n"); return 1; }
    printf("Read %d roll-no keys\n", count);

    /* prepare three orders: original (input), sorted, random-shuffled */
    int *keys_orig = malloc(sizeof(int)*count);
    int *keys_sorted = malloc(sizeof(int)*count);
    int *keys_rand = malloc(sizeof(int)*count);
    for(int i=0;i<count;i++){ keys_orig[i] = keys[i]; keys_sorted[i] = keys[i]; keys_rand[i] = keys[i]; }
    /* sort keys_sorted */
    int cmp_int(const void *a, const void *b){ return (*(int*)a) - (*(int*)b); }
    qsort(keys_sorted, count, sizeof(int), cmp_int);

    /* random shuffle into keys_rand */
    for(int i=count-1;i>0;i--){ int j = rand() % (i+1); int t = keys_rand[i]; keys_rand[i] = keys_rand[j]; keys_rand[j] = t; }

    /* experiment variants: unsorted (input order), sorted (bulk-sorted), random-insert */
    const char *basename = "student_am";
    PFstats before, after;

    printf("\nMethod, build-time-ms, phys_reads, phys_writes, logical_reads, logical_writes, page_hits, page_misses\n");

    /* 1) Unsorted build (input order) */
    int fd1 = create_and_open_index(basename);
    if(fd1 < 0) return 1;
    long t_unsorted = build_index_insert(fd1, keys_orig, count, &before, &after);
    PF_CloseFile(fd1);
    printf("unsorted,%ld,%d,%d,%d,%d,%d,%d\n", t_unsorted,
        after.phys_reads - before.phys_reads,
        after.phys_writes - before.phys_writes,
        after.logical_reads - before.logical_reads,
        after.logical_writes - before.logical_writes,
        after.page_hits - before.page_hits,
        after.page_misses - before.page_misses);

    /* 2) Sorted bulk-load (sort keys first then insert) */
    int fd2 = create_and_open_index(basename);
    if(fd2 < 0) return 1;
    long t_sorted = build_index_insert(fd2, keys_sorted, count, &before, &after);
    PF_CloseFile(fd2);
    printf("sorted,%ld,%d,%d,%d,%d,%d,%d\n", t_sorted,
        after.phys_reads - before.phys_reads,
        after.phys_writes - before.phys_writes,
        after.logical_reads - before.logical_reads,
        after.logical_writes - before.logical_writes,
        after.page_hits - before.page_hits,
        after.page_misses - before.page_misses);

    /* 3) Random incremental build */
    int fd3 = create_and_open_index(basename);
    if(fd3 < 0) return 1;
    long t_random = build_index_insert(fd3, keys_rand, count, &before, &after);
    PF_CloseFile(fd3);
    printf("random,%ld,%d,%d,%d,%d,%d,%d\n", t_random,
        after.phys_reads - before.phys_reads,
        after.phys_writes - before.phys_writes,
        after.logical_reads - before.logical_reads,
        after.logical_writes - before.logical_writes,
        after.page_hits - before.page_hits,
        after.page_misses - before.page_misses);

    /* Query workload: reopen sorted index and run sample point lookups */
    int fdq = PF_OpenFile("student_am.0");
    if(fdq < 0) { fprintf(stderr,"open index for query failed\n"); }
    PF_GetStats(&before);
    long qtime = measure_point_queries(fdq, keys_sorted, count, MAX_KEYS_SAMPLE, &before, &after);
    PF_CloseFile(fdq);
    printf("\nPoint-query sample (%d), time-ms=%ld, phys_reads=%d\n", MAX_KEYS_SAMPLE, qtime,
        after.phys_reads - before.phys_reads);

    /* cleanup */
    AM_DestroyIndex((char*)basename, INDEXNO);

    /* free buffers */
    free(keys); free(keys_orig); free(keys_sorted); free(keys_rand);
    return 0;
}
