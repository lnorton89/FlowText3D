# Generates flowtext3d.ico: a flowing gradient tile with an extruded "3D" glyph.
from PIL import Image, ImageDraw, ImageFilter, ImageFont
import math, sys
S = 256
img = Image.new("RGBA", (S, S))
px = img.load()
cols = [(47,107,255), (134,182,255), (163,92,255), (24,56,176)]
def pal(t):
    t = (t % 1.0) * 4; i = int(t); f = t - i; f = f*f*(3-2*f)
    a, b = cols[i % 4], cols[(i+1) % 4]
    return tuple(int(a[k] + (b[k]-a[k])*f) for k in range(3))
for y in range(S):
    for x in range(S):
        u, v = x/S, y/S
        w = math.sin(u*3.1 + math.sin(v*4.0)*1.2) * 0.5 + math.sin(v*2.3 - u*1.7) * 0.5
        px[x, y] = pal(0.15 + w*0.35 + u*0.2) + (255,)
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle((0, 0, S-1, S-1), 52, fill=255)
img.putalpha(mask)
font = None
for f in ["/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "DejaVuSans-Bold.ttf"]:
    try: font = ImageFont.truetype(f, 150); break
    except OSError: pass
font = font or ImageFont.load_default()
txt = Image.new("RGBA", (S, S), (0,0,0,0)); d = ImageDraw.Draw(txt)
for i in range(14, 0, -1):  # extrusion
    c = int(40 + i*4)
    d.text((S/2 + i*0.9, S/2 + i*1.1), "3D", font=font, anchor="mm", fill=(c, c, c+30, 255))
d.text((S/2, S/2), "3D", font=font, anchor="mm", fill=(250, 250, 255, 255))
shadow = txt.split()[3].filter(ImageFilter.GaussianBlur(8))
sh = Image.new("RGBA", (S, S), (0,0,20,140)); sh.putalpha(shadow.point(lambda a: a*0.5))
img = Image.alpha_composite(img, sh); img = Image.alpha_composite(img, txt)
img.save(sys.argv[1], sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
