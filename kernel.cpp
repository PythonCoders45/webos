#include <stdint.h>
#include "splash_image.h"

volatile uint8_t* VGA = (uint8_t*)0xA000;
const int WIDTH = 320;
const int HEIGHT = 200;

// Cursor
int cursor_x = WIDTH/2;
int cursor_y = HEIGHT/2;
const int CURSOR_RADIUS = 5;
uint8_t cursor_color = 15; // white

// Keyboard port
#define KBD_PORT 0x60

// Draw filled circle
void draw_circle(int x, int y, int radius, uint8_t color){
    for(int dy=-radius; dy<=radius; dy++){
        for(int dx=-radius; dx<=radius; dx++){
            if(dx*dx + dy*dy <= radius*radius){
                int px=x+dx;
                int py=y+dy;
                if(px>=0 && px<WIDTH && py>=0 && py<HEIGHT)
                    VGA[py*WIDTH + px] = color;
            }
        }
    }
}

// Erase circle (draw with background)
void erase_circle(int x,int y,int radius,uint8_t bg){
    draw_circle(x,y,radius,bg);
}

// Draw splash
void draw_splash(){
    for(int i=0; i<WIDTH*HEIGHT; i++)
        VGA[i] = splash_image[i];
}

// Simple I/O port read
uint8_t inb(uint16_t port){
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Read keyboard scan code
uint8_t read_key(){
    while(!(inb(0x64) & 1));
    return inb(KBD_PORT);
}

// Move cursor with arrow keys
void move_cursor_with_keyboard(){
    uint8_t key = read_key();
    erase_circle(cursor_x, cursor_y, CURSOR_RADIUS, 0);

    if(key == 0x48) cursor_y -= 2; // up
    if(key == 0x50) cursor_y += 2; // down
    if(key == 0x4B) cursor_x -= 2; // left
    if(key == 0x4D) cursor_x += 2; // right

    if(cursor_x < 0) cursor_x = 0;
    if(cursor_y < 0) cursor_y = 0;
    if(cursor_x >= WIDTH) cursor_x = WIDTH-1;
    if(cursor_y >= HEIGHT) cursor_y = HEIGHT-1;

    draw_circle(cursor_x, cursor_y, CURSOR_RADIUS, cursor_color);
}

// Dummy FAT32 root directory listing (replace with real SD/USB later)
const char* files[] = {"FILE1.TXT", "FILE2.BIN", "IMAGE.BMP", "APP.EXE"};
const int file_count = 4;

// Draw file names on the side
void draw_file_list(){
    for(int i=0; i<file_count; i++){
        int y = i*8;
        const char* name = files[i];
        for(int j=0; name[j]; j++){
            uint8_t c = name[j];
            VGA[y*WIDTH + 10 + j] = c; // simple ASCII for demo
        }
    }
}

// Kernel main
extern "C" void kernel_main(){
    draw_splash();
    draw_file_list();
    draw_circle(cursor_x, cursor_y, CURSOR_RADIUS, cursor_color);

    while(1){
        move_cursor_with_keyboard();
    }
}
