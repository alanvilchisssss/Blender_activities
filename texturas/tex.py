import zlib, struct

def crear_png(nombre, r, g, b, w=64, h=64):
    # Crea una imagen PNG sólida de 64x64 píxeles sin librerías externas
    raw_data = b"".join(b"\x00" + bytes([r, g, b]) * w for _ in range(h))
    
    def chunk(tag, data):
        return (struct.pack("!I", len(data)) + tag + data +
                struct.pack("!I", zlib.crc32(tag + data) & 0xffffffff))
    
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack("!2I5B", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw_data)) +
           chunk(b"IEND", b""))
    
    with open(nombre, "wb") as f:
        f.write(png)
    print(f"Creada: {nombre}")

texturas = {
    "tex_glass.png": (38, 51, 64),         # Azul oscuro cristal
    "tex_metal.png": (192, 192, 200),      # Plata / Metal
    "tex_metal_blue.png": (25, 90, 215),   # Azul metálico
    "tex_metal_red.png": (204, 25, 25),    # Rojo metálico
    "tex_purple_car.png": (115, 25, 190),  # Morado carrocería
    "tex_purple_lamp.png": (165, 65, 240), # Morado brillante (faros)
    "tex_red_lamp.png": (230, 15, 15),     # Rojo brillante (calaveras)
    "tex_tire.png": (30, 30, 30),          # Gris muy oscuro / Negro caucho
    "tex_yellow_car.png": (242, 190, 12)   # Amarillo deportivo
}

for archivo, (r, g, b) in texturas.items():
    crear_png(archivo, r, g, b)