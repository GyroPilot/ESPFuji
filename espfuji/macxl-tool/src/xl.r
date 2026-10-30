/* xl.r: ESPFuji XL's menus and dialogs.  Everything fits the Macintosh XL's 608 x 431 screen and a Plus's 512 x 342. */
#include "Multiverse.r"
#include "Processes.r"

resource 'MBAR' (128) { { 128; 129; 130; 131 } };

resource 'MENU' (128) { 128, textMenuProc, 0x7FFFFFFD, enabled, apple, {
    "About ESPFuji XL\0xC9", noIcon, noKey, noMark, plain;
    "-", noIcon, noKey, noMark, plain
} };

resource 'MENU' (129) { 129, textMenuProc, 0x7FFFFFFF, enabled, "File", {
    "Quit", noIcon, "Q", noMark, plain
} };

resource 'MENU' (130) { 130, textMenuProc, 0x7FFFFFFF, enabled, "Edit", {
    "Undo", noIcon, "Z", noMark, plain;
    "-", noIcon, noKey, noMark, plain;
    "Cut", noIcon, "X", noMark, plain;
    "Copy", noIcon, "C", noMark, plain;
    "Paste", noIcon, "V", noMark, plain;
    "Clear", noIcon, noKey, noMark, plain
} };

resource 'MENU' (131) { 131, textMenuProc, 0x7FFFFFFF, enabled, "Network", {
    "Hello (the drive)", noIcon, "H", noMark, plain;
    "Wi-Fi", noIcon, "W", noMark, plain;
    "-", noIcon, noKey, noMark, plain;
    "Set Clock from the Network", noIcon, "K", noMark, plain;
    "Open Page\0xC9", noIcon, "G", noMark, plain;
    "-", noIcon, noKey, noMark, plain;
    "News Headlines", noIcon, "N", noMark, plain;
    "-", noIcon, noKey, noMark, plain;
    "Clear Window", noIcon, noKey, noMark, plain
} };

resource 'WIND' (128) { { 40, 8, 330, 504 }, documentProc, visible, noGoAway, 0, "ESPFuji XL", noAutoCenter };

/* Open Page: an address */
resource 'DLOG' (128) { { 70, 20, 186, 492 }, dBoxProc, invisible, noGoAway, 0, 128, "", centerMainScreen };
resource 'DITL' (128) { {
    { 84, 380, 104, 452 }, Button { enabled, "Open" };
    { 84, 296, 104, 368 }, Button { enabled, "Cancel" };
    { 10, 14, 28, 452 }, StaticText { disabled, "Page to open (http or https, up to 32 KB):" };
    { 36, 16, 70, 450 }, EditText { enabled, "" }
} };

/* About */
resource 'DLOG' (129) { { 60, 30, 226, 482 }, dBoxProc, invisible, noGoAway, 0, 129, "", centerMainScreen };
resource 'DITL' (129) { {
    { 130, 360, 150, 432 }, Button { enabled, "OK" };
    { 10, 14, 120, 432 }, StaticText { disabled,
        "ESPFuji XL 0.1 (test)\n\n"
        "The ESPFuji network for a Macintosh XL running MacWorks: the ESProFile you boot from "
        "fetches pages and the time over Wi-Fi, through the file MAILBOX.DATA beside this application.\n"
        "Needs ESPFuji firmware 1.1 or later on the drive. Not supported; Tim O'Connor's lab, 2026." }
} };

resource 'vers' (1) { 0x00, 0x10, development, 0x01, verUS, "0.1d1", "0.1d1, ESPFuji XL, Macintosh XL / MacWorks" };

resource 'SIZE' (-1) {
    reserved, acceptSuspendResumeEvents, reserved, canBackground, doesActivateOnFGSwitch,
    backgroundAndForeground, dontGetFrontClicks, ignoreChildDiedEvents, is32BitCompatible,
    notHighLevelEventAware, onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
    reserved, reserved, reserved,
    200 * 1024,
    160 * 1024
};
