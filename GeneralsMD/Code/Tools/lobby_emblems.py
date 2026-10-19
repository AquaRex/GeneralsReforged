"""Flat seat marks for the staging rooms: one solid white shape a side, plus Random and Observer.

The lobby's seat rows used to open with the general's own portrait glyph and a side badge, detailed
art that read as noise at 50 pixels. These are the three side crests EA drew for the shell
(SCShellUserInterface512_003/004/001.tga, the 389 pixel emblems in the corner of each sheet) cut to
their outline: the crest's own alpha, white, with a few dark cut-outs drawn back in where a plain
outline turned China's and the GLA's crests into blobs (the disc and star, the bowl, daggers and
disc). Random is a question mark and Observer an eye, drawn here in the same weight. The game
multiplies them by the seat's colour as it draws them (UpdateLobbySeatVisuals in GUIUtil.cpp), so
the white turns that colour and the cut-outs stay dark.

    python Tools/lobby_emblems.py <EnglishZH.big>
        writes Data/Art/Textures/ReforgedLobbyEmblems.tga; ReforgedLobbyEmblems.ini maps its cells.
        The archive is Zero Hour's, Run/EnglishZH.big on this machine."""
import io
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bigfile import _read_index, _matches

CELL = 128
# cell order is the order of the MappedImage blocks in ReforgedLobbyEmblems.ini
CRESTS = [
    ("data/english/art/textures/scshelluserinterface512_003.tga", (1, 1, 389, 389), None),	# USA
    ("data/english/art/textures/scshelluserinterface512_004.tga", (1, 1, 389, 388), "china"),
    ("data/english/art/textures/scshelluserinterface512_001.tga", (1, 1, 389, 389), "gla"),
]
TEXTURE_W, TEXTURE_H = 1024, 128
OUT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Data", "Art", "Textures",
                        "ReforgedLobbyEmblems.tga")


def read_member(archive, pattern):
    with open(archive, "rb") as f:
        for path, offset, size in list(_read_index(f)):
            if _matches(path, pattern):
                f.seek(offset)
                return f.read(size)
    raise ValueError("%s has no %s" % (archive, pattern))


WHITE = (255, 255, 255, 255)
# near black, so the tint multiplied into it stays dark: the cut-outs that keep a solid mark readable
CUT = (22, 26, 38, 255)


def star(draw, centre_x, centre_y, radius):
    points = []
    for corner in range(10):
        angle = -math.pi / 2 + corner * math.pi / 5
        reach = radius if corner % 2 == 0 else radius * 0.42
        points.append((centre_x + reach * math.cos(angle), centre_y + reach * math.sin(angle)))
    draw.polygon(points, fill=WHITE)


def china_cuts(draw):
    """The red disc inside the dragon's ring goes dark, its star stays."""
    draw.ellipse((190 - 122, 212 - 122, 190 + 122, 212 + 122), fill=CUT)
    star(draw, 190, 252, 78)


def gla_cuts(draw):
    """The dark bowl between the blades, the three daggers pointing into it and the disc they guard."""
    draw.ellipse((80, 100, 310, 335), fill=CUT)
    for x0, y0, x1, y1 in ((195, 90, 195, 166), (100, 118, 146, 176), (290, 118, 244, 176)):
        angle = math.atan2(y1 - y0, x1 - x0)
        half_x, half_y = -math.sin(angle) * 13, math.cos(angle) * 13
        draw.polygon([(x0 + half_x, y0 + half_y), (x0 - half_x, y0 - half_y), (x1, y1)], fill=WHITE)
    draw.ellipse((195 - 62, 236 - 62, 195 + 62, 236 + 62), fill=WHITE)


def silhouette(image, cuts):
    """White in the crest's own outline, its alpha kept so the edge stays smooth; cuts, drawn in the
    crest's 389 pixel frame, put the dark detail back that a plain outline loses."""
    white = Image.new("RGBA", image.size, WHITE)
    if cuts:
        cuts(ImageDraw.Draw(white))
    white.putalpha(image.getchannel("A"))
    return white


def fit(image):
    image = image.crop(image.getbbox())
    scale = (CELL - 8) / max(image.size)
    image = image.resize((max(1, round(image.width * scale)), max(1, round(image.height * scale))), Image.LANCZOS)
    cell = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    cell.alpha_composite(image, ((CELL - image.width) // 2, (CELL - image.height) // 2))
    return cell


def drawn(paint):
    big = Image.new("RGBA", (CELL * 4, CELL * 4), (0, 0, 0, 0))
    paint(ImageDraw.Draw(big), CELL * 4)
    return fit(big)


def question(draw, size):
    font = ImageFont.truetype("arialbd.ttf", int(size * 0.9))
    draw.text((size // 2, size // 2), "?", font=font, fill=(255, 255, 255, 255), anchor="mm")


def eye(draw, size):
    w, h = size * 0.92, size * 0.5
    box = (size / 2 - w / 2, size / 2 - h / 2, size / 2 + w / 2, size / 2 + h / 2)
    draw.ellipse(box, fill=(255, 255, 255, 255))
    draw.ellipse((box[0] + size * 0.06, box[1] + size * 0.06, box[2] - size * 0.06, box[3] - size * 0.06),
                 fill=(0, 0, 0, 0))
    r = size * 0.16
    draw.ellipse((size / 2 - r, size / 2 - r, size / 2 + r, size / 2 + r), fill=(255, 255, 255, 255))


def texture(archive):
    cells = []
    cut_functions = {"china": china_cuts, "gla": gla_cuts}
    for member, box, cuts in CRESTS:
        sheet = Image.open(io.BytesIO(read_member(archive, member))).convert("RGBA")
        cells.append(fit(silhouette(sheet.crop(box), cut_functions.get(cuts))))
    cells.append(drawn(question))
    cells.append(drawn(eye))
    out = Image.new("RGBA", (TEXTURE_W, TEXTURE_H), (0, 0, 0, 0))
    for index, cell in enumerate(cells):
        out.paste(cell, (index * CELL, 0))
    # every cell has a shape in it and transparent corners
    for index in range(len(cells)):
        assert out.getpixel((index * CELL + 1, 1))[3] == 0
        assert out.crop((index * CELL, 0, (index + 1) * CELL, CELL)).getbbox() is not None
    return out


def main():
    texture(sys.argv[1]).save(OUT_PATH)
    print(OUT_PATH)


if __name__ == "__main__":
    main()
