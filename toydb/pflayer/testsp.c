/* testsp.c
 * Small test driver for slotted-page layer. Inserts variable-length records,
 * scans them back, deletes a portion, and reports space utilization.
 */

#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

/* getline() is a GNU extension; declare it here to avoid implicit-decl warnings
    on stricter compilation modes. */
ssize_t getline(char **__lineptr, size_t *__n, FILE *__stream);
#include "splayer.h"

int main(int argc, char **argv) {
    char fnamebuf[256];
    snprintf(fnamebuf, sizeof(fnamebuf), "/tmp/sp_testfile_%d", (int)getpid());
    const char *fname = fnamebuf;
    int fd;
    int nrecs = 200; /* default number of records to insert */
    int maxrec = 200; /* max record length used only for random generation */
    const char *datafile = NULL;
    if (argc > 1) nrecs = atoi(argv[1]);
    if (argc > 2) maxrec = atoi(argv[2]);
    if (argc > 3) datafile = argv[3];

    srand(42);

    SP_CreateFile(fname);
    fd = SP_OpenFile(fname);
    if (fd < 0) { fprintf(stderr, "open failed\n"); return 1; }

    /* prepare record source: either read from data file (one line per record)
       or generate random variable-length records as before */
    char **records = malloc(sizeof(char*) * nrecs);
    int *reclens = malloc(sizeof(int) * nrecs);
    int actual_n = 0;
    if (datafile) {
        FILE *f = fopen(datafile, "r");
        if (!f) { fprintf(stderr, "failed to open data file %s\n", datafile); return 1; }
        size_t cap = 0; char *line = NULL;
        while (actual_n < nrecs && getline(&line, &cap, f) > 0) {
            /* strip newline */
            int L = strlen(line);
            while (L>0 && (line[L-1]=='\n' || line[L-1]=='\r')) { line[L-1] = '\0'; --L; }
            records[actual_n] = malloc(L);
            memcpy(records[actual_n], line, L);
            reclens[actual_n] = L;
            actual_n++;
        }
        free(line);
        fclose(f);
        if (actual_n == 0) { fprintf(stderr, "no records read from %s\n", datafile); return 1; }
    } else {
        for (int i=0;i<nrecs;i++){
            int len = 1 + rand()%maxrec;
            char *buf = malloc(len);
            for (int j=0;j<len;j++) buf[j] = 'A' + (i+j)%26;
            records[actual_n] = buf; reclens[actual_n] = len; actual_n++;
        }
    }

    /* insert records into slotted pages */
    SPRID *rids = malloc(sizeof(SPRID)*actual_n);
    for (int i=0;i<actual_n;i++){
        if (SP_InsertRec(fd, records[i], reclens[i], &rids[i]) != 0) {
            fprintf(stderr,"insert failed at %d\n", i); break;
        }
    }

    /* scan count */
    SPscan *scan;
    SP_ScanOpen(fd, &scan);
    int cnt = 0;
    char *rec; int rlen; SPRID rid;
    while (SP_ScanNext(scan, &rec, &rlen, &rid) == 0) {
        cnt++; free(rec);
    }
    SP_ScanClose(scan);
    printf("Inserted %d records; scanned %d records\n", actual_n, cnt);

    /* delete half of them (every other inserted record) */
    for (int i=0;i<actual_n;i+=2) SP_DeleteRec(fd, rids[i]);

    /* compute per-page utilization and total pages */
    int page = 0; char *pagebuf;
    int used_pages = 0; long total_used = 0; int pages_examined = 0;
    while (1) {
        int err = PF_GetThisPage(fd, page, &pagebuf);
        if (err == PFE_INVALIDPAGE) break;
        if (err != PFE_OK) { fprintf(stderr,"PF_GetThisPage error %d\n", err); break; }
        int used = SP_PageUsedBytes(pagebuf);
        pages_examined++;
        total_used += used;
        PF_UnfixPage(fd, page, FALSE);
        page++;
    }

    printf("Pages used: %d, total used bytes: %ld, avg util per page: %.2f%%\n",
        pages_examined, total_used, 100.0 * (double)total_used / (pages_examined * PF_PAGE_SIZE));

    /* static fixed-length comparison (exact simulation using actual record lengths)
       For each candidate fixed-slot size 'M' we compute slots_per_page = PF_PAGE_SIZE / M
       pages_needed = ceil(actual_n / slots_per_page)
       utilization = sum(actual_lengths) / (pages_needed * PF_PAGE_SIZE)
    */
    long sum_actual = 0;
    for (int i=0;i<actual_n;i++) sum_actual += reclens[i];
    printf("Total user bytes (sum of record lengths): %ld\n", sum_actual);
    printf("\nStatic fixed-slot comparison (M = slot size in bytes)\n");
    printf("M\tslots/page\tpages_needed\tutilization(%%)\tnotes\n");
    for (int maxrec_try = 32; maxrec_try <= 256; maxrec_try *= 2) {
        int perpage = PF_PAGE_SIZE / maxrec_try;
        if (perpage <= 0) {
            printf("%d\t%d\t-\t-\tslot too large\n", maxrec_try, perpage);
            continue;
        }
        /* check whether all records fit into slots of size maxrec_try */
        int oversized = 0;
        for (int i=0;i<actual_n;i++) if (reclens[i] > maxrec_try) { oversized++; }
        if (oversized > 0) {
            printf("%d\t%d\t-\t-\tinapplicable: %d records exceed slot size\n",
                maxrec_try, perpage, oversized);
            continue;
        }
        int pages_needed = (actual_n + perpage -1)/perpage;
        double util = (double)sum_actual / (pages_needed * PF_PAGE_SIZE);
        printf("%d\t%d\t%d\t%.2f\t-\n", maxrec_try, perpage, pages_needed, util*100.0);
    }

    free(rids);
    /* free record buffers */
    for (int i=0;i<actual_n;i++) free(records[i]);
    free(records); free(reclens);
    SP_CloseFile(fd);
    return 0;
}
