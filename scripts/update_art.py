#!/usr/bin/env python3
"""Download the poster of a popular movie from TMDB and convert it to the
native format of the Waveshare 7.3" e-Paper (E) panel: 800x480, 6 colors,
4 bits per pixel (2 pixels per byte). The poster is composed in 480x800,
for a frame hung vertically, and the buffer is rotated for the panel.

Requires the free themoviedb.org API key in the TMDB_API_KEY environment
variable (not needed with --from-file, which converts a local image).

Output (in the --out folder):
  art.bin      image in the panel's native format (192,000 bytes)
  art.png      preview of the dithered image
  meta.json    title, source, update time
  version.txt  hash of the art.bin content (the ESP uses it to tell
               whether there is a new poster without downloading it all)
"""

import argparse
import hashlib
import io
import json
import os
import random
import sys
from datetime import datetime, timezone

import requests
from PIL import Image, ImageEnhance, ImageOps

WIDTH, HEIGHT = 800, 480

# Spectra 6 panel palette: (r, g, b) -> 4-bit controller code
PALETTE = [
    ((0, 0, 0), 0x0),        # black
    ((255, 255, 255), 0x1),  # white
    ((255, 255, 0), 0x2),    # yellow
    ((255, 0, 0), 0x3),      # red
    ((0, 0, 255), 0x5),      # blue
    ((0, 255, 0), 0x6),      # green
]
WHITE = 1  # index of white in PALETTE, used for the bars

TIMEOUT = 60


def tmdb_get(url, params=None):
    """GET request to TMDB with sanitized error messages. The API key
    travels in the query string and requests would include it in the
    exception text (full URL in both HTTP and network errors): here it
    never reaches the message. Raises RuntimeError on failure."""
    key = (params or {}).get("api_key", "")
    try:
        r = requests.get(url, params=params, timeout=TIMEOUT)
    except requests.RequestException as e:
        msg = str(e).replace(key, "***") if key else str(e)
        raise RuntimeError("network error while contacting TMDB: " + msg) \
            from None
    if not r.ok:
        try:
            detail = r.json().get("status_message", "")
        except ValueError:
            detail = ""
        raise RuntimeError("TMDB replied %d %s%s" % (
            r.status_code, r.reason, ": " + detail if detail else ""))
    return r


def fetch_tmdb(rng, key):
    """Poster of a popular movie from TMDB, using the free themoviedb.org
    API key. Posters are copyrighted (personal use only)."""
    base = "https://api.themoviedb.org/3/discover/movie"
    params = {
        "api_key": key,
        "language": "it-IT",  # Italian titles and, where available, posters
        "sort_by": "popularity.desc",
        "vote_count.gte": "1000",  # only reasonably well-known movies
        "include_adult": "false",
    }
    r = tmdb_get(base, params)
    # pick among the ~1000 most popular movies (20 per page)
    max_page = min(r.json().get("total_pages", 0), 50)
    if max_page < 1:
        raise RuntimeError("TMDB returned no movies for the filters in use")
    r = tmdb_get(base, {**params, "page": str(rng.randint(1, max_page))})
    movies = [m for m in r.json().get("results", []) if m.get("poster_path")]
    if not movies:
        raise RuntimeError("no poster in the chosen TMDB page")
    movie = rng.choice(movies)
    return {
        "title": movie.get("title") or movie.get("original_title")
                 or "Untitled",
        "date": (movie.get("release_date") or "")[:4],
        "source": "TMDB",
        "source_url": "https://www.themoviedb.org/movie/%d" % movie["id"],
        "image_url": "https://image.tmdb.org/t/p/w780%s" % movie["poster_path"],
    }


def pick_and_download(rng, key):
    """Picks a poster on TMDB and downloads its image."""
    meta = fetch_tmdb(rng, key)
    print("Picked: %(title)s (%(date)s) - %(source)s" % meta)
    r = tmdb_get(meta["image_url"])
    img = Image.open(io.BytesIO(r.content))
    img.load()
    return meta, img


def convert(img, fill):
    """Scales the image onto the 480x800 portrait panel and reduces it to
    the 6 colors with Floyd-Steinberg dithering. fill goes from 0 (the
    whole image is kept, with two white bars top and bottom) to 0.5 (half
    of the excess is center cropped, the other half becomes bars)."""
    target = (HEIGHT, WIDTH)  # portrait: 480 wide, 800 tall
    img = ImageOps.exif_transpose(img).convert("RGB")
    w, h = img.size
    contain = min(target[0] / w, target[1] / h)  # whole image, with bars
    cover = max(target[0] / w, target[1] / h)    # full panel, cropped
    scale = contain + (cover - contain) * fill
    size = (min(target[0], round(w * scale)), min(target[1], round(h * scale)))
    img = ImageOps.fit(img, size, Image.Resampling.LANCZOS)
    # on e-ink a bit of extra saturation and contrast looks better
    img = ImageEnhance.Color(img).enhance(1.25)
    img = ImageEnhance.Contrast(img).enhance(1.05)

    pal = Image.new("P", (1, 1))
    pal.putpalette([c for rgb, _ in PALETTE for c in rgb])
    dithered = img.quantize(palette=pal, dither=Image.Dither.FLOYDSTEINBERG)

    # the white bars are added after dithering so they stay a pure color
    frame = Image.new("P", target, WHITE)
    frame.putpalette(pal.getpalette())
    frame.paste(dithered, ((target[0] - dithered.width) // 2,
                           (target[1] - dithered.height) // 2))
    return frame


def pack(dithered):
    """Packs the palette indexes into the panel's 4-bit codes,
    2 pixels per byte (first pixel in the high nibble)."""
    # 256-entry translation table, palette index -> panel code (the
    # quantized image only uses the first len(PALETTE) indexes)
    codes = bytes(code for _, code in PALETTE).ljust(256, b"\x01")
    idx = dithered.tobytes().translate(codes)
    return bytes(hi << 4 | lo for hi, lo in zip(idx[::2], idx[1::2]))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default="docs", help="output folder")
    ap.add_argument("--fill", type=float, default=0, metavar="0..0.5",
                    help="0 = whole poster with white bars, 0.5 = half "
                         "cropped and half bars, in between a bit of both "
                         "(default: 0)")
    ap.add_argument("--from-file", metavar="PATH",
                    help="convert a local image instead of downloading one")
    ap.add_argument("--seed", type=int, help="random seed (for tests)")
    args = ap.parse_args()
    if not 0 <= args.fill <= 0.5:
        ap.error("--fill must be between 0 and 0.5")

    key = os.environ.get("TMDB_API_KEY")
    if not args.from_file and not key:
        sys.exit("Error: TMDB_API_KEY environment variable not set.\n"
                 "The free themoviedb.org API key is required\n"
                 "(https://www.themoviedb.org/settings/api); alternatively\n"
                 "use --from-file to convert a local image.")

    rng = random.Random(args.seed)

    if args.from_file:
        meta = {"title": args.from_file, "date": "", "source": "local file",
                "source_url": "", "image_url": ""}
        img = Image.open(args.from_file)
    else:
        try:
            meta, img = pick_and_download(rng, key)
        except RuntimeError as e:
            sys.exit("Error: %s" % e)

    dithered = convert(img, args.fill)
    # The panel always scans in 800x480: the buffer must be rotated.
    # If the image comes out upside down on your frame, use ROTATE_90.
    data = pack(dithered.transpose(Image.Transpose.ROTATE_270))
    if len(data) != WIDTH * HEIGHT // 2:
        sys.exit("Error: unexpected buffer size (%d bytes)" % len(data))

    version = hashlib.sha1(data).hexdigest()[:8]
    meta["version"] = version
    meta["updated_at"] = datetime.now(timezone.utc).isoformat(timespec="seconds")

    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "art.bin"), "wb") as f:
        f.write(data)
    dithered.convert("RGB").save(os.path.join(args.out, "art.png"),
                                 optimize=True)
    with open(os.path.join(args.out, "meta.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=2)
    with open(os.path.join(args.out, "version.txt"), "w") as f:
        f.write(version + "\n")

    print("Wrote art.bin (%d bytes), art.png, meta.json, version.txt "
          "to %s (version %s)" % (len(data), args.out, version))


if __name__ == "__main__":
    main()
