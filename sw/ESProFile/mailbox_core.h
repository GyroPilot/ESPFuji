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
#define MB_REQUEST   1
#define MB_REPLY     2
#define MB_STATUS    3

static const uint8_t MB_SIGNATURE[8] = { 'L', 'I', 'S', 'A', 'F', 'U', 'J', 'I' };

typedef struct {
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
    if (mb_get16(d + 8) != MB_VERSION) return 0;
    hdr->seq = mb_get16(d + 10);
    hdr->kind = mb_get16(d + 12);
    hdr->len = mb_get16(d + 14);
    hdr->reserved = mb_get32(d + 16);
    hdr->payload = d + 20;
    if (hdr->len > MB_PAYLOAD) return 0;
    return 1;
}

/* Write a mailbox page's 512 data bytes: header + payload, zero padded. */
static inline void mb_build_data(uint8_t *data, uint16_t seq, uint16_t kind, uint32_t reserved,
                                 const uint8_t *payload, uint16_t len) {
    if (len > MB_PAYLOAD) len = MB_PAYLOAD;
    memset(data, 0, MB_DATA);
    memcpy(data, MB_SIGNATURE, 8);
    mb_put16(data + 8, MB_VERSION);
    mb_put16(data + 10, seq);
    mb_put16(data + 12, kind);
    mb_put16(data + 14, len);
    mb_put32(data + 16, reserved);
    if (len) memcpy(data + 20, payload, len);
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

#endif
