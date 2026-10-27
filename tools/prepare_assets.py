"""Converts the designer's asset package (PEDAL folder) into the files embedded in the plugin.
Usage: python3 tools/prepare_assets.py <PEDAL folder> <assets out dir>"""
import sys, os
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
p = lambda n: os.path.join(src, n)

# Opaque panel -> high quality JPEG (6 MB PNG -> well under 1 MB)
Image.open(p('panel@2x.png')).convert('RGB').save(os.path.join(out, 'panel.jpg'), quality=92, optimize=True, subsampling=0)

# Main knob: single upright image, rotated in code for smooth motion
Image.open(p('knob@2x.png')).convert('RGBA').save(os.path.join(out, 'knob.png'), optimize=True)

# Trim knob: frame 0 of the strip sits at -135 degrees; rotate it upright
t = Image.open(p('trim-64@2x.png')).convert('RGBA')
w = t.width
f0 = t.crop((0, 0, w, w))
f0.rotate(-135, resample=Image.BICUBIC).save(os.path.join(out, 'trim.png'), optimize=True)

# Bandwidth selector: 5 discrete frames, narrow (left) to wide (right)
Image.open(p('bandwidth-5@2x.png')).convert('RGBA').save(os.path.join(out, 'bandwidth.png'), optimize=True)

for n in ['footswitch-pressed', 'footswitch-released', 'led-green-on', 'led-green-off', 'led-blue-on', 'led-blue-off']:
    Image.open(p(n + '@2x.png')).convert('RGBA').save(os.path.join(out, n.replace('-', '_') + '.png'), optimize=True)
