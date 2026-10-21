"""Rank marks for China, one for every rank of the PLA Ground Force, from private to general.

Each mark is the shoulder board's sign without the board, simplified for twelve to fourteen pixels
high: silver chevrons for conscripts, gold for sergeants, the senior sergeants' pine branches traded
for one to four pips beside three chevrons, a row of big stars over one stripe for company officers
and two for field officers, and the generals' stars over a thick laurel arc.  A dark red edge and no
background, so they read over sand and grass the way the America and GLA marks do.

The ranks are the 2022 ones.  The reform renamed two sergeant grades and kept their boards:
二级上士 was 上士 and 一级上士 was 四级军士长, so those two marks serve the old names as well.

    python Tools/rank_insignia_china.py
        writes Data/Art/Textures/ReforgedRanksChina.tga, one 64-pixel cell per rank, and the
        ReforgedRanksChina.ini that names each cell RankChina_<code>.

    python Tools/rank_insignia_china.py preview <png>
        also writes a labelled sheet of every mark at game size and at full size.

Drawn eight times too big and brought down, which is the antialiasing."""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

from rank_insignia_cells import ink_entries

HERE = os.path.dirname(os.path.abspath(__file__))
SCALE = 8
CELL = 64
COLUMNS = 8
ROWS = 4
OUTLINE = 21    # a MaxFilter size, odd: the dark edge round every mark, 10 drawn pixels
EDGE = (40, 6, 4)
GOLD_TOP = (255, 226, 110)
GOLD_BOTTOM = (200, 128, 20)
NAME = "ReforgedRanksChina"
TEXTURE_PATH = os.path.join(HERE, "..", "Data", "Art", "Textures", NAME + ".tga")
INI_PATH = os.path.join(HERE, "..", "Data", "INI", "MappedImages", "HandCreated", NAME + ".ini")
INI_HEADER = """; China's rank marks, one per PLA Ground Force rank, all in one texture.  Written by
; Tools/rank_insignia_china.py with the texture it names; do not edit by hand.  Mapped images are
; not in the multiplayer INI checksum.
"""
INI_ENTRY = """
MappedImage RankChina_%s
  Texture = %s.tga
  TextureWidth = %d
  TextureHeight = %d
  Coords = Left:%d Top:%d Right:%d Bottom:%d
  Status = NONE
End
"""

CHEVRON = 8.0   # every chevron one weight: the board's thin and thick came out alike at game size
GAP = 3.5
RISE = 9.0
PIP = 5.5       # radius of a senior sergeant's pip
PIP_PITCH = 12.5
SILVER_TOP = (252, 252, 252)
SILVER_BOTTOM = (138, 146, 160)

# code, English, Chinese, what the mark is: chevrons with their count, pips beside and metal;
# conscripts wear silver so a private and a corporal part by colour, not by a chevron's thickness,
# and the senior sergeants keep three chevrons and count pips instead of pine needles
RANKS = (
    ("NONE", "Unranked", "", ("none",)),    # grade 0 on every side: no mark, so the cell stays empty
    ("PVT", "Private", "列兵", ("chevrons", 1, 0, "silver")),
    ("PFC", "Private First Class", "上等兵", ("chevrons", 2, 0, "silver")),
    ("CPL", "Corporal", "下士", ("chevrons", 1, 0, "gold")),
    ("SGT", "Sergeant", "中士", ("chevrons", 2, 0, "gold")),
    ("SSG2", "Staff Sergeant 2nd Class (was Staff Sergeant 上士)", "二级上士", ("chevrons", 3, 0, "gold")),
    ("SSG1", "Staff Sergeant 1st Class (was Master Sergeant 4th Class 四级军士长)", "一级上士",
     ("chevrons", 3, 1, "gold")),
    ("MSG3", "Master Sergeant 3rd Class", "三级军士长", ("chevrons", 3, 2, "gold")),
    ("MSG2", "Master Sergeant 2nd Class", "二级军士长", ("chevrons", 3, 3, "gold")),
    ("MSG1", "Master Sergeant 1st Class", "一级军士长", ("chevrons", 3, 4, "gold")),
    ("2LT", "Second Lieutenant", "少尉", ("officer", 1, 1)),
    ("1LT", "First Lieutenant", "中尉", ("officer", 1, 2)),
    ("CPT", "Captain", "上尉", ("officer", 1, 3)),
    ("MAJ", "Major", "少校", ("officer", 2, 1)),
    ("LTC", "Lieutenant Colonel", "中校", ("officer", 2, 2)),
    ("COL", "Colonel", "上校", ("officer", 2, 3)),
    ("SCOL", "Senior Colonel", "大校", ("officer", 2, 4)),
    ("MG", "Major General", "少将", ("general", 1)),
    ("LTG", "Lieutenant General", "中将", ("general", 2)),
    ("GEN", "General", "上将", ("general", 3)),
)


def scaled(points):
    """Points in shipped pixels, in drawn pixels."""
    return [(x * SCALE, y * SCALE) for x, y in points]


def star(draw, cx, cy, radius):
    """A five-pointed star, point up."""
    points = []
    for i in range(10):
        r = radius if i % 2 == 0 else radius * 0.5
        angle = math.radians(-90 + 36 * i)
        points.append((cx + r * math.cos(angle), cy + r * math.sin(angle)))
    draw.polygon(scaled(points), fill=255)


def stars(draw, count, cy, radius, pitch):
    """count stars in a row centred across the cell at height cy."""
    for i in range(count):
        star(draw, 32 + (i - (count - 1) / 2) * pitch, cy, radius)


def chevron(draw, cx, top, half_width):
    """A chevron pointing up, its apex at (cx, top)."""
    draw.polygon(scaled([(cx, top), (cx + half_width, top + RISE), (cx + half_width, top + RISE + CHEVRON),
                         (cx, top + CHEVRON), (cx - half_width, top + RISE + CHEVRON),
                         (cx - half_width, top + RISE)]), fill=255)


def pips(draw, count, cx, cy):
    """Up to four pips two by two, the top row filled first."""
    rows = [min(count, 2), count - 2] if count > 2 else [count]
    for r, in_row in enumerate(rows):
        y = cy + (r - (len(rows) - 1) / 2) * PIP_PITCH
        for c in range(in_row):
            x = cx + (c - 0.5) * PIP_PITCH
            draw.ellipse(scaled([(x - PIP, y - PIP), (x + PIP, y + PIP)]), fill=255)


def mark(kind):
    """The shape of one rank's mark, white on black, CELL by CELL in drawn pixels."""
    shape = Image.new("L", (CELL * SCALE, CELL * SCALE), 0)
    draw = ImageDraw.Draw(shape)
    if kind[0] == "chevrons":
        _, count, pip_count, _ = kind
        height = count * CHEVRON + (count - 1) * GAP + RISE
        top = (CELL - height) / 2
        cx, half_width = (19, 16) if pip_count else (32, 26)
        for i in range(count):
            chevron(draw, cx, top + i * (CHEVRON + GAP), half_width)
        if pip_count:
            pips(draw, pip_count, 50, top + height / 2)
    elif kind[0] == "none":
        pass
    elif kind[0] == "officer":
        # big stars over the board's stripe, one for company officers and two for field officers
        _, stripe_count, count = kind
        # the stripe only as wide as the stars, so a lone star is not a dot on a long bar
        radius, pitch = 8.5, 16.5
        half = max(12, ((count - 1) * pitch) / 2 + radius + 2)
        stars(draw, count, 20, radius, pitch)
        for i in range(stripe_count):
            draw.rectangle(scaled([(32 - half, 30 + 6.5 * i), (32 + half, 34 + 6.5 * i)]), fill=255)
    else:
        # bigger stars over a plain thick laurel arc for the wreath
        _, count = kind
        stars(draw, count, 18, 9.5, 19.5)
        draw.arc(scaled([(5, -14), (59, 42)]), start=25, end=155, fill=255, width=round(5.5 * SCALE))
    return shape


def finish(shape, kind):
    """The mark in its metal, light at the top and dark at the bottom, on its dark edge."""
    size = shape.size
    colour = Image.new("RGB", size)
    draw = ImageDraw.Draw(colour)
    top_colour, bottom_colour = (SILVER_TOP, SILVER_BOTTOM) if kind[-1] == "silver" else (GOLD_TOP, GOLD_BOTTOM)
    for y in range(size[1]):
        share = y / (size[1] - 1)
        draw.line((0, y, size[0], y), fill=tuple(round(top + (bottom - top) * share)
                                                for top, bottom in zip(top_colour, bottom_colour)))
    art = Image.composite(colour, Image.new("RGB", size, EDGE), shape)
    art.putalpha(shape.filter(ImageFilter.MaxFilter(OUTLINE)))
    return art.resize((CELL, CELL), Image.LANCZOS)


def cell_origin(index):
    return (index % COLUMNS) * CELL, (index // COLUMNS) * CELL


def build():
    atlas = Image.new("RGBA", (COLUMNS * CELL, ROWS * CELL), (0, 0, 0, 0))
    cells = []
    ini_text = INI_HEADER
    for index, (code, _, _, kind) in enumerate(RANKS):
        art = finish(mark(kind), kind)
        x, y = cell_origin(index)
        atlas.paste(art, (x, y))
        cells.append(art)
        ini_text += INI_ENTRY % (code, NAME, atlas.width, atlas.height, x, y, x + CELL, y + CELL)
    return atlas, cells, ini_text


def preview(cells, path):
    """Every mark at 16 and 64 pixels on sand and on grass, with its name in both languages."""
    font = ImageFont.truetype("msyh.ttc", 15)
    small_font = ImageFont.truetype("msyh.ttc", 12)
    row_h, width = 76, 900
    sheet = Image.new("RGB", (width, 40 + row_h * len(RANKS)), (30, 32, 34))
    draw = ImageDraw.Draw(sheet)
    draw.text((10, 10), "China ranks  RankChina_<code>  (16 px game size and 64 px cell, on sand and grass)",
              font=font, fill=(230, 230, 230))
    for i, ((code, english, chinese, _), art) in enumerate(zip(RANKS, cells)):
        top = 40 + i * row_h
        for j, ground in enumerate(((196, 170, 112), (78, 106, 48))):
            left = 10 + j * 120
            draw.rectangle((left, top, left + 112, top + row_h - 6), fill=ground)
            small = art.resize((16, 16), Image.LANCZOS)
            sheet.paste(small, (left + 6, top + 28), small)
            sheet.paste(art, (left + 40, top + 3), art)
        draw.text((260, top + 10), "%s  %s" % (chinese, english), font=font, fill=(240, 240, 240))
        draw.text((260, top + 36), "RankChina_" + code, font=small_font, fill=(170, 170, 170))
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    sheet.save(path)
    print(path)


def main():
    atlas, cells, ini_text = build()
    # the three-chevron sergeants, the two-stripe field officers and the generals draw 14 high
    ini_text = ink_entries(atlas, ini_text, "China", tall=list(range(5, 10)) + list(range(13, 20)))
    atlas.save(TEXTURE_PATH)
    print(TEXTURE_PATH)
    with open(INI_PATH, "w", newline="\r\n") as ini:
        ini.write(ini_text)
    print(INI_PATH)
    if len(sys.argv) == 3 and sys.argv[1] == "preview":
        preview(cells, sys.argv[2])


if __name__ == "__main__":
    main()
