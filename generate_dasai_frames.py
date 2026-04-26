import os
from PIL import Image

folder = r'E:\Espressif\frameworks\esp-idf-v5.4.3\examples\ai-xiaozhi-esp32-c3-super-mini\imoji-dasai-image'
files = sorted([f for f in os.listdir(folder) if f.endswith('.jpg')])

header_path = 'main/display/dasai_frames.h'
cpp_path = 'main/display/dasai_frames.cc'

with open(header_path, 'w') as h:
    h.write('#pragma once\n')
    h.write('#include "lvgl.h"\n\n')
    h.write('#define DASAI_FRAME_COUNT %d\n\n' % len(files))
    h.write('extern const lv_image_dsc_t* dasai_frames_dsc[%d];\n' % len(files))

with open(cpp_path, 'w') as c:
    c.write('#include "dasai_frames.h"\n\n')
    for i, file in enumerate(files):
        img_path = os.path.join(folder, file)
        img = Image.open(img_path).convert('1') # Convert to 1bpp (black and white)
        
        # Resize to 128x64 if necessary, or just keep original size 
        # (Assuming it's already 128x64 or fits on screen)
        # We will resize just in case
        if img.size != (128, 64):
            img = img.resize((128, 64))
            
        w, h = img.size
        
        # Convert to 1bpp array (8 pixels per byte) for LV_COLOR_FORMAT_I1
        # In LVGL, 1bpp means each bit is an index into a 2-color palette.
        # But for OLED it's usually rendered well if we just format it as I1.
        data = []
        pixels = list(img.getdata())
        for row in range(h):
            for col in range(0, w, 8):
                byte = 0
                for b in range(8):
                    if col + b < w:
                        # Đảo ngược màu: Nét vẽ đen -> Trắng trên OLED (px=1), nền trắng -> Đen (px=0)
                        px = 0 if pixels[row * w + col + b] > 128 else 1
                        byte |= (px << (7 - b))
                data.append(byte)
                
        # Write array
        c.write(f'const uint8_t dasai_frame_{i}_map[] = {{\n')
        # We need to prepend the 2-color palette (8 bytes for ARGB8888 * 2)
        # Color 0: Black (0x00, 0x00, 0x00, 0xFF)
        # Color 1: White (0xFF, 0xFF, 0xFF, 0xFF)
        palette = "0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,\n"
        c.write(f'    {palette}')
        c.write(','.join(hex(b) for b in data))
        c.write('};\n\n')
        
        c.write(f'const lv_image_dsc_t dasai_frame_{i}_dsc = {{\n')
        c.write(f'    .header = {{\n')
        c.write(f'        .magic = LV_IMAGE_HEADER_MAGIC,\n')
        c.write(f'        .cf = LV_COLOR_FORMAT_I1,\n')
        c.write(f'        .flags = 0,\n')
        c.write(f'        .w = {w},\n')
        c.write(f'        .h = {h},\n')
        c.write(f'        .stride = {w // 8},\n')
        c.write(f'    }},\n')
        c.write(f'    .data_size = sizeof(dasai_frame_{i}_map),\n')
        c.write(f'    .data = dasai_frame_{i}_map,\n')
        c.write(f'}};\n\n')
        
    c.write('\nconst lv_image_dsc_t* dasai_frames_dsc[] = {\n')
    for i in range(len(files)):
        c.write(f'    &dasai_frame_{i}_dsc,\n')
    c.write('};\n\n')

print("Generated dasai_frames.h and dasai_frames.cc successfully.")
