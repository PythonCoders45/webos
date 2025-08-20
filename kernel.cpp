// kernel.cpp — Bootable kernel with FAT32 SD/USB support
extern "C" void kernel_main();

#include <stdint.h>
#include <stddef.h>

// Multiboot header for GRUB
extern "C" {
    __attribute__((section(".multiboot"))) unsigned long multiboot_header[] = {
        0x1BADB002, 0x0, -(0x1BADB002)
    };
}

// ---------------- VGA OUTPUT ----------------
volatile uint16_t* VGA = (uint16_t*)0xB8000;
const int VGA_WIDTH = 80;
const int VGA_HEIGHT = 25;
int vga_row = 0, vga_col = 0;

void print(const char* str) {
    while (*str) {
        if (*str == '\n') { vga_col = 0; vga_row++; }
        else VGA[vga_row * VGA_WIDTH + vga_col++] = (VGA[vga_row * VGA_WIDTH + vga_col] & 0xFF00) | *str;
        if (vga_col >= VGA_WIDTH) { vga_col = 0; vga_row++; }
        if (vga_row >= VGA_HEIGHT) vga_row = 0;
        str++;
    }
}

// ---------------- KEYBOARD ----------------
bool ctrl_pressed = false;
extern "C" void keyboard_interrupt_handler() {
    uint8_t scancode;
    asm volatile("inb %1, %0" : "=a"(scancode) : "Nd"(0x60));
    ctrl_pressed = (scancode == 0x1D); // Ctrl key
}

// ---------------- CMOS DATE/TIME ----------------
uint8_t read_cmos(uint8_t reg) {
    asm volatile("outb %0, $0x70" : : "a"(reg));
    uint8_t val;
    asm volatile("inb $0x71, %0" : "=a"(val));
    return val;
}

void show_date() {
    uint8_t sec = read_cmos(0x00);
    uint8_t min = read_cmos(0x02);
    uint8_t hour = read_cmos(0x04);
    uint8_t day = read_cmos(0x07);
    uint8_t month = read_cmos(0x08);
    uint8_t year = read_cmos(0x09);
    char buf[64];
    snprintf(buf, sizeof(buf), "Date: %02x/%02x/20%02x %02x:%02x:%02x\n", day, month, year, hour, min, sec);
    print(buf);
}

// ---------------- BLOCK DEVICE ----------------
struct BlockDevice {
    void (*read_sector)(uint32_t lba, uint8_t* buf);
};

uint8_t sector[512];

// ---------------- FAKE SD/USB DRIVER ----------------
void fake_sd_read(uint32_t lba, uint8_t* buf) {
    // TODO: implement real AHCI/IDE read
    for (int i=0;i<512;i++) buf[i] = 0;
}

BlockDevice sd = {fake_sd_read};

// ---------------- FAT32 STRUCTS ----------------
struct FAT32_BootSector {
    uint8_t  jmp[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_small;
    uint8_t  media;
    uint16_t fat_size_small;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors;
    uint32_t fat_size;
    uint16_t flags;
    uint16_t version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
} __attribute__((packed));

struct FAT32_DirEntry {
    char     name[11];
    uint8_t  attr;
    uint8_t  ntres;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t size;
} __attribute__((packed));

// ---------------- FAT32 FUNCTIONS ----------------
FAT32_BootSector bs;

void read_boot_sector() {
    sd.read_sector(0, sector);
    bs = *(FAT32_BootSector*)sector;
    print("Boot sector read.\n");
}

void list_root_dir() {
    uint32_t cluster = bs.root_cluster;
    print("Root directory:\n");
    // For simplicity: read only first cluster
    uint32_t first_sector = bs.reserved_sectors + (bs.num_fats * bs.fat_size) + (cluster - 2) * bs.sectors_per_cluster;
    for (uint32_t s = 0; s < bs.sectors_per_cluster; s++) {
        sd.read_sector(first_sector + s, sector);
        for (int i = 0; i < 512; i += sizeof(FAT32_DirEntry)) {
            FAT32_DirEntry* e = (FAT32_DirEntry*)(sector + i);
            if (e->name[0] == 0x00) break; // no more entries
            if ((e->attr & 0x0F) == 0x0F) continue; // long filename
            char name[13] = {};
            for (int j = 0; j < 11; j++) {
                name[j] = e->name[j];
            }
            print(name);
            print("\n");
        }
    }
}

// ---------------- KERNEL ENTRY ----------------
extern "C" void kernel_main() {
    print("Booting Betnix OS Real Kernel (FAT32)\n");
    show_date();
    read_boot_sector();
    list_root_dir();
    print("Press Ctrl key to test keyboard.\n");
    while (1) {
        if (ctrl_pressed) {
            print("Ctrl pressed!\n");
            ctrl_pressed = false;
        }
    }
}
