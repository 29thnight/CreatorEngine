"""Authoring-only EXR decode; runtime distributes only the .ceibl cook."""
from array import array
from pathlib import Path
import struct
import sys
import bpy

source, destination = map(Path, sys.argv[sys.argv.index('--') + 1:])
image = bpy.data.images.load(str(source.resolve()), check_existing=False)
if image.colorspace_settings.name != 'Linear Rec.709':
    image.colorspace_settings.name = 'Linear Rec.709'
width, height = image.size
pixels = array('f', [0]) * (width * height * 4)
image.pixels.foreach_get(pixels)
destination.parent.mkdir(parents=True, exist_ok=True)
with destination.open('wb') as output:
    output.write(struct.pack('<II', width, height))
    # bpy is bottom-up; GPU texture upload rows are top-down.
    for row in reversed(range(height)):
        output.write(pixels[row * width * 4:(row + 1) * width * 4].tobytes())
print(f'ENVIRONMENT_EXR_DECODE_OK width={width} height={height} output={destination}')
