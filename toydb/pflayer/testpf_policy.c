/* testpf_policy.c
   Simple test harness to exercise PF buffer with different read/write mixes
*/
/* testpf_policy.c
   Test harness to exercise PF buffer with configurable parameters.
   Usage:
     testpf_policy [pool] [policy] [ops] [pages] [write_frac] [out_csv]
   where:
     pool      - buffer pool size (int)
     policy    - lru or mru
     ops       - number of operations (int)
     pages     - number of distinct pages to use (int)
     write_frac- fraction of operations that are writes (0..1)
     out_csv   - optional path to append CSV results
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "pf.h"
#include "pftypes.h"

int main(int argc, char **argv)
{
    int fd;
    char *buf;
    int i, page;
    struct PFstats stats;
    int pool = 5;
    int policy = PF_REPL_LRU;
    int ops = 50;
    int npages = 10;
    double write_frac = 0.3;
    const char *out_csv = NULL;

    if (argc > 1) pool = atoi(argv[1]);
    if (argc > 2) {
        if (strcasecmp(argv[2], "mru") == 0) policy = PF_REPL_MRU;
        else policy = PF_REPL_LRU;
    }
    if (argc > 3) ops = atoi(argv[3]);
    if (argc > 4) npages = atoi(argv[4]);
    if (argc > 5) write_frac = atof(argv[5]);
    if (argc > 6) out_csv = argv[6];

    PF_Init();
    PF_SetBufferParams(pool, policy);

    /* create and open a file */
    unlink("/tmp/pftestfile");
    if (PF_CreateFile("/tmp/pftestfile")!= PFE_OK){
        PF_PrintError("create");
        return 1;
    }
    if ((fd=PF_OpenFile("/tmp/pftestfile"))<0){
        PF_PrintError("open");
        return 1;
    }

    /* allocate npages pages and write something */
    for (i=0;i<npages;i++){
        if (PF_AllocPage(fd,&page,&buf)!= PFE_OK){
            PF_PrintError("alloc");
            return 1;
        }
        snprintf(buf, PF_PAGE_SIZE, "page-%d", i);
        if (PF_UnfixPage(fd,page,1)!= PFE_OK){
            PF_PrintError("unfix");
            return 1;
        }
    }

    /* deterministic pseudo-random sequence */
    srand(42);

    /* perform ops accesses; decide write vs read by write_frac */
    for (i=0;i<ops;i++){
        int p = i % npages; /* simple working set */
        if (PF_GetThisPage(fd,p,&buf)!= PFE_OK){
            PF_PrintError("getthis");
            return 1;
        }
        double r = (double)rand() / (double)RAND_MAX;
        if (r < write_frac){
            /* write */
            snprintf(buf, PF_PAGE_SIZE, "page-%d-mod-%d", p, i);
            if (PF_UnfixPage(fd,p,1)!= PFE_OK){
                PF_PrintError("unfix_write");
                return 1;
            }
        } else {
            /* read-only */
            if (PF_UnfixPage(fd,p,0)!= PFE_OK){
                PF_PrintError("unfix_read");
                return 1;
            }
        }
    }

    PF_GetStats(&stats);

    /* Print a single CSV line to stdout for easy parsing by runner */
    printf("policy=%s,pool=%d,ops=%d,pages=%d,write_frac=%.2f,logical_reads=%d,logical_writes=%d,phys_reads=%d,phys_writes=%d,page_hits=%d,page_misses=%d\n",
        (policy==PF_REPL_MRU)?"MRU":"LRU", pool, ops, npages, write_frac,
        stats.logical_reads, stats.logical_writes, stats.phys_reads, stats.phys_writes, stats.page_hits, stats.page_misses);

    /* optionally append to CSV file */
    if (out_csv){
        FILE *f = fopen(out_csv, "a");
        if (f){
            fprintf(f, "%s,%d,%d,%d,%.2f,%d,%d,%d,%d,%d,%d\n",
                (policy==PF_REPL_MRU)?"MRU":"LRU", pool, ops, npages, write_frac,
                stats.logical_reads, stats.logical_writes, stats.phys_reads, stats.phys_writes, stats.page_hits, stats.page_misses);
            fclose(f);
        }
    }

    if (PF_CloseFile(fd)!= PFE_OK){
        PF_PrintError("close");
        return 1;
    }
    PF_DestroyFile("/tmp/pftestfile");
    return 0;
}
