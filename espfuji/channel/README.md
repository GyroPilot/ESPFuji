# The ESPFuji channel: a Lisa driver talks to the drive through one reserved block

Design documents for the next step after the file mailbox (`MAILBOX_PROTOCOL.md`, one folder up): a loadable Lisa
Office System driver that reaches the ESProFile through Apple's own ProFile driver, and a channel in the firmware
on block `$FFFEF8` that answers it. Design and status only; no code is published here yet.

- `CHANNEL_PROTOCOL.md`: what travels between a host and the drive through the channel block, and nothing else.
  Read this first.
- `LISAOS.md`: the Lisa Office System driver: how it is loaded, how it reaches the drive through Apple's driver,
  its `DEVICE_CONTROL` interface, sessions, and the rules a program must keep.
- `MACWORKS.md`: the Macintosh XL side, which does not exist yet, and what it would have to settle.
- `ESPROFILE.md`: the changes to the ESProFile firmware that serve the channel.
- `CLIENTS.md`: what a FujiNet client program must do on each platform, beyond the driver.
- `NOTES.md`: the development record: tests, findings, facts from the sources, the open questions, the test plan.
- `STATUS_2026-11-11.md`: the status note of 11 November as posted.
- `ESPFUJI_CHANNEL_SPEC.md`: draft 0.2 of the specification with its day-by-day sections, kept unchanged as the
  working record; superseded by the files above.

Nothing in this folder has run on a real Lisa or a real ESProFile yet. The code (the Lisa driver, its test tool,
the channel firmware) will be published after the first hardware run.
