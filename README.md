# newalliancegallery

A small browser-based tool for planning the layout of circular tables (a "sculpture
group") in a gallery space. You arrange and resize the circles, and their positions
are persisted to disk so the layout survives a page reload.

## Live display

A public, read-only display is published via GitHub Pages:

**https://llama-with-thumbs.github.io/newalliancegallery/**

It shows only the circles (with their animated oil-paint fill) on a black
background — no controls, rulers, or labels. The page polls `coords.json` every
few seconds, so it stays synchronized with the committed layout: edit locally,
commit and push `coords.json`, and any open display refreshes on its own within a
few seconds.

`index.html` is the display page (deployed from `master` / root); it fetches
`./coords.json` from the repo rather than the local `/coords` API.

## Views

- **Top-down blueprint** — a flat, to-scale plan of the circles with their
  coordinates and diameters shown in millimetres.
- **Tilted perspective** — the same layout rendered as a 3D scene on a table, with
  adjustable tilt and rotation, plus a "Sculpture Group" presentation tab.

## Controls

- **Flow speed / Tilt angle / Rotation** — animate and orient the perspective view.
- **Circle sizes** — grow or shrink all circle diameters in 5 mm steps.
- **Table depth** — adjust the thickness of the table in the perspective view.
- **Coordinates (mm)** — live readout of each circle's position and diameter.

## Running it

Requires Python 3 (no third-party packages).

```sh
python server.py            # serves on http://localhost:8000
python server.py 8080       # custom port
```

Open the printed URL in any browser. Every change is saved automatically and
reloaded on the next visit. Press `Ctrl+C` to stop the server.

## Files

| File             | Purpose                                                          |
| ---------------- | ---------------------------------------------------------------- |
| `server.py`      | Local HTTP server; serves the page and the `/coords` JSON API.   |
| `rectangle.html` | The full editor UI (SVG + controls, single self-contained file). |
| `coords.json`    | Persisted circle coordinates (`d`, `cx`, `cy` per circle, in mm).|

## How persistence works

`rectangle.html` reads the saved layout from `GET /coords` on load and writes back to
`POST /coords` on every change. The server validates each payload (array of objects
with numeric `d`, `cx`, `cy` within sane bounds) and writes `coords.json` atomically.
