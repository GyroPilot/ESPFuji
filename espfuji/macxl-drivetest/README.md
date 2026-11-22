# Drive Test XL: a disk test for a Macintosh XL (MacWorks) on an ESProFile running ESPFuji

Version 1.0, 22 November 2026.

It checks the drive the XL starts from. Each pass writes a 256 KB file of known data to the ESProFile volume, reads it
back past the File Manager's cache and compares every byte, while the drive fetches a small page over Wi-Fi (so its
radio is sending). Before and after, it shows the drive's own STATUS line and says how far `refused` and `odd` moved.
The last line is `RESULT: PASS` or `RESULT: FAIL`.

- Test > Quick Test (Apple-T): 5 passes, about three minutes on a 16 MHz XL. Test > Long Test (Apple-L): 50 passes,
  about half an hour. Apple-period stops a test.
- Network > Hello, Wi-Fi, Drive Status: the drive's own lines, as in ESPFuji XL.
- It uses the mailbox of ESPFuji XL (`MAILBOX.DATA`, protocol version 2; see `../macxl-tool/DESIGN.md`): run one of
  the two at a time. The drive needs ESPFuji 1.1b2 or later; the disk counters need 1.1.3.
- MacWorks (Plus, Plus II) only. 68000 code, System 6.0.2 and later, built with Retro68: `bash build.sh`.
  `tools/package10.py` makes the release files (800K Disk Copy 4.2 image, MacBinary, share folder).

Tested on a real Macintosh XL (MacWorks Plus II, System 7.5.5, 16 MHz XLerator, ESPFuji 1.1.3): Quick Test and Long
Test pass (50 passes, 12,800 KB each way, refused 0, odd 0). It starts on System 6.0.8 with 1 MB in an emulator.

By GyroPilot (Tim O'Connor).
