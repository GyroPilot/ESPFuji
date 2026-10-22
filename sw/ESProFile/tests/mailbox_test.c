/* mailbox_test.c -- a PC check of mailbox_core.h: build a request page the way the Lisa would, parse
   it, swap in a reply, and prove the Lisa's acceptance rule (the whole 532-byte page XORs to 0).
   Build:  gcc -I.. -o mailbox_test mailbox_test.c     Run:  ./mailbox_test  (prints PASS or the failure) */
#include <stdio.h>
#include <stdlib.h>
#include "../mailbox_core.h"

static int fails = 0;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL: %s\n", what); fails++; } } while (0)

/* A page as the Lisa OS would write it: a plausible tag (file id 0x0123, rel page 2, checksum bit on),
   then our request data; then the checksum byte made right so the page XORs to 0. */
static void lisa_page(uint8_t *page, uint16_t fileid, uint16_t relpage, const uint8_t *data) {
    memset(page, 0, MB_PAGE);
    page[0] = 0; page[1] = 2;                  /* version */
    mb_put16(page + 4, fileid);
    page[6] = 0x80; page[7] = 0;               /* dataused with the checksum bit */
    page[8] = 0; page[9] = 1; page[10] = 0x44; /* abs page */
    mb_put16(page + 12, relpage);
    page[14] = 0xFF; page[15] = 0xFF; page[16] = 0xFF;
    page[17] = 0xFF; page[18] = 0xFF; page[19] = 0xFF;
    memcpy(page + MB_TAG, data, MB_DATA);
    page[11] = 0;
    page[11] = mb_page_xor(page);
}

int main(void) {
    uint8_t data[MB_DATA], page[MB_PAGE], reply[MB_DATA];
    mb_header_t h;
    const char *req = "ECHO hello from the Lisa";

    /* 1. a request page */
    mb_build_data(data, 7, MB_REQUEST, 0, (const uint8_t *)req, (uint16_t)strlen(req));
    lisa_page(page, 0x0123, 2, data);
    CHECK(mb_page_xor(page) == 0, "a Lisa-written page XORs to 0");
    CHECK(mb_parse(page, &h) == 1, "the request page parses");
    CHECK(h.seq == 7 && h.kind == MB_REQUEST && h.len == strlen(req), "header fields");
    CHECK(memcmp(h.payload, req, h.len) == 0, "payload");
    CHECK(mb_tag_fileid(page) == 0x0123 && mb_tag_relpage(page) == 2, "tag file id and rel page");

    /* 2. not a mailbox page */
    page[MB_TAG] = 'X';
    CHECK(mb_parse(page, &h) == 0, "a page without the signature is ignored");
    page[MB_TAG] = 'L';

    /* 3. a reply swapped into a stored page: tag kept, checksum right */
    const char *ans = "OK hello from the Lisa";
    mb_build_data(reply, 7, MB_REPLY, 0x1234, (const uint8_t *)ans, (uint16_t)strlen(ans));
    mb_swap_data(page, reply);
    CHECK(mb_page_xor(page) == 0, "after the swap the page still XORs to 0 (byte 11 fixed)");
    CHECK(mb_tag_fileid(page) == 0x0123 && mb_tag_relpage(page) == 2, "the tag survived the swap");
    CHECK(mb_parse(page, &h) == 1 && h.kind == MB_REPLY && h.seq == 7 && h.reserved == 0x1234, "the reply parses");
    CHECK(h.len == strlen(ans) && memcmp(h.payload, ans, h.len) == 0, "reply payload");

    /* 4. a payload too long is refused by the parser */
    mb_put16(page + MB_TAG + 14, MB_PAYLOAD + 1);
    CHECK(mb_parse(page, &h) == 0, "an over-long length is refused");

    if (fails == 0) printf("PASS: mailbox_core.h, 11 checks\n");
    return fails ? 1 : 0;
}
