# Omascan

Scan paper to PDF from a SANE scanner, on Omarchy.

![Omascan with three scanned pages](docs/screenshot.png)

Pick the scanner, press Space, and the pages appear as they come off the
glass or out of the feeder. Turn, reorder or drop them, then export one PDF.
Omascan follows the Omarchy theme and language as they change, and is laid
out like Signed, Omarchy's PDF signer, so the two work the same way.

It drives SANE's `scanimage` rather than linking SANE, so a misbehaving
scanner driver can only end its own scan, never the app. Driverless network
scanners (eSCL/AirScan, which most scanners from the last several years
speak) work through `sane-airscan` with no vendor driver.

## Install

Omascan is in the [AUR](https://aur.archlinux.org/packages/omascan):

```sh
omarchy-pkg-aur-add omascan   # or: yay -S omascan
```

It brings SANE and `sane-airscan` with it, which is all a network scanner
needs. For a scanner on USB, add `ipp-usb` too (`omarchy-pkg-add ipp-usb`).
Omascan then appears in the app launcher (`Super + Space`).

To install from this checkout instead, run `bin/install`.

## Tested scanners

| Scanner | Connection |
|---|---|
| Brother MFC-J6955DW | Network (eSCL) |
| Canon PIXMA TR4755i | Network (eSCL) |
| Epson WF-C5710 | Network (eSCL) |

Most scanners that speak eSCL/AirScan should work the same way. If you try
Omascan with another one, whether it works or not, please
[report it](https://github.com/alchemy/omascan/issues/new?template=scanner-report.yml)
so it can join this list.

## Keys

| Key | Does |
| --- | --- |
| `Space` | Scan, or cancel the scan |
| `Esc` | Cancel the scan |
| `Ctrl+S` | Export PDF |
| `Ctrl+N` | New document |
| `←` `→` or `PgUp` `PgDn` / `Home` `End` | Previous or next page / first or last |
| `Shift+←` `Shift+→` | Move the page |
| `R` / `Shift+R` | Rotate right / left |
| `Delete` or `X` | Delete the page |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / redo |
| `S` | Next source (Flatbed, ADF, ADF Duplex…) |
| `M` | Next color mode |
| `D` / `Shift+D` | Higher / lower resolution |
| `P` | Next paper size |
| `F5` | Find scanners again |
| `Ctrl+=` `Ctrl+-` `Ctrl+0` | Zoom in, out, fit (or `Ctrl` + scroll) |
| `?` | Show the shortcuts |
| `Q` | Quit (asks first if pages are not exported) |

Click a setting in the top bar for its menu, the scanner name for the other
scanners, and right-click a page for what can be done to it.

## Command line

Every command but `open` runs without a display, exits 0 or 1, prints errors
on stderr, and takes `--json`.

```sh
omascan open                       # the window
omascan devices                    # scanners SANE can reach
omascan options --device <id>      # sources, modes, resolutions
omascan scan --source ADF --resolution 300 --page a4 -o letter.pdf
omascan skill install              # teach coding agents to use omascan
```

`OMASCAN_TEST_SCANNER=1` adds SANE's built-in test scanner to the list, for
working without hardware.

## Develop

```sh
bin/build && build/omascan open
bin/test
```

Scans live in `~/.cache/omascan/session-*` while the window is open and are
removed when it closes. The originals are never changed: rotation and order
are applied only when the PDF is written, and the PDF is written beside its
target and renamed into place.

## License

MIT. Theme, translation, portal, command line and window patterns come from
Omacut, Monologue, Hype and Signed by David Heinemeier Hansson, also MIT.
