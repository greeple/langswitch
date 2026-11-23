#!/usr/bin/env python3
"""Генерация app.ico / app_paused.ico — две стрелки обмена на скруглённом фоне.
Рисуется векторно (полигоны), поэтому одинаково чётко в размерах 16..256."""
from PIL import Image, ImageDraw
import sys, os

def rounded(size, radius, fill):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=fill)
    return img

def make(size, bg1, bg2, arrow):
    S = 256  # рисуем крупно, потом уменьшаем (сглаживание)
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # фон — скруглённый квадрат с вертикальным градиентом
    for y in range(S):
        t = y / (S - 1)
        c = tuple(int(bg1[i] + (bg2[i] - bg1[i]) * t) for i in range(3)) + (255,)
        d.line([(0, y), (S, y)], fill=c)
    # маска скругления
    mask = Image.new("L", (S, S), 0)
    dm = ImageDraw.Draw(mask)
    dm.rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * 0.22), fill=255)
    img.putalpha(mask)

    d = ImageDraw.Draw(img)
    # две стрелки обмена: верхняя вправо, нижняя влево
    cx = S / 2
    bw = int(S * 0.10)          # толщина «тела» стрелки
    x0 = int(S * 0.20)          # левый край
    x1 = int(S * 0.80)          # правый край
    head = int(S * 0.16)        # выступ наконечника
    gap = int(S * 0.07)         # зазор между стрелками по вертикали

    ytop = int(S * 0.30)
    ybot = int(S * 0.70)

    # верхняя стрелка (вправо)
    pts = [
        (x0, ytop - bw // 2),
        (x1 - head, ytop - bw // 2),
        (x1 - head, ytop - bw - gap // 2),
        (x1 + head // 4, ytop),
        (x1 - head, ytop + bw + gap // 2),
        (x1 - head, ytop + bw // 2),
        (x0, ytop + bw // 2),
    ]
    d.polygon(pts, fill=arrow)

    # нижняя стрелка (влево)
    pts = [
        (x1, ybot + bw // 2),
        (x0 + head, ybot + bw // 2),
        (x0 + head, ybot + bw + gap // 2),
        (x0 - head // 4, ybot),
        (x0 + head, ybot - bw - gap // 2),
        (x0 + head, ybot - bw // 2),
        (x1, ybot - bw // 2),
    ]
    d.polygon(pts, fill=arrow)

    return img.resize((size, size), Image.LANCZOS)

def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "."
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]

    active = [make(s, (0, 120, 215), (0, 90, 170), (255, 255, 255, 255)) for s in sizes]
    paused = [make(s, (150, 150, 150), (110, 110, 110), (235, 235, 235, 255)) for s in sizes]

    # база — самый крупный кадр, Pillow сам уменьшит его до всех размеров
    active[-1].save(os.path.join(outdir, "app.ico"), format="ICO",
                    sizes=[(s, s) for s in sizes])
    paused[-1].save(os.path.join(outdir, "app_paused.ico"), format="ICO",
                    sizes=[(s, s) for s in sizes])
    print("OK")

if __name__ == "__main__":
    main()
