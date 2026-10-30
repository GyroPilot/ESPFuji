/* ESPFuji XL: the ESPFuji network for a Macintosh XL (MacWorks), 0.1d1, 30 Oct 2026.
 *
 * The ESProFile the XL boots from fetches web pages and the time over Wi-Fi.  This application talks to it through
 * MAILBOX.DATA, a 64-page file it makes in its own folder: a request goes onto page k, then the other pages are
 * read until one comes back as the reply (protocol version 2, see DESIGN.md and xlproto.h).  The drive needs
 * ESPFuji firmware 1.1 (version 2 support); a 1.0 drive never answers a version 2 page.
 *
 * 68000 code, System 6 and 7 (MacWorks Plus and Plus II), built with Retro68.  No Color QuickDraw, no System 7
 * calls: WaitNextEvent only when the trap exists, TextEdit scrolled by hand (the XL's 64K ROM lacks TEAutoView).
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

enum { mApple = 128, mFile, mEdit, mNet };
enum { iAbout = 1 };
enum { iQuit = 1 };
enum { iHello = 1, iWifi, iClock = 4, iOpen, iNews = 7, iClearWin = 9 };

#define kPages      64                           /* MAILBOX.DATA: 64 pages of 512 bytes, one contiguous run */
#define kWaitTicks  (45L * 60)                   /* an OPEN may take the drive 30 s; give it 45 */
#define kKeepText   24000                        /* TextEdit's 32 KB limit, with room */
#define kNewsURL    "https://fujinet.online/8bitnews/news.php?t=lf&ps=60x14&l=9&c=top&p=1"

static WindowPtr gWin;
static TEHandle  gTE;
static Boolean   gQuit, gHasWNE;
static short     gMbRef = 0;                     /* MAILBOX.DATA, open; 0 = no mailbox */
static unsigned short gSeq = 0, gNextK = 0;
static Str255    gLastURL;

/* ---------- small things ---------- */

static Boolean TrapAvailable(short trap)
{
    return NGetTrapAddress(trap, kToolboxTrapType) != NGetTrapAddress(0xA89F, kToolboxTrapType);
}

static void CtoP(const char *c, unsigned char *p)
{
    size_t n = strlen(c);
    if (n > 255) n = 255;
    p[0] = (unsigned char)n;
    memcpy(p + 1, c, n);
}

static void PtoC(const unsigned char *p, char *c)
{
    memcpy(c, p + 1, p[0]);
    c[p[0]] = 0;
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
static void Out(const char *s, long n)
{
    char buf[256];
    long i, j = 0;
    SetPort(gWin);
    TESetSelect((*gTE)->teLength, (*gTE)->teLength, gTE);
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\n') c = '\r';
        else if (c == '\r' && i + 1 < n && s[i + 1] == '\n') continue;
        else if ((unsigned char)c < 32 && c != '\r' && c != '\t') continue;
        buf[j++] = c;
        if (j == sizeof buf) { TEInsert(buf, j, gTE); j = 0; }
    }
    if (j) TEInsert(buf, j, gTE);
    FitText();
}
static void Say(const char *s) { Out(s, (long)strlen(s)); Out("\r", 1); }

static Boolean CancelPressed(void)              /* Command-period, as on any Mac */
{
    EventRecord e;
    if (EventAvail(keyDownMask, &e) && (e.modifiers & cmdKey) && (e.message & charCodeMask) == '.') {
        GetNextEvent(keyDownMask, &e);
        return true;
    }
    return false;
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
    static unsigned char page[XL_PAGE];
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

/* One request and its reply.  out gets up to XL_PAYLOAD bytes; echo the reply's bytes 16-19.
 * Returns noErr, userCanceledErr (Command-period) or 1 (no answer in 45 s). */
static OSErr Transact(const char *cmd, unsigned char *out, short *outlen, unsigned long *echo)
{
    static unsigned char page[XL_PAGE];
    XLHeader h;
    unsigned long deadline;
    short k, j = 1;
    OSErr err;

    if (!gMbRef) return fnOpnErr;
    if (++gSeq == 0) gSeq = 1;
    k = (short)gNextK;
    gNextK = (unsigned short)((gNextK + 1) % kPages);
    XLBuildPage(page, gSeq, XL_REQUEST, (unsigned short)k, kPages, (const unsigned char *)cmd, (unsigned short)strlen(cmd));
    if ((err = PageIO(true, k, page)) != noErr) return err;
    deadline = TickCount() + kWaitTicks;
    while (TickCount() < deadline) {
        short p = (short)((k + j) % kPages);     /* the other pages in turn: a cached page comes back stale */
        j = (short)(j % (kPages - 1) + 1);
        if (PageIO(false, p, page) == noErr && XLIsReplyTo(page, gSeq, &h)) {
            memcpy(out, h.payload, h.len);
            *outlen = (short)h.len;
            if (echo) *echo = h.reserved;
            return noErr;
        }
        if (CancelPressed()) return userCanceledErr;
        Breathe();
    }
    return 1;
}

static void SayError(const char *what, OSErr err)
{
    char s[160];
    if (err == 1) sprintf(s, "%s: no answer in 45 s. Is MAILBOX.DATA on the ESPFuji drive, with firmware 1.1 or later?", what);
    else if (err == userCanceledErr) sprintf(s, "%s: stopped.", what);
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

static void SetClock(void)
{
    unsigned char out[XL_PAYLOAD + 1];
    short n;
    int y, mo, d, hh, mi, ss;
    OSErr err;
    char s[120];
    SetCursor(*GetCursor(watchCursor));
    err = Transact("CLOCK", out, &n, NULL);
    InitCursor();
    if (err) { SayError("CLOCK", err); return; }
    out[n] = 0;
    if (sscanf((const char *)out, "OK %d %d %d %d %d %d", &y, &mo, &d, &hh, &mi, &ss) == 6) {
        DateTimeRec dt;
        unsigned long secs;
        memset(&dt, 0, sizeof dt);
        dt.year = (short)y; dt.month = (short)mo; dt.day = (short)d;
        dt.hour = (short)hh; dt.minute = (short)mi; dt.second = (short)ss;
        DateToSeconds(&dt, &secs);
        SetDateTime(secs);
        sprintf(s, "Clock set: %04d-%02d-%02d %02d:%02d:%02d (UTC unless espfuji.ini has timezone=).", y, mo, d, hh, mi, ss);
        Say(s);
    } else {
        Out("CLOCK: ", 7); Out((const char *)out, n); Out("\r", 1);
    }
}

/* News lines are "id|date time|headline": show "date time  headline". */
static void NewsLine(const char *l, long n)
{
    long a = -1, b = -1, i;
    for (i = 0; i < n; i++) if (l[i] == '|') { if (a < 0) a = i; else { b = i; break; } }
    if (a < 0 || b < 0) { Out(l, n); Out("\r", 1); return; }
    Out(l + a + 1, b - a - 1);
    Out("  ", 2);
    Out(l + b + 1, n - b - 1);
    Out("\r", 1);
}

static void OpenPage(const char *url, Boolean news)
{
    static char body[kKeepText + 1];
    unsigned char out[XL_PAYLOAD + 1];
    char cmd[XL_PAYLOAD + 1];
    short n;
    unsigned long echo, total = 0, off = 0;
    long have = 0;
    int code = 0;
    OSErr err;

    if (strlen(url) + 5 > XL_PAYLOAD) { Say("That address is too long (492 characters at most)."); return; }
    sprintf(cmd, "OPEN %s", url);
    SetCursor(*GetCursor(watchCursor));
    Say(news ? "Asking the news service..." : url);
    err = Transact(cmd, out, &n, NULL);
    if (err) { InitCursor(); SayError("OPEN", err); return; }
    out[n] = 0;
    if (sscanf((const char *)out, "OK %lu %d", &total, &code) != 2) { InitCursor(); Out((const char *)out, n); Out("\r", 1); return; }
    while (off < total && have < kKeepText) {
        sprintf(cmd, "READ %lu %d", off, XL_PAYLOAD);
        err = Transact(cmd, out, &n, &echo);
        if (err) { SayError("READ", err); break; }
        if (n <= 0 || echo != off) { Say("READ: the drive answered out of step; stopped."); break; }
        if (have + n > kKeepText) n = (short)(kKeepText - have);
        memcpy(body + have, out, n);
        have += n;
        off += (unsigned long)n;
    }
    Transact("CLOSE", out, &n, NULL);
    InitCursor();
    if (news) {
        long i, start = 0;
        for (i = 0; i <= have; i++)
            if (i == have || body[i] == '\n') { if (i > start) NewsLine(body + start, i - start); start = i + 1; }
    } else {
        char s[80];
        Out(body, have);
        sprintf(s, "\r(%lu bytes, HTTP %d)", total, code);
        Say(s);
    }
}

static void AskURL(void)
{
    DialogPtr d = GetNewDialog(128, NULL, (WindowPtr)-1);
    short item = 0, type;
    Handle h;
    Rect r;
    char url[256];
    if (!d) return;
    GetDialogItem(d, 4, &type, &h, &r);
    SetDialogItemText(h, gLastURL);
    SelectDialogItemText(d, 4, 0, 255);
    ShowWindow(d);
    while (item != 1 && item != 2) ModalDialog(NULL, &item);
    GetDialogItemText(h, gLastURL);
    DisposeDialog(d);
    if (item != 1 || gLastURL[0] == 0) return;
    PtoC(gLastURL, url);
    OpenPage(url, false);
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
        case iClock:    SetClock(); break;
        case iOpen:     AskURL(); break;
        case iNews:     OpenPage(kNewsURL, true); break;
        case iClearWin: TESetSelect(0, (*gTE)->teLength, gTE); TEDelete(gTE); break;
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

    CtoP(kNewsURL, gLastURL);
    Say("ESPFuji XL 0.1d1: the ESPFuji network for a Macintosh XL. Test version, not supported.");
    err = OpenMailbox();
    if (err) {
        sprintf(s, "MAILBOX.DATA could not be made beside this application (error %d). It needs 32 KB in one piece on the ESPFuji drive.", (int)err);
        Say(s);
    } else {
        Say("MAILBOX.DATA: 64 pages in one piece, ready. Network > Hello asks the drive who it is.");
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
