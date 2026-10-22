#!/usr/bin/env python3
"""Fill a generated map's skeleton with a shipped map's own ground: patch quilting guided by height.

The generator lays out the map: the starts, the ring of plateaus, one ramp down each, the ridges
between them and the bowl in the middle. This prototype keeps that layout, heights and cliffs
included, and lays the exemplar's local character over it (Twilight Flame unless told otherwise):
its height detail, its painted textures and blends, its trees, rocks, tents and sounds.

How. Both maps get one guide channel, the height normalised so the bowl's floor is 0 and the bases'
plateau is 1 (blurred, so the guide is the layout rather than the roll), plus the cliff flag. The
target is cut into 48 cell patches overlapping by 12, in rows. Each patch picks the exemplar patch
whose guide has the same shape (a whole-patch lift is allowed, at a price, so a slope can come from
any level) and whose overlap agrees best with what is already placed: image quilting (Efros and
Freeman 2001) guided the way texture-by-numbers is. The seam inside the overlap is a minimum cut.
The search tries every fourth offset over every fourth cell, then every offset round the sixteen
best over every cell. What comes out is one source cell per target cell, and every layer is read
through that map:

  height   the skeleton's, plus the exemplar's detail (its normalised height less a 21 cell blur of
           it), none within RIM_CELLS of a skeleton cliff, where an exemplar edge laid a cell off
           would double the wall. The pads round the starts, docks and derricks and the skeleton's
           walkable slopes (its ramps, its ways up a ridge) keep the skeleton's height, feathered.
  texture  the exemplar's class a cell, its tile worked out again for where the cell landed (the
           exemplar's tiles are exactly tileIndexForCell of class and cell), the same for both blend
           layers, so the tiling runs on across a seam. Its own texture table goes in with them.
  cliffs   from the final heights: the exemplar's painted cliff bits are exactly its heights'.
  scenery  every tree, rock, tent, sound and hut whose source cell landed off the pads.

The piles go where the exemplar keeps them, up on the high ground between two bases with a tent: a
pair at the bearing halfway to the next seat ("mid") and a pair a third of the way ("field"),
instead of the ramp's foot. One TechOilRefinery goes at the middle.

Why not wave function collapse: `wfcprobe` measures it. Cut to six height bands and four texture
groups the exemplar already shows 7101 distinct 3x3 patterns (11302 with its 21 textures), an
adjacency table of 200 MB, and with the skeleton's band as the constraint a 48x48 crop contradicts
before the first collapse, every try: 0.5% of the skeleton's cells hold a band pattern Twilight never
shows, and one of them is enough. Softening the constraint turns it into the optimisation quilting
already is, and WFC hands back quantised symbols, so the height would want synthesising again.
Quilting copies real cells and is a fixed amount of work.

    python synthmap.py <skeleton.map> [...] [-o outdir] [--source maps.big] [--seed N]
    python synthmap.py table <twilight.json> <map> [...]
    python synthmap.py render <out.png> <map> [...]
    python synthmap.py wfcprobe <skeleton.map>
    python synthmap.py selfcheck

A skeleton is what `test_gameengine.exe write_the_maps_RMG_DUMP_names` writes with RMG_DUMP set.
Output per skeleton: <name>_synth.map (raw CkMp, BlendTileData version 8 with the exemplar's texture
table and the cliff bits) and <name>_synth.png, each cell its texture's average colour under the
map's sun.
"""

import argparse
import json
import math
import os
import re
import struct
import sys
import time
import zlib
from collections import deque

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "GeneralsMD", "Code", "Tools"))

import bigfile                                       # noqa: E402
import mapstyle                                      # noqa: E402
from mapscore import MAP_HEIGHT_SCALE, MAP_XY_FACTOR, CLIFF_WORLD_SPAN  # noqa: E402

RUN = os.path.join(HERE, "..", "..", "GeneralsMD", "Run")
SOURCE_BIG = os.path.join(RUN, "ZH_Generals", "maps.big")
SOURCE_MAP = "maps\\Twilight Flame\\Twilight Flame.map"

PATCH = 48                  # cells a side
OVERLAP = 12                # cells two neighbouring patches share
COARSE = 4                  # the first pass tries every fourth offset over every fourth cell
REFINE = 16                 # best coarse offsets whose neighbours the full comparison tries
DETAIL_RADIUS = 10          # cells: the exemplar's height finer than this blur is its detail
RIM_CELLS = 3               # cells round a skeleton cliff that take none of it
SHIFT_WEIGHT = 0.5          # lifting a source patch by the plateau's height, against one unit of guide
CLIFF_WEIGHT = 0.6          # a cliff where the guide has none, against one unit of height
OVERLAP_WEIGHT = 2.0        # disagreeing with what is already placed, against the guide
CLASS_WEIGHT = 0.15         # a texture class that differs in the overlap
REUSE_WEIGHT = 0.02         # every earlier use of the same source patch
JITTER = 0.04               # the seed's say among near-equal candidates
SEAM_BAND = 2               # cells either side of a seam whose height is smoothed
PAD_RADIUS = (24, 34)       # a start's pad keeps the skeleton's height inside the first, fades out by the second
SITE_RADIUS = (7, 12)       # the same round a dock or a derrick
SLOPE_REACH = 100           # cells from a start the skeleton's walkable slopes are kept
PILE_FLAT = 4               # cells round a pile levelled
REFINERY_FLAT = 8

SCENERY = re.compile(r"^(Tree|Rocks|RockCluster|Tent|Amb_|AmbientWind|WoodTower|QuonsetHut)")
SKELETON_SCENERY = re.compile(r"^(Tree|Rocks|RockCluster)")


#-----------------------------------------------------------------------------------------------
# Chunks
#-----------------------------------------------------------------------------------------------

class RawMap:
    """A CkMp file as its name table and its top level chunks, bodies untouched."""

    def __init__(self, data):
        data = mapstyle.unpack_map(data)
        if data[:4] != b"CkMp":
            raise ValueError("not a CkMp map")
        count = struct.unpack_from("<i", data, 4)[0]
        pos = 8
        self.ids = {}
        for _ in range(count):
            length = data[pos]
            name = data[pos + 1:pos + 1 + length].decode("latin-1")
            pos += 1 + length
            self.ids[name] = struct.unpack_from("<I", data, pos)[0]
            pos += 4
        self.header = data[:pos]
        self.chunks = split_chunks(data, pos, len(data))
        self.names = {v: k for k, v in self.ids.items()}

    def body(self, name):
        for cid, version, body in self.chunks:
            if self.names[cid] == name:
                return version, body
        raise KeyError(name)

    def replace(self, name, version, body):
        self.chunks = [(cid, version, body) if self.names[cid] == name else (cid, v, b)
                       for cid, v, b in self.chunks]

    def bytes(self):
        return self.header + join_chunks(self.chunks)


def split_chunks(data, pos, end):
    out = []
    while pos + 10 <= end:
        cid, version, size = struct.unpack_from("<IHi", data, pos)
        out.append((cid, version, data[pos + 10:pos + 10 + size]))
        pos += 10 + size
    return out


def join_chunks(chunks):
    return b"".join(struct.pack("<IHi", cid, version, len(body)) + body
                    for cid, version, body in chunks)


def ascii_at(data, pos):
    length = struct.unpack_from("<H", data, pos)[0]
    return data[pos + 2:pos + 2 + length].decode("latin-1"), pos + 2 + length


def object_head(body):
    """x, y (world), angle, template of an Object chunk body."""
    x, y, _, angle = struct.unpack_from("<4f", body, 0)
    template, _ = ascii_at(body, 20)
    return x, y, angle, template


def blend_cells(version, body, width, height):
    """The per-cell arrays of a BlendTileData body and the rest of it, which is the texture table."""
    size = struct.unpack_from("<i", body, 0)[0]
    pos = 4
    layers = []
    for present in (True, True, version >= 6, version >= 5):
        if present:
            layers.append(np.frombuffer(body, "<i2", size, pos).reshape(height, width).copy())
            pos += size * 2
        else:
            layers.append(np.zeros((height, width), np.int16))
    cliff = np.zeros((height, width), bool)
    if version >= 7:
        row = (width + 1) // 8 if version == 7 else (width + 7) // 8
        bits = np.frombuffer(body, np.uint8, row * height, pos).reshape(height, row)
        cliff = np.unpackbits(bits, axis=1, bitorder="little")[:, :width].astype(bool)
        if cliff.shape[1] < width:
            cliff = np.pad(cliff, ((0, 0), (0, width - cliff.shape[1])))
        pos += row * height
    return layers, cliff, body[pos:]


def texture_table(tail):
    """WorldHeightMap::ParseBlendTileData past the cells: the table up to the blend entries, and
    each blend entry as (tile, the five flag bytes and the long diagonal, custom edge class)."""
    bitmap, blended, cliffs, classes = struct.unpack_from("<4i", tail, 0)
    pos = 16
    for _ in range(classes):
        pos += 16
        pos = ascii_at(tail, pos)[1]
    edge_tiles, edge_classes = struct.unpack_from("<2i", tail, pos)
    pos += 8
    for _ in range(edge_classes):
        pos += 12
        pos = ascii_at(tail, pos)[1]
    head = tail[12:pos]                         # from the class count on
    entries = [(0, b"\0" * 6, -1)]
    for _ in range(blended - 1):
        tile, flags, custom, _ = struct.unpack_from("<i6sii", tail, pos)
        entries.append((tile, flags, custom))
        pos += 18
    return bitmap, head, entries


def tile_for(cls, first, width, y, x):
    """The tile WorldBuilder paints a class with at a cell (the generator's tileIndexForCell)."""
    w = width[cls]
    return ((first[cls] + (x // 2) % w + w * ((y // 2) % w)) << 2) + 2 * (y & 1) + (x & 1)


#-----------------------------------------------------------------------------------------------
# The two maps
#-----------------------------------------------------------------------------------------------

def box(a, r):
    """Mean over a (2r+1) square, edges clamped."""
    k = 2 * r + 1
    c = np.pad(np.pad(a.astype(np.float64), r, mode="edge").cumsum(0).cumsum(1), ((1, 0), (1, 0)))
    return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)


def span_cliff(h):
    """WorldHeightMapData::initCliffFlagsFromHeights: a cell whose corners differ by more than the
    cliff span (mapscore's CLIFF_WORLD_SPAN) is a cliff."""
    h = h.astype(np.int32)
    corners = np.stack([h[:-1, :-1], h[:-1, 1:], h[1:, :-1], h[1:, 1:]])
    span = (corners.max(0) - corners.min(0)) * MAP_HEIGHT_SCALE
    out = np.zeros(h.shape, bool)
    out[:-1, :-1] = span > CLIFF_WORLD_SPAN
    return out


def walkable(cliff):
    """Pathfinder's one-cell growth of every cliff."""
    grown = cliff.copy()
    grown[1:] |= cliff[:-1]
    grown[:-1] |= cliff[1:]
    g2 = grown.copy()
    g2[:, 1:] |= grown[:, :-1]
    g2[:, :-1] |= grown[:, 1:]
    return ~g2


def reach(passable, start):
    """Cells walkable from one cell, four ways."""
    h, w = passable.shape
    seen = np.zeros_like(passable)
    sy, sx = start
    seen[sy, sx] = True
    queue = deque([(sy, sx)])
    while queue:
        y, x = queue.popleft()
        for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
            if 0 <= ny < h and 0 <= nx < w and passable[ny, nx] and not seen[ny, nx]:
                seen[ny, nx] = True
                queue.append((ny, nx))
    return seen


class Grid:
    """A map's heights and its two levels: the bowl's floor (the median of the middle, as
    mapstyle measures it) and the bases' plateau (the median start)."""

    def __init__(self, data):
        self.m = mapstyle.StyleMap(data)
        m = self.m
        self.h = np.frombuffer(bytes(m.heights), np.uint8).reshape(m.height, m.width).astype(np.int32)
        self.starts = [(int(y + 0.5) + m.border, int(x + 0.5) + m.border) for x, y in m.starts]
        self.plateau = float(np.median([np.median(self.h[y - 3:y + 4, x - 3:x + 4])
                                        for y, x in self.starts]))
        cy = m.border + m.playable_y / 2.0
        cx = m.border + m.playable_x / 2.0
        yy, xx = np.mgrid[0:m.height, 0:m.width]
        middle = np.hypot(xx + 0.5 - cx, yy + 0.5 - cy) <= 0.25 * m.playable
        self.bowl = float(np.median(self.h[middle]))
        self.centre = (cy, cx)
        self.t = np.clip((self.h - self.bowl) / (self.plateau - self.bowl), -0.6, 2.2)
        self.guide = box(self.t, 2)


def load_exemplar(path, inner):
    data = mapstyle.load_bytes(path, inner)
    grid = Grid(data)
    raw = RawMap(data)
    version, body = raw.body("BlendTileData")
    grid.layers, grid.cliff, grid.tail = blend_cells(version, body, grid.m.width, grid.m.height)
    owner = np.zeros(4096, np.int32)
    for index, (name, first, count) in enumerate(grid.m.texture_classes):
        owner[first:first + count] = index
    grid.cls = owner[grid.layers[0] >> 2]
    grid.class_names = [name for name, _, _ in grid.m.texture_classes]
    grid.first = np.array([first for _, first, _ in grid.m.texture_classes])
    grid.width = np.array([math.isqrt(count) for _, _, count in grid.m.texture_classes])
    grid.bitmap_tiles, grid.table_head, entries = texture_table(grid.tail)
    # a blend is the class laid over the cell and how; its tile follows from where it lands
    grid.blend_class = np.array([owner[tile >> 2] for tile, _, _ in entries])
    shapes = sorted(set((flags, custom) for _, flags, custom in entries[1:]))
    grid.shapes = shapes
    grid.blend_shape = np.array([0] + [shapes.index((f, c)) for _, f, c in entries[1:]])

    # the scenery that moves with the ground, by the cell it stands on
    grid.scenery = {}
    _, objects = raw.body("ObjectsList")
    for cid, _, body in split_chunks(objects, 0, len(objects)):
        x, y, angle, template = object_head(body)
        if SCENERY.match(template):
            cell = (int(y / MAP_XY_FACTOR) + grid.m.border, int(x / MAP_XY_FACTOR) + grid.m.border)
            grid.scenery.setdefault(cell, []).append(
                (template, x / MAP_XY_FACTOR % 1.0, y / MAP_XY_FACTOR % 1.0, angle))
    return grid


#-----------------------------------------------------------------------------------------------
# Hashing, the generator's (RandomMapGenerator.cpp hashCell), so a port draws the same numbers
#-----------------------------------------------------------------------------------------------

def hash_cell(seed, x, y):
    m = 0xffffffff
    h = (seed * 374761393) & m
    h = (h + x * 668265263) & m
    h ^= h >> 13
    h = (h + y * 2246822519) & m
    h ^= h >> 16
    h = (h * 2654435761) & m
    return h ^ (h >> 15)


def hash_unit(seed, x, y):
    return hash_cell(seed, x, y) / 4294967296.0


#-----------------------------------------------------------------------------------------------
# Quilting
#-----------------------------------------------------------------------------------------------

def min_cut(error):
    """Column per row of the cheapest top-to-bottom path through error (rows x cols)."""
    rows, cols = error.shape
    total = error.copy()
    for r in range(1, rows):
        prev = total[r - 1]
        left = np.concatenate(([np.inf], prev[:-1]))
        right = np.concatenate((prev[1:], [np.inf]))
        total[r] += np.minimum(prev, np.minimum(left, right))
    path = np.zeros(rows, np.int32)
    path[-1] = int(np.argmin(total[-1]))
    for r in range(rows - 2, -1, -1):
        c = path[r + 1]
        lo, hi = max(c - 1, 0), min(c + 2, cols)
        path[r] = lo + int(np.argmin(total[r, lo:hi]))
    return path


def positions(size):
    out = list(range(0, size - PATCH, PATCH - OVERLAP)) + [size - PATCH]
    return sorted(set(out))


def protection(skel):
    """1 where the skeleton's own height stays: the pads, the sites, the walkable slopes."""
    m = skel.m
    yy, xx = np.mgrid[0:m.height, 0:m.width]

    def ring(points, inner, outer):
        out = np.zeros((m.height, m.width))
        for y, x in points:
            d = np.hypot(yy - y, xx - x)
            out = np.maximum(out, np.clip((outer - d) / float(outer - inner), 0, 1))
        return out

    near = np.full((m.height, m.width), np.inf)
    for y, x in skel.starts:
        near = np.minimum(near, np.hypot(yy - y, xx - x))
    slope = (walkable(span_cliff(skel.h)) & (near < SLOPE_REACH) &
             (((skel.t > 0.15) & (skel.t < 0.85)) | ((skel.t > 1.1) & (skel.t < 1.45))))
    slope = np.clip(box(slope, 2) * 2.0 - 0.5, 0, 1)
    sites = [(int(y + 0.5) + m.border, int(x + 0.5) + m.border) for x, y in m.supplies + m.derricks]
    pads = np.maximum(ring(skel.starts, *PAD_RADIUS), ring(sites, *SITE_RADIUS))
    return np.maximum(pads, slope), pads


def quilt(src, skel, keep, seed, field):
    """One source cell for every target cell, (sy, sx) arrays, and field read through them."""
    th, tw = skel.h.shape
    sh, sw = src.h.shape
    weight = 1.0 + 4.0 * keep
    target_cliff = span_cliff(skel.h).astype(np.float64)
    source_cliff = src.cliff.astype(np.float64)
    windows = np.lib.stride_tricks.sliding_window_view
    src_guide = windows(src.guide, (PATCH, PATCH))
    src_cliff = windows(source_cliff, (PATCH, PATCH))
    src_t = windows(field, (PATCH, PATCH))
    src_cls = windows(src.cls, (PATCH, PATCH))

    sy_map = np.zeros((th, tw), np.int32)
    sx_map = np.zeros((th, tw), np.int32)
    t_out = np.zeros((th, tw))
    cls_out = np.zeros((th, tw), np.int32)
    filled = np.zeros((th, tw), bool)
    used = np.zeros((sh, sw), np.int32)
    samples = range(0, PATCH, COARSE)
    ny = (sh - PATCH) // COARSE + 1
    nx = (sw - PATCH) // COARSE + 1
    near = np.arange(-COARSE // 2, COARSE // 2 + 1)

    xs, ys = positions(tw), positions(th)
    patch_no = 0
    for row, ty in enumerate(ys):
        for col, tx in enumerate(xs):
            # coarse: every fourth source offset, every fourth cell of the guide and cliff flag. A
            # source patch may be lifted or lowered as a whole (shift), at a price, so the skeleton
            # sets the level and the exemplar the shape.
            s1 = np.zeros((ny, nx))
            s2 = np.zeros((ny, nx))
            cliffs = np.zeros((ny, nx))
            total = 0.0
            for i in samples:
                for j in samples:
                    g = skel.guide[ty + i, tx + j]
                    c = target_cliff[ty + i, tx + j]
                    w = weight[ty + i, tx + j]
                    d = g - src.guide[i:i + COARSE * ny:COARSE, j:j + COARSE * nx:COARSE]
                    cs = source_cliff[i:i + COARSE * ny:COARSE, j:j + COARSE * nx:COARSE]
                    s1 += w * d
                    s2 += w * d * d
                    cliffs += w * (cs - c) ** 2
                    total += w
            shift = s1 / total
            cost = (s2 / total - shift ** 2) + SHIFT_WEIGHT * shift ** 2 + CLIFF_WEIGHT * cliffs / total
            order = np.argsort(cost, axis=None, kind="stable")[:REFINE]

            # full: every offset round the best coarse ones, every cell, the overlap with what is placed
            sy = (COARSE * (order // nx))[:, None, None] + near[None, :, None]
            sx = (COARSE * (order % nx))[:, None, None] + near[None, None, :]
            sy, sx = np.broadcast_arrays(sy, sx)
            pick = (sy >= 0) & (sx >= 0) & (sy <= sh - PATCH) & (sx <= sw - PATCH)
            flat = np.unique(sy[pick] * sw + sx[pick])
            sy, sx = flat // sw, flat % sw
            tg = skel.guide[ty:ty + PATCH, tx:tx + PATCH]
            tc = target_cliff[ty:ty + PATCH, tx:tx + PATCH]
            tw_ = weight[ty:ty + PATCH, tx:tx + PATCH]
            have = filled[ty:ty + PATCH, tx:tx + PATCH]
            shift = (tw_ * (tg - src_guide[sy, sx])).sum((1, 2)) / tw_.sum()
            lifted = src_guide[sy, sx] + shift[:, None, None]
            c = (tw_ * ((lifted - tg) ** 2 +
                        CLIFF_WEIGHT * (src_cliff[sy, sx] - tc) ** 2)).sum((1, 2)) / tw_.sum()
            c += SHIFT_WEIGHT * shift ** 2
            if have.any():
                c += OVERLAP_WEIGHT * (
                    ((src_t[sy, sx] - t_out[ty:ty + PATCH, tx:tx + PATCH]) ** 2 +
                     CLASS_WEIGHT * (src_cls[sy, sx] != cls_out[ty:ty + PATCH, tx:tx + PATCH]))
                    * have).sum((1, 2)) / have.sum()
            c += REUSE_WEIGHT * used[sy, sx]
            c *= 1.0 + JITTER * np.array([hash_unit(seed, patch_no, int(f)) for f in flat])
            best = int(np.argmin(c))
            sy, sx = int(sy[best]), int(sx[best])
            used[max(sy - 4, 0):sy + 5, max(sx - 4, 0):sx + 5] += 1
            window = (slice(sy, sy + PATCH), slice(sx, sx + PATCH))

            # the seam: a minimum cut through the left overlap and one through the top
            new = np.ones((PATCH, PATCH), bool)
            error = ((field[window] - t_out[ty:ty + PATCH, tx:tx + PATCH]) ** 2 +
                     CLASS_WEIGHT * (src.cls[window] != cls_out[ty:ty + PATCH, tx:tx + PATCH]))
            if col > 0:
                width = xs[col - 1] + PATCH - tx
                path = min_cut(error[:, :width])
                new &= np.arange(PATCH)[None, :] >= path[:, None]
            if row > 0:
                height = ys[row - 1] + PATCH - ty
                path = min_cut(error[:height, :].T)
                new &= np.arange(PATCH)[:, None] >= path[None, :]
            new |= ~have

            yy, xx = np.nonzero(new)
            sy_map[ty + yy, tx + xx] = sy + yy
            sx_map[ty + yy, tx + xx] = sx + xx
            t_out[ty + yy, tx + xx] = field[sy + yy, sx + xx]
            cls_out[ty + yy, tx + xx] = src.cls[sy + yy, sx + xx]
            filled[ty + yy, tx + xx] = True
            patch_no += 1
    return sy_map, sx_map, t_out, patch_no


#-----------------------------------------------------------------------------------------------
# Assembly
#-----------------------------------------------------------------------------------------------

def seams(sy_map, sx_map):
    th, tw = sy_map.shape
    dy = sy_map - np.arange(th)[:, None]
    dx = sx_map - np.arange(tw)[None, :]
    seam = np.zeros((th, tw), bool)
    seam[:, :-1] |= (dy[:, :-1] != dy[:, 1:]) | (dx[:, :-1] != dx[:, 1:])
    seam[:-1, :] |= (dy[:-1, :] != dy[1:, :]) | (dx[:-1, :] != dx[1:, :])
    return box(seam, SEAM_BAND) > 0


def level(h, y, x, radius):
    """Flatten a disc to its median, the way a site's footprint wants it."""
    yy, xx = np.ogrid[0:h.shape[0], 0:h.shape[1]]
    disc = (yy - y) ** 2 + (xx - x) ** 2 <= radius ** 2
    h[disc] = int(np.median(h[disc]))


def blend_chunk(src, sy_map, sx_map, cliff):
    """BlendTileData, version 8: every cell's class and blends from its source cell, the tiles
    worked out again for where the cell landed, the exemplar's texture table, a blend table of the
    blends that occur, no cliff texture mapping (Twilight's cells use none) and the cliff bits."""
    th, tw = sy_map.shape
    yy, xx = np.mgrid[0:th, 0:tw]
    cls = src.cls[sy_map, sx_map]
    layers = [tile_for(cls, src.first, src.width, yy, xx)]
    keys = []
    for layer in (1, 2):
        entry = src.layers[layer][sy_map, sx_map].astype(np.int64)
        over = src.blend_class[entry]
        tile = tile_for(over, src.first, src.width, yy, xx)
        keys.append(np.where(entry > 0, (tile * len(src.shapes) + src.blend_shape[entry]) + 1, 0))
    table, index = np.unique(np.concatenate([k.ravel() for k in keys]), return_inverse=True)
    if table[0] != 0:                           # entry 0, "no blend", is always there
        table = np.concatenate(([0], table))
        index = index + 1
    if len(table) > 16192:                      # NUM_BLEND_TILES
        raise ValueError("%d blends, the renderer holds 16192" % len(table))
    layers += [index[:th * tw].reshape(th, tw), index[th * tw:].reshape(th, tw)]
    layers.append(np.zeros((th, tw), np.int64))

    out = struct.pack("<i", th * tw) + b"".join(l.astype("<i2").tobytes() for l in layers)
    row = (tw + 7) // 8
    out += np.packbits(np.pad(cliff, ((0, 0), (0, row * 8 - tw))), axis=1, bitorder="little").tobytes()
    out += struct.pack("<3i", src.bitmap_tiles, len(table), 1) + src.table_head
    for key in table[1:]:
        tile, shape = divmod(int(key) - 1, len(src.shapes))
        flags, custom = src.shapes[shape]
        out += struct.pack("<i6sii", tile, flags, custom, 0x7ADA0000)
    return out


def bearing_of(centre, point):
    return math.atan2(point[0] - centre[0], point[1] - centre[1])


def place_on_bearing(h, ok, centre, angle, inner, outer):
    """The furthest cell out along a bearing, between two radii, where ok holds over a 5x5."""
    for r in np.arange(outer, inner, -1.0):
        y = int(centre[0] + r * math.sin(angle) + 0.5)
        x = int(centre[1] + r * math.cos(angle) + 0.5)
        if ok[y - 2:y + 3, x - 2:x + 3].all() and np.ptp(h[y - 2:y + 3, x - 2:x + 3]) <= 4:
            return y, x
    return None


def synthesise(skeleton_path, src, out_dir, seed):
    began = time.time()
    with open(skeleton_path, "rb") as handle:
        data = handle.read()
    skel = Grid(data)
    raw = RawMap(data)
    m = skel.m
    th, tw = skel.h.shape

    # the exemplar's detail: what its height has that its own blur does not
    keep, pads = protection(skel)
    sy_map, sx_map, detail, patches = quilt(src, skel, keep, seed, src.t - box(src.t, DETAIL_RADIUS))
    quilted = time.time()

    # heights: the skeleton's, with the detail on top, seams smoothed. The skeleton's cliffs take
    # none (an exemplar edge laid a cell or two off one doubles it) and the kept ground none either.
    detail = np.where(seams(sy_map, sx_map), box(detail, 1), detail)
    rim = np.clip(box(span_cliff(skel.h), RIM_CELLS) * 4.0, 0, 1)
    t = skel.t + (1.0 - rim) * detail
    h = skel.bowl + t * (skel.plateau - skel.bowl)
    h = keep * skel.h + (1.0 - keep) * h
    h = np.clip(np.rint(h), 0, 255).astype(np.int32)
    cliff = span_cliff(h)       # Twilight's painted bits are exactly its heights' (selfcheck)

    # the skeleton's money and seats stay, its scenery goes; the piles move
    _, objects = raw.body("ObjectsList")
    kept, piles = [], []
    for chunk in split_chunks(objects, 0, len(objects)):
        template = object_head(chunk[2])[3]
        if SKELETON_SCENERY.match(template):
            continue
        (piles if template == "SupplyPileSmall" else kept).append(chunk)

    passable = walkable(cliff)
    reached = reach(passable, skel.starts[0])
    centre = skel.centre
    seats = sorted(range(len(skel.starts)), key=lambda i: bearing_of(centre, skel.starts[i]))
    angles = [bearing_of(centre, skel.starts[i]) for i in seats]
    seat_ring = np.mean([math.hypot(y - centre[0], x - centre[1]) for y, x in skel.starts])
    outer = m.playable / 2.0 - 12.0
    spots = []
    for k in range(len(seats)):
        a = angles[k]
        b = angles[(k + 1) % len(seats)]
        gap = (b - a) % (2 * math.pi)
        for share in (0.5, 0.38 if k % 2 == 0 else 0.62):       # the mid pair, then the field pair
            # Twilight keeps its piles up on the high ground between two bases, with a camp
            high = passable & reached & (h > skel.plateau + 0.25 * (skel.plateau - skel.bowl))
            for ground in (high, passable & reached):
                for nudge in (0.0, 0.05, -0.05, 0.1, -0.1):
                    angle = a + (share + nudge) * gap
                    spot = place_on_bearing(h, ground, centre, angle, 0.6 * seat_ring, outer)
                    if spot:
                        break
                if spot:
                    spots.append((spot, angle))
                    break
    sites = []
    added = []
    for (y, x), angle in spots:
        ty, tx = math.cos(angle) * 4.0, -math.sin(angle) * 4.0
        sites.extend([(y + ty, x + tx), (y - ty, x - tx)])
        ry, rx = math.sin(angle) * 6.0, math.cos(angle) * 6.0
        added.append(("Tent03", x + rx + 0.5, y + ry + 0.5, angle + math.pi))
    for index, chunk in enumerate(piles):
        cid, version, body = chunk
        if index < len(sites):
            y, x = sites[index]
            body = struct.pack("<2f", (x - m.border) * MAP_XY_FACTOR,
                               (y - m.border) * MAP_XY_FACTOR) + body[8:]
            level(h, int(y), int(x), PILE_FLAT)
        kept.append((cid, version, body))

    # one refinery, at the nearest flat walkable cell to the middle clear of the derricks
    yy, xx = np.nonzero(passable & reached)
    order = np.argsort(np.hypot(yy - centre[0], xx - centre[1]), kind="stable")
    derricks = [(y + m.border, x + m.border) for x, y in m.derricks]
    refinery = None
    for i in order[:4000]:
        y, x = yy[i], xx[i]
        if passable[y - 6:y + 7, x - 6:x + 7].all() and \
                all(math.hypot(y - dy, x - dx) > 16 for dy, dx in derricks):
            refinery = (y, x)
            break
    if refinery:
        level(h, refinery[0], refinery[1], REFINERY_FLAT)
        added.append(("TechOilRefinery", refinery[1] + 0.5, refinery[0] + 0.5, 0.0))

    # the exemplar's scenery, wherever its cell landed off the pads and the kept ways down
    hit = np.zeros(src.h.shape, bool)
    for cell in src.scenery:
        hit[cell] = True
    landed = hit[sy_map, sx_map] & (pads < 0.9) & (keep < 0.95)
    landed[:m.border, :] = landed[-m.border:, :] = False
    landed[:, :m.border] = landed[:, -m.border:] = False
    taken = [(int(p[0]), int(p[1])) for p in sites] + ([refinery] if refinery else [])
    for y, x in zip(*np.nonzero(landed)):
        if any(abs(y - py) < 6 and abs(x - px) < 6 for py, px in taken):
            continue
        for template, fx, fy, angle in src.scenery[(sy_map[y, x], sx_map[y, x])]:
            added.append((template, x + fx, y + fy, angle))

    counter = [0]

    def neutral(template, cx, cy, angle):
        counter[0] += 1
        ids = raw.ids
        body = struct.pack("<4fi", (cx - m.border) * MAP_XY_FACTOR, (cy - m.border) * MAP_XY_FACTOR,
                           0.0, angle, 0)
        body += struct.pack("<H", len(template)) + template.encode("latin-1")
        entries = [("objectInitialHealth", 1, struct.pack("<i", 100)),
                   ("objectEnabled", 0, b"\1"), ("objectIndestructible", 0, b"\0"),
                   ("objectUnsellable", 0, b"\0"), ("objectPowered", 0, b"\1"),
                   ("objectRecruitableAI", 0, b"\1"), ("objectName", 3, b""),
                   ("originalOwner", 3, b"team"),
                   ("uniqueID", 3, ("%s synth%d" % (template, counter[0])).encode("latin-1")),
                   ("objectLayer", 3, b""), ("objectSelectable", 0, b"\1")]
        body += struct.pack("<H", len(entries))
        for key, kind, value in entries:
            body += struct.pack("<I", ids[key] << 8 | kind)
            body += struct.pack("<H", len(value)) + value if kind == 3 else value
        return (raw.ids["Object"], 3, body)

    kept.extend(neutral(*entry) for entry in added)
    raw.replace("ObjectsList", 3, join_chunks(kept))

    version, body = raw.body("HeightMapData")
    raw.replace("HeightMapData", version, body[:-th * tw] + h.astype(np.uint8).tobytes())
    cliff = span_cliff(h)
    raw.replace("BlendTileData", 8, blend_chunk(src, sy_map, sx_map, cliff))

    name = os.path.splitext(os.path.basename(skeleton_path))[0] + "_synth"
    out = os.path.join(out_dir, name + ".map")
    with open(out, "wb") as handle:
        handle.write(raw.bytes())
    finished = time.time()

    # every start still walks to every other one
    reached = reach(walkable(cliff), skel.starts[0])
    cut_off = sum(1 for y, x in skel.starts if not reached[y, x])
    print("%s: %d patches, quilt %.1f s, total %.1f s, %d scenery, piles %d/%d, refinery %s, "
          "starts cut off %d" % (name, patches, quilted - began, finished - began,
                                 len(added) - (1 if refinery else 0), min(len(piles), len(sites)),
                                 len(piles), "yes" if refinery else "no", cut_off))
    return out


#-----------------------------------------------------------------------------------------------
# Pictures
#-----------------------------------------------------------------------------------------------

def big_member(paths, member):
    wanted = member.lower()
    for path in paths:
        with open(path, "rb") as handle:
            for stored, offset, size in list(bigfile._read_index(handle)):
                if stored.lower() == wanted:
                    handle.seek(offset)
                    return handle.read(size)
    return None


_palette = {}


def texture_colour(name):
    """The average colour of a terrain class's texture, read off Terrain.ini and the art."""
    if not _palette:
        ini = big_member([os.path.join(RUN, "INIZH.big"), os.path.join(RUN, "ZH_Generals", "INI.big")],
                         "data\\ini\\terrain.ini").decode("latin-1")
        for terrain, texture in re.findall(r"^\s*Terrain\s+(\S+)\s.*?^\s*Texture\s*=\s*(\S+)",
                                           ini, re.M | re.S):
            _palette[terrain] = texture
    texture = _palette.get(name)
    if isinstance(texture, tuple):
        return texture
    colour = (128, 112, 90)
    if texture:
        art = big_member([os.path.join(RUN, "TerrainZH.big"), os.path.join(RUN, "ZH_Generals", "Terrain.big")],
                         "art\\terrain\\" + texture.lower())
        if art:
            import io
            colour = tuple(int(c) for c in np.asarray(Image.open(io.BytesIO(art)).convert("RGB"))
                           .reshape(-1, 3).mean(0))
    _palette[name] = colour
    return colour


def render(path):
    """Top-down picture of a map: each cell its texture's average colour, lit by the map's sun,
    starts white, docks blue, derricks black, piles yellow, refinery magenta, trees dark green."""
    with open(path, "rb") as handle:
        data = handle.read()
    m = mapstyle.StyleMap(data)
    h = np.frombuffer(bytes(m.heights), np.uint8).reshape(m.height, m.width).astype(np.float64)
    owner = np.zeros(4096, np.int32)
    for index, (name, first, count) in enumerate(m.texture_classes):
        owner[first:first + count] = index
    colours = np.array([texture_colour(name) for name, _, _ in m.texture_classes], np.float64)
    tiles = np.array(m.tiles, np.int32).reshape(m.height, m.width)
    rgb = colours[owner[tiles >> 2]]

    z = h * MAP_HEIGHT_SCALE
    gy, gx = np.gradient(z, MAP_XY_FACTOR)
    normal = np.stack([-gx, -gy, np.ones_like(z)])
    normal /= np.linalg.norm(normal, axis=0)
    light = m.lighting["terrain"]["direction"] if m.lighting else [-0.894, -0.19, -0.407]
    sun = -np.array(light)[:, None, None]
    shade = 0.45 + 0.75 * np.clip((normal * sun).sum(0) / np.linalg.norm(sun), 0, 1)
    rgb = np.clip(rgb * shade[..., None], 0, 255)

    b = m.border
    rgb = rgb[b:b + m.playable_y, b:b + m.playable_x]
    image = Image.fromarray(rgb.astype(np.uint8)[::-1].copy())
    draw = ImageDraw.Draw(image)
    top = m.playable_y - 1

    def dot(x, y, r, colour):
        draw.ellipse((x - r, top - y - r, x + r, top - y + r), fill=colour, outline=(0, 0, 0))

    for template, x, y in m.objects:
        if template.startswith("Tree"):
            draw.point((x, top - y), fill=(30, 60, 25))
        elif template.startswith("Tent") or template.startswith("Rock"):
            draw.point((x, top - y), fill=(70, 70, 70))
    for x, y in m.piles:
        dot(x, y, 2, (240, 210, 40))
    for x, y in m.supplies:
        dot(x, y, 3, (60, 120, 230))
    for x, y in m.derricks:
        dot(x, y, 3, (10, 10, 10))
    for template, x, y in m.objects:
        if template == "TechOilRefinery":
            dot(x, y, 4, (220, 40, 200))
    for x, y in m.starts:
        dot(x, y, 4, (255, 255, 255))
    return image


def side_by_side(out_path, paths, labels=None):
    images = [render(p) for p in paths]
    size = max(max(i.size) for i in images)
    sheet = Image.new("RGB", (len(images) * (size + 8) + 8, size + 30), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for k, image in enumerate(images):
        x = 8 + k * (size + 8)
        sheet.paste(image, (x, 24 + (size - image.size[1]) // 2))
        label = labels[k] if labels else os.path.basename(paths[k])
        draw.text((x, 6), label, fill=(230, 230, 230))
    sheet.save(out_path)


#-----------------------------------------------------------------------------------------------
# Measures
#-----------------------------------------------------------------------------------------------

def texture_distance(a, b):
    """Half the summed difference of the two maps' texture shares: 0 the same mix, 1 none shared."""
    shares = [{t["name"]: t["share"] for t in p["textures"]["classes"]} for p in (a, b)]
    return 0.5 * sum(abs(shares[0].get(n, 0) - shares[1].get(n, 0)) for n in set(shares[0]) | set(shares[1]))


def table_row(result, twilight):
    g = result["generator"]
    ground = result["ground"]
    objects = result["objects"]
    top = result["textures"]["classes"][:3]
    trees = sum(v for k, v in objects["trees"].items() if not k.startswith("Amb_"))
    tents = sum(v for k, v in objects["top_other"] if k.startswith("Tent"))
    return [
        ("cliff", "%.1f%%" % (100 * ground["cliff_share"])),
        ("mid cliff", "%.1f%%" % (100 * ground["middle_cliff_share"])),
        ("mid stdev", "%.1f" % ground["middle_height_stdev"]),
        ("mid step", "%.2f" % ground["middle_step_bytes"]),
        ("tex off Twilight", "%d%%" % round(100 * texture_distance(result, twilight))),
        ("top tex", " ".join("%s %d" % (t["name"], round(100 * t["share"])) for t in top)),
        ("money", " ".join("%s %d" % (k, v) for k, v in sorted(result["money"]["classes"].items()))),
        ("trees", "%d (1/%s)" % (trees, g["cells_per_tree"])),
        ("tents/ref", "%d/%d" % (tents, objects["tech"].get("TechOilRefinery", 0))),
        ("raise", "%s" % g["start_raise_bytes"]),
        ("ramps", "%s (%s down, %s wide)" % (g["ramps_per_base"], g["ramps_down_per_base"],
                                             g["ramp_down_width_cells"])),
        ("plateau r", "%s" % g["base_plateau_radius_cells"]),
    ]


def table(profiles):
    rows = [table_row(p, profiles[0][1]) for _, p in profiles]
    names = [name for name, _ in profiles]
    print("| measure | " + " | ".join(names) + " |")
    print("|---" * (len(names) + 1) + "|")
    for k in range(len(rows[0])):
        print("| %s | %s |" % (rows[0][k][0], " | ".join(r[k][1] for r in rows)))


#-----------------------------------------------------------------------------------------------
# The wave function collapse probe
#-----------------------------------------------------------------------------------------------

def band_of(t):
    return np.clip(np.floor(t * 2.0 + 0.5) + 1, 0, 5).astype(np.int32)


def wfc_probe(skeleton_path, src, crop=48, tries=5):
    """Overlapping WFC, 3x3, over the exemplar's height band and texture group, constrained to the
    skeleton's band: how many patterns, and whether a crop solves."""
    b = src.m.border
    play = (slice(b, b + src.m.playable_y), slice(b, b + src.m.playable_x))
    band = band_of(src.t)[play]
    group_of = np.array([0 if n.startswith(("Cliff", "Rocks")) else 1 if n.startswith("Grass")
                         else 2 if n.startswith("Dirt") else 3 for n in src.class_names])
    groups = group_of[src.cls][play]
    for label, symbols in (("band x 21 classes", band * 64 + src.cls[play]),
                           ("band x 4 groups", band * 4 + groups), ("band only", band)):
        windows = np.lib.stride_tricks.sliding_window_view(symbols, (3, 3)).reshape(-1, 9)
        patterns, counts = np.unique(windows, axis=0, return_counts=True)
        print("%-18s %6d distinct 3x3 patterns" % (label, len(patterns)))

    with open(skeleton_path, "rb") as handle:
        skel = Grid(handle.read())
    seen = set(map(bytes, np.lib.stride_tricks.sliding_window_view(band, (3, 3)).reshape(-1, 9)
                   .astype(np.uint8)))
    b = skel.m.border
    wanted = np.lib.stride_tricks.sliding_window_view(band_of(skel.t)[b:-b, b:-b], (3, 3)).reshape(-1, 9)
    unseen = np.mean([bytes(row) not in seen for row in wanted.astype(np.uint8)])
    print("skeleton cells whose 3x3 band pattern the exemplar never shows: %.1f%%" % (100 * unseen))

    windows = np.lib.stride_tricks.sliding_window_view(band * 4 + groups, (3, 3)).reshape(-1, 9)
    patterns, counts = np.unique(windows, axis=0, return_counts=True)
    p = patterns.reshape(-1, 3, 3)
    n = len(p)
    if n > 8000:
        print("%d patterns: the adjacency alone is %.0f MB, no solve tried" % (n, 4 * n * n / 1e6))
        return
    compat = {}
    for d, (dy, dx) in enumerate(((0, 1), (1, 0), (0, -1), (-1, 0))):
        a = p[:, max(dy, 0):3 + min(dy, 0), max(dx, 0):3 + min(dx, 0)].reshape(n, -1)
        c = p[:, max(-dy, 0):3 + min(-dy, 0), max(-dx, 0):3 + min(-dx, 0)].reshape(n, -1)
        compat[(dy, dx)] = (a[:, None, :] == c[None, :, :]).all(2)
    print("adjacency: %d x %d x 4 = %.0f MB as bytes" % (n, n, 4 * n * n / 1e6))

    cy, cx = int(skel.centre[0]), int(skel.starts[0][1] + skel.centre[1]) // 2
    goal = band_of(skel.t)[cy - crop // 2:cy + crop // 2, cx - crop // 2:cx + crop // 2]
    centre_band = p[:, 1, 1] // 4
    for attempt in range(tries):
        began = time.time()
        wave = centre_band[None, None, :] == goal[:, :, None]
        queue = deque((y, x) for y in range(crop) for x in range(crop))
        ok = propagate(wave, compat, queue)
        step = 0
        while ok:
            sizes = wave.sum(2)
            open_ = sizes > 1
            if not open_.any():
                break
            noise = np.array([[hash_unit(attempt, y * crop + x, step) for x in range(crop)]
                              for y in range(crop)])
            y, x = np.unravel_index(np.argmin(np.where(open_, sizes + noise, np.inf)), sizes.shape)
            allowed = np.nonzero(wave[y, x])[0]
            weights = counts[allowed].cumsum()
            pick = allowed[np.searchsorted(weights, hash_unit(attempt, step, 7) * weights[-1])]
            wave[y, x] = False
            wave[y, x, pick] = True
            ok = propagate(wave, compat, deque([(y, x)]))
            step += 1
        print("crop %dx%d try %d: %s after %d collapses, %.1f s" % (
            crop, crop, attempt, "solved" if ok else "contradiction", step, time.time() - began))


def propagate(wave, compat, queue):
    size = wave.shape[0]
    while queue:
        y, x = queue.popleft()
        here = wave[y, x]
        if not here.any():
            return False
        for (dy, dx), table_ in compat.items():
            ny, nx = y + dy, x + dx
            if 0 <= ny < size and 0 <= nx < size:
                support = table_[here].any(0)
                narrowed = wave[ny, nx] & support
                if (narrowed != wave[ny, nx]).any():
                    if not narrowed.any():
                        return False
                    wave[ny, nx] = narrowed
                    queue.append((ny, nx))
    return True


#-----------------------------------------------------------------------------------------------
# Exemplar size, for the port
#-----------------------------------------------------------------------------------------------

def exemplar_size(src):
    """What a port has to bake in, over the whole grid (the search reads the border too): the
    heights, the class a cell, each blend layer as its overlay class and shape, the scenery. The
    tiles and the cliff bits follow from those."""
    templates = sorted(set(o[0] for v in src.scenery.values() for o in v))
    scenery = b"".join(struct.pack("<BHHBBB", templates.index(t), x, y, int(fx * 255), int(fy * 255),
                                   int(angle % (2 * math.pi) / (2 * math.pi) * 255))
                       for (y, x), v in sorted(src.scenery.items()) for t, fx, fy, angle in v)
    parts = {
        "heights, byte a cell": src.h.astype(np.uint8).tobytes(),
        "texture class, byte a cell": src.cls.astype(np.uint8).tobytes(),
        "blend layers, class+shape, 2x2 bytes": b"".join(
            (src.blend_class[layer] * 32 + src.blend_shape[layer]).astype("<u2").tobytes()
            for layer in src.layers[1:3]),
        "scenery, 7 bytes each + names": scenery + "\0".join(templates).encode("latin-1"),
    }
    total = [0, 0]
    for name, blob in parts.items():
        packed = len(zlib.compress(blob, 9))
        total = [total[0] + len(blob), total[1] + packed]
        print("%-38s %8d raw %8d zlib" % (name, len(blob), packed))
    print("%-38s %8d raw %8d zlib" % ("exemplar", total[0], total[1]))


#-----------------------------------------------------------------------------------------------
# Self check and CLI
#-----------------------------------------------------------------------------------------------

def selfcheck():
    error = np.ones((6, 5))
    error[:, 3] = 0.0
    assert list(min_cut(error)) == [3] * 6
    error[2:, 3] = 1.0
    error[2:, 4] = 0.0
    assert list(min_cut(error)) == [3, 3, 4, 4, 4, 4], min_cut(error)
    assert np.allclose(box(np.full((7, 9), 5.0), 2), 5.0)
    assert positions(483)[-1] == 483 - PATCH and positions(483)[1] == PATCH - OVERLAP
    assert hash_cell(1, 2, 3) == hash_cell(1, 2, 3) != hash_cell(1, 2, 4)
    step = np.zeros((6, 6), np.int32)
    step[:, 3:] = 40
    assert span_cliff(step)[:5, 2].all() and not span_cliff(step)[:5, 0].any()

    # what the synthesis leans on: the exemplar's cliff bits are its heights', and its tiles and
    # blend tiles are a function of class and cell
    if os.path.exists(SOURCE_BIG):
        src = load_exemplar(SOURCE_BIG, SOURCE_MAP)
        b = src.m.border
        play = (slice(b, b + src.m.playable_y), slice(b, b + src.m.playable_x))
        assert (src.cliff[play] == span_cliff(src.h)[play]).all()     # the border is not painted
        yy, xx = np.mgrid[0:src.h.shape[0], 0:src.h.shape[1]]
        assert (tile_for(src.cls, src.first, src.width, yy, xx) == src.layers[0]).all()
        tiles = np.array([tile for tile, _, _ in texture_table(src.tail)[2]])
        for layer in src.layers[1:3]:
            blended = layer > 0
            over = tile_for(src.blend_class[layer], src.first, src.width, yy, xx)
            assert (over[blended] == tiles[layer][blended]).all()
    print("synthmap selfcheck OK")


def main():
    if sys.argv[1:] == ["selfcheck"]:
        selfcheck()
        return 0
    if sys.argv[1:2] == ["table"]:
        with open(sys.argv[2]) as handle:
            profiles = [("Twilight", json.load(handle))]
        for path in sys.argv[3:]:
            with open(path, "rb") as handle:
                profiles.append((os.path.basename(path)[4:-4], mapstyle.profile(handle.read(), path)))
        table(profiles)
        return 0
    if sys.argv[1:2] == ["render"]:
        side_by_side(sys.argv[2], sys.argv[3:])
        return 0
    if sys.argv[1:2] == ["wfcprobe"]:
        src = load_exemplar(SOURCE_BIG, SOURCE_MAP)
        wfc_probe(sys.argv[2], src)
        exemplar_size(src)
        return 0

    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("skeletons", nargs="+", help="generated .map files (RMG_DUMP)")
    parser.add_argument("-o", "--out", default=".", help="directory for the .map and .png")
    parser.add_argument("--source", default=SOURCE_BIG, help="the .big holding the exemplar")
    parser.add_argument("--inner", default=SOURCE_MAP, help="the exemplar's path inside it")
    parser.add_argument("--seed", type=int, help="the jitter's seed (default: from the file name)")
    arguments = parser.parse_args()

    src = load_exemplar(arguments.source, arguments.inner)
    for path in arguments.skeletons:
        found = re.search(r"rmg_(\d+)_", os.path.basename(path))
        seed = arguments.seed if arguments.seed is not None else int(found.group(1)) if found else 0
        out = synthesise(path, src, arguments.out, seed)
        render(out).save(out[:-4] + ".png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
