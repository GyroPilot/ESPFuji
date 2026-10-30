// mailbox_core.h -- the FujiNet MAILBOX for an ESProFile: the pure parts, no hardware.
// EXPERIMENT, October 2026, Tim O'Connor (bmwcyclist) with Claude.  Public since 27 Oct 2026 with
// Alex Anderson-McLeod's agreement (github.com/GyroPilot/ESPFuji).  GPL-3.0, to match the FujiNet firmware.
//
// THE IDEA.  A Lisa Office System program cannot talk to a drive except through files.  So the
// program creates an ordinary file, MAILBOX.DATA, and writes a REQUEST into one of its pages
// with whole-page I/O.  The drive sees every page the Lisa writes, and every page comes with its
// 20-byte tag, and the tag carries the file's S-file ID.  The firmware recognises a request by a
// signature at the start of the page, remembers the file ID from the tag, does the network work
// on the other core, and when the Lisa next READS a page of that file, hands it the REPLY page
// instead of the stored one (with the tag checksum put right).  The Lisa OS never knows.
//
// A page on the bus is 532 bytes: the 20-byte tag, then 512 data bytes.
//   tag  0-1 version   2-3 vol_id   4-5 file_id   6-7 dataused (bit 7 of byte 6: checksum present)
//        8-10 abs page   11 checksum   12-13 rel page   14-16 fwd link   17-19 bkwd link
//   The Lisa refuses a page it reads unless the XOR of all 532 bytes is 0 (OS SOURCE-PROFILEASM
//   591-598): byte 11 makes it so.
//
// THE MAILBOX PAGE (the 512 data bytes), all numbers big-endian like the 68000 writes them:
//   0-7   "LISAFUJI"        8-9  protocol version, 1
//   10-11 sequence number   12-13 kind: 1 request, 2 reply, 3 the drive's status page
//   14-15 payload length    16-19 reserved (a reply echoes the block number it answered on)
//   20..  payload, at most MB_PAYLOAD bytes; the rest zero
//
// VERSION 2, the Macintosh XL (MacWorks), 30 Oct 2026.  MacWorks puts the Mac File Manager's tag in the ProFile
// tag (file number 0-3, flags 4-5, logical block 6-7, date 8-11, zeros, and a checksum in byte 19 that makes the
// page XOR to 0), so bytes 4-5 are not a file ID there and must never be used to find the mailbox.  A version 2
// request says where it sits instead:  16-17 = k, its page index in the mailbox file;  18-19 = n, the pages in
// the file, which is one contiguous run of blocks.  Written to block b, the mailbox is blocks b-k .. b-k+n-1, and
// the reply is served on a read of any of them; the swap keeps tag byte 19 right.  A version 1 drive (ESPFuji 1.0)
// rejects a version 2 page in mb_parse, so it never answers one and never swaps anything for it.
//
// The Lisa writes a request on page k of the mailbox and then reads OTHER pages of the mailbox
// (k+1, k+2 ...) until one comes back as a reply with the same sequence number: a page the OS
// still has in its cache comes back stale (an old sequence number, or not a reply) and the
// program simply tries the next page.  The firmware answers the first real read of ANY mailbox
// page while a reply is waiting, and then forgets the reply.

#ifndef MAILBOX_CORE_H
#define MAILBOX_CORE_H

#include <stdint.h>
#include <string.h>

#define MB_PAGE      532
#define MB_TAG       20
#define MB_DATA      512
#define MB_PAYLOAD   492                      /* 512 - 20 bytes of header */
#define MB_VERSION   1
#define MB_VERSION_XL 2                            /* the Macintosh XL (MacWorks): the mailbox found by block range */
#define MB_XL_MAXPAGES 1024
#define MB_IDLE      0                             /* version 2: a page the XL application writes to fill the file */
#define MB_REQUEST   1
#define MB_REPLY     2
#define MB_STATUS    3

static const uint8_t MB_SIGNATURE[8] = { 'L', 'I', 'S', 'A', 'F', 'U', 'J', 'I' };

typedef struct {
    uint16_t version;                         /* 1 (Lisa Office System) or 2 (Macintosh XL) */
    uint16_t seq;
    uint16_t kind;
    uint16_t len;
    uint32_t reserved;
    const uint8_t *payload;                   /* points into the page */
} mb_header_t;

static inline uint16_t mb_get16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline void mb_put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline uint32_t mb_get32(const uint8_t *p) { return ((uint32_t)mb_get16(p) << 16) | mb_get16(p + 2); }
static inline void mb_put32(uint8_t *p, uint32_t v) { mb_put16(p, (uint16_t)(v >> 16)); mb_put16(p + 2, (uint16_t)v); }

/* The S-file ID a page belongs to, from its tag. */
static inline uint16_t mb_tag_fileid(const uint8_t *page) { return mb_get16(page + 4); }
/* The page's position in its file, from its tag. */
static inline uint16_t mb_tag_relpage(const uint8_t *page) { return mb_get16(page + 12); }

/* Is this page (532 bytes: tag + data) a mailbox page?  Fills hdr when it is. */
static inline int mb_parse(const uint8_t *page, mb_header_t *hdr) {
    const uint8_t *d = page + MB_TAG;
    if (memcmp(d, MB_SIGNATURE, 8) != 0) return 0;
    hdr->version = mb_get16(d + 8);
    if (hdr->version != MB_VERSION && hdr->version != MB_VERSION_XL) return 0;
    hdr->seq = mb_get16(d + 10);
    hdr->kind = mb_get16(d + 12);
    hdr->len = mb_get16(d + 14);
    hdr->reserved = mb_get32(d + 16);
    hdr->payload = d + 20;
    if (hdr->len > MB_PAYLOAD) return 0;
    return 1;
}

/* Version 2: the mailbox's blocks, from a request written to block b.  0 when k/n make no sense. */
static inline int mb_xl_range(uint32_t block, const mb_header_t *h, uint32_t *first, uint16_t *count) {
    uint16_t k = (uint16_t)(h->reserved >> 16), n = (uint16_t)h->reserved;
    if (h->version != MB_VERSION_XL || n == 0 || n > MB_XL_MAXPAGES || k >= n || block < k) return 0;
    *first = block - k;
    *count = n;
    return 1;
}

/* Write a mailbox page's 512 data bytes: header + payload, zero padded, for either version. */
static inline void mb_build_data_v(uint8_t *data, uint16_t version, uint16_t seq, uint16_t kind, uint32_t reserved,
                                   const uint8_t *payload, uint16_t len) {
    if (len > MB_PAYLOAD) len = MB_PAYLOAD;
    memset(data, 0, MB_DATA);
    memcpy(data, MB_SIGNATURE, 8);
    mb_put16(data + 8, version);
    mb_put16(data + 10, seq);
    mb_put16(data + 12, kind);
    mb_put16(data + 14, len);
    mb_put32(data + 16, reserved);
    if (len) memcpy(data + 20, payload, len);
}

static inline void mb_build_data(uint8_t *data, uint16_t seq, uint16_t kind, uint32_t reserved,
                                 const uint8_t *payload, uint16_t len) {
    mb_build_data_v(data, MB_VERSION, seq, kind, reserved, payload, len);
}

/* XOR of all 532 bytes of a page; the Lisa wants it to be 0 when the checksum bit is set. */
static inline uint8_t mb_page_xor(const uint8_t *page) {
    uint8_t x = 0;
    for (int i = 0; i < MB_PAGE; i++) x ^= page[i];
    return x;
}

/* Replace a stored page's data with new data, keeping its tag and making tag byte 11 right again:
   the Lisa checks that the XOR of the 20 tag bytes and the 512 data bytes is 0 (OS SOURCE-PROFILEASM
   591-598), so byte 11 absorbs whatever the new data changed. */
static inline void mb_swap_data(uint8_t *page, const uint8_t *newdata) {
    memcpy(page + MB_TAG, newdata, MB_DATA);
    page[11] = 0;
    page[11] = mb_page_xor(page);              /* now the whole page XORs to 0 */
}

/* Version 2 (MacWorks): replace the data and keep the page's checksum property.  MacWorks keeps its checksum in
   tag byte 19; a page that XORed to 0 before the swap XORs to 0 after it, and a page that did not is left alone. */
static inline void mb_swap_data_xl(uint8_t *page, const uint8_t *newdata) {
    int was_ok = mb_page_xor(page) == 0;
    memcpy(page + MB_TAG, newdata, MB_DATA);
    if (was_ok) {
        page[19] = 0;
        page[19] = mb_page_xor(page);
    }
}

#endif
