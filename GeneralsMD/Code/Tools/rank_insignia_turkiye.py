"""Rank marks for Turkiye, one for every rank of the Turkish Land Forces the ladder climbs, from Er to
Mareşal.

Each mark is the sign on the shoulder board without the board, cut down for twelve to fourteen
pixels high. The Er wears nothing, as he does in the army. Onbaşı and Çavuş wear one and two
silver chevrons pointing down, the Uzman Çavuş three. The astsubay grades wear gold bars, one to
three, and the three senior ones put a star over one to three bars. Company officers wear one to
three stars; field officers the same stars over the oak wreath; generals the crescent and star
(ay yıldız) over the wreath with one to four stars above it, and the Mareşal the crescent and star
inside a closed wreath. The astsubay shapes are simplified: the army's own boards mix stripe widths
that blur together at game size, so the count of bars and the star carry the order instead. A dark
red edge and no background, so they read over sand and grass the way the other sides' marks do.

    python Tools/rank_insignia_turkiye.py
        writes Data/Art/Textures/ReforgedRanksTurkiye.tga, one 64-pixel cell per rank, and the
        ReforgedRanksTurkiye.ini that names each cell RankTurkiye_<grade>.

    python Tools/rank_insignia_turkiye.py preview <png>
        also writes a labelled sheet of every mark at game size and at full size.

Drawn eight times too big and brought down, which is the antialiasing."""
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

from rank_insignia_cells import ink_entries

HERE = os.path.dirname(os.path.abspath(__file__))
SCALE = 8
CELL = 64
COLUMNS = 8
ROWS = 3
OUTLINE = 21    # a MaxFilter size, odd: the dark edge round every mark, 10 drawn pixels
EDGE = (48, 4, 6)
GOLD_TOP = (255, 226, 110)
GOLD_BOTTOM = (200, 128, 20)
SILVER_TOP = (252, 252, 252)
SILVER_BOTTOM = (138, 146, 160)
NAME = "ReforgedRanksTurkiye"
TEXTURE_PATH = os.path.join(HERE, "..", "Data", "Art", "Textures", NAME + ".tga")
INI_PATH = os.path.join(HERE, "..", "Data", "INI", "MappedImages", "HandCreated", NAME + ".ini")
INI_HEADER = """; Turkiye's rank marks, one per Turkish Land Forces rank, all in one texture.  Written by
; Tools/rank_insignia_turkiye.py with the texture it names; do not edit by hand.  Mapped images are
; not in the multiplayer INI checksum.
"""
INI_ENTRY = """
MappedImage RankTurkiye_%d
  Texture = %s.tga
  TextureWidth = %d
  TextureHeight = %d
  Coords = Left:%d Top:%d Right:%d Bottom:%d
  Status = NONE
End
"""

CHEVRON = 6.5
GAP = 3.0
RISE = 12.0
CHEVRON_HALF_WIDTH = 14
BAR = 6.0
BAR_GAP = 3.0
BAR_HALF_WIDTH = 10    # a lone bar comes out as long for its height as America's gold bar

# grade, name, mark; grade 0 is unranked on every side and the Er wears nothing, so both cells stay empty
RANKS = (
    (0, "Rütbesiz", ("none",)),
    (1, "Er", ("none",)),
    (2, "Onbaşı", ("chevrons", 1)),
    (3, "Çavuş", ("chevrons", 2)),
    (4, "Uzman Çavuş", ("chevrons", 3)),
    (5, "Astsubay Çavuş", ("bars", 1, 0)),
    (6, "Astsubay Kıdemli Çavuş", ("bars", 2, 0)),
    (7, "Astsubay Üstçavuş", ("bars", 3, 0)),
    (8, "Astsubay Kıdemli Üstçavuş", ("bars", 1, 1)),
    (9, "Astsubay Başçavuş", ("bars", 2, 1)),
    (10, "Astsubay Kıdemli Başçavuş", ("bars", 3, 1)),
    (11, "Teğmen", ("officer", 1, False)),
    (12, "Üsteğmen", ("officer", 2, False)),
    (13, "Yüzbaşı", ("officer", 3, False)),
    (14, "Binbaşı", ("officer", 1, True)),
    (15, "Yarbay", ("officer", 2, True)),
    (16, "Albay", ("officer", 3, True)),
    (17, "Tuğgeneral", ("general", 1)),
    (18, "Tümgeneral", ("general", 2)),
    (19, "Korgeneral", ("general", 3)),
    (20, "Orgeneral", ("general", 4)),
    (21, "Mareşal", ("marshal",)),
)


def scaled(points):
    """Points in shipped pixels, in drawn pixels."""
    return [(x * SCALE, y * SCALE) for x, y in points]


def star_points(cx, cy, radius, turn=-90):
    points = []
    for i in range(10):
        r = radius if i % 2 == 0 else radius * 0.5
        angle = math.radians(turn + 36 * i)
        points.append((cx + r * math.cos(angle), cy + r * math.sin(angle)))
    return scaled(points)


def star(draw, cx, cy, radius, fill=255):
    """A five-pointed star, point up."""
    draw.polygon(star_points(cx, cy, radius), fill=fill)


def stars(draw, count, cy, radius, pitch):
    """count stars in a tight cluster centred across the cell on cy: two side by side, three in a
    triangle, four two over two, so no mark runs much wider than its height. Returns the cluster's
    half width and its bottom."""
    layout = {1: [(0, 0)],
              2: [(-0.5, 0), (0.5, 0)],
              3: [(0, -0.45), (-0.5, 0.45), (0.5, 0.45)],
              4: [(-0.5, -0.5), (0.5, -0.5), (-0.5, 0.5), (0.5, 0.5)]}[count]
    for x, y in layout:
        star(draw, 32 + x * pitch, cy + y * pitch, radius)
    half_width = max(x for x, _ in layout) * pitch + radius * 0.95
    return half_width, cy + max(y for _, y in layout) * pitch + radius * 0.81


def chevron_down(draw, cx, top, half_width):
    """A chevron pointing down, its arms' tops on top."""
    draw.polygon(scaled([(cx - half_width, top), (cx, top + RISE), (cx + half_width, top),
                         (cx + half_width, top + CHEVRON), (cx, top + RISE + CHEVRON),
                         (cx - half_width, top + CHEVRON)]), fill=255)


def wreath(draw, cx, cy, half, drop, closed=False):
    """The oak wreath: a thick arc cupping what stands on it, with a leaf notch every few degrees
    so it reads as leaves and not a ring when the cell is enlarged. Closed for the Mareşal."""
    box = scaled([(cx - half, cy - drop), (cx + half, cy + drop)])
    start, end = (0, 360) if closed else (20, 160)
    draw.arc(box, start=start, end=end, fill=255, width=round(4.5 * SCALE))
    step = 24 if closed else 28
    for angle in range(start + step // 2, end, step):
        rad = math.radians(angle)
        x = cx + half * math.cos(rad)
        y = cy + drop * math.sin(rad)
        draw.ellipse(scaled([(x - 1.1, y - 1.1), (x + 1.1, y + 1.1)]), fill=0)


def crescent_star(shape, cx, cy, radius):
    """The crescent and star of the flag, opening to the right, drawn into shape."""
    size = shape.size
    moon = Image.new("L", size, 0)
    draw = ImageDraw.Draw(moon)
    draw.ellipse(scaled([(cx - radius, cy - radius), (cx + radius, cy + radius)]), fill=255)
    inner = radius * 0.8
    shift = radius * 0.4
    bite = Image.new("L", size, 0)
    ImageDraw.Draw(bite).ellipse(scaled([(cx + shift - inner, cy - inner), (cx + shift + inner, cy + inner)]),
                                 fill=255)
    moon = ImageChops.subtract(moon, bite)
    shape.paste(255, (0, 0), moon)
    # the flag's star stands just outside the crescent's opening, one point toward the moon
    ImageDraw.Draw(shape).polygon(star_points(cx + radius * 0.75, cy, radius * 0.45, turn=180), fill=255)


def mark(kind):
    """The shape of one rank's mark, white on black, CELL by CELL in drawn pixels."""
    shape = Image.new("L", (CELL * SCALE, CELL * SCALE), 0)
    draw = ImageDraw.Draw(shape)
    if kind[0] == "chevrons":
        count = kind[1]
        height = count * CHEVRON + (count - 1) * GAP + RISE
        top = (CELL - height) / 2
        for i in range(count):
            chevron_down(draw, 32, top + i * (CHEVRON + GAP), CHEVRON_HALF_WIDTH)
    elif kind[0] == "bars":
        _, count, star_count = kind
        star_height = 18 if star_count else 0
        height = star_height + count * BAR + (count - 1) * BAR_GAP
        top = (CELL - height) / 2
        if star_count:
            star(draw, 32, top + 7, 8)
        for i in range(count):
            y = top + star_height + i * (BAR + BAR_GAP)
            draw.rectangle(scaled([(32 - BAR_HALF_WIDTH, y), (32 + BAR_HALF_WIDTH, y + BAR)]), fill=255)
    elif kind[0] == "officer":
        _, count, field = kind
        if field:
            half, bottom = stars(draw, count, 22, 8.5, 16)
            wreath(draw, 32, 22, max(half, 12) + 2, bottom - 22 + 9)
        else:
            stars(draw, count, 32, 9, 17)
    elif kind[0] == "general":
        count = kind[1]
        # the stars in one row over the emblem, the emblem cupped by the wreath
        pitch = 11
        for i in range(count):
            star(draw, 32 + (i - (count - 1) / 2) * pitch, 14, 5.5)
        crescent_star(shape, 30, 34, 10)
        wreath(draw, 32, 34, 18, 13)
    elif kind[0] == "marshal":
        crescent_star(shape, 30, 32, 12)
        wreath(draw, 32, 32, 22, 22, closed=True)
    return shape


def finish(shape, kind):
    """The mark in its metal, light at the top and dark at the bottom, on its dark edge."""
    size = shape.size
    colour = Image.new("RGB", size)
    draw = ImageDraw.Draw(colour)
    top_colour, bottom_colour = (SILVER_TOP, SILVER_BOTTOM) if kind[0] == "chevrons" else (GOLD_TOP, GOLD_BOTTOM)
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
    for index, (grade, _, kind) in enumerate(RANKS):
        assert grade == index, "the atlas holds the ladder in order"
        art = finish(mark(kind), kind)
        x, y = cell_origin(index)
        atlas.paste(art, (x, y))
        cells.append(art)
        ini_text += INI_ENTRY % (grade, NAME, atlas.width, atlas.height, x, y, x + CELL, y + CELL)
    return atlas, cells, ini_text


def preview(cells, path):
    """Every mark at 14 and 64 pixels on sand and on grass, with its grade and name, two columns."""
    font = ImageFont.truetype("arial.ttf", 15)
    small_font = ImageFont.truetype("arial.ttf", 12)
    row_h, column_w = 76, 560
    rows = (len(RANKS) + 1) // 2
    sheet = Image.new("RGB", (2 * column_w + 10, 40 + row_h * rows), (30, 32, 34))
    draw = ImageDraw.Draw(sheet)
    draw.text((10, 10), "Turkiye ranks  RankTurkiye_<grade>  (14 px game size and 64 px cell, on sand and grass)",
              font=font, fill=(230, 230, 230))
    for i, ((grade, name, _), art) in enumerate(zip(RANKS, cells)):
        left0 = 10 + (i // rows) * column_w
        top = 40 + (i % rows) * row_h
        for j, ground in enumerate(((196, 170, 112), (78, 106, 48))):
            left = left0 + j * 120
            draw.rectangle((left, top, left + 112, top + row_h - 6), fill=ground)
            # the game crops a cell to its ink and draws it 12 or 14 high (rank_insignia_cells.py)
            box = art.getchannel("A").point(lambda v: 255 if v >= 16 else 0).getbbox()
            if box:
                ink = art.crop(box)
                small = ink.resize((max(1, round(ink.width * 14 / ink.height)), 14), Image.LANCZOS)
                sheet.paste(small, (left + 6, top + 28), small)
            sheet.paste(art, (left + 40, top + 3), art)
        draw.text((left0 + 250, top + 10), "%d  %s" % (grade, name), font=font, fill=(240, 240, 240))
        draw.text((left0 + 250, top + 36), "RankTurkiye_%d" % grade, font=small_font, fill=(170, 170, 170))
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    sheet.save(path)
    print(path)


def main():
    atlas, cells, ini_text = build()
    # three chevrons, the starred and stacked bars, the field officers, generals and the Mareşal draw 14 high
    ini_text = ink_entries(atlas, ini_text, "Turkiye", tall=[4, 7] + list(range(8, 11)) + list(range(14, 22)))
    assert "RankTurkiye_0\n" not in ini_text and "RankTurkiye_1\n" not in ini_text, "grade 0 and the Er wear nothing"
    atlas.save(TEXTURE_PATH)
    print(TEXTURE_PATH)
    with open(INI_PATH, "w", newline="\r\n") as ini:
        ini.write(ini_text)
    print(INI_PATH)
    if len(sys.argv) == 3 and sys.argv[1] == "preview":
        preview(cells, sys.argv[2])


if __name__ == "__main__":
    main()
