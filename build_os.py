# build_os.py — Build and launch your bootable OS with conditional rebuilds

import os
import subprocess
from PIL import Image

# ---------- CONFIG ----------
KERNEL_CPP = "kernel.cpp"
BOOTLOADER_ASM = "boot_img.asm"
BOOT_IMAGE_PNG = "boot.png"
OUTPUT_IMG = "os.img"
KERNEL_BIN = "kernel.bin"
BOOT_BIN = "boot_img.bin"
RAW_BOOT_IMAGE = "boot.img"
QEMU_CMD = ["qemu-system-i386", "-drive", f"format=raw,file={OUTPUT_IMG}"]

# ---------- HELPER FUNCTIONS ----------

def needs_rebuild(source, target):
    return not os.path.exists(target) or os.path.getmtime(source) > os.path.getmtime(target)

def compile_kernel():
    if needs_rebuild(KERNEL_CPP, KERNEL_BIN):
        print("[*] Compiling kernel...")
        subprocess.run([
            "i686-elf-g++", "-ffreestanding", "-O2", "-c", KERNEL_CPP, "-o", "kernel.o"
        ], check=True)
        subprocess.run([
            "i686-elf-ld", "-Ttext", "0x8000", "-o", KERNEL_BIN, "--oformat", "binary", "kernel.o"
        ], check=True)
        print("[+] Kernel compiled.")
    else:
        print("[*] Kernel up-to-date, skipping compilation.")

def assemble_bootloader():
    if needs_rebuild(BOOTLOADER_ASM, BOOT_BIN):
        print("[*] Assembling bootloader...")
        subprocess.run([
            "nasm", "-f", "bin", BOOTLOADER_ASM, "-o", BOOT_BIN
        ], check=True)
        print("[+] Bootloader assembled.")
    else:
        print("[*] Bootloader up-to-date, skipping assembly.")

def convert_image_to_raw():
    if needs_rebuild(BOOT_IMAGE_PNG, RAW_BOOT_IMAGE):
        print("[*] Converting image to raw 320x200 256-color format...")
        img = Image.open(BOOT_IMAGE_PNG)
        img = img.convert("P")  # 8-bit palette
        img = img.resize((320, 200))
        data = img.tobytes()
        with open(RAW_BOOT_IMAGE, "wb") as f:
            f.write(data)
        print("[+] Image converted.")
    else:
        print("[*] Boot image up-to-date, skipping conversion.")

def build_image():
    print("[*] Building final OS image...")
    with open(OUTPUT_IMG, "wb") as out:
        for f in [BOOT_BIN, RAW_BOOT_IMAGE, KERNEL_BIN]:
            with open(f, "rb") as part:
                out.write(part.read())
    print(f"[+] OS image built: {OUTPUT_IMG}")

def run_qemu():
    print("[*] Running OS in QEMU...")
    subprocess.run(QEMU_CMD)

# ---------- MAIN ----------
if __name__ == "__main__":
    compile_kernel()
    assemble_bootloader()
    convert_image_to_raw()
    build_image()
    run_qemu()
