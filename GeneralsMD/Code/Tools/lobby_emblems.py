"""Flat seat marks for the staging rooms: one solid white shape a side, plus Random and Observer.

The lobby's seat rows used to open with the general's own portrait glyph and a side badge, detailed
art that read as noise at 50 pixels. These are the three side crests EA drew for the shell
(SCShellUserInterface512_003/004/001.tga, the 389 pixel emblems in the corner of each sheet) cut to
their outline: the crest's own alpha, every pixel white. Random is a question mark and Observer an
eye, drawn here in the same weight. The game tints them with the seat's colour as it draws them
(UpdateLobbySeatVisuals in GUIUtil.cpp), so white is the only colour in the texture.

    python Tools/lobby_emblems.py <EnglishZH.big>
        writes Data/Art/Textures/ReforgedLobbyEmblems.tga; ReforgedLobbyEmblems.ini maps its cells.
        The archive is Zero Hour's, Run/EnglishZH.big on this machine."""
import io
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bigfile import _read_index, _matches

CELL = 128
# cell order is the order of the MappedImage blocks in ReforgedLobbyEmblems.ini
CRESTS = [
    ("data/english/art/textures/scshelluserinterface512_003.tga", (1, 1, 389, 389)),	# USA
    ("data/english/art/textures/scshelluserinterface512_004.tga", (1, 1, 389, 388)),	# China
    ("data/english/art/textures/scshelluserinterface512_001.tga", (1, 1, 389, 389)),	# GLA
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


def silhouette(image):
    """White, keeping the shape's own alpha so the edge stays smooth."""
    white = Image.new("RGBA", image.size, (255, 255, 255, 255))
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
    for member, box in CRESTS:
        sheet = Image.open(io.BytesIO(read_member(archive, member))).convert("RGBA")
        cells.append(fit(silhouette(sheet.crop(box))))
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
