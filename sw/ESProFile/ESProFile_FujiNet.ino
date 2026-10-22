// ESProFile_FujiNet.ino -- the FujiNet MAILBOX: the drive answers network requests written into a
// file by a Lisa program.  EXPERIMENT, October 2026, Tim O'Connor (bmwcyclist) with Claude.
// NOT published; nothing goes anywhere until Alex Anderson has seen it.  GPL-3.0.
//
// Compiled in only with FUJINET_MAILBOX defined (see ESProFile.ino).  The three hooks in the
// emulator are tiny: mbSetup() from emulatorSetup(), mbOnWrite() in writeDrive() before the block
// is stored, mbOnRead() in readDrive() after the block is read from the image.  Everything else
// is here.  The format of a mailbox page is in mailbox_core.h.
//
// TWO CORES, Alex's rule: the ProFile interface runs on the interface core with interrupts OFF;
// the network work runs in a FreeRTOS task pinned to the OTHER core, with interrupts on.  The two
// share plain memory only (no queues, no mutexes from the interface core: it cannot block), with
// one flag each way: req.pending (interface -> network) and rep.ready (network -> interface).
// Nothing in this file writes flash while the interface core runs (Alex: that locks the ESP up).

#ifdef FUJINET_MAILBOX

#include "mailbox_core.h"

#define MB_NET_CORE 1                                  // the core the ProFile interface does NOT use

struct MbRequest {
    volatile bool pending;                             // set by the interface core, cleared by the net task
    uint32_t block;                                    // the block the request was written on
    uint16_t fileid;                                   // the mailbox file's S-file ID (from the tag)
    uint16_t relpage;                                  // which page of the file
    uint16_t seq;
    uint16_t len;
    uint8_t  payload[MB_PAYLOAD];
};

struct MbReply {
    volatile bool ready;                               // set by the net task, cleared when served
    uint32_t block;                                    // echoed in the reply header
    uint16_t seq;
    uint16_t len;
    uint8_t  payload[MB_PAYLOAD];
};

static MbRequest mbReq;
static MbReply   mbRep;
static volatile uint16_t mbFileId = 0;                 // 0: no mailbox seen yet since power-up
static uint32_t mbRequests = 0, mbReplies = 0, mbServed = 0;
static uint8_t  mbReplyData[MB_DATA];                  // the reply page's data, built once per reply

// ---- the interface core (called with interrupts off; must not block, must be quick) -------------

// A block is about to be stored.  blk = the 532 bytes on the bus (tag, then data).  Returns true
// when it was a mailbox request (the caller stores the block all the same).
bool mbOnWrite(uint32_t blockNum, const uint8_t *blk) {
    mb_header_t h;
    if (!mb_parse(blk, &h)) return false;
    if (h.kind != MB_REQUEST) return false;
    mbFileId = mb_tag_fileid(blk);
    if (mbReq.pending) {                               // a request the net task has not taken yet: the
        // newer one wins (the Lisa retried, or moved on); nothing to free, nothing blocks
    }
    mbReq.block = blockNum;
    mbReq.fileid = mbFileId;
    mbReq.relpage = mb_tag_relpage(blk);
    mbReq.seq = h.seq;
    mbReq.len = h.len;
    memcpy(mbReq.payload, h.payload, h.len);
    mbRep.ready = false;                               // an old reply is not the answer to this
    mbReq.pending = true;
    mbRequests++;
    return true;
}

// A block has just been read from the image into blk (532 bytes) and is about to go to the host.
// If it belongs to the mailbox file and a reply is waiting, the reply goes instead.  Returns true
// when the data was replaced.
bool mbOnRead(uint8_t *blk) {
    if (!mbRep.ready) return false;
    if (mbFileId == 0 || mb_tag_fileid(blk) != mbFileId) return false;
    mb_build_data(mbReplyData, mbRep.seq, MB_REPLY, mbRep.block, mbRep.payload, mbRep.len);
    mb_swap_data(blk, mbReplyData);                    // keeps the tag; fixes the checksum byte
    mbRep.ready = false;                               // served once; the Lisa keeps what it read
    mbServed++;
    return true;
}

// ---- the network core -----------------------------------------------------------------------------

// What the drive does with a request.  The prototype understands a few text commands; the
// FujiNet network device (OPEN/STATUS/READ/CLOSE on N:) comes next, once Wi-Fi is on.
static void mbServe(const uint8_t *req, uint16_t len, uint8_t *out, uint16_t *outlen) {
    char cmd[MB_PAYLOAD + 1];
    memcpy(cmd, req, len);
    cmd[len] = 0;
    String r;
    if (strncmp(cmd, "ECHO ", 5) == 0) {
        r = String("OK ") + (cmd + 5);
    } else if (strcmp(cmd, "HELLO") == 0) {
        r = String("ESProFile mailbox 1, up ") + String(millis() / 1000) + " s, " + String(mbRequests) + " requests";
    } else if (strcmp(cmd, "STATUS") == 0) {
        r = String("requests ") + String(mbRequests) + " replies " + String(mbReplies) + " served " + String(mbServed);
    } else if (strncmp(cmd, "GET ", 4) == 0) {
        r = "ERROR no network yet: the Wi-Fi side is the next step";
    } else {
        r = "ERROR unknown request";
    }
    *outlen = r.length() > MB_PAYLOAD ? MB_PAYLOAD : r.length();
    memcpy(out, r.c_str(), *outlen);
}

static void mbTask(void *arg) {
    for (;;) {
        if (mbReq.pending) {
            uint16_t seq = mbReq.seq, len = mbReq.len;
            uint32_t block = mbReq.block;
            uint8_t payload[MB_PAYLOAD];
            memcpy(payload, mbReq.payload, len);
            mbReq.pending = false;                     // taken; a newer request may now arrive
            uint8_t out[MB_PAYLOAD];
            uint16_t outlen = 0;
            mbServe(payload, len, out, &outlen);
            mbRep.block = block;
            mbRep.seq = seq;
            mbRep.len = outlen;
            memcpy(mbRep.payload, out, outlen);
            mbRep.ready = true;
            mbReplies++;
        }
        delay(5);
    }
}

// From emulatorSetup(), BEFORE the interface core turns interrupts off.
void mbSetup() {
    mbReq.pending = false;
    mbRep.ready = false;
    mbFileId = 0;
    xTaskCreatePinnedToCore(mbTask, "fujinet-mailbox", 8192, NULL, 1, NULL, MB_NET_CORE);
    Serial.println("FujiNet mailbox: on (experiment)");
}

#endif
