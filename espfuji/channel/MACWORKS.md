# The channel from MacWorks: not built

A Lisa running MacWorks (the Macintosh XL) has no driver for the channel yet. This note records what is known
and what a MacWorks route would have to settle. Today a Macintosh XL program reaches ESPFuji through the file
mailbox, protocol version 2 (`MAILBOX_PROTOCOL.md`, section 7), which stays in the firmware for that reason.

## What is known

- MacWorks' ProFile driver rearranges the low four bits of a block number before it goes on the cable, in the
  order 0, 13, 10, 7, 4, 1, 14 and so on: 13n mod 16, the inverse of the Lisa Office System's 5n mod 16. The two
  share the fixed points 0, 4, 8 and C, so `$FFFEF8` reaches the drive unchanged from MacWorks too. The order was
  measured on a Macintosh XL while the version 2 mailbox was being debugged.
- MacWorks puts the Macintosh File Manager's tag in the ProFile tag (file number, flags, logical block, date,
  its own checksum in byte 19). The channel ignores the tag of a block the host writes and answers with a zero
  tag, so none of that matters to the protocol; whether MacWorks itself objects to a zero tag on a block it did
  not ask for through the File Manager is not known, because no MacWorks program has read the channel block.

## What is not known

- **Whether MacWorks' ProFile driver passes a block number like `$FFFEF8` through at all.** A Macintosh driver or
  desk accessory would call the ProFile driver with a raw block number, if the driver has such a door; if it has
  none, or clips the number to the volume, the XL side would have to talk to the 6522 of the parallel port
  directly, as stepleton's `lisa_io` library does on the bare machine, and arrange not to collide with MacWorks'
  own use of the port.
- The shape of a MacWorks client: a driver (`DRVR` resource) with a control call per channel operation, or a
  library linked into each application. The requests and replies are the same texts as on the Lisa Office
  System, so an application would change only its transport.
- Client numbers: MacWorks runs one application at a time without MultiFinder, so a fixed number per application
  would do; with MultiFinder the same questions arise as on the Lisa Office System (a pool, a cancel at start).

## Until then

The ESPFuji XL application and the other Macintosh XL tools keep the version 2 file mailbox. A drive that serves
the channel serves the mailbox as well, so a mixed fleet (a Lisa Office System with the driver, a Macintosh XL
with the mailbox) works against one firmware.
