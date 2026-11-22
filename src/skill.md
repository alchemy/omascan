---
name: omascan
description: |
  Scan paper documents to PDF from a SANE scanner (flatbed or document feeder)
  with omascan. Use for ANY request to scan a page, a letter, a receipt, or a
  stack of paper, or to find out which scanners are available.
---

# omascan

omascan drives SANE scanners through `scanimage` and writes PDFs. Every
command below runs without a display, prints errors on stderr, and exits 0 on
success or 1 on failure. Add `--json` for machine-readable output.

## Workflow

1. `omascan devices --json` lists the scanners. Network discovery takes a few
   seconds. Each has an `id` to pass as `--device`.
2. `omascan options --device <id> --json` shows the sources (for example
   `Flatbed`, `ADF`, `ADF Duplex`), modes (`Color`, `Gray`), resolutions,
   and page sizes that scanner supports.
3. `omascan scan --device <id> --source <source> --mode <mode> --resolution 300 --page a4 -o <file.pdf> --json`
   scans and writes the PDF. A feeder source scans every sheet in the feeder;
   a flatbed scans one page.

Without `--device`, omascan uses the scanner last chosen in its window, or the
first one it finds.

## Rules

- Scanning moves paper in someone's machine. Confirm with the user that the
  document is in place, and on which source, before running `omascan scan`.
- `The document feeder is empty.` means nothing was loaded: ask the user to
  load it, do not retry in a loop.
- For a page count above one on a flatbed, scan one PDF per page or ask the
  user to open the window with `omascan open`, which collects pages, lets
  them reorder and rotate them, and exports one PDF.
- Never overwrite a file the user did not name as the output.
