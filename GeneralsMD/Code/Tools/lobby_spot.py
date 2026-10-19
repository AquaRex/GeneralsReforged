"""The disc a taken start spot on a staging room's map preview is filled with.

One white disc with a smooth edge; the game multiplies it by the spot's team colour, or its player's
colour without a team, as it draws it (drawStartSpot in SkirmishGameOptionsMenu.cpp), and writes
the seat's number on it in black or white, whichever reads better on that colour.

    python Tools/lobby_spot.py
        writes Data/Art/Textures/ReforgedLobbySpot.tga and the ReforgedLobbySpot.ini that maps it."""
import os

from PIL import Image, ImageDraw

SIZE = 64
# drawn this many times larger and scaled down, which is what smooths the edge
OVERSAMPLE = 8
HERE = os.path.dirname(os.path.abspath(__file__))
OUT_PATH = os.path.join(HERE, "..", "Data", "Art", "Textures", "ReforgedLobbySpot.tga")
INI_PATH = os.path.join(HERE, "..", "Data", "INI", "MappedImages", "HandCreated", "ReforgedLobbySpot.ini")
INI_TEXT = """; The disc a taken start spot on a staging room's map preview is filled with, tinted with its team's
; colour as it is drawn.  Written by Tools/lobby_spot.py with Art/Textures/ReforgedLobbySpot.tga; do
; not edit by hand.  Mapped images are not in the multiplayer INI checksum.

MappedImage ReforgedLobbySpot
  Texture = ReforgedLobbySpot.tga
  TextureWidth = %d
  TextureHeight = %d
  Coords = Left:0 Top:0 Right:%d Bottom:%d
  Status = NONE
End
""" % (SIZE, SIZE, SIZE, SIZE)


def disc():
    big = SIZE * OVERSAMPLE
    image = Image.new("RGBA", (big, big), (255, 255, 255, 0))
    ImageDraw.Draw(image).ellipse((OVERSAMPLE, OVERSAMPLE, big - OVERSAMPLE, big - OVERSAMPLE),
                                  fill=(255, 255, 255, 255))
    image = image.resize((SIZE, SIZE), Image.LANCZOS)
    if image.getpixel((0, 0))[3] != 0 or image.getpixel((SIZE // 2, SIZE // 2))[3] != 255:
        raise ValueError("expected a disc with clear corners and a solid middle")
    return image


def main():
    disc().save(OUT_PATH)
    with open(INI_PATH, "w", newline="\r\n") as ini:
        ini.write(INI_TEXT)
    print(OUT_PATH)
    print(INI_PATH)


if __name__ == "__main__":
    main()
