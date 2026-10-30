/* xl_end_to_end.c: the Macintosh XL mailbox, end to end on a PC, no hardware.
 *
 * A pretend ProFile of 4,000 blocks with MacWorks-style tags (Mac file number in 0-3, flags in 4-5, logical block
 * in 6-7, date in 8-11, checksum in 19 so each page XORs to 0).  The Mac side is xlproto.c, as built into
 * ESPFuji XL.  The drive side uses mailbox_core.h from the patched firmware and copies of the two hooks'
 * version 2 logic.  Checks:
 *   1. version 1 pages still parse and swap exactly as in 1.0 (byte 11);
 *   2. a version 2 request makes the mailbox = blocks b-k .. b-k+n-1, and only those get the reply;
 *   3. the swap keeps tag byte 19 right (the page still XORs to 0) and never touches the Mac tag's other bytes;
 *   4. a block of another Mac file with the SAME flag word in bytes 4-5 is never swapped (1.0's danger);
 *   5. firmware 1.0's parser (version must be 1) rejects a version 2 page.
 * Build:  cc -I../src -I../../../sw/ESProFile -o xl_end_to_end xl_end_to_end.c ../src/xlproto.c && ./xl_end_to_end
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mailbox_core.h"
#include "xlproto.h"

#define BLOCKS 4000
static uint8_t disk[BLOCKS][MB_PAGE];
static int fails = 0;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL: %s\n", what); fails++; } else printf("ok    %s\n", what); } while (0)

/* --- the drive: version 2 parts of mbOnWrite / mbOnRead, as patched --- */
static uint32_t xlFirst; static uint16_t xlCount; static uint16_t fileId;
static struct { int ready; uint16_t version, seq, len; uint32_t block; uint8_t payload[MB_PAYLOAD]; } rep;
static struct { int pending; uint16_t version, seq, len; uint32_t block; uint8_t payload[MB_PAYLOAD]; } req;

static void driveWrite(uint32_t b, const uint8_t *blk) {
    mb_header_t h;
    if (mb_parse(blk, &h) && h.kind == MB_REQUEST) {
        if (h.version == MB_VERSION_XL) {
            uint32_t f; uint16_t c;
            if (mb_xl_range(b, &h, &f, &c)) { xlFirst = f; xlCount = c; goto take; }
        } else { fileId = mb_tag_fileid(blk); goto take; }
    }
    goto store;
take:
    req.block = b; req.seq = h.seq; req.len = h.len; req.version = h.version;
    memcpy(req.payload, h.payload, h.len); rep.ready = 0; req.pending = 1;
store:
    memcpy(disk[b], blk, MB_PAGE);
}

static void driveNet(void) {                    /* the network task: answers "OK <request>" */
    if (!req.pending) return;
    req.pending = 0;
    rep.len = (uint16_t)snprintf((char *)rep.payload, MB_PAYLOAD, "OK %.*s", req.len, req.payload);
    rep.seq = req.seq; rep.version = req.version; rep.block = req.block; rep.ready = 1;
}

static void driveRead(uint32_t b, uint8_t *out) {
    uint8_t data[MB_DATA];
    memcpy(out, disk[b], MB_PAGE);
    if (!rep.ready) return;
    if (rep.version == MB_VERSION_XL) {
        if (xlCount == 0 || b < xlFirst || b - xlFirst >= xlCount) return;
        mb_build_data_v(data, MB_VERSION_XL, rep.seq, MB_REPLY, rep.block, rep.payload, rep.len);
        mb_swap_data_xl(out, data);
    } else {
        if (fileId == 0 || mb_tag_fileid(out) != fileId) return;
        mb_build_data(data, rep.seq, MB_REPLY, rep.block, rep.payload, rep.len);
        mb_swap_data(out, data);
    }
}

/* --- MacWorks: a page as its ProFile driver writes it --- */
static void macTag(uint8_t *page, uint32_t fileNum, uint16_t flags, uint16_t lblock) {
    memset(page, 0, MB_TAG);
    page[0] = fileNum >> 24; page[1] = fileNum >> 16; page[2] = fileNum >> 8; page[3] = fileNum;
    page[4] = flags >> 8; page[5] = flags;
    page[6] = lblock >> 8; page[7] = lblock;
    page[8] = 0xAC; page[9] = 0xAF; page[10] = 0xA9; page[11] = 0x7B;      /* a date */
    page[19] = 0;
    page[19] = mb_page_xor(page);
}
static void macWrite(uint32_t b, uint32_t fileNum, uint16_t flags, uint16_t lblock, const uint8_t *data) {
    uint8_t page[MB_PAGE];
    memcpy(page + MB_TAG, data, MB_DATA);
    macTag(page, fileNum, flags, lblock);
    driveWrite(b, page);
}

int main(void) {
    uint8_t page[MB_PAGE], data[MB_DATA], back[MB_PAGE];
    unsigned i;
    XLHeader xh;
    const uint32_t MBOX = 1200, N = 64, OTHER = 3000, MAILFILE = 0x3F;

    /* a volume: every block written by MacWorks, all with flag word 0x8100 */
    for (i = 0; i < BLOCKS; i++) { memset(data, 0x35, MB_DATA); macWrite(i, 0x10 + (i / 100), 0x8100, (uint16_t)(i % 100), data); }
    /* the XL application fills MAILBOX.DATA with idle pages */
    for (i = 0; i < N; i++) { XLBuildPage(data, 0, XL_IDLE, (unsigned short)i, (unsigned short)N, NULL, 0); macWrite(MBOX + i, MAILFILE, 0x8100, (uint16_t)i, data); }

    /* 1. version 1 unchanged */
    {
        const char *t = "ECHO v1";
        mb_build_data(data, 7, MB_REQUEST, 0, (const uint8_t *)t, (uint16_t)strlen(t));
        mb_header_t h;
        memcpy(page + MB_TAG, data, MB_DATA); memset(page, 0, MB_TAG);
        CHECK(mb_parse(page, &h) && h.version == 1 && h.seq == 7, "version 1 page parses as before");
    }

    /* 2-4. a version 2 exchange */
    {
        const char *t = "HELLO";
        unsigned short k = 5, seq = 42;
        XLBuildPage(data, seq, XL_REQUEST, k, (unsigned short)N, (const unsigned char *)t, 5);
        macWrite(MBOX + k, MAILFILE, 0x8100, k, data);
        CHECK(req.pending && req.version == 2 && xlFirst == MBOX && xlCount == N, "v2 request: mailbox = blocks 1200..1263");
        driveNet();
        driveRead(OTHER, back);
        CHECK(memcmp(back, disk[OTHER], MB_PAGE) == 0, "another Mac file with the same flag word 0x8100 is NOT swapped");
        driveRead(MBOX - 1, back);
        CHECK(memcmp(back, disk[MBOX - 1], MB_PAGE) == 0, "the block before the mailbox is NOT swapped");
        driveRead(MBOX + N, back);
        CHECK(memcmp(back, disk[MBOX + N], MB_PAGE) == 0, "the block after the mailbox is NOT swapped");
        driveRead(MBOX + k + 1, back);
        CHECK(XLIsReplyTo(back + MB_TAG, seq, &xh) && xh.len == 8 && memcmp(xh.payload, "OK HELLO", 8) == 0,
              "the next mailbox page reads back as the reply \"OK HELLO\"");
        CHECK(mb_page_xor(back) == 0, "the swapped page still XORs to 0 (tag byte 19 fixed)");
        CHECK(memcmp(back, disk[MBOX + k + 1], 19) == 0, "tag bytes 0-18 (the Mac file tag) are untouched");
        driveRead(MBOX + N - 1, back);
        CHECK(XLIsReplyTo(back + MB_TAG, seq, &xh), "the last mailbox page serves the reply too");
        CHECK(!XLIsReplyTo(back + MB_TAG, (unsigned short)(seq + 1), &xh), "a reply is not taken for another sequence number");
    }

    /* 5. firmware 1.0's rule: version must be exactly 1 */
    XLBuildPage(data, 1, XL_REQUEST, 0, 64, (const unsigned char *)"HELLO", 5);
    CHECK(((data[8] << 8) | data[9]) != 1, "a 1.0 drive's mb_parse (version == 1 only) rejects every XL page");

    /* bad ranges are refused */
    {
        mb_header_t h; uint32_t f; uint16_t c;
        XLBuildPage(data, 1, XL_REQUEST, 70, 64, NULL, 0);
        memcpy(page + MB_TAG, data, MB_DATA);
        CHECK(mb_parse(page, &h) && !mb_xl_range(1000, &h, &f, &c), "k >= n is refused");
        XLBuildPage(data, 1, XL_REQUEST, 5, 64, NULL, 0);
        memcpy(page + MB_TAG, data, MB_DATA);
        CHECK(mb_parse(page, &h) && !mb_xl_range(3, &h, &f, &c), "a range that would start before block 0 is refused");
    }

    printf("%s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
