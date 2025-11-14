/* splayer.h: Slotted page layer on top of PF
 * Provides insertion, deletion and sequential scanning of variable-length records
 */
#ifndef SPLAYER_H
#define SPLAYER_H

#include "pf.h"

/* Record identifier: page number and slot number */
typedef struct {
    int page;
    int slot;
} SPRID;

/* Scan descriptor (opaque) */
typedef struct SPscan SPscan;

/* API */
int SP_CreateFile(const char *fname);
int SP_OpenFile(const char *fname);
int SP_CloseFile(int fd);

int SP_InsertRec(int fd, const char *rec, int reclen, SPRID *rid);
int SP_DeleteRec(int fd, SPRID rid);

int SP_ScanOpen(int fd, SPscan **scan);
int SP_ScanNext(SPscan *scan, char **recbuf, int *reclen, SPRID *rid);
int SP_ScanClose(SPscan *scan);

/* Utility: compute per-page used bytes (for reporting). Returns -1 on error. */
int SP_PageUsedBytes(char *pagebuf);

#endif
