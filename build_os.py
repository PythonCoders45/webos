# build_os.py
import os
import subprocess
from PIL import Image

# Config
KERNEL_CPP = "kernel.cpp"
BOOTLOADER_ASM = "boot_img.asm"
BOOT_IMAGE_PNG = "boot.png"
OUTPUT_IMG = "os.img"
KERNEL_BIN = "kernel.bin"
BOOT_BIN = "boot_img.bin"
RAW_HEADER = "splash_image.h"
QEMU_CMD = ["qemu-system-i386","-drive",f"format=raw,file={OUTPUT_IMG}"]

def needs_rebuild(source,target):
    return not os.path.exists(target) or os.path.getmtime(source) > os.path.getmtime(target)

# 1. Compile kernel
def compile_kernel():
    if needs_rebuild(KERNEL_CPP,KERNEL_BIN):
        print("[*] Compiling kernel...")
        subprocess.run(["i686-elf-g++","-ffreestanding","-O2","-c",KERNEL_CPP,"-o","kernel.o"],check=True)
        subprocess.run(["i686-elf-ld","-Ttext","0x8000","-o",KERNEL_BIN,"--oformat","binary","kernel.o"],check=True)
        print("[+] Kernel compiled")
    else:
        print("[*] Kernel up-to-date")

# 2. Assemble bootloader
def assemble_bootloader():
    if needs_rebuild(BOOTLOADER_ASM,BOOT_BIN):
        print("[*] Assembling bootloader...")
        subprocess.run(["nasm","-f","bin",BOOTLOADER_ASM,"-o",BOOT_BIN],check=True)
        print("[+] Bootloader assembled")
    else:
        print("[*] Bootloader up-to-date")

# 3. Convert image to C++ header
def convert_image():
    if needs_rebuild(BOOT_IMAGE_PNG,RAW_HEADER):
        print("[*] Converting image to splash_image.h...")
        img = Image.open(BOOT_IMAGE_PNG).convert("P").resize((320,200))
        data = img.tobytes()
        with open(RAW_HEADER,"w") as f:
            f.write("uint8_t splash_image[320*200]={")
            f.write(",".join(str(b) for b in data))
            f.write("};")
        print("[+] Image converted")
    else:
        print("[*] Splash image up-to-date")

# 4. Build bootable image
def build_image():
    print("[*] Building OS image...")
    with open(OUTPUT_IMG,"wb") as out:
        for f in [BOOT_BIN,KERNEL_BIN]:
            with open(f,"rb") as part:
                out.write(part.read())
    print(f"[+] OS image built: {OUTPUT_IMG}")

# 5. Run QEMU
def run_qemu():
    print("[*] Running OS in QEMU...")
    subprocess.run(QEMU_CMD)

if __name__=="__main__":
    compile_kernel()
    assemble_bootloader()
    convert_image()
    build_image()
    run_qemu()
