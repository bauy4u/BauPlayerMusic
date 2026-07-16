#!/usr/bin/env python3
import math
import re
import sys
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GLYPH_FILE = ROOT / "data" / "chinese_strokes.txt"
SOURCE_DIR = ROOT / "data" / "glyph_sources"
NORM_STROKE_FILE = SOURCE_DIR / "NormStroke.svg"
NORM_STROKE_URL = "https://raw.githubusercontent.com/octycs/norm-stroke/master/font/NormStroke.svg"
MARKER = "# ASCII glyphs for /chi."


ASCII_CHARS = (
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    " !?.,:;'\"-_+=/\\()[]{}#*@&%<>|~`^$"
)

FULLWIDTH_ALIASES = {
    "，": ",",
    "。": ".",
    "！": "!",
    "？": "?",
    "：": ":",
    "；": ";",
    "（": "(",
    "）": ")",
    "【": "[",
    "】": "]",
    "“": '"',
    "”": '"',
    "‘": "'",
    "’": "'",
    "《": "<",
    "》": ">",
    "、": ",",
    "·": ".",
    "～": "~",
}

MONO_ADVANCE = 0.60
SPACE_ADVANCE = 0.32
ASCII_X_STRETCH = 1.40
SYNTHETIC_GLYPHS = {
    "|": "512,102 512,922",
    "^": "250,560 512,840 774,560",
    "`": "366,846 512,700",
}


def target_codepoints():
    chars = set(ASCII_CHARS) | set(FULLWIDTH_ALIASES.keys())
    return {ord(char) for char in chars}


def fetch_norm_stroke():
    if NORM_STROKE_FILE.exists():
        return NORM_STROKE_FILE.read_text(encoding="utf-8")

    SOURCE_DIR.mkdir(parents=True, exist_ok=True)
    req = urllib.request.Request(
        NORM_STROKE_URL,
        headers={"User-Agent": "DDNet-BauPlayerMusic glyph generator"},
    )
    with urllib.request.urlopen(req, timeout=20) as response:
        text = response.read().decode("utf-8")
    NORM_STROKE_FILE.write_text(text, encoding="utf-8")
    return text


def clamp(value, low, high):
    return max(low, min(high, value))


def parse_glyphs(svg_text):
    root = ET.fromstring(svg_text)
    glyphs = {}
    for elem in root.iter():
        if not elem.tag.endswith("glyph"):
            continue
        unicode_value = elem.attrib.get("unicode")
        if not unicode_value or len(unicode_value) != 1:
            continue
        glyphs[unicode_value] = {
            "advance": float(elem.attrib.get("horiz-adv-x", "720")),
            "path": elem.attrib.get("d", ""),
        }
    return glyphs


def path_tokens(path):
    return re.findall(r"[MLA]|[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?", path)


def angle_between(ux, uy, vx, vy):
    dot = ux * vx + uy * vy
    length = math.hypot(ux, uy) * math.hypot(vx, vy)
    if length <= 0.0:
        return 0.0
    value = clamp(dot / length, -1.0, 1.0)
    sign = -1.0 if ux * vy - uy * vx < 0.0 else 1.0
    return sign * math.acos(value)


def sample_arc(start, rx, ry, x_axis_rotation, large_arc, sweep, end):
    x1, y1 = start
    x2, y2 = end
    rx = abs(rx)
    ry = abs(ry)
    if rx <= 0.0 or ry <= 0.0:
        return [end]

    phi = math.radians(x_axis_rotation)
    cos_phi = math.cos(phi)
    sin_phi = math.sin(phi)
    dx = (x1 - x2) * 0.5
    dy = (y1 - y2) * 0.5
    x1p = cos_phi * dx + sin_phi * dy
    y1p = -sin_phi * dx + cos_phi * dy

    radii_check = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if radii_check > 1.0:
        scale = math.sqrt(radii_check)
        rx *= scale
        ry *= scale

    numerator = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    denominator = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    if denominator <= 0.0:
        return [end]

    coef = math.sqrt(max(0.0, numerator / denominator))
    if large_arc == sweep:
        coef = -coef
    cxp = coef * rx * y1p / ry
    cyp = coef * -ry * x1p / rx
    cx = cos_phi * cxp - sin_phi * cyp + (x1 + x2) * 0.5
    cy = sin_phi * cxp + cos_phi * cyp + (y1 + y2) * 0.5

    theta1 = angle_between(1.0, 0.0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    delta = angle_between((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and delta > 0.0:
        delta -= 2.0 * math.pi
    elif sweep and delta < 0.0:
        delta += 2.0 * math.pi

    segments = max(4, int(math.ceil(abs(delta) * max(rx, ry) / 52.0)))
    points = []
    for i in range(1, segments + 1):
        theta = theta1 + delta * i / segments
        xp = rx * math.cos(theta)
        yp = ry * math.sin(theta)
        x = cos_phi * xp - sin_phi * yp + cx
        y = sin_phi * xp + cos_phi * yp + cy
        points.append((x, y))
    return points


def parse_path(path):
    tokens = path_tokens(path)
    strokes = []
    current = []
    pos = (0.0, 0.0)
    i = 0

    while i < len(tokens):
        command = tokens[i]
        i += 1
        if command == "M":
            if len(current) >= 2:
                strokes.append(current)
            current = []
            x = float(tokens[i])
            y = float(tokens[i + 1])
            i += 2
            pos = (x, y)
            current.append(pos)
        elif command == "L":
            x = float(tokens[i])
            y = float(tokens[i + 1])
            i += 2
            pos = (x, y)
            current.append(pos)
        elif command == "A":
            rx = float(tokens[i])
            ry = float(tokens[i + 1])
            rotation = float(tokens[i + 2])
            large_arc = int(float(tokens[i + 3]))
            sweep = int(float(tokens[i + 4]))
            x = float(tokens[i + 5])
            y = float(tokens[i + 6])
            i += 7
            for point in sample_arc(pos, rx, ry, rotation, large_arc, sweep, (x, y)):
                current.append(point)
            pos = (x, y)
        else:
            raise ValueError(f"unsupported SVG path command: {command}")

    if len(current) >= 2:
        strokes.append(current)
    return strokes


def normalize_strokes(strokes, advance, x_stretch=1.0):
    if not strokes:
        return ""

    min_x = min(point[0] for stroke in strokes for point in stroke)
    max_x = max(point[0] for stroke in strokes for point in stroke)
    min_y = min(point[1] for stroke in strokes for point in stroke)
    max_y = max(point[1] for stroke in strokes for point in stroke)

    # NormStroke is a technical-drawing font. Keep the shared baseline/cap-height
    # box stable so all Latin letters and symbols align instead of each glyph
    # being independently stretched.
    min_x = min(min_x, 0.0)
    max_x = max(max_x, advance)
    min_y = min(min_y, -200.0)
    max_y = max(max_y, 800.0)
    width = max(1.0, max_x - min_x)
    height = max(1.0, max_y - min_y)
    scale = min(820.0 / width, 820.0 / height)
    drawn_width = width * scale
    drawn_height = height * scale
    offset_x = (1024.0 - drawn_width) * 0.5
    offset_y = (1024.0 - drawn_height) * 0.5

    out = []
    for stroke in strokes:
        points = []
        last = None
        for x, y in stroke:
            nx = round((x - min_x) * scale + offset_x)
            ny = round((y - min_y) * scale + offset_y)
            nx = round(512.0 + (nx - 512.0) * x_stretch)
            point = (clamp(nx, 0, 1024), clamp(ny, 0, 1024))
            if point != last:
                points.append(f"{point[0]},{point[1]}")
                last = point
        if len(points) >= 2:
            out.append(" ".join(points))
    return "|".join(out)


def line_for_char(char, glyphs):
    if char == " ":
        return f"{ord(char):04X};;{SPACE_ADVANCE:.2f}"

    source_char = FULLWIDTH_ALIASES.get(char, char)
    if source_char in SYNTHETIC_GLYPHS:
        return f"{ord(char):04X};{SYNTHETIC_GLYPHS[source_char]};{MONO_ADVANCE:.2f}"

    glyph = glyphs.get(source_char)
    if glyph is None:
        return None
    strokes = parse_path(glyph["path"])
    stroke_text = normalize_strokes(strokes, glyph["advance"], ASCII_X_STRETCH)
    if not stroke_text:
        return None

    advance = max(MONO_ADVANCE, SPACE_ADVANCE) if ord(source_char) < 128 else MONO_ADVANCE
    return f"{ord(char):04X};{stroke_text};{advance:.2f}"


def strip_old_ascii(lines):
    targets = target_codepoints()
    out = []
    skipping_old_block = False
    for line in lines:
        stripped = line.strip()
        if stripped == MARKER:
            skipping_old_block = True
            continue
        if skipping_old_block:
            if not stripped:
                continue
            code_text = stripped.split(";", 1)[0]
            try:
                codepoint = int(code_text, 16)
            except ValueError:
                skipping_old_block = False
            else:
                if codepoint in targets:
                    continue
                skipping_old_block = False

        if ";" in stripped:
            code_text = stripped.split(";", 1)[0]
            try:
                codepoint = int(code_text, 16)
            except ValueError:
                pass
            else:
                if codepoint in targets:
                    continue
        out.append(line)

    while out and not out[-1].strip():
        out.pop()
    return out


def main():
    if not GLYPH_FILE.exists():
        print(f"missing glyph file: {GLYPH_FILE}", file=sys.stderr)
        return 1

    glyphs = parse_glyphs(fetch_norm_stroke())
    lines = GLYPH_FILE.read_text(encoding="utf-8").splitlines()
    lines = strip_old_ascii(lines)

    generated = []
    seen = set()
    missing = []
    for char in ASCII_CHARS:
        if char in seen:
            continue
        seen.add(char)
        line = line_for_char(char, glyphs)
        if line is None:
            missing.append(char)
        else:
            generated.append(line)
    for char in FULLWIDTH_ALIASES:
        line = line_for_char(char, glyphs)
        if line is None:
            missing.append(char)
        else:
            generated.append(line)

    lines.append(MARKER)
    lines.extend(generated)
    GLYPH_FILE.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"appended {len(generated)} NormStroke ASCII/symbol glyphs to {GLYPH_FILE}")
    if missing:
        print("missing:", "".join(missing))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
