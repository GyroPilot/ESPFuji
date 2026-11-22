/* Drive Test XL: a disk test for a Macintosh XL (MacWorks) on an ESProFile running ESPFuji.  1.0, 22 Nov 2026 (0.1d1 the same day).
 *
 * Tim, 22 Nov 2026: "would it make sense to build a new app specifically for testing?"  Until now a firmware build was
 * judged on the XL by watching a Finder copy.  This writes a file of known data to the volume it sits on, reads it back
 * past the File Manager's cache and compares every byte, pass after pass, while the drive fetches a web page over
 * Wi-Fi (the case in which ESPFuji 1.1.2 lost bytes), and shows the drive's own counters (STATUS: refused, odd)
 * before and after.  The mailbox code is ESPFuji XL's (protocol version 2, MAILBOX.DATA beside the application).
 *
 * 68000 code, System 6 and 7, built with Retro68.  No Color QuickDraw, no System 7 calls.
 */
#include <Multiverse.h>
#include <string.h>
#include <stdio.h>
#include "xlproto.h"

/* AllocContig: Retro68 has no glue for it, so the trap itself (_Allocate with the contiguous bit, $A210). */
#pragma parameter __D0 PBAllocContigTrap(__A0)
pascal OSErr PBAllocContigTrap(ParmBlkPtr pb) M68K_INLINE(0xA210);

#ifndef noCacheMask
#define noCacheMask 0x20                         /* ioPosMode: read/write past the File Manager's cache */
#endif

enum { mApple = 128, mFile, mEdit, mNet, mTest };
enum { iAbout = 1 };
enum { iQuit = 1 };
enum { iHello = 1, iWifi, iStatus, iClearWin = 5 };
enum { iQuick = 1, iLong, iNetwork = 4 };

#define kPages      64                           /* MAILBOX.DATA: 64 pages of 512 bytes, one contiguous run */
#define kWaitTicks  (45L * 60)                   /* an OPEN may take the drive 30 s; give it 45 */
#define kChunk      16384L                       /* the test file is written and read in pieces of this size */
#define kChunks     16                           /* 16 x 16 KB = 256 KB a pass */
#define kKeepText   24000                        /* TextEdit's 32 KB limit, with room */
#define kNewsURL    "https://fujinet.online/8bitnews/news.php?t=lf&ps=60x14&l=9&c=top&p=1"

static WindowPtr gWin;
static TEHandle  gTE;
static Boolean   gQuit, gHasWNE;
static short     gMbRef = 0;                     /* MAILBOX.DATA, open; 0 = no mailbox */
static unsigned short gSeq = 0, gNextK = 0;
static Boolean   gNetDuring = true;              /* fetch a page while the file is written and read */

/* ---------- small things ---------- */

static Boolean TrapAvailable(short trap)
{
    return NGetTrapAddress(trap, kToolboxTrapType) != NGetTrapAddress(0xA89F, kToolboxTrapType);
}

/* Keep the newest lines in view: TextEdit on the XL has no auto-scroll, so the oldest text goes instead. */
static void FitText(void)
{
    TEPtr te = *gTE;
    short h = te->viewRect.bottom - te->viewRect.top;
    while ((*gTE)->nLines > 1 && (long)(*gTE)->nLines * (*gTE)->lineHeight > h) {
        short end = (*gTE)->lineStarts[1];
        TESetSelect(0, end, gTE);
        TEDelete(gTE);
    }
    if ((*gTE)->teLength > kKeepText) { TESetSelect(0, (*gTE)->teLength - kKeepText, gTE); TEDelete(gTE); }
    TESetSelect((*gTE)->teLength, (*gTE)->teLength, gTE);
}

/* Append text: LF becomes CR, other control characters go. */
/* Command-period while a long page is being drawn stops the drawing: the rest of that page is skipped until the next
 * command (Tim, 4 Nov 2026: it stopped while asking, not while filling the window). */
static Boolean gStopDraw;
static Boolean CancelPressed(void);
static void Out(const char *s, long n)
{
    static char buf[1024];                       /* 1 KB at a time: TEInsert rewraps the whole window each time */
    long i, j = 0;
    if (gStopDraw) return;
    SetPort(gWin);
    TESetSelect((*gTE)->teLength, (*gTE)->teLength, gTE);
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\n') c = '\r';
        else if (c == '\r' && i + 1 < n && s[i + 1] == '\n') continue;
        else if ((unsigned char)c < 32 && c != '\r' && c != '\t') continue;
        buf[j++] = c;
        if (j == sizeof buf) {
            TEInsert(buf, j, gTE); j = 0;
            if (CancelPressed()) { gStopDraw = true; TEInsert("\r(stopped)\r", 11, gTE); break; }
        }
    }
    if (j && !gStopDraw) TEInsert(buf, j, gTE);
    FitText();
}
static void Say(const char *s) { Out(s, (long)strlen(s)); Out("\r", 1); }

/* Command-period (or Escape) stops a request.  0.1d4: on Tim's XL it did nothing.  Two causes: the wait read the
 * mailbox back to back, leaving the keyboard almost no time to be heard (the disk driver keeps interrupts off while
 * it transfers), and a key event does not reach a busy program anyway.  So the wait now idles between reads
 * (Idle, below), and the keys are also read directly with GetKeys, a press remembered for half a second. */
static Boolean CancelPressed(void)
{
    static unsigned long cmdAt, dotAt;
    EventRecord e;
    unsigned char k[16];                         /* the KeyMap: key code n is bit n & 7 of byte n >> 3 */
    unsigned long now = TickCount();
    if (EventAvail(keyDownMask, &e)) {
        char c = (char)(e.message & charCodeMask);
        if (((e.modifiers & cmdKey) && (c == '.' || c == '>')) || c == 0x1B) { GetNextEvent(keyDownMask, &e); return true; }
    }
    GetKeys(k);
    if ((k[0x37 >> 3] >> (0x37 & 7)) & 1) cmdAt = now;           /* Command */
    if ((k[0x2F >> 3] >> (0x2F & 7)) & 1) dotAt = now;           /* period */
    if ((k[0x35 >> 3] >> (0x35 & 7)) & 1) return true;           /* Escape */
    if (cmdAt && dotAt && now - cmdAt < 30 && now - dotAt < 30) { cmdAt = dotAt = 0; return true; }
    return false;
}

/* Sleep while a request waits; true when the user stopped it.  0.1d5: takes no key events out of the queue
 * (CancelPressed takes only Command-period and Escape), so a key typed during a request is kept for afterwards. */
static Boolean Idle(short ticks)
{
    EventRecord e;
    if (gHasWNE) WaitNextEvent(0, &e, ticks, NULL);
    else { unsigned long t = TickCount() + ticks; while (TickCount() < t) SystemTask(); }
    return CancelPressed();
}

static void Breathe(void)                        /* let the drive work and other programs run */
{
    EventRecord e;
    if (gHasWNE) WaitNextEvent(0, &e, 2, NULL);
    else SystemTask();
}

/* ---------- the mailbox ---------- */

static OSErr PageIO(Boolean write, short k, unsigned char *page)
{
    ParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.ioParam.ioRefNum = gMbRef;
    pb.ioParam.ioBuffer = (Ptr)page;
    pb.ioParam.ioReqCount = XL_PAGE;
    pb.ioParam.ioPosMode = fsFromStart | noCacheMask;
    pb.ioParam.ioPosOffset = (long)k * XL_PAGE;
    return write ? PBWriteSync(&pb) : PBReadSync(&pb);
}

/* HFS: is the file one extent?  The FCB's first extent record is at offset 38: three (start, count) pairs;
 * the second count is 0 when everything is in the first.  MFS (MacWorks XL 3.0) keeps no extents: trusted. */
static Boolean Contiguous(short ref)
{
    Ptr fcbs = *(Ptr volatile *)0x034E;          /* FCBSPtr */
    Ptr fcb = fcbs + ref;
    Ptr vcb = *(Ptr *)(fcb + 20);
    if (*(unsigned short *)(vcb + 8) != 0x4244) return true;       /* not HFS */
    return *(unsigned short *)(fcb + 38 + 6) == 0;
}

static OSErr OpenMailbox(void)
{
    static unsigned char page[XL_PAGE] __attribute__((aligned(2)));
    short vref, k;
    long count;
    OSErr err;
    unsigned char name[] = "\pMAILBOX.DATA";

    GetVol(NULL, &vref);                         /* this application's folder, as the Finder launched it */
    err = FSOpen(name, vref, &gMbRef);
    if (err == fnfErr) {
        err = Create(name, vref, 'EFXL', 'MBOX');
        if (err == noErr) err = FSOpen(name, vref, &gMbRef);
    }
    if (err) { gMbRef = 0; return err; }
    SetEOF(gMbRef, 0);                           /* fresh, so the run of blocks is contiguous */
    count = (long)kPages * XL_PAGE;
    {
        ParamBlockRec pb;
        memset(&pb, 0, sizeof pb);
        pb.ioParam.ioRefNum = gMbRef;
        pb.ioParam.ioReqCount = count;
        err = PBAllocContigTrap(&pb);
    }
    if (err == noErr) err = SetEOF(gMbRef, (long)kPages * XL_PAGE);
    if (err) { FSClose(gMbRef); gMbRef = 0; return err; }
    if (!Contiguous(gMbRef)) { FSClose(gMbRef); gMbRef = 0; return dskFulErr; }
    for (k = 0; k < kPages; k++) {               /* every page an idle version 2 page */
        XLBuildPage(page, 0, XL_IDLE, (unsigned short)k, kPages, NULL, 0);
        if ((err = PageIO(true, k, page)) != noErr) { FSClose(gMbRef); gMbRef = 0; return err; }
    }
    FlushVol(NULL, vref);
    return noErr;
}

/* A request is written to page k (StartRequest); its reply is looked for on the other pages (FinishRequest).
 * Between the two the drive is at work, and the disk test uses that time.  Transact = both, one after the other.
 * FinishRequest returns noErr, userCanceledErr (Apple-period) or 1 (no answer in 45 s). */
static unsigned char gPage[XL_PAGE] __attribute__((aligned(2)));
static short gReqK;

static OSErr StartRequest(const char *cmd)
{
    if (!gMbRef) return fnOpnErr;
    if (++gSeq == 0) gSeq = 1;
    gReqK = (short)gNextK;
    gNextK = (unsigned short)((gNextK + 1) % kPages);
    XLBuildPage(gPage, gSeq, XL_REQUEST, (unsigned short)gReqK, kPages, (const unsigned char *)cmd, (unsigned short)strlen(cmd));
    return PageIO(true, gReqK, gPage);
}

static OSErr FinishRequest(unsigned char *out, short *outlen, unsigned long *echo)
{
    XLHeader h;
    unsigned long deadline = TickCount() + kWaitTicks;
    short j = 1, tries = 0;
    while (TickCount() < deadline) {
        short p = (short)((gReqK + j) % kPages); /* the other pages in turn: a cached page comes back stale */
        j = (short)(j % (kPages - 1) + 1);
        if (PageIO(false, p, gPage) == noErr && XLIsReplyTo(gPage, gSeq, &h)) {
            memcpy(out, h.payload, h.len);
            *outlen = (short)h.len;
            if (echo) *echo = h.reserved;
            return noErr;
        }
        if (CancelPressed()) return userCanceledErr;
        Breathe();
        if (++tries > 10 && Idle(6)) return userCanceledErr;
    }
    return 1;
}

static OSErr Transact(const char *cmd, unsigned char *out, short *outlen, unsigned long *echo)
{
    OSErr err = StartRequest(cmd);
    return err ? err : FinishRequest(out, outlen, echo);
}

static void SayError(const char *what, OSErr err)
{
    char s[160];
    if (err == 1) sprintf(s, "%s: no answer in 45 s. Is MAILBOX.DATA on the ESPFuji drive, with firmware 1.1 or later?", what);
    else if (err == userCanceledErr) sprintf(s, "%s: stopped.", what);
    else if (err == fnOpnErr) sprintf(s, "%s: no mailbox. Copy Drive Test XL to the ESProFile volume and open it there.", what);
    else sprintf(s, "%s: error %d.", what, (int)err);
    Say(s);
}

/* ---------- the commands ---------- */

static void Simple(const char *cmd)
{
    unsigned char out[XL_PAYLOAD + 1];
    short n;
    OSErr err;
    char s[40];
    SetCursor(*GetCursor(watchCursor));
    err = Transact(cmd, out, &n, NULL);
    InitCursor();
    if (err) { SayError(cmd, err); return; }
    sprintf(s, "%s: ", cmd);
    Out(s, (long)strlen(s));
    Out((const char *)out, n);
    Out("\r", 1);
}

/* ---------- the disk test ---------- */

/* The drive's counters from its STATUS line: "... refused N ... odd N ...".  false when this firmware has none. */
static Boolean Counters(long *refused, long *odd, Boolean show)
{
    unsigned char out[XL_PAYLOAD + 1];
    short n;
    char *p, *q;
    if (Transact("STATUS", out, &n, NULL) != noErr) return false;
    out[n] = 0;
    if (show) { Out("  drive: ", 9); Out((const char *)out, n); Out("\r", 1); }
    p = strstr((char *)out, "refused ");
    q = strstr((char *)out, " odd ");
    if (!p || !q) return false;
    sscanf(p + 8, "%ld", refused);
    sscanf(q + 5, "%ld", odd);
    return true;
}

/* The data of one piece: a different, repeatable byte stream for every pass and piece (a 32-bit generator; the
 * top byte of each step), so a byte lost, doubled or moved shows as a difference from there on. */
static void Fill(unsigned char *b, long n, unsigned long seed)
{
    long i;
    unsigned long x = seed * 2654435761UL + 12345UL;
    for (i = 0; i < n; i++) { x = x * 1664525UL + 1013904223UL; b[i] = (unsigned char)(x >> 24); }
}

static OSErr FileIO(Boolean write, short ref, long off, unsigned char *buf, long n)
{
    ParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.ioParam.ioRefNum = ref;
    pb.ioParam.ioBuffer = (Ptr)buf;
    pb.ioParam.ioReqCount = n;
    pb.ioParam.ioPosMode = fsFromStart | noCacheMask;
    pb.ioParam.ioPosOffset = off;
    return write ? PBWriteSync(&pb) : PBReadSync(&pb);
}

static void RunTest(short passes)
{
    static unsigned char wbuf[kChunk] __attribute__((aligned(2)));
    static unsigned char rbuf[kChunk] __attribute__((aligned(2)));
    unsigned char name[] = "\pDRIVETEST.DATA";
    unsigned char out[XL_PAYLOAD + 1];
    char s[200], cmd[XL_PAYLOAD + 1];
    short vref, ref = 0, n, p, c;
    long ref0 = 0, odd0 = 0, ref1 = 0, odd1 = 0, differ = 0, firstPass = 0, firstOff = -1, i, pages = 0, nopage = 0, done = 0;
    unsigned long t0, total = 0;
    int code = 0;
    Boolean have0, have1, stopped = false, pending;
    OSErr err;

    if (!gMbRef) { SayError("TEST", fnOpnErr); return; }
    SetCursor(*GetCursor(watchCursor));
    sprintf(s, "TEST: %d passes of %ld KB written, read back and compared%s. Apple-period stops it.", (int)passes,
            kChunk * kChunks / 1024, gNetDuring ? ", a page fetched over Wi-Fi during each" : ", no network use");
    Say(s);
    have0 = Counters(&ref0, &odd0, true);
    if (!have0) Say("  (this drive's firmware reports no disk counters; the comparison still counts)");

    GetVol(NULL, &vref);
    FSDelete(name, vref);
    err = Create(name, vref, 'DTXL', 'DATA');
    if (err == noErr) err = FSOpen(name, vref, &ref);
    if (err == noErr) err = SetEOF(ref, kChunk * kChunks);
    if (err) { InitCursor(); sprintf(s, "TEST: the test file could not be made (error %d). It needs %ld KB free.", (int)err, kChunk * kChunks / 1024); Say(s); if (ref) FSClose(ref); FSDelete(name, vref); return; }

    t0 = TickCount();
    for (p = 1; p <= passes && !stopped; p++) {
        long d = 0;
        pending = false;
        if (gNetDuring) { sprintf(cmd, "OPEN %s", kNewsURL); pending = (StartRequest(cmd) == noErr); }
        for (c = 0; c < kChunks && !err; c++) {                    /* write the whole file */
            Fill(wbuf, kChunk, (unsigned long)p * 1000UL + (unsigned long)c);
            err = FileIO(true, ref, (long)c * kChunk, wbuf, kChunk);
            if (CancelPressed()) { stopped = true; break; }
        }
        if (!err) err = FlushVol(NULL, vref);
        for (c = 0; c < kChunks && !err && !stopped; c++) {        /* read it back past the cache and compare */
            Fill(wbuf, kChunk, (unsigned long)p * 1000UL + (unsigned long)c);
            memset(rbuf, 0, kChunk);
            err = FileIO(false, ref, (long)c * kChunk, rbuf, kChunk);
            if (err) break;
            for (i = 0; i < kChunk; i++)
                if (rbuf[i] != wbuf[i]) { if (firstOff < 0) { firstOff = (long)c * kChunk + i; firstPass = p; } d++; }
            if (CancelPressed()) stopped = true;
        }
        if (pending) {                                             /* the page the drive fetched meanwhile */
            OSErr e2 = FinishRequest(out, &n, NULL);
            if (e2 == userCanceledErr) stopped = true;
            out[e2 == noErr ? n : 0] = 0;
            if (e2 == noErr && sscanf((const char *)out, "OK %lu %d", &total, &code) == 2) pages++; else nopage++;
            Transact("CLOSE", out, &n, NULL);
        }
        if (err) { sprintf(s, "  pass %d: disk error %d.", (int)p, (int)err); Say(s); break; }
        if (stopped) break;
        differ += d; done++;
        if (gNetDuring && pending && nopage == 0) sprintf(s, "  pass %d: %ld bytes differ; page %lu bytes, HTTP %d", (int)p, d, total, code);
        else sprintf(s, "  pass %d: %ld bytes differ%s", (int)p, d, gNetDuring ? "; no page (network?)" : "");
        Say(s);
    }
    FSClose(ref);
    FSDelete(name, vref);
    FlushVol(NULL, vref);
    have1 = Counters(&ref1, &odd1, true);
    InitCursor();

    if (stopped) Say("TEST: stopped.");
    sprintf(s, "  %ld passes, %ld KB each way, %lu s.", done, done * kChunk * kChunks / 1024, (TickCount() - t0) / 60);
    Say(s);
    if (firstOff >= 0) { sprintf(s, "  first difference: pass %ld, byte %ld of the file.", firstPass, firstOff); Say(s); }
    if (gNetDuring) { sprintf(s, "  pages fetched during the passes: %ld (no page: %ld).", pages, nopage); Say(s); }
    if (have0 && have1) { sprintf(s, "  drive counters during the test: refused +%ld, odd +%ld.", ref1 - ref0, odd1 - odd0); Say(s); }
    if (err || differ || (have0 && have1 && (ref1 != ref0 || odd1 != odd0))) Say("RESULT: FAIL.");
    else if (done == 0) Say("RESULT: nothing tested.");
    else if (!(have0 && have1)) Say("RESULT: PASS (every byte compared equal; this firmware has no counters to confirm it).");
    else if (gNetDuring && pages == 0) Say("RESULT: PASS for the disk, but no page was fetched, so the radio was not busy. Check Network > Wi-Fi.");
    else Say("RESULT: PASS.");
}

static void About(void)
{
    DialogPtr d = GetNewDialog(129, NULL, (WindowPtr)-1);
    short item = 0;
    if (!d) return;
    ShowWindow(d);
    while (item != 1) ModalDialog(NULL, &item);
    DisposeDialog(d);
}

/* ---------- the usual Mac application ---------- */

static void DoMenu(long mi)
{
    short menu = (short)(mi >> 16), item = (short)mi;
    gStopDraw = false;
    switch (menu) {
    case mApple:
        if (item == iAbout) About();
        else { Str255 name; GetMenuItemText(GetMenuHandle(mApple), item, name); OpenDeskAcc(name); }
        break;
    case mFile:
        if (item == iQuit) gQuit = true;
        break;
    case mEdit:
        if (!SystemEdit(item - 1)) {
            if (item == 4) TECopy(gTE);
        }
        break;
    case mNet:
        switch (item) {
        case iHello:    Simple("HELLO"); break;
        case iWifi:     Simple("WIFI"); break;
        case iStatus:   Simple("STATUS"); break;
        case iClearWin: TESetSelect(0, (*gTE)->teLength, gTE); TEDelete(gTE); break;
        }
        break;
    case mTest:
        switch (item) {
        case iQuick:   RunTest(5); break;
        case iLong:    RunTest(50); break;
        case iNetwork: gNetDuring = !gNetDuring; CheckItem(GetMenuHandle(mTest), iNetwork, gNetDuring); break;
        }
        break;
    }
    HiliteMenu(0);
}

static void DoMouse(EventRecord *e)
{
    WindowPtr w;
    switch (FindWindow(e->where, &w)) {
    case inMenuBar: DoMenu(MenuSelect(e->where)); break;
    case inSysWindow: SystemClick(e, w); break;
    case inDrag: DragWindow(w, e->where, &qd.screenBits.bounds); break;
    case inContent:
        if (w != FrontWindow()) SelectWindow(w);
        else { Point p = e->where; SetPort(w); GlobalToLocal(&p); TEClick(p, (e->modifiers & shiftKey) != 0, gTE); }
        break;
    }
}

static void Setup(void)
{
    Rect r;
    char s[200];
    OSErr err;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    FlushEvents(everyEvent, 0);
    gHasWNE = TrapAvailable(0xA860);

    SetMenuBar(GetNewMBar(128));
    AppendResMenu(GetMenuHandle(mApple), 'DRVR');
    DrawMenuBar();

    gWin = GetNewWindow(128, NULL, (WindowPtr)-1);
    r = qd.screenBits.bounds;                    /* the XL's 608 x 431 or a Plus's 512 x 342: fit the screen */
    SizeWindow(gWin, r.right - r.left - 16, r.bottom - r.top - 52, true);
    SetPort(gWin);
    TextFont(4);                                 /* Monaco */
    TextSize(9);
    r = gWin->portRect;
    InsetRect(&r, 4, 4);
    gTE = TENew(&r, &r);

    CheckItem(GetMenuHandle(mTest), iNetwork, gNetDuring);
    Say("Drive Test XL 1.0: checks an ESProFile running ESPFuji, from a Macintosh XL.");
    err = OpenMailbox();
    if (err) {
        sprintf(s, "MAILBOX.DATA could not be made beside this application (error %d). It needs 32 KB in one piece on the ESPFuji drive.", (int)err);
        Say(s);
    } else {
        Say("MAILBOX.DATA: 64 pages in one piece, ready. Test > Quick Test (Apple-T) starts; wait a minute after power-up so Wi-Fi has joined.");
    }
}

int main(void)
{
    EventRecord e;
    Setup();
    while (!gQuit) {
        Boolean got = gHasWNE ? WaitNextEvent(everyEvent, &e, 20, NULL) : (SystemTask(), GetNextEvent(everyEvent, &e));
        if (!got) { TEIdle(gTE); continue; }
        switch (e.what) {
        case mouseDown: DoMouse(&e); break;
        case keyDown: case autoKey:
            if (e.modifiers & cmdKey) DoMenu(MenuKey((char)(e.message & charCodeMask)));
            break;
        case updateEvt:
            BeginUpdate((WindowPtr)e.message);
            EraseRect(&gWin->portRect);
            TEUpdate(&gWin->portRect, gTE);
            EndUpdate((WindowPtr)e.message);
            break;
        case activateEvt:
            if (e.modifiers & activeFlag) TEActivate(gTE); else TEDeactivate(gTE);
            break;
        }
    }
    if (gMbRef) FSClose(gMbRef);
    return 0;
}
