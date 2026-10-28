// ESProFile_FujiNet.ino -- ESPFuji: the FujiNet MAILBOX.  The drive answers network requests written into
// a file by a Lisa program.  EXPERIMENT, October 2026, Tim O'Connor (bmwcyclist) with Claude.
// Public since 27 Oct 2026 with Alex Anderson-McLeod's agreement (github.com/GyroPilot/ESPFuji).  GPL-3.0.
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
#include "soc/uart_struct.h"
#if defined(CONFIG_IDF_TARGET_ESP32)
#include "hal/uart_ll.h"                              // the plain ESP32: the USB serial is UART0
#endif
// WiFi.h, WiFiClientSecure.h and HTTPClient.h are included at the top of ESProFile.ino (before the emulator defines).

#define ESPFUJI_VERSION "ESPFuji 0.5.1"
#define MB_NET_CORE   0                                // Wi-Fi's core; the interface owns core 1 with interrupts off
#define MB_BODY_MAX   32768                            // the largest page body kept (a Lisa picture is 13 KB)
#define MB_JOIN_MS    8000                             // how long mbSetup waits for the first Wi-Fi join
#ifndef MB_STACK
#define MB_STACK      12288                            // the net task's stack in bytes (0.3: 12288)
#endif
#define MB_FETCH_MS   30000                            // 0.4: an OPEN ends within this, so its reply beats the Lisa's 40 s wait

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
static uint8_t *mbBody = NULL;                         // the body of the page last OPENed (heap: the plain ESP32's DRAM is small)
static uint32_t mbBodyLen = 0;
static volatile int mbWifiReason = 0;                  // the last Wi-Fi disconnect reason (esp_wifi: 15 = wrong password, 201 = no such network)

static void mbWifiEvent(WiFiEvent_t ev, WiFiEventInfo_t info) {
    if (ev == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) mbWifiReason = info.wifi_sta_disconnected.reason;
    else if (ev == ARDUINO_EVENT_WIFI_STA_GOT_IP) mbWifiReason = 0;
}
static bool     mbBodyOpen = false, mbTruncated = false;
static int      mbHttpCode = 0;

// 0.4: where the time goes.  TRACE reports the last fetch, the slowest, and how many ran long or were cut short.
static uint32_t trLastMs = 0, trLastGet = 0, trWorstMs = 0, trFetches = 0, trSlow = 0, trCut = 0;
static uint32_t trServeWorst = 0;                     // the longest any request took to answer (ms)
static uint32_t trStackMin = 0xFFFFFFFF;              // the least stack the net task has had left (bytes)
static char     trWorstStage[8] = "-";
static bool     mbSntpOn = false;
static int      mbNetFails = 0;                       // 0.5: consecutive fetches that could not even connect                     // 0.5: SNTP started once Wi-Fi is up; CLOCK answers UTC

static void trNote(const char *stage, uint32_t tStart, uint32_t tGet) {
    uint32_t ms = millis() - tStart;
    trLastMs = ms;
    trLastGet = tGet;
    if (ms > 20000) trSlow++;
    if (ms > trWorstMs) { trWorstMs = ms; strlcpy(trWorstStage, stage, sizeof trWorstStage); }
}

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
    mbServed++;                                        // the reply stays on offer until the next request: a read the
                                                       // OS makes on its own cannot swallow it (24 Oct, the 2/10)
    return true;
}

// ---- set-up (core 1, before the interface turns interrupts off) ---------------------------------

// espfuji.ini on the SD card: key=value lines.
static void mbReadConfig() {
    memset(&mbCfg, 0, sizeof mbCfg);
    File32 f;
    if (!f.open("/espfuji.ini", O_RDONLY) && !f.open("espfuji.ini", O_RDONLY)) {
        Serial.println("ESPFuji: espfuji.ini did not open (no card, no file, or the volume is not mounted)");
        return;
    }
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
    Serial.printf("ESPFuji: espfuji.ini read: ssid %s, password %s, hostname %s\n",
                  mbCfg.ssid[0] ? "yes" : "MISSING", mbCfg.pass[0] ? "yes" : "MISSING", mbCfg.host[0] ? mbCfg.host : "(none)");
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
static void mbFetch(char *url, char *out, size_t outsz) {
    for (char *q = url; *q && *q != ':' && *q != '/'; q++) // 0.5.1: the scheme in lower case, in place. The Lisa
        *q = tolower((unsigned char)*q);                 // tools write N1:HTTP://..., which the FujiNet accepts and
                                                       // HTTPClient (case-sensitive) refused: "ERROR bad address"
                                                       // on the 2/10, 28 Oct, LisaFuji's first fetch via the drive
    uint32_t tStart = millis(), tGet = 0;
    trFetches++;
    mbBodyLen = 0;
    mbTruncated = false;
    mbBodyOpen = false;
    mbHttpCode = 0;
    if (WiFi.status() != WL_CONNECTED) {
        snprintf(out, outsz, "ERROR no wifi (%s)", mbWifiWord());
        return;
    }
    if (mbBody == NULL) {
        snprintf(out, outsz, "ERROR no page buffer");
        return;
    }
    // 0.5: the client objects persist (static): the crash decoded 27 Oct was lwIP freeing an already-freed TCP
    // segment in its own thread right after a slow fetch closed, i.e. a close/destructor race; one set of
    // objects, no connection reuse, and an explicit stop before the next fetch takes the destructors out of it.
    static HTTPClient http;
    static WiFiClientSecure sec;
    static WiFiClient plain;
    http.end();
    sec.stop();
    plain.stop();
    bool https = strncasecmp(url, "https://", 8) == 0;
    if (https) sec.setInsecure();                      // no certificate store on the drive: the FujiNet does the same by default
    http.setReuse(false);                              // 0.5: Connection: close; never keep a socket between fetches
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setRedirectLimit(2);                          // 0.4: each redirect is a new connection
    http.setConnectTimeout(5000);                      // 0.4: bound every stage (the library defaults add up past 40 s,
    http.setTimeout(8000);                             //      and a TLS handshake may otherwise wait 120 s): one
    if (https) sec.setHandshakeTimeout(8);             //      connection takes at most 5 + 8 + 8 s
    http.setUserAgent(ESPFUJI_VERSION " (Apple Lisa)");
    const char *hdrs[] = { "Location" };
    http.collectHeaders(hdrs, 1);
    bool began = https ? http.begin(sec, url) : http.begin(plain, url);
    if (!began) {
        snprintf(out, outsz, "ERROR bad address");
        return;
    }
    int code = http.GET();
    if (!https && (code == 301 || code == 302 || code == 307 || code == 308) && http.hasHeader("Location")
        && millis() - tStart < 10000) {                // 0.4: a second connection only while there is time for it
        String where = http.header("Location");       // a plain client cannot follow a redirect to https: redo it secure
        if (where.startsWith("https://")) {
            http.end();
            sec.setInsecure();
            sec.setHandshakeTimeout(8);
            if (!http.begin(sec, where)) {
                snprintf(out, outsz, "ERROR bad redirect");
                return;
            }
            code = http.GET();
        }
    }
    tGet = millis() - tStart;
    mbHttpCode = code;
    if (code <= 0) {
        snprintf(out, outsz, "ERROR %s", http.errorToString(code).c_str());
        http.end();
        trNote("get", tStart, tGet);
        if (code == HTTPC_ERROR_CONNECTION_REFUSED || code == HTTPC_ERROR_CONNECTION_LOST) {
            if (++mbNetFails >= 3) {                   // 0.5: three in a row: the link is probably dead although
                mbNetFails = 0;                        //      WiFi.status() still says connected; join again
                WiFi.disconnect();
                delay(200);
                WiFi.begin(mbCfg.ssid, mbCfg.pass);
            }
        }
        return;
    }
    mbNetFails = 0;
    WiFiClient *s = http.getStreamPtr();
    int total = http.getSize();                        // -1 when the length is not known
    uint32_t t0 = millis();
    uint8_t junk[256];
    bool late = false;
    while (s && (http.connected() || s->available()) && millis() - t0 < 8000) {
        if (millis() - tStart >= MB_FETCH_MS) { late = true; break; }   // 0.4: answer the Lisa with what came
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
    if (late) { mbTruncated = true; trCut++; }
    trNote(late ? "cut" : "body", tStart, tGet);
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
        snprintf(txt, sizeof txt, "%s up %lus %s %s req %lu heap %luk/%luk", ESPFUJI_VERSION,   // fits a Lisa line
                 (unsigned long)(millis() / 1000), mbWifiWord(), WiFi.localIP().toString().c_str(),
                 (unsigned long)mbRequests, (unsigned long)(ESP.getFreeHeap() / 1024), (unsigned long)(ESP.getMinFreeHeap() / 1024));
    } else if (strcmp(cmd, "STATUS") == 0) {
        snprintf(txt, sizeof txt, "requests %lu replies %lu served %lu body %lu open %d",
                 (unsigned long)mbRequests, (unsigned long)mbReplies, (unsigned long)mbServed,
                 (unsigned long)mbBodyLen, mbBodyOpen ? 1 : 0);
    } else if (strcmp(cmd, "CLOCK") == 0) {            // 0.5: "OK yyyy mm dd hh mi ss" (UTC, from SNTP)
        time_t now = time(NULL);
        struct tm t;
        if (now < 1700000000 || !gmtime_r(&now, &t))
            snprintf(txt, sizeof txt, "ERROR no time yet (%s)", mbSntpOn ? "SNTP waiting" : "no wifi");
        else
            snprintf(txt, sizeof txt, "OK %04d %02d %02d %02d %02d %02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                     t.tm_hour, t.tm_min, t.tm_sec);
    } else if (strcmp(cmd, "TRACE") == 0) {            // 0.4: timing of the fetches since power-up
        snprintf(txt, sizeof txt, "fetch last %lums (get %lu) worst %lums at %s; %lu fetches, %lu over 20s, %lu cut; "
                 "slowest reply %lums; req %lu rep %lu; stack left %lu of %u",
                 (unsigned long)trLastMs, (unsigned long)trLastGet, (unsigned long)trWorstMs, trWorstStage,
                 (unsigned long)trFetches, (unsigned long)trSlow, (unsigned long)trCut, (unsigned long)trServeWorst,
                 (unsigned long)mbRequests, (unsigned long)mbReplies,
                 (unsigned long)trStackMin, (unsigned)MB_STACK);
    } else if (strcmp(cmd, "WIFI") == 0) {
        snprintf(txt, sizeof txt, "ssid %s status %s ip %s rssi %d reason %d", mbCfg.have ? mbCfg.ssid : "(none)",
                 mbWifiWord(), WiFi.localIP().toString().c_str(), (int)WiFi.RSSI(), mbWifiReason);
    } else if (strcmp(cmd, "SCAN") == 0) {
        WiFi.disconnect();                             // a scan fails while a join is in progress
        delay(300);
        int n = WiFi.scanNetworks();                   // blocks a few seconds; names, channels and strengths only
        size_t at = 0;
        at += snprintf(txt + at, sizeof txt - at, "%d networks:", n);
        for (int i = 0; i < n && at < sizeof txt - 40; i++)
            at += snprintf(txt + at, sizeof txt - at, " [%s ch%d %ddBm]", WiFi.SSID(i).c_str(), WiFi.channel(i), (int)WiFi.RSSI(i));
        WiFi.scanDelete();
        WiFi.begin(mbCfg.ssid, mbCfg.pass);            // resume the join
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

// SSIDs are case-sensitive and people type them from memory: scan once (about 3 s) and, when the configured name
// matches a network only without regard to case, use the name as the network broadcasts it.
static void mbFixSsidCase(void) {
    int n = WiFi.scanNetworks();
    bool exact = false; int alt = -1;
    for (int i = 0; i < n; i++) {
        const char *nm = WiFi.SSID(i).c_str();
        if (strcmp(nm, mbCfg.ssid) == 0) exact = true;
        else if (alt < 0 && strcasecmp(nm, mbCfg.ssid) == 0) alt = i;
    }
    if (!exact && alt >= 0) {
        Serial.printf("ESPFuji: espfuji.ini says %s, the network is %s: using the broadcast name\n", mbCfg.ssid, WiFi.SSID(alt).c_str());
        strlcpy(mbCfg.ssid, WiFi.SSID(alt).c_str(), sizeof mbCfg.ssid);
    } else if (!exact && n >= 0) {
        Serial.printf("ESPFuji: %s not seen in a scan of %d networks (2.4 GHz only; hidden networks do not show)\n", mbCfg.ssid, n);
    }
    WiFi.scanDelete();
}

// A request typed on the USB serial port goes through the same path as a mailbox page (any build): the
// drive can be exercised from the banana with the real emulator running, no Lisa needed.
static bool mbSerialWanted = false;

// 0.4: replies go out the way requests come in, straight through the UART0 transmit FIFO.  Serial.print from this
// core waits for the driver's "FIFO empty" interrupt once the 128-byte FIFO fills, and that interrupt lives on the
// interface core with interrupts off: a reply longer than the FIFO froze the net task for good (26 Oct, TRACE).
static void mbSerialOut(const char *p, size_t n) {
#if !defined(CONFIG_IDF_TARGET_ESP32)
    Serial.write((const uint8_t *)p, n);               // ESP32-S3: USB CDC (setTxTimeoutMs(0) in ESProFile.ino: never blocks). UNTESTED
    return;
#else
    while (n) {
        uint32_t room = uart_ll_get_txfifo_len(&UART0);   // free bytes of the 128; keep well clear of full,
        if (room < 96) { delay(1); continue; }            // a slightly stale count must never overflow it
        uint32_t k = n < 64 ? n : 64;
        uart_ll_write_txfifo(&UART0, (const uint8_t *)p, k);
        p += k;
        n -= k;
    }
#endif
}
static uint16_t mbSerialSeq = 9000;

static void mbSerialLine(const char *txt) {
    static uint8_t page[MB_PAGE];
    memset(page, 0, MB_PAGE);
    mb_put16(page + 4, 0x0101);                        // a pretend S-file ID
    mb_put16(page + 12, 1);
    mb_build_data(page + MB_TAG, ++mbSerialSeq, MB_REQUEST, 0, (const uint8_t *)txt, strlen(txt));
    page[11] = 0;
    page[11] = mb_page_xor(page);
    uint16_t keep = mbFileId;                          // the Lisa's real mailbox file stays known
    mbOnWrite(1234, page);
    mbFileId = keep;
    mbSerialWanted = true;
}

// The UART driver's receive interrupt lives on the interface core, which runs with interrupts off, so
// Serial.available() never sees a byte there: this reads the UART0 receive FIFO directly (128 bytes).
static void mbSerialPoll(void) {
    static char line[200];
    static int n = 0;
#if defined(CONFIG_IDF_TARGET_ESP32)
    while (UART0.status.rxfifo_cnt) {
        char c = (char)UART0.fifo.rw_byte;
#else
    while (Serial.available()) {                       // ESP32-S3: USB CDC. UNTESTED on real hardware
        char c = (char)Serial.read();
#endif
        if (c == '\n' || c == '\r') {
            if (n) { line[n] = 0; mbSerialLine(line); n = 0; }
        } else if (n < (int)sizeof line - 1) {
            line[n++] = c;
        }
    }
}

static void mbTask(void *arg) {
    uint32_t lastJoin = 0;
    for (;;) {
#ifndef ESPFUJI_BENCH
        mbSerialPoll();
#endif
        if (mbCfg.have && WiFi.status() != WL_CONNECTED && millis() - lastJoin > 15000) {
            static int tries = 0;
            if (++tries % 4 == 0) {                    // every fourth try: the boot-time scan can miss (seen right
                WiFi.disconnect();                     // after a flash), so look the name up again and start over
                delay(300);
                mbFixSsidCase();
                WiFi.begin(mbCfg.ssid, mbCfg.pass);
            } else {
                WiFi.reconnect();                      // no flash write in a reconnect
            }
            lastJoin = millis();
        }
        if (!mbSntpOn && WiFi.status() == WL_CONNECTED) {   // 0.5: the clock, once the network is there
            configTime(0, 0, "pool.ntp.org", "time.nist.gov");
            mbSntpOn = true;
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
            uint32_t tServe = millis();
            mbServe(payload, len, out, &outlen, &echo);
            tServe = millis() - tServe;
            if (tServe > trServeWorst) trServeWorst = tServe;
            uint32_t hwm = uxTaskGetStackHighWaterMark(NULL);
            if (hwm < trStackMin) trStackMin = hwm;
            mbRep.block = echo ? echo : block;
            mbRep.seq = seq;
            mbRep.len = outlen;
            memcpy(mbRep.payload, out, outlen);
            mbRep.ready = true;
            mbReplies++;
            if (mbSerialWanted) {                      // typed on the USB port: print it there, not to the Lisa
                mbSerialWanted = false;
                mbRep.ready = false;
                char line[MB_PAYLOAD + 48];             // 0.5: on the task's stack (static DRAM is full)
                int n = snprintf(line, sizeof line, "reply seq %u, %u bytes: ", seq, outlen);
                for (int i = 0; i < outlen && n < (int)sizeof line - 3; i++) {
                    char c = (char)out[i];
                    line[n++] = (c >= 32 && c < 127) || c == '\n' ? c : '.';
                }
                line[n++] = '\r';
                line[n++] = '\n';
                mbSerialOut(line, n);              // 0.4: never Serial.print from this core (see mbSerialOut)
            }
        }
        delay(5);
    }
}

// From emulatorSetup(), BEFORE the interface core turns interrupts off (and from the bench).
void mbSetup() {
    mbReq.pending = false;
    mbRep.ready = false;
    mbFileId = 0;
    if (mbBody == NULL) mbBody = (uint8_t *)malloc(MB_BODY_MAX);   // once, here, before the interface core runs
    mbReadConfig();
    if (mbCfg.have) {
        WiFi.persistent(false);                        // never store the credentials: no flash writes later
        WiFi.onEvent(mbWifiEvent);
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);                          // 0.5: no modem sleep. 27 Oct: with it on, the drive fell off
                                                       // the access point minutes into a soak while still saying
                                                       // "connected" (pings dead, fetches stalled); ~80 mA more.
        if (mbCfg.host[0]) WiFi.setHostname(mbCfg.host);
        mbFixSsidCase();                               // "Wifi" in the file, "WiFi" on the air: take the name as broadcast
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
    xTaskCreatePinnedToCore(mbTask, "espfuji", MB_STACK, NULL, 1, NULL, MB_NET_CORE);
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
    if (!SDCard.begin(SD_CONFIG)) {
        Serial.println("ESPFuji bench: SD card did NOT mount (SDCard.begin failed), so no espfuji.ini");
    } else {
        Serial.printf("ESPFuji bench: SD card mounted, %lu MB, FAT%d, cluster %lu bytes; root:",
                      (unsigned long)(SDCard.card()->sectorCount() / 2048), SDCard.fatType(),
                      (unsigned long)SDCard.bytesPerCluster());
        FatFile root, e;
        char nm[64];
        if (root.open("/")) {
            while (e.openNext(&root, O_RDONLY)) {
                e.getName(nm, sizeof nm);
                Serial.printf(" %s", nm);
                e.close();
            }
            root.close();
        } else {
            Serial.print(" (root did not open)");
        }
        Serial.println();
    }
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
