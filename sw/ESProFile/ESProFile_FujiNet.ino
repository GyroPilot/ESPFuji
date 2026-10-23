// ESProFile_FujiNet.ino -- ESPFuji: the FujiNet MAILBOX.  The drive answers network requests written into
// a file by a Lisa program.  EXPERIMENT, October 2026, Tim O'Connor (bmwcyclist) with Claude.
// NOT published; nothing goes anywhere until Alex Anderson has seen it.  GPL-3.0.
//
// Compiled in only with FUJINET_MAILBOX defined (see ESProFile.ino).  The three hooks in the
// emulator are tiny: mbSetup() from emulatorSetup(), mbOnWrite() in writeDrive() before the block
// is stored, mbOnRead() in readDrive() after the block is read from the image.  Everything else
// is here.  The format of a mailbox page is in mailbox_core.h.
//
// TWO CORES.  arduino-esp32 runs setup() and loop() on core 1, and emulatorSetup() turns interrupts
// OFF there for good (Alex: FreeRTOS preemption would break the ProFile timing).  The ESP32's Wi-Fi
// and TCP/IP tasks live on core 0.  So the network task is pinned to core 0, next to Wi-Fi, and the
// two cores share plain memory only, with one flag each way: req.pending (interface -> network)
// and rep.ready (network -> interface).  Nothing here writes flash after the interface core has
// started (Alex: that stalls the ESP); the one place flash may be written -- Wi-Fi's first
// calibration -- happens in mbSetup(), before the interrupts go off, and the credentials are never
// stored (WiFi.persistent(false)).
//
// THE REQUESTS (text in the page's payload; a reply is text, or bytes for READ):
//   HELLO                  -> "ESPFuji 0.2 up <s> s, wifi <status>, ip <a.b.c.d>, requests <n>"
//   ECHO <text>            -> "OK <text>"
//   STATUS                 -> counters
//   WIFI                   -> "ssid <name> status <n> ip <a.b.c.d> rssi <dBm>"
//   OPEN <url>             -> fetches the whole body (GET, redirects followed, TLS unchecked) into a
//                             32 KB buffer: "OK <bytes> <http code>[ truncated]" or "ERROR <why>"
//   READ <offset> <count>  -> count bytes of the body from offset (count <= 492), binary; the reply's
//                             reserved field is the offset, so a retried READ is harmless
//   CLOSE                  -> drops the body
//
// BENCH MODE (ESPFUJI_BENCH in ESProFile.ino): no ProFile emulation; the same requests are typed on
// the USB serial port and the replies printed, so the network side is tested on a PC before the
// drive ever meets a Lisa.
//
// SD CARD SET-UP: a text file espfuji.ini in the root of the ESProFile's SD card:
//   ssid=<network name>
//   password=<password>
//   hostname=espfuji          (optional)

#ifdef FUJINET_MAILBOX

#include "mailbox_core.h"
// WiFi.h, WiFiClientSecure.h and HTTPClient.h are included at the top of ESProFile.ino (before the emulator defines).

#define ESPFUJI_VERSION "ESPFuji 0.2"
#define MB_NET_CORE   0                                // Wi-Fi's core; the interface owns core 1 with interrupts off
#define MB_BODY_MAX   32768                            // the largest page body kept (a Lisa picture is 13 KB)
#define MB_JOIN_MS    8000                             // how long mbSetup waits for the first Wi-Fi join

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
    uint32_t block;                                    // echoed in the reply header (a READ echoes its offset)
    uint16_t seq;
    uint16_t len;
    uint8_t  payload[MB_PAYLOAD];
};

struct MbConfig {
    char ssid[33];
    char pass[65];
    char host[33];
    bool have;
};

static MbRequest mbReq;
static MbReply   mbRep;
static MbConfig  mbCfg;
static volatile uint16_t mbFileId = 0;                 // 0: no mailbox seen yet since power-up
static uint32_t mbRequests = 0, mbReplies = 0, mbServed = 0;
static uint8_t  mbReplyData[MB_DATA];                  // the reply page's data, built once per reply
static uint8_t  mbBody[MB_BODY_MAX];                   // the body of the page last OPENed
static uint32_t mbBodyLen = 0;
static bool     mbBodyOpen = false, mbTruncated = false;
static int      mbHttpCode = 0;

// ---- the interface core (called with interrupts off; must not block, must be quick) -------------

// A block is about to be stored.  blk = the 532 bytes on the bus (tag, then data).  Returns true
// when it was a mailbox request (the caller stores the block all the same).
bool mbOnWrite(uint32_t blockNum, const uint8_t *blk) {
    mb_header_t h;
    if (!mb_parse(blk, &h)) return false;
    if (h.kind != MB_REQUEST) return false;
    mbFileId = mb_tag_fileid(blk);
    // a request the net task has not taken yet is simply replaced: the newer one wins
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

// ---- set-up (core 1, before the interface turns interrupts off) ---------------------------------

// espfuji.ini on the SD card: key=value lines.
static void mbReadConfig() {
    memset(&mbCfg, 0, sizeof mbCfg);
    File32 f;
    if (!f.open("/espfuji.ini", O_RDONLY)) return;
    char line[160];
    while (f.fgets(line, sizeof line) > 0) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == ';' || *p == 0) continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = eq + 1;
        char *e = v + strlen(v);
        while (e > v && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ')) *--e = 0;
        if (strcmp(p, "ssid") == 0) strncpy(mbCfg.ssid, v, sizeof mbCfg.ssid - 1);
        else if (strcmp(p, "password") == 0) strncpy(mbCfg.pass, v, sizeof mbCfg.pass - 1);
        else if (strcmp(p, "hostname") == 0) strncpy(mbCfg.host, v, sizeof mbCfg.host - 1);
    }
    f.close();
    mbCfg.have = mbCfg.ssid[0] != 0;
}

static const char *mbWifiWord() {
    switch (WiFi.status()) {
        case WL_CONNECTED: return "connected";
        case WL_NO_SSID_AVAIL: return "no such network";
        case WL_CONNECT_FAILED: return "join failed";
        case WL_DISCONNECTED: return "disconnected";
        case WL_IDLE_STATUS: return "idle";
        default: return "unknown";
    }
}

// ---- the network core -----------------------------------------------------------------------------

// OPEN: fetch the whole body now; the Lisa reads it in pages with READ.
static void mbFetch(const char *url, char *out, size_t outsz) {
    mbBodyLen = 0;
    mbTruncated = false;
    mbBodyOpen = false;
    mbHttpCode = 0;
    if (WiFi.status() != WL_CONNECTED) {
        snprintf(out, outsz, "ERROR no wifi (%s)", mbWifiWord());
        return;
    }
    HTTPClient http;
    WiFiClientSecure sec;
    WiFiClient plain;
    bool https = strncasecmp(url, "https://", 8) == 0;
    if (https) sec.setInsecure();                      // no certificate store on the drive: the FujiNet does the same by default
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    http.setUserAgent(ESPFUJI_VERSION " (Apple Lisa)");
    bool began = https ? http.begin(sec, url) : http.begin(plain, url);
    if (!began) {
        snprintf(out, outsz, "ERROR bad address");
        return;
    }
    int code = http.GET();
    mbHttpCode = code;
    if (code <= 0) {
        snprintf(out, outsz, "ERROR %s", http.errorToString(code).c_str());
        http.end();
        return;
    }
    WiFiClient *s = http.getStreamPtr();
    int total = http.getSize();                        // -1 when the length is not known
    uint32_t t0 = millis();
    uint8_t junk[256];
    while (s && (http.connected() || s->available()) && millis() - t0 < 20000) {
        if (total >= 0 && mbBodyLen >= (uint32_t)total) break;
        size_t avail = s->available();
        if (avail == 0) {
            delay(5);
            continue;
        }
        if (mbBodyLen >= MB_BODY_MAX) {
            mbTruncated = true;
            s->read(junk, avail > sizeof junk ? sizeof junk : avail);
        } else {
            size_t room = MB_BODY_MAX - mbBodyLen;
            int n = s->read(mbBody + mbBodyLen, avail > room ? room : avail);
            if (n > 0) mbBodyLen += n;
        }
        t0 = millis();
    }
    http.end();
    mbBodyOpen = true;
    snprintf(out, outsz, "OK %lu %d%s", (unsigned long)mbBodyLen, code, mbTruncated ? " truncated" : "");
}

// What the drive does with a request.  Text in, text or bytes out.
static void mbServe(const uint8_t *req, uint16_t len, uint8_t *out, uint16_t *outlen, uint32_t *echo) {
    char cmd[MB_PAYLOAD + 1];
    char txt[MB_PAYLOAD + 1];
    memcpy(cmd, req, len);
    cmd[len] = 0;
    txt[0] = 0;
    *echo = 0;
    if (strncmp(cmd, "ECHO ", 5) == 0) {
        snprintf(txt, sizeof txt, "OK %s", cmd + 5);
    } else if (strcmp(cmd, "HELLO") == 0) {
        snprintf(txt, sizeof txt, "%s up %lu s, wifi %s, ip %s, requests %lu", ESPFUJI_VERSION,
                 (unsigned long)(millis() / 1000), mbWifiWord(), WiFi.localIP().toString().c_str(),
                 (unsigned long)mbRequests);
    } else if (strcmp(cmd, "STATUS") == 0) {
        snprintf(txt, sizeof txt, "requests %lu replies %lu served %lu body %lu open %d",
                 (unsigned long)mbRequests, (unsigned long)mbReplies, (unsigned long)mbServed,
                 (unsigned long)mbBodyLen, mbBodyOpen ? 1 : 0);
    } else if (strcmp(cmd, "WIFI") == 0) {
        snprintf(txt, sizeof txt, "ssid %s status %s ip %s rssi %d", mbCfg.have ? mbCfg.ssid : "(none)",
                 mbWifiWord(), WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    } else if (strncmp(cmd, "OPEN ", 5) == 0) {
        mbFetch(cmd + 5, txt, sizeof txt);
    } else if (strncmp(cmd, "READ ", 5) == 0) {
        unsigned long off = 0, cnt = 0;
        if (sscanf(cmd + 5, "%lu %lu", &off, &cnt) != 2 || !mbBodyOpen) {
            snprintf(txt, sizeof txt, "ERROR %s", mbBodyOpen ? "READ wants offset and count" : "nothing open");
        } else {
            if (cnt > MB_PAYLOAD) cnt = MB_PAYLOAD;
            if (off > mbBodyLen) off = mbBodyLen;
            if (off + cnt > mbBodyLen) cnt = mbBodyLen - off;
            memcpy(out, mbBody + off, cnt);
            *outlen = (uint16_t)cnt;
            *echo = (uint32_t)off;
            return;
        }
    } else if (strcmp(cmd, "CLOSE") == 0) {
        mbBodyOpen = false;
        mbBodyLen = 0;
        snprintf(txt, sizeof txt, "OK closed");
    } else {
        snprintf(txt, sizeof txt, "ERROR unknown request");
    }
    size_t n = strlen(txt);
    if (n > MB_PAYLOAD) n = MB_PAYLOAD;
    memcpy(out, txt, n);
    *outlen = (uint16_t)n;
}

static void mbTask(void *arg) {
    uint32_t lastJoin = 0;
    for (;;) {
        if (mbCfg.have && WiFi.status() != WL_CONNECTED && millis() - lastJoin > 15000) {
            WiFi.reconnect();                          // no flash write in a reconnect
            lastJoin = millis();
        }
        if (mbReq.pending) {
            uint16_t seq = mbReq.seq, len = mbReq.len;
            uint32_t block = mbReq.block;
            uint8_t payload[MB_PAYLOAD];
            memcpy(payload, mbReq.payload, len);
            mbReq.pending = false;                     // taken; a newer request may now arrive
            uint8_t out[MB_PAYLOAD];
            uint16_t outlen = 0;
            uint32_t echo = 0;
            mbServe(payload, len, out, &outlen, &echo);
            mbRep.block = echo ? echo : block;
            mbRep.seq = seq;
            mbRep.len = outlen;
            memcpy(mbRep.payload, out, outlen);
            mbRep.ready = true;
            mbReplies++;
        }
        delay(5);
    }
}

// From emulatorSetup(), BEFORE the interface core turns interrupts off (and from the bench).
void mbSetup() {
    mbReq.pending = false;
    mbRep.ready = false;
    mbFileId = 0;
    mbReadConfig();
    if (mbCfg.have) {
        WiFi.persistent(false);                        // never store the credentials: no flash writes later
        WiFi.mode(WIFI_STA);
        if (mbCfg.host[0]) WiFi.setHostname(mbCfg.host);
        WiFi.begin(mbCfg.ssid, mbCfg.pass);
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < MB_JOIN_MS) delay(100);
        if (WiFi.status() == WL_CONNECTED)
            Serial.printf("ESPFuji: on %s as %s\n", mbCfg.ssid, WiFi.localIP().toString().c_str());
        else
            Serial.printf("ESPFuji: %s not joined yet (%s); keeps trying\n", mbCfg.ssid, mbWifiWord());
    } else {
        Serial.println("ESPFuji: no espfuji.ini on the SD card: mailbox without network");
    }
    xTaskCreatePinnedToCore(mbTask, "espfuji", 12288, NULL, 1, NULL, MB_NET_CORE);
    Serial.println("ESPFuji mailbox: on (experiment)");
}

// ---- bench mode: the same requests from the USB serial port, no ProFile emulation --------------

#ifdef ESPFUJI_BENCH
static uint16_t benchSeq = 0;

static void benchRequest(const char *txt) {
    static uint8_t page[MB_PAGE];
    memset(page, 0, MB_PAGE);
    mb_put16(page + 4, 0x0101);                        // a pretend S-file ID
    mb_put16(page + 12, 1);
    mb_build_data(page + MB_TAG, ++benchSeq, MB_REQUEST, 0, (const uint8_t *)txt, strlen(txt));
    page[11] = 0;
    page[11] = mb_page_xor(page);
    uint32_t t0 = millis();
    mbOnWrite(1234, page);
    while (!mbRep.ready && millis() - t0 < 40000) delay(10);
    if (!mbRep.ready) {
        Serial.println("(no reply in 40 s)");
        return;
    }
    memset(page, 0, MB_PAGE);
    mb_put16(page + 4, 0x0101);
    mbOnRead(page);
    mb_header_t h;
    if (!mb_parse(page, &h)) {
        Serial.println("(the reply page did not parse)");
        return;
    }
    Serial.printf("reply seq %u, %u bytes, %lu ms, field %lu: ", h.seq, h.len, (unsigned long)(millis() - t0), (unsigned long)h.reserved);
    for (int i = 0; i < h.len; i++) {
        char c = (char)h.payload[i];
        Serial.print((c >= 32 && c < 127) || c == '\n' ? c : '.');
    }
    Serial.println();
}

void mbBench() {
    SD_SPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    if (!SDCard.begin(SD_CONFIG)) Serial.println("ESPFuji bench: no SD card, so no espfuji.ini");
    mbSetup();
    Serial.println("ESPFuji bench: type a request and RETURN -- HELLO, WIFI, ECHO x, OPEN <url>, READ <off> <n>, CLOSE");
    char line[200];
    int n = 0;
    for (;;) {
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') {
                if (n) {
                    line[n] = 0;
                    benchRequest(line);
                    n = 0;
                }
            } else if (n < (int)sizeof line - 1) {
                line[n++] = c;
            }
        }
        delay(10);
    }
}
#endif

#endif
