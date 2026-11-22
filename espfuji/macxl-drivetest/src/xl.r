/* xl.r: Drive Test XL's menus and dialogs.  Everything fits the Macintosh XL's 608 x 431 screen and a Plus's 512 x 342. */
#include "Multiverse.r"
#include "Processes.r"

resource 'MBAR' (128) { { 128; 129; 130; 131; 132 } };

resource 'MENU' (128) { 128, textMenuProc, 0x7FFFFFFD, enabled, apple, {
    "About Drive Test XL\0xC9", noIcon, noKey, noMark, plain;
    "-", noIcon, noKey, noMark, plain
} };

resource 'MENU' (129) { 129, textMenuProc, 0x7FFFFFFF, enabled, "File", {
    "Quit", noIcon, "Q", noMark, plain
} };

resource 'MENU' (130) { 130, textMenuProc, 0x7FFFFFFD, enabled, "Edit", {
    "Undo", noIcon, "Z", noMark, plain;
    "-", noIcon, noKey, noMark, plain;
    "Cut", noIcon, "X", noMark, plain;
    "Copy", noIcon, "C", noMark, plain;
    "Paste", noIcon, "V", noMark, plain;
    "Clear", noIcon, noKey, noMark, plain
} };

resource 'MENU' (131) { 131, textMenuProc, 0x7FFFFFF7, enabled, "Network", {
    "Hello (the drive)", noIcon, "H", noMark, plain;
    "Wi-Fi", noIcon, "W", noMark, plain;
    "Drive Status", noIcon, "D", noMark, plain;
    "-", noIcon, noKey, noMark, plain;
    "Clear Window", noIcon, noKey, noMark, plain
} };

resource 'MENU' (132) { 132, textMenuProc, 0x7FFFFFFB, enabled, "Test", {
    "Quick Test (5 passes)", noIcon, "T", noMark, plain;
    "Long Test (50 passes)", noIcon, "L", noMark, plain;
    "-", noIcon, noKey, noMark, plain;
    "Fetch a Page During Each Pass", noIcon, noKey, noMark, plain
} };

resource 'WIND' (128) { { 40, 8, 330, 504 }, documentProc, visible, noGoAway, 0, "Drive Test XL", noAutoCenter };

/* About */
resource 'DLOG' (129) { { 60, 30, 250, 482 }, dBoxProc, invisible, noGoAway, 0, 129, "", centerMainScreen };
resource 'DITL' (129) { {
    { 156, 360, 176, 432 }, Button { enabled, "OK" };
    { 10, 14, 108, 432 }, StaticText { disabled,
        "Drive Test XL 1.0 (22 November 2026)\r\r"
        "Writes a 256 KB file of known data to this volume, reads it back and compares every byte, pass after "
        "pass, while the ESProFile fetches a page over Wi-Fi. Shows the drive's own counters before and after." };
    { 110, 14, 148, 432 }, StaticText { disabled,
        "For an ESProFile running ESPFuji 1.1b2 or later; counters need 1.1.3. By GyroPilot (Tim O'Connor), 2026." }
} };

resource 'vers' (1) { 0x01, 0x00, release, 0x00, verUS, "1.0", "1.0, Drive Test XL, Macintosh XL / MacWorks" };

resource 'SIZE' (-1) {
    reserved, acceptSuspendResumeEvents, reserved, canBackground, doesActivateOnFGSwitch,
    backgroundAndForeground, dontGetFrontClicks, ignoreChildDiedEvents, is32BitCompatible,
    notHighLevelEventAware, onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
    reserved, reserved, reserved,
    200 * 1024,
    160 * 1024
};

/* The Finder icon (tools/mkicon.py C: the Macintosh XL with a test trace on its screen; Tim chose C, 22 Nov 2026). */
#include "icons.r"
data 'DTXL' (0, "Owner resource") { $"0D447269 76652054 65737420 584C" };   /* "Drive Test XL" */
resource 'FREF' (128) { 'APPL', 0, "" };
resource 'BNDL' (128) { 'DTXL', 0, { 'ICN#', { 0, 128 }; 'FREF', { 0, 128 } } };
