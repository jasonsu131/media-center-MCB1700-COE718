# img2c.py: turns a picture into an rgb565 C header for GLCD_Bitmap
# usage: python img2c.py photo.jpg pic_name [width height]
# (does the same job as the GIMP steps in the project handout:
#  scale, flip vertically, save as rgb565)
# needs pillow: pip install pillow
import sys
from PIL import Image

src, name = sys.argv[1], sys.argv[2]
w = int(sys.argv[3]) if len(sys.argv) > 3 else 200
h = int(sys.argv[4]) if len(sys.argv) > 4 else 150

img = Image.open(src).convert("RGB")
img.thumbnail((w, h))                       # keep the aspect ratio
w, h = img.size
img = img.transpose(Image.FLIP_TOP_BOTTOM)  # GLCD_Bitmap draws the last row first

px = []
for r, g, b in img.getdata():
    px.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))

up = name.upper()
with open(name + ".h", "w") as f:
    f.write("// %s.h: made by img2c.py from %s\n" % (name, src))
    f.write("// %d x %d, rgb565, flipped vertically for GLCD_Bitmap\n\n" % (w, h))
    f.write("#define %s_W  %d\n#define %s_H  %d\n\n" % (up, w, up, h))
    f.write("static const unsigned short %s[%d] = {\n" % (name, w * h))
    for i in range(0, len(px), 12):
        f.write("    " + ", ".join("0x%04X" % v for v in px[i:i + 12]) + ",\n")
    f.write("};\n")
print("wrote %s.h (%d x %d, %d bytes)" % (name, w, h, w * h * 2))
