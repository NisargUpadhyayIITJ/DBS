/* splayer.c
 * Basic slotted-page implementation on top of the PF layer.
 *
 * Page layout (in a PF page buffer):
 * - at offset 0: int freeStart (first free byte for data area)
 * - data area grows upward from freeStart
 * - slot directory grows downward from page end:
 *     [ slot_n ] [ slot_{n-1} ] ... [ slot_0 ] [ int nslots ]
 *   where each slot entry is two ints: offset (from page start) and length
 *
 * This module keeps interfaces simple and uses PF_AllocPage / PF_GetThisPage
 * / PF_UnfixPage for page allocation and access.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "splayer.h"
#include "pftypes.h"

/* Internal constants */
#define SP_SLOT_SZ (sizeof(int)*2) /* offset + length */
#define SP_HDR_SZ (sizeof(int))    /* freeStart stored at start */

typedef struct {
    int offset;
    int length;
} sp_slot_t;

struct SPscan {
    int fd;
    int curpage;
    int curslot;
};

/* helpers */
static int read_nslots(char *pagebuf) {
    int nslots;
    int pos = PF_PAGE_SIZE - sizeof(int);
    memcpy(&nslots, pagebuf + pos, sizeof(int));
    return nslots;
}

static void write_nslots(char *pagebuf, int nslots) {
    int pos = PF_PAGE_SIZE - sizeof(int);
    memcpy(pagebuf + pos, &nslots, sizeof(int));
}

static int get_slot_pos(int idx) {
    /* slot idx counted from 0..nslots-1; slot entries are placed before the trailing nslots int */
    return PF_PAGE_SIZE - sizeof(int) - (idx+1) * SP_SLOT_SZ;
}

static void read_slot(char *pagebuf, int idx, sp_slot_t *s) {
    int pos = get_slot_pos(idx);
    memcpy(&s->offset, pagebuf + pos, sizeof(int));
    memcpy(&s->length, pagebuf + pos + sizeof(int), sizeof(int));
}

static void write_slot(char *pagebuf, int idx, sp_slot_t *s) {
    int pos = get_slot_pos(idx);
    memcpy(pagebuf + pos, &s->offset, sizeof(int));
    memcpy(pagebuf + pos + sizeof(int), &s->length, sizeof(int));
}

int SP_CreateFile(const char *fname) {
    return PF_CreateFile((char*)fname);
}

int SP_OpenFile(const char *fname) {
    return PF_OpenFile((char*)fname);
}

int SP_CloseFile(int fd) {
    return PF_CloseFile(fd);
}

/* compute free space available for data + one slot entry */
static int page_free_space(char *pagebuf) {
    int freeStart;
    memcpy(&freeStart, pagebuf + 0, sizeof(int));
    if (freeStart == 0) freeStart = SP_HDR_SZ; /* initialize if needed */
    int nslots = read_nslots(pagebuf);
    int slot_dir_start = PF_PAGE_SIZE - sizeof(int) - nslots * SP_SLOT_SZ;
    return slot_dir_start - freeStart;
}

int SP_InsertRec(int fd, const char *rec, int reclen, SPRID *rid) {
    int pagenum = 0;
    char *pagebuf;
    int error;

    for (pagenum = 0; ; pagenum++) {
        error = PF_GetThisPage(fd, pagenum, &pagebuf);
        if (error == PFE_OK) {
            /* ensure headers present */
            int freeStart;
            memcpy(&freeStart, pagebuf + 0, sizeof(int));
            if (freeStart == 0) {
                freeStart = SP_HDR_SZ;
                memcpy(pagebuf + 0, &freeStart, sizeof(int));
                write_nslots(pagebuf, 0);
            }
            int nslots = read_nslots(pagebuf);
            int free = page_free_space(pagebuf);
            int need = reclen + SP_SLOT_SZ;
            if (need <= free) {
                /* insert here */
                sp_slot_t s;
                s.offset = freeStart;
                s.length = reclen;
                /* copy record data */
                memcpy(pagebuf + s.offset, rec, reclen);
                /* write slot */
                write_slot(pagebuf, nslots, &s);
                nslots++;
                write_nslots(pagebuf, nslots);
                /* update freeStart */
                freeStart += reclen;
                memcpy(pagebuf + 0, &freeStart, sizeof(int));
                /* unfix page as dirty */
                PF_UnfixPage(fd, pagenum, TRUE);
                if (rid) { rid->page = pagenum; rid->slot = nslots-1; }
                return 0;
            }
            /* not enough space, unfix and continue */
            PF_UnfixPage(fd, pagenum, FALSE);
            continue;
        } else if (error == PFE_INVALIDPAGE) {
            /* need to allocate a new page */
            int newp;
            char *nbuf;
            if ((error = PF_AllocPage(fd, &newp, &nbuf)) != PFE_OK) return -1;
            /* init header */
            int freeStart = SP_HDR_SZ;
            memcpy(nbuf + 0, &freeStart, sizeof(int));
            write_nslots(nbuf, 0);
            /* insert into this new page (same logic) */
            sp_slot_t s;
            s.offset = freeStart;
            s.length = reclen;
            memcpy(nbuf + s.offset, rec, reclen);
            write_slot(nbuf, 0, &s);
            write_nslots(nbuf, 1);
            freeStart += reclen;
            memcpy(nbuf + 0, &freeStart, sizeof(int));
            PF_UnfixPage(fd, newp, TRUE);
            if (rid) { rid->page = newp; rid->slot = 0; }
            return 0;
        } else {
            /* other error */
            return -1;
        }
    }
}

int SP_DeleteRec(int fd, SPRID rid) {
    char *pagebuf;
    int error;
    if ((error = PF_GetThisPage(fd, rid.page, &pagebuf)) != PFE_OK) return -1;
    int nslots = read_nslots(pagebuf);
    if (rid.slot < 0 || rid.slot >= nslots) { PF_UnfixPage(fd, rid.page, FALSE); return -1; }
    sp_slot_t s; read_slot(pagebuf, rid.slot, &s);
    if (s.length <= 0) { PF_UnfixPage(fd, rid.page, FALSE); return -1; }
    /* mark deleted by setting length = -1 */
    s.length = -1;
    write_slot(pagebuf, rid.slot, &s);
    PF_UnfixPage(fd, rid.page, TRUE);
    return 0;
}

int SP_ScanOpen(int fd, SPscan **scanptr) {
    SPscan *s = (SPscan*)malloc(sizeof(SPscan));
    if (!s) return -1;
    s->fd = fd; s->curpage = 0; s->curslot = 0;
    *scanptr = s; return 0;
}

int SP_ScanNext(SPscan *scan, char **recbuf, int *reclen, SPRID *rid) {
    int p = scan->curpage;
    char *pagebuf;
    int error;
    for (; ; p++) {
        error = PF_GetThisPage(scan->fd, p, &pagebuf);
        if (error == PFE_INVALIDPAGE) return PFE_EOF;
        if (error != PFE_OK) return -1;
        int nslots = read_nslots(pagebuf);
        int sidx = (p == scan->curpage) ? scan->curslot : 0;
        for (int i = sidx; i < nslots; i++) {
            sp_slot_t sst; read_slot(pagebuf, i, &sst);
            if (sst.length > 0) {
                /* return copy of record */
                char *buf = (char*)malloc(sst.length);
                if (!buf) { PF_UnfixPage(scan->fd, p, FALSE); return -1; }
                memcpy(buf, pagebuf + sst.offset, sst.length);
                *recbuf = buf; *reclen = sst.length; if (rid) { rid->page = p; rid->slot = i; }
                /* prepare scan state */
                scan->curpage = p; scan->curslot = i+1;
                PF_UnfixPage(scan->fd, p, FALSE);
                return 0;
            }
        }
        /* no record found on page */
        PF_UnfixPage(scan->fd, p, FALSE);
        /* continue to next page */
        scan->curslot = 0;
        scan->curpage = p+1;
    }
}

int SP_ScanClose(SPscan *scan) { free(scan); return 0; }

int SP_PageUsedBytes(char *pagebuf) {
    if (!pagebuf) return -1;
    int freeStart; memcpy(&freeStart, pagebuf + 0, sizeof(int)); if (freeStart == 0) freeStart = SP_HDR_SZ;
    int nslots = read_nslots(pagebuf);
    int slotdir = sizeof(int) + nslots * SP_SLOT_SZ; /* trailing int + slots */
    int used = freeStart + slotdir;
    if (used > PF_PAGE_SIZE) used = PF_PAGE_SIZE;
    return used;
}
