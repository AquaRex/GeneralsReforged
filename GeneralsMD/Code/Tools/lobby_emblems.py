"""Flat seat marks for the staging rooms: one solid shape a general, the side's crest for a side's
plain faction, plus Random, Observer, a mod faction's disc and the ring a taken start spot wears.

The lobby's seat rows used to open with the general's own portrait glyph and a side badge, detailed
art that read as noise at 50 pixels. The three plain factions get the side crests EA drew for the
shell (SCShellUserInterface512_003/004/001.tga, the 389 pixel emblems in the corner of each sheet)
cut to their outline: the crest's own alpha, white, with a few dark cut-outs drawn back in where a
plain outline turned China's and the GLA's crests into blobs (the disc and star, the bowl, daggers
and disc). Each general has a mark drawn here: a jet, a gun sight, a beam striking from orbit, the
radiation trefoil, a tank, a helmet, a poison drop, a lit bomb and a hooded figure. Random is a question mark
and Observer an open eye. The game multiplies every cell by a seat's or a team's colour as it draws
it (GUIUtil.cpp), so the white turns that colour and the cut-outs stay dark.

    python Tools/lobby_emblems.py <EnglishZH.big>
        writes Data/Art/Textures/ReforgedLobbyEmblems.tga and the ReforgedLobbyEmblems.ini that maps
        its cells. The archive is Zero Hour's, Run/EnglishZH.big on this machine."""
import io
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bigfile import _read_index, _matches

CELL = 128
CRESTS = [
    ("data/english/art/textures/scshelluserinterface512_003.tga", (1, 1, 389, 389), None),	# USA
    ("data/english/art/textures/scshelluserinterface512_004.tga", (1, 1, 389, 388), "china"),
    ("data/english/art/textures/scshelluserinterface512_001.tga", (1, 1, 389, 389), "gla"),
]
TEXTURE_W, TEXTURE_H = 1024, 256
HERE = os.path.dirname(os.path.abspath(__file__))
OUT_PATH = os.path.join(HERE, "..", "Data", "Art", "Textures", "ReforgedLobbyEmblems.tga")
INI_PATH = os.path.join(HERE, "..", "Data", "INI", "MappedImages", "HandCreated", "ReforgedLobbyEmblems.ini")


def read_member(archive, pattern):
    with open(archive, "rb") as f:
        for path, offset, size in list(_read_index(f)):
            if _matches(path, pattern):
                f.seek(offset)
                return f.read(size)
    raise ValueError("%s has no %s" % (archive, pattern))


WHITE = (255, 255, 255, 255)
CLEAR = (0, 0, 0, 0)
# near black, so the tint multiplied into it stays dark: the cut-outs that keep a solid mark readable
CUT = (22, 26, 38, 255)


def star(draw, centre_x, centre_y, radius, fill=WHITE):
    points = []
    for corner in range(10):
        angle = -math.pi / 2 + corner * math.pi / 5
        reach = radius if corner % 2 == 0 else radius * 0.42
        points.append((centre_x + reach * math.cos(angle), centre_y + reach * math.sin(angle)))
    draw.polygon(points, fill=fill)


# The generals' own marks, each drawn on a 512 pixel canvas: one solid shape a general, with dark
# cut-outs where the shape alone would not say which general it is.

def air_force(draw, size):
    """A jet from above."""
    draw.polygon([(256, 30), (290, 120), (292, 230), (480, 330), (480, 372), (292, 320), (286, 420),
                  (350, 470), (350, 495), (256, 470), (162, 495), (162, 470), (226, 420), (220, 320),
                  (32, 372), (32, 330), (220, 230), (222, 120)], fill=WHITE)
    draw.polygon([(256, 70), (272, 130), (240, 130)], fill=CUT)


def laser(draw, size):
    """A gun sight: a ring, its cross hairs and the dot on the target."""
    draw.ellipse((40, 40, 472, 472), fill=WHITE)
    draw.ellipse((100, 100, 412, 412), fill=CUT)
    draw.rectangle((236, 20, 276, 200), fill=WHITE)
    draw.rectangle((236, 312, 276, 492), fill=WHITE)
    draw.rectangle((20, 236, 200, 276), fill=WHITE)
    draw.rectangle((312, 236, 492, 276), fill=WHITE)
    draw.ellipse((216, 216, 296, 296), fill=WHITE)


def superweapon(draw, size):
    """A beam from orbit and the burst where it lands."""
    burst = []
    for corner in range(16):
        angle = corner * math.pi / 8
        reach = 230 if corner % 2 == 0 else 120
        burst.append((256 + reach * math.cos(angle), 330 + reach * 0.75 * math.sin(angle)))
    draw.polygon(burst, fill=WHITE)
    draw.ellipse((196, 290, 316, 370), fill=CUT)
    draw.polygon([(206, 0), (306, 0), (276, 330), (236, 330)], fill=WHITE)


def nuke(draw, size):
    """The radiation trefoil, cut dark out of a disc."""
    draw.ellipse((24, 24, 488, 488), fill=WHITE)
    for start in (-120, 0, 120):
        draw.pieslice((80, 80, 432, 432), start - 30 - 90, start + 30 - 90, fill=CUT)
    draw.ellipse((196, 196, 316, 316), fill=WHITE)
    draw.ellipse((222, 222, 290, 290), fill=CUT)


def tank(draw, size):
    """A tank side on: tracks, hull, turret and gun."""
    draw.rounded_rectangle((30, 320, 482, 440), radius=60, fill=WHITE)
    for wheel in range(5):
        x = 100 + wheel * 78
        draw.ellipse((x - 26, 354, x + 26, 406), fill=CUT)
    draw.polygon([(70, 320), (110, 250), (420, 250), (450, 320)], fill=WHITE)
    draw.rounded_rectangle((150, 170, 330, 260), radius=30, fill=WHITE)
    draw.rectangle((320, 190, 500, 222), fill=WHITE)


def infantry(draw, size):
    """A soldier's helmet with the star on its front."""
    draw.pieslice((60, 80, 452, 472), 180, 360, fill=WHITE)
    draw.rounded_rectangle((20, 262, 492, 320), radius=26, fill=WHITE)
    draw.rectangle((110, 320, 400, 420), fill=WHITE)
    draw.rectangle((150, 330, 360, 400), fill=CUT)
    star(draw, 256, 190, 70, CUT)


def toxin(draw, size):
    """A drop of poison, bubbles in it."""
    draw.polygon([(256, 20), (420, 300), (92, 300)], fill=WHITE)
    draw.ellipse((86, 160, 426, 500), fill=WHITE)
    draw.ellipse((150, 300, 240, 390), fill=CUT)
    draw.ellipse((270, 340, 330, 400), fill=CUT)
    draw.ellipse((230, 230, 270, 270), fill=CUT)


def demolition(draw, size):
    """A round bomb with its fuse lit."""
    draw.ellipse((40, 130, 400, 490), fill=WHITE)
    draw.rectangle((250, 120, 330, 190), fill=WHITE)
    draw.line([(290, 130), (340, 70), (400, 80)], fill=WHITE, width=24)
    star(draw, 430, 70, 66)
    draw.pieslice((100, 190, 260, 350), 180, 270, fill=CUT)


def stealth(draw, size):
    """A hooded figure, its face in shadow but for the eyes."""
    draw.polygon([(256, 20), (380, 120), (420, 300), (500, 492), (12, 492), (92, 300), (132, 120)], fill=WHITE)
    draw.ellipse((166, 140, 346, 330), fill=CUT)
    draw.polygon([(196, 220), (246, 236), (240, 252), (200, 240)], fill=WHITE)
    draw.polygon([(316, 220), (266, 236), (272, 252), (312, 240)], fill=WHITE)


def plain_disc(draw, size):
    """A mod's faction, which this texture cannot know: the game writes its initials on this."""
    draw.ellipse((16, 16, 496, 496), fill=WHITE)


def start_ring(draw, size):
    """Laid over a taken start spot in its team's colour."""
    draw.ellipse((0, 0, 512, 512), fill=WHITE)
    draw.ellipse((150, 150, 362, 362), fill=CLEAR)


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
    cell = Image.new("RGBA", (CELL, CELL), CLEAR)
    cell.alpha_composite(image, ((CELL - image.width) // 2, (CELL - image.height) // 2))
    return cell


def drawn(paint):
    big = Image.new("RGBA", (CELL * 4, CELL * 4), CLEAR)
    paint(ImageDraw.Draw(big), CELL * 4)
    return fit(big)


def question(draw, size):
    font = ImageFont.truetype("arialbd.ttf", int(size * 0.9))
    draw.text((size // 2, size // 2), "?", font=font, fill=WHITE, anchor="mm")


def eye(draw, size):
    w, h = size * 0.92, size * 0.5
    box = (size / 2 - w / 2, size / 2 - h / 2, size / 2 + w / 2, size / 2 + h / 2)
    draw.ellipse(box, fill=WHITE)
    draw.ellipse((box[0] + size * 0.06, box[1] + size * 0.06, box[2] - size * 0.06, box[3] - size * 0.06),
                 fill=CLEAR)
    r = size * 0.16
    draw.ellipse((size / 2 - r, size / 2 - r, size / 2 + r, size / 2 + r), fill=WHITE)


# (mapped image name, how its cell is made), in cell order, eight cells a row. A crest is (member,
# crop box, cuts); a drawn mark is its paint function. The game looks a seat up by its faction's side
# name ("ReforgedLobby" + AmericaAirForceGeneral), then by its base side (USA, China, GLA).
MARKS = [
    ("ReforgedLobbyUSA", CRESTS[0]),
    ("ReforgedLobbyChina", CRESTS[1]),
    ("ReforgedLobbyGLA", CRESTS[2]),
    ("ReforgedLobbyRandom", question),
    ("ReforgedLobbyObserver", eye),
    ("ReforgedLobbyAmericaAirForceGeneral", air_force),
    ("ReforgedLobbyAmericaLaserGeneral", laser),
    ("ReforgedLobbyAmericaSuperWeaponGeneral", superweapon),
    ("ReforgedLobbyChinaNukeGeneral", nuke),
    ("ReforgedLobbyChinaTankGeneral", tank),
    ("ReforgedLobbyChinaInfantryGeneral", infantry),
    ("ReforgedLobbyGLAToxinGeneral", toxin),
    ("ReforgedLobbyGLADemolitionGeneral", demolition),
    ("ReforgedLobbyGLAStealthGeneral", stealth),
    ("ReforgedLobbyFaction", plain_disc),
    ("ReforgedLobbyStartRing", start_ring),
]
PER_ROW = TEXTURE_W // CELL
INI_HEAD = """; The staging rooms' seat marks, one solid shape a cell: the side crests, a mark for each general,
; Random's question mark, the observer's eye, the disc a mod faction's initials go on and the ring a
; taken start spot wears.  White, with dark cut-outs; the game multiplies each by its seat's or its
; team's colour as it draws it.  Written by Tools/lobby_emblems.py with
; Art/Textures/ReforgedLobbyEmblems.tga; do not edit by hand.  Mapped images are not in the
; multiplayer INI checksum.
"""


def cell_origin(index):
    return (index % PER_ROW) * CELL, (index // PER_ROW) * CELL


def texture(archive):
    cut_functions = {"china": china_cuts, "gla": gla_cuts}
    out = Image.new("RGBA", (TEXTURE_W, TEXTURE_H), CLEAR)
    for index, (name, maker) in enumerate(MARKS):
        if isinstance(maker, tuple):
            member, box, cuts = maker
            sheet = Image.open(io.BytesIO(read_member(archive, member))).convert("RGBA")
            cell = fit(silhouette(sheet.crop(box), cut_functions.get(cuts)))
        else:
            cell = drawn(maker)
        # every cell has a shape in it and transparent corners
        if cell.getpixel((1, 1))[3] != 0 or cell.getbbox() is None:
            raise ValueError("%s: expected a shape with clear corners" % name)
        out.paste(cell, cell_origin(index))
    return out


def mapped_images():
    blocks = [INI_HEAD]
    for index, (name, _maker) in enumerate(MARKS):
        left, top = cell_origin(index)
        blocks.append("MappedImage %s\n  Texture = ReforgedLobbyEmblems.tga\n  TextureWidth = %d\n"
                      "  TextureHeight = %d\n  Coords = Left:%d Top:%d Right:%d Bottom:%d\n  Status = NONE\nEnd\n"
                      % (name, TEXTURE_W, TEXTURE_H, left, top, left + CELL, top + CELL))
    return "\n".join(blocks)


def main():
    texture(sys.argv[1]).save(OUT_PATH)
    with open(INI_PATH, "w", newline="\r\n") as ini:
        ini.write(mapped_images())
    print(OUT_PATH)
    print(INI_PATH)


if __name__ == "__main__":
    main()
