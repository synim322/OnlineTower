"""Original periodic PBR texture synthesis. Requires NumPy; no downloaded inputs."""
from pathlib import Path
import json
import struct
import zlib
import numpy as np

ROOT = Path(__file__).resolve().parent
SIZE = 1024
Y, X = np.mgrid[0:SIZE, 0:SIZE].astype(np.float32) / SIZE


def png(path, array):
    a = np.uint8(np.clip(array, 0, 1) * 255 + .5)
    if a.ndim == 2:
        a = np.repeat(a[..., None], 3, axis=2)
    h, w, _ = a.shape
    raw = b''.join(b'\0' + row.tobytes() for row in a)
    def chunk(tag, data):
        return struct.pack('!I', len(data)) + tag + data + struct.pack('!I', zlib.crc32(tag + data))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', w, h, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def noise(nx, ny=None, seed=0, u=X, v=Y):
    ny = ny or nx
    grid = np.random.default_rng(seed).random((ny, nx)).astype(np.float32)
    px, py = (u % 1) * nx, (v % 1) * ny
    ix, iy = np.floor(px).astype(int), np.floor(py).astype(int)
    fx, fy = px - ix, py - iy
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a = grid[iy % ny, ix % nx] * (1 - fx) + grid[iy % ny, (ix + 1) % nx] * fx
    b = grid[(iy + 1) % ny, ix % nx] * (1 - fx) + grid[(iy + 1) % ny, (ix + 1) % nx] * fx
    return a * (1 - fy) + b * fy


def fbm(seed):
    return sum(noise(n, seed=seed + n) * weight for n, weight in [(4, .4), (16, .3), (64, .2), (256, .1)])


def cells(count, seed):
    rng = np.random.default_rng(seed)
    jitter = rng.random((count, count, 2)).astype(np.float32)
    colors = rng.random((count, count)).astype(np.float32)
    ux, uy = X * count, Y * count
    gx, gy = ux.astype(int), uy.astype(int)
    first = np.full_like(X, 100)
    second = np.full_like(X, 100)
    label = np.zeros_like(X)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            cx, cy = (gx + dx) % count, (gy + dy) % count
            distance = ((gx + dx + jitter[cy, cx, 0] - ux) ** 2
                        + (gy + dy + jitter[cy, cx, 1] - uy) ** 2)
            closer = distance < first
            second = np.where(closer, first, np.minimum(second, distance))
            first = np.minimum(first, distance)
            label = np.where(closer, colors[cy, cx], label)
    return np.sqrt(first), np.sqrt(second) - np.sqrt(first), label


def tint(base, variation):
    return np.clip(np.array(base)[None, None, :] * variation[..., None], 0, 1)


def normal(height, strength):
    # DirectX tangent-space normal. UE imports this without green-channel flip.
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * strength
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * strength
    vector = np.stack((-dx, dy, np.ones_like(dx)), axis=-1)
    vector /= np.sqrt((vector * vector).sum(axis=-1))[..., None]
    return vector * .5 + .5


def build():
    fine = noise(384, seed=32)
    broad = fbm(10)
    _, edges, regions = cells(7, 80)
    crack = np.exp(-edges * 100)
    layers = .5 + .5 * np.sin(2 * np.pi * (Y * 14 + noise(8, seed=31) * .5))
    rock_h = broad * .6 + layers * .12 - crack * .25 + fine * .07
    rock = tint((.48, .52, .55), .62 + broad * .6 + regions * .18 - crack * .4)
    sandstone_h = broad * .35 + layers * .22 - crack * .12 + fine * .04
    sandstone = tint((.67, .49, .32), .65 + broad * .35 + layers * .2 - crack * .2)
    ripples = .5 + .5 * np.sin(2 * np.pi * (Y * 20 + noise(6, seed=27) * .32))
    sand_h = ripples * .08 + fine * .03 + broad * .06
    sand = tint((.79, .64, .42), .83 + ripples * .07 + fine * .1 + broad * .08)
    distance, _, chips = cells(64, 71)
    chip_mask = distance < (.18 + chips * .13)
    chip_color = np.stack((.38 + chips * .52, .40 + chips * .46, .41 + chips * .39), axis=-1)
    terrazzo = np.where(chip_mask[..., None], chip_color,
                         tint((.72, .74, .73), .96 + fine * .06))
    terrazzo_h = .5 + fine * .004 - chip_mask * .002
    wood_grain = noise(128, 4, seed=42, u=X + noise(4, seed=44) * .04)
    wood_lines = .5 + .5 * np.sin(2 * np.pi * (X * 45 + noise(5, 2, seed=99) * 2))
    wood_h = wood_grain * .06 + wood_lines * .018
    wood = tint((.61, .40, .23), .6 + wood_grain * .36 + wood_lines * .22)
    brush = noise(8, 512, seed=60)
    metal = tint((.27, .34, .39), .84 + brush * .13 + broad * .09)
    metal_h = brush * .006
    leaves, _, leaf_id = cells(36, 23)
    leaf = np.clip(1 - leaves * 1.8, 0, 1)
    shrub_h = leaf * .16 + fine * .015
    shrub = tint((.38, .43, .22), .48 + leaf * .58 + leaf_id * .22)
    sets = {
        'Rock': (rock, rock_h, .83 + fine * .12, 5),
        'Sandstone': (sandstone, sandstone_h, .84 + fine * .12, 5),
        'Sand': (sand, sand_h, .91 + fine * .08, 4),
        'Terrazzo': (terrazzo, terrazzo_h, .38 + fine * .08, 3),
        'Wood': (wood, wood_h, .53 + wood_grain * .13, 4),
        'Metal': (metal, metal_h, .42 + brush * .12, 2),
        'Shrub': (shrub, shrub_h, .82 + fine * .14, 5),
    }
    thumbs = []
    report = {}
    for name, (color, height, rough, strength) in sets.items():
        norm = normal(height, strength)
        png(ROOT / name / 'BaseColor.png', color)
        png(ROOT / name / 'Normal.png', norm)
        png(ROOT / name / 'Roughness.png', rough)
        thumbs.append(np.concatenate([color[::4, ::4], norm[::4, ::4],
                                      np.repeat(rough[::4, ::4, None], 3, axis=2)], axis=1))
        report[name] = {'size': SIZE, 'seeded': True, 'periodic': True,
                        'basecolor_range': [float(color.min()), float(color.max())],
                        'roughness_range': [float(rough.min()), float(rough.max())]}
    png(ROOT / 'Preview.png', np.concatenate(thumbs, axis=0))
    (ROOT / 'generation_report.json').write_text(json.dumps(report, indent=2))
    print('Created 21 PNG maps at 1024x1024 and Preview.png')


if __name__ == '__main__':
    build()
