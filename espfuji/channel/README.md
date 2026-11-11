# The ESPFuji channel: a Lisa driver talks to the drive through one reserved block

Design documents for the next step after the file mailbox (MAILBOX_PROTOCOL.md, one folder up): a loadable Lisa
Office System driver that reaches the ESProFile through Apple's own ProFile driver, and a channel in the firmware on
block $FFFEF8 that answers it. Design and status only; no code is published here yet.

- ESPFUJI_CHANNEL_SPEC.md: the firmware-side specification (proposed), with what writing and testing the code changed.
- STATUS_2026-11-11.md: where the project stands, what has been shown in LisaEm, what is not known, what comes next.

Nothing in this folder has run on a real Lisa or a real ESProFile yet. The code (the Lisa driver, its test tool, the
channel firmware) will be published after the first hardware run.
