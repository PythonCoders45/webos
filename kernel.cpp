// kernel.cpp — webOS toy kernel (x86, GRUB/Multiboot2, 32-bit)
// Adds: simulated Wi-Fi (UI + status), toy text-mode Browser (local HTML from FAT32).
// Keeps: VGA console, RTC clock, PS/2 keyboard (Ctrl shown as "ord"), PIC+IDT,
//        SD(SPI) skeleton, FAT32 reader.
// Shortcuts: ord+w (Wi-Fi), ord+b (Browser), ord+q (quit app).
//
// Build:
//   i686-elf-g++ -ffreestanding -m32 -O2 -c kernel.cpp -o kernel.o
//   i686-elf-g++ -ffreestanding -m32 -O2 -T linker.ld -nostdlib -o kernel.elf kernel.o

#include <stdint.h>
#include <stddef.h>

// ---------- Multiboot2 ----------
extern "C" {
static const uint32_t MB2_HEADER_MAGIC=0xE85250D6, MB2_ARCH_I386=0, MB2_HEADER_LEN=24;
__attribute__((section(".multiboot"), used))
static const struct { uint32_t magic, arch, len, csum; uint16_t end_type, end_flags; uint32_t end_size; }
mb2 = { MB2_HEADER_MAGIC, MB2_ARCH_I386, MB2_HEADER_LEN,
        (uint32_t)(0 - (MB2_HEADER_MAGIC+MB2_ARCH_I386+MB2_HEADER_LEN)), 0,0,8 };
}

// ---------- tiny libc ----------
static inline void* kmemset(void* d,int v,size_t n){uint8_t*p=(uint8_t*)d;while(n--)*p++=(uint8_t)v;return d;}
static inline void* kmemcpy(void* d,const void* s,size_t n){uint8_t*D=(uint8_t*)d;const uint8_t*S=(const uint8_t*)s;while(n--)*D++=*S++;return d;}
static inline size_t kstrlen(const char* s){size_t n=0;while(s[n])n++;return n;}

// ---------- x86 I/O ----------
static inline void outb(uint16_t p,uint8_t v){asm volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline uint8_t inb(uint16_t p){uint8_t v;asm volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline void io_wait(){outb(0x80,0);}

// ---------- VGA ----------
namespace vga{
    volatile uint16_t* const V=(uint16_t*)0xB8000; const int W=80,H=25;
    uint8_t col=0x0F; int cx=0,cy=0;
    void clear(){for(int y=0;y<H;y++)for(int x=0;x<W;x++)V[y*W+x]=(' '|(col<<8));cx=cy=0;}
    void move(int x,int y){cx=x;cy=y;}
    void putc(char c){ if(c=='\n'){cx=0;cy++;} else {V[cy*W+cx]=(c|(col<<8)); if(++cx>=W){cx=0;cy++;}}
        if(cy>=H){for(int y=1;y<H;y++)for(int x=0;x<W;x++)V[(y-1)*W+x]=V[y*W+x];
            for(int x=0;x<W;x++)V[(H-1)*W+x]=(' '|(col<<8)); cy=H-1;}}
    void puts(const char*s){for(;*s;s++)putc(*s);}
    void print_dec(uint32_t v){char b[16];int i=0;if(!v){putc('0');return;}while(v){b[i++]='0'+v%10;v/=10;}while(i--)putc(b[i]);}
    void hline(int y){move(0,y);for(int i=0;i<W;i++)putc('-');}
    void fill_line(int y){move(0,y);for(int i=0;i<W;i++)putc(' ');}
}

// ---------- RTC ----------
namespace rtc{
    static inline uint8_t r(uint8_t reg){outb(0x70,reg);return inb(0x71);}
    static inline uint8_t b2(uint8_t b){return (b&0x0F)+10*(b>>4);}
    struct DT{uint8_t s,m,h,d,mo,y;};
    DT read(){while(r(0x0A)&0x80){} DT t; uint8_t s=r(0),m=r(2),h=r(4),d=r(7),mo=r(8),y=r(9),b=r(0x0B);
        bool bin=b&0x04,h24=b&0x02; if(!bin){s=b2(s);m=b2(m);h=b2(h);d=b2(d);mo=b2(mo);y=b2(y);}
        if(!h24){bool pm=h&0x80;h&=0x7F;if(pm&&h<12)h+=12;if(!pm&&h==12)h=0;} t.s=s;t.m=m;t.h=h;t.d=d;t.mo=mo;t.y=y; return t;}
}

// ---------- IDT/PIC ----------
struct IDTEntry{uint16_t off1;uint16_t sel;uint8_t ist;uint8_t type;uint16_t off2;uint32_t off3;uint32_t zero;};
struct IDTPtr{uint16_t limit; uint64_t base;} __attribute__((packed));
IDTEntry idt[256]; IDTPtr idtp;
extern "C" void irq1_stub();
static void lidt(const IDTPtr* p){asm volatile("lidt (%0)"::"r"(p));}
static void set_gate(int n, void(*h)(), uint8_t type=0x8E){uint64_t a=(uint64_t)h; IDTEntry&e=idt[n];
    e.off1=a&0xFFFF; e.sel=0x08; e.ist=0; e.type=type; e.off2=(a>>16)&0xFFFF; e.off3=(uint32_t)(a>>32); e.zero=0; }
static void pic_remap(){outb(0x20,0x11);io_wait();outb(0xA0,0x11);io_wait();
    outb(0x21,0x20);io_wait();outb(0xA1,0x28);io_wait();
    outb(0x21,0x04);io_wait();outb(0xA1,0x02);io_wait();
    outb(0x21,0x01);io_wait();outb(0xA1,0x01);io_wait();
    outb(0x21,0x00);outb(0xA1,0x00);}

// ---------- Mods / Status ----------
struct Mods{bool ctrl=false,shift=false,alt=false;} mods;

static const char* wifi_bars(int rssi){
    if(rssi>=75) return "📶▮▮▮▮";
    if(rssi>=50) return "📶▮▮▮ ";
    if(rssi>=25) return "📶▮▮  ";
    if(rssi>=10) return "📶▮   ";
    return         "📶•    ";
}

struct WifiState{
    bool connected=false;
    int rssi=0;                // 0..100
    char ssid[32];
} wifi;

void draw_status(){
    vga::move(0,0);
    // left: mods
    vga::puts("mods:["); vga::puts(mods.ctrl?"ord":"   "); vga::puts("][");
    vga::puts(mods.shift?"shift":"     "); vga::puts("][");
    vga::puts(mods.alt?"alt":"   "); vga::puts("]  ");
    // middle: wifi
    vga::puts("wifi: ");
    if(wifi.connected){
        vga::puts(wifi_bars(wifi.rssi)); vga::puts(" ");
        vga::puts(wifi.ssid);
    } else {
        vga::puts("disconnected");
    }
    // right: clock
    int pad = 80 - vga::cx - 20; while(pad-->0) vga::putc(' ');
    rtc::DT t=rtc::read();
    if(t.h<10)vga::putc('0'); vga::print_dec(t.h); vga::putc(':');
    if(t.m<10)vga::putc('0'); vga::print_dec(t.m); vga::putc(':');
    if(t.s<10)vga::putc('0'); vga::print_dec(t.s);
    vga::fill_line(1);
    vga::move(0,2);
}

// ---------- Keyboard ----------
namespace kbd{
    bool e0=false;
    const char map[128]={0,27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
        '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
        'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
        'z','x','c','v','b','n','m',',','.','/', 0,'*',0,' ',0};
    char up(char c){ if(c>='a'&&c<='z') return c-32;
        const struct {char a,b;} t[]={{'`','~'},{'1','!'},{'2','@'},{'3','#'},{'4','$'},{'5','%'},{'6','^'},{'7','&'},{'8','*'},{'9','('},{'0',')'},{'-','_'},{'=','+'},{'[','{'},{']','}'},{'\\','|'},{';',':'},{'\'','\"'},{',','<'},{'.','>'},{'/','?'}};
        for(auto &p:t) if(c==p.a) return p.b; return c;}
    // simple input queue
    volatile char last_key=0;

    void on_sc(uint8_t sc){
        if(sc==0xE0){e0=true;return;}
        bool rel=sc&0x80; uint8_t c=sc&0x7F;
        if(c==0x1D){mods.ctrl=!rel; draw_status(); return;}
        if(c==0x2A||c==0x36){mods.shift=!rel; draw_status(); return;}
        if(c==0x38){mods.alt=!rel; draw_status(); return;}
        if(rel){e0=false;return;}
        char ch=0; if(c<sizeof(map) && map[c]) ch=map[c]; if(ch && mods.shift) ch=up(ch);
        if(ch){ last_key=ch; }
        e0=false;
    }

    void init(){ uint8_t m=inb(0x21); m&=~(1<<1); outb(0x21,m); }
}
extern "C" void irq1_handler_c(){ uint8_t sc=inb(0x60); kbd::on_sc(sc); outb(0x20,0x20); }
extern "C" __attribute__((naked)) void irq1_stub(){ asm volatile(
    "pusha\ncall irq1_handler_c\npopa\niret\n"); }

// ---------- Block & FAT32 (trim, as before) ----------
struct BlockDevice{virtual bool read_sector(uint32_t lba,uint8_t*buf)=0;virtual const char* name()const=0;virtual ~BlockDevice(){}};
namespace spi{ void init(){} uint8_t transfer(uint8_t v){(void)v;return 0xFF;} }
class SdSpiDevice: public BlockDevice{
    bool initd=false,sdhc=false; void cs(bool){/*TODO for real hw*/} uint8_t x(uint8_t v){return spi::transfer(v);}
    void clocks(int n){while(n--)x(0xFF);} uint8_t cmd(uint8_t c,uint32_t a,uint8_t crc){
        x(0x40|c); x(a>>24); x(a>>16); x(a>>8); x(a); x(crc); for(int i=0;i<8;i++){uint8_t r=x(0xFF); if(!(r&0x80)) return r;} return 0xFF;}
public: const char* name()const override{return "SD(SPI)";}
    bool init(){ spi::init(); cs(true);clocks(10);cs(false);
        if(cmd(0,0,0x95)!=0x01){cs(true);return false;}
        uint8_t r=cmd(8,0x1AA,0x87);
        if(r==0x01){uint8_t o[4];for(int i=0;i<4;i++)o[i]=x(0xFF); do{cmd(55,0,0x01); r=cmd(41,1UL<<30,0x01);}while(r!=0x00);
            cmd(58,0,0x01); uint8_t ocr[4]; for(int i=0;i<4;i++) ocr[i]=x(0xFF); sdhc=(ocr[0]&0x40)!=0;}
        else{do{cmd(55,0,0x01); r=cmd(41,0,0x01);}while(r!=0x00); sdhc=false;}
        cs(true); initd=true; return true;}
    bool read_sector(uint32_t lba,uint8_t* b) override{
        if(!initd) if(!init()) return false; cs(false); uint32_t a=sdhc?lba:lba*512U;
        if(cmd(17,a,0x01)!=0x00){ cs(true); return false; } int t=100000; uint8_t tok;
        do{tok=x(0xFF);}while(tok==0xFF && --t); if(tok!=0xFE){cs(true);return false;}
        for(int i=0;i<512;i++) b[i]=x(0xFF); x(0xFF); x(0xFF); cs(true); return true;}
};
class UsbMscDevice: public BlockDevice{
public: const char* name()const override{return "USB(MSC)";}
    bool read_sector(uint32_t, uint8_t*) override { return false; } // TODO
};

#pragma pack(push,1)
struct MbrPart{uint8_t st,chs1[3],type,chs2[3];uint32_t lba,secs;};
struct Bpb32{uint8_t jmp[3];char oem[8];uint16_t bps;uint8_t spc;uint16_t rsvd;uint8_t fats;uint16_t re16,ts16;uint8_t media;uint16_t spf16,spt,heads;uint32_t hidden,ts32;
    uint32_t spf32;uint16_t flags,ver;uint32_t rootclus;uint16_t fsinfo,backup;uint8_t rsvd2[12];uint8_t drvnum,rsvd1,bootsig;uint32_t volid;char lbl[11];char fst[8];};
struct DirEnt{char name[11];uint8_t attr,ntres,t10;uint16_t ctime,cdate,adate,clus_hi,wtime,wdate,clus_lo;uint32_t size;};
#pragma pack(pop)

struct FAT32{
    BlockDevice* dev=nullptr; uint32_t pstart=0; Bpb32 bpb{}; uint32_t fat_lba=0,clus_lba=0;
    bool mount(BlockDevice&d,uint32_t l){dev=&d;pstart=l;uint8_t s[512]; if(!dev->read_sector(pstart,s)) return false;
        kmemcpy(&bpb,s,sizeof(Bpb32)); if(bpb.bps!=512||!bpb.spc) return false;
        fat_lba=pstart+bpb.rsvd; clus_lba=pstart+bpb.rsvd+bpb.fats*bpb.spf32; return true;}
    uint32_t clus2lba(uint32_t c)const{return clus_lba+(c-2)*bpb.spc;}
    bool read_fat(uint32_t c,uint32_t&n){uint32_t off=c*4,sec=fat_lba+off/512,ofs=off%512;uint8_t s[512]; if(!dev->read_sector(sec,s)) return false;
        n=(*(uint32_t*)(s+ofs))&0x0FFFFFFF; return true;}
    bool read_cluster(uint32_t c,uint8_t*out){uint32_t l=clus2lba(c); for(uint8_t i=0;i<bpb.spc;i++) if(!dev->read_sector(l+i,out+i*512)) return false; return true;}
    bool list_root(){ if(bpb.spc>8) return false; uint8_t buf[4096]; uint32_t c=bpb.rootclus; vga::puts("root:\n");
        while(c<0x0FFFFFF8){ if(!read_cluster(c,buf)) return false; const DirEnt* e=(const DirEnt*)buf; size_t N=(512*bpb.spc)/sizeof(DirEnt);
            for(size_t i=0;i<N;i++){ if(e[i].name[0]==0x00) return true; if((uint8_t)e[i].name[0]==0xE5||e[i].attr==0x0F) continue;
                char n[13]; size_t p=0; for(int k=0;k<8;k++) if(e[i].name[k]!=' ') n[p++]=e[i].name[k];
                if(e[i].name[8]!=' '){ n[p++]='.'; for(int k=8;k<11;k++) if(e[i].name[k]!=' ') n[p++]=e[i].name[k]; } n[p]=0;
                vga::puts("  "); vga::puts(n); vga::puts(" size="); vga::print_dec(e[i].size); vga::puts("\n"); }
            uint32_t nx; if(!read_fat(c,nx)) return false; c=nx; } return true;}
    size_t read_file_83(const char* q,uint8_t*out,size_t cap){
        auto U=[](char c){return (c>='a'&&c<='z')?char(c-32):c;};
        char Q[11]; kmemset(Q,' ',11); int i=0; while(q[i] && q[i]!='.' && i<8){Q[i]=U(q[i]); i++;}
        if(q[i]=='.'){ i++; for(int j=0;j<3 && q[i]; j++,i++) Q[8+j]=U(q[i]); }
        if(bpb.spc>8) return 0; uint8_t buf[4096]; uint32_t c=bpb.rootclus;
        while(c<0x0FFFFFF8){ if(!read_cluster(c,buf)) return 0; const DirEnt* e=(const DirEnt*)buf; size_t N=(512*bpb.spc)/sizeof(DirEnt);
            for(size_t k=0;k<N;k++){ if(e[k].name[0]==0x00) return 0; if((uint8_t)e[k].name[0]==0xE5||e[k].attr==0x0F) continue;
                bool m=true; for(int j=0;j<11;j++){ char a=e[k].name[j]; if(a>='a'&&a<='z') a-=32; if(a!=Q[j]){m=false;break;}}
                if(m){ uint32_t fc=((uint32_t)e[k].clus_hi<<16)|e[k].clus_lo; uint32_t rem=e[k].size; size_t wr=0; uint8_t cbuf[4096];
                    while(fc<0x0FFFFFF8 && rem){ if(!read_cluster(fc,cbuf)) return wr; size_t chunk=(size_t)bpb.spc*512; if(chunk>rem) chunk=rem;
                        if(wr+chunk>cap) chunk=cap-wr; kmemcpy(out+wr,cbuf,chunk); wr+=chunk; rem-=chunk; uint32_t nx; if(!read_fat(fc,nx)) break; fc=nx;}
                    return wr; } }
            uint32_t nx; if(!read_fat(c,nx)) return 0; c=nx; } return 0; }
};

// ---------- App framework (super tiny) ----------
enum class App { None, Wifi, Browser };
App current_app = App::None;

// ---------- Wi-Fi "driver" (simulated) ----------
struct Net{const char* ssid; int rssi;};
Net scan_results[6]; int scan_count=0;

void wifi_scan(){
    // Fake scan results
    Net tmp[]={{"HomeNet",82},{"CoffeeShop",58},{"Bemnet-Phone",67},{"Guest",35},{"IoT",22},{"OpenAP",12}};
    scan_count= (int)(sizeof(tmp)/sizeof(tmp[0]));
    for(int i=0;i<scan_count;i++) scan_results[i]=tmp[i];
}
void wifi_connect(const char* ssid,int rssi){
    kmemset(wifi.ssid,0,sizeof(wifi.ssid));
    // copy
    int i=0; for(; ssid[i] && i< (int)sizeof(wifi.ssid)-1; i++) wifi.ssid[i]=ssid[i];
    wifi.connected=true; wifi.rssi=rssi; draw_status();
}
void wifi_disconnect(){ wifi.connected=false; wifi.rssi=0; wifi.ssid[0]=0; draw_status(); }

void app_wifi_draw(){
    vga::move(0,3);
    vga::puts("[Wi-Fi]\n");
    vga::puts("Enter: s=scan, 1..9=connect, d=disconnect, q=exit\n\n");
    if(!scan_count){ vga::puts("No scan yet. Press 's' to scan.\n"); return; }
    for(int i=0;i<scan_count && i<9;i++){
        vga::puts("  "); vga::putc('1'+i); vga::puts(") ");
        vga::puts(wifi_bars(scan_results[i].rssi)); vga::puts(" ");
        vga::puts(scan_results[i].ssid); vga::puts("\n");
    }
}
void app_wifi_key(char ch){
    if(ch=='q'){ current_app=App::None; vga::move(0,3); vga::puts("\nExiting Wi-Fi.\n"); return; }
    if(ch=='s'){ wifi_scan(); vga::fill_line(2); vga::move(0,3); vga::puts("\n"); }
    if(ch=='d'){ wifi_disconnect(); vga::move(0,3); vga::puts("\nDisconnected.\n"); }
    if(ch>='1'&&ch<='9'){ int idx=ch-'1'; if(idx<scan_count){ wifi_connect(scan_results[idx].ssid,scan_results[idx].rssi);
            vga::move(0,3); vga::puts("\nConnected to "); vga::puts(scan_results[idx].ssid); vga::puts(".\n"); } }
    app_wifi_draw();
}

// ---------- Browser (text-mode, local HTML) ----------
struct Browser{
    FAT32* fs=nullptr;
    // render buffer
    char lines[18][81]; // rows 7..24 (18 lines), 80 cols + NUL
    int link_count=0;
    struct Link{char name[32]; char target[32];} links[9];

    void clear_lines(){ for(int i=0;i<18;i++){ for(int j=0;j<80;j++) lines[i][j]=' '; lines[i][80]=0; } }
    void print_page(){
        // header
        vga::move(0,3);
        vga::puts("[Browser]\n");
        vga::puts("Use: digits 1..9 to follow link, b=back to root, q=exit\n");
        vga::hline(5);
        // body
        for(int i=0;i<18;i++){ vga::move(0,6+i); vga::puts(lines[i]); }
        vga::fill_line(24);
    }

    void add_line(int& row,const char* s){
        if(row>=18) return;
        int col=0; for(size_t i=0; s[i] && col<80; i++){ lines[row][col++]=s[i]; }
        while(col<80) lines[row][col++]=' ';
        row++;
    }

    // very tiny HTML-to-text: strips tags, extracts <a href="...">text</a> as [n] text -> target
    void render_html(const uint8_t* buf, size_t n){
        clear_lines(); link_count=0;
        int row=0; char acc[256]; int ai=0; bool intag=false; bool in_a=false;
        char atext[64]; int ati=0; char href[64]; int hri=0;
        auto flush_text=[&](){ if(ai>0){ acc[ai]=0; add_line(row,acc); ai=0; } };
        for(size_t i=0;i<n;i++){
            char c=(char)buf[i];
            if(c=='<'){ intag=true; // check if starting <a ...>
                // detect closing </a>
                if(i+3<n && (buf[i+1]=='/'||buf[i+1]=='A'||buf[i+1]=='a')){
                    // handled in tag parser below
                }
                continue;
            }
            if(intag){
                // crude tag parsing until '>'
                if(c=='>'){ intag=false; continue; }
                // capture <a href="...">
                // NOTE: super naive
                static const char A1[]="a href=\"";
                static const char A2[]="A HREF=\"";
                // Start anchor?
                if(!in_a){
                    // try match window around here (not robust)
                    // skip heavy parsing; we'll rely on text phase to accumulate once in_a=true
                }
                continue;
            }
            // text
            if(ai<240){
                // track links: if we see pattern [n:text] embedded by pre-pass (we'll do a pre-pass next)
                acc[ai++]=c=='\r' ? ' ' : c;
                if(c=='\n'){ flush_text(); }
            }
        }
        flush_text(); // lines now hold raw text; we’ll re-render with links pass below
        // Second pass: find anchors by very simple pattern and show enumerated links.
        // Instead: rebuild from scratch using a super-naive extractor:
        clear_lines(); row=0; link_count=0; ai=0; intag=false; in_a=false; ati=0; hri=0;
        for(size_t i=0;i<n;i++){
            char c=(char)buf[i];
            if(c=='<'){ // entering a tag
                intag=true; // check if it's <a ...
                // peek ahead for href
                if(i+2<n && (buf[i+1]=='a'||buf[i+1]=='A')){
                    // try to find href="..."
                    hri=0; href[0]=0; bool got_href=false;
                    for(size_t j=i+2; j<n && j<i+120; j++){
                        if(buf[j]=='>'){ i=j; intag=false; in_a=true; ati=0; break; }
                        if(j+5<n && (buf[j]=='h'||buf[j]=='H')&&(buf[j+1]=='r'||buf[j+1]=='R')&&(buf[j+2]=='e'||buf[j+2]=='E')&&(buf[j+3]=='f'||buf[j+3]=='F')&&buf[j+4]=='='){
                            j+=5;
                            if(buf[j]=='\"'){ j++; while(j<n && buf[j]!='\"' && hri<63){ href[hri++]=(char)buf[j++]; }
                                href[hri]=0; got_href=true; }
                        }
                    }
                    (void)got_href;
                } else {
                    // skip other tags
                    for(size_t j=i+1; j<n; j++){ if(buf[j]=='>'){ i=j; intag=false; break; } }
                }
                continue;
            }
            if(in_a){
                if(c=='<'){ // end of text? expect </a>
                    // close anchor
                    if(link_count<9){
                        links[link_count].name[0]=0; for(int k=0;k<ati && k<31;k++) links[link_count].name[k]=atext[k];
                        links[link_count].name[ati<31?ati:31]=0;
                        links[link_count].target[0]=0; for(int k=0; href[k] && k<31; k++) links[link_count].target[k]=href[k];
                        links[link_count].target[kstrlen(links[link_count].target)]=0;
                        char line[80]; int pos=0;
                        line[pos++]='['; line[pos++]=char('1'+link_count); line[pos++]=']'; line[pos++]=' ';
                        for(int k=0;k<ati && pos<76;k++) line[pos++]=atext[k];
                        line[pos]=0; add_line(row,line);
                        link_count++;
                    }
                    // skip to '>'
                    for(size_t j=i+1;j<n;j++){ if(buf[j]=='>'){ i=j; break; } }
                    in_a=false; ati=0; continue;
                } else {
                    if(ati<63) atext[ati++]=c;
                    continue;
                }
            }
            // plain text outside tags: collect into wrapped lines
            if(c=='\r') continue;
            if(c=='\n'){ acc[ai]=0; add_line(row,acc); ai=0; continue; }
            if(ai<240) acc[ai++]=c;
        }
        if(ai){ acc[ai]=0; add_line(row,acc); ai=0; }
        // If no content lines, hint
        if(row==0){ add_line(row,"(empty page)"); }
        print_page();
    }

    // Load a root file and render
    void open_file(const char* name){
        static uint8_t buf[64*1024];
        size_t n=fs->read_file_83(name,buf,sizeof(buf));
        vga::move(0,3); vga::puts("\n");
        if(!n){ vga::puts("Cannot open "); vga::puts(name); vga::puts("\n"); return; }
        render_html(buf,n);
    }
} browser;

// ---------- Global FS mount helper ----------
bool mount_first_fat(BlockDevice& dev, FAT32& fs){
    uint8_t mbr[512]; if(!dev.read_sector(0,mbr) || mbr[510]!=0x55 || mbr[511]!=0xAA) return false;
    const MbrPart* p=(const MbrPart*)(mbr+446);
    for(int i=0;i<4;i++){ if(p[i].type==0x0B||p[i].type==0x0C||p[i].type==0x0E){
            return fs.mount(dev,p[i].lba); } }
    return false;
}

// ---------- Kernel entry ----------
extern "C" void _start(); // fwd
extern "C" void kmain(){
    vga::clear();
    // IDT/PIC/KBD
    kmemset(idt,0,sizeof(idt)); set_gate(0x21, irq1_stub);
    idtp.limit=sizeof(idt)-1; idtp.base=(uint64_t)(uintptr_t)idt; lidt(&idtp); pic_remap(); kbd::init();

    // initial status
    wifi.connected=false; wifi.rssi=0; wifi.ssid[0]=0;
    draw_status();
    vga::puts("webOS ready — ord+w Wi-Fi, ord+b Browser, ord+q quit app\n\n");

    // Try to make a filesystem ready (SD first, then USB)
    SdSpiDevice sd; UsbMscDevice usb;
    BlockDevice* dev=nullptr; FAT32 fs;
    uint8_t tmp[512];
    if(sd.read_sector(0,tmp) && mount_first_fat(sd,fs)) dev=&sd;
    else if(usb.read_sector(0,tmp) && mount_first_fat(usb,fs)) dev=&usb;

    if(dev){ vga::puts("Storage: "); vga::puts(dev->name()); vga::puts(" mounted (FAT32)\n"); fs.list_root(); }
    else    { vga::puts("No storage mounted (implement SPI/USB TODOs for real HW)\n"); }

    // main loop
    for(;;){
        // keep status fresh
        draw_status();
        // poll key (very simple)
        if(kbd::last_key){
            char ch = kbd::last_key; kbd::last_key=0;
            // ord+ combos
            if(mods.ctrl){
                if(ch=='w'){ current_app=App::Wifi; app_wifi_draw(); continue; }
                if(ch=='b'){ if(dev){ current_app=App::Browser; browser.fs=&fs; browser.open_file("DESKTOP.HTML"); } else { vga::puts("(no FS mounted)\n"); } continue; }
                if(ch=='q'){ current_app=App::None; vga::puts("\n(apps closed)\n"); continue; }
            }
            // app-specific keys
            if(current_app==App::Wifi)   app_wifi_key(ch);
            else if(current_app==App::Browser){
                if(ch=='q'){ current_app=App::None; vga::puts("\nExit browser\n"); }
                else if(ch=='b'){ browser.open_file("INDEX.HTML"); } // optional "home"
                else if(ch>='1'&&ch<='9'){
                    int idx=ch-'1'; if(idx<browser.link_count){
                        // assume links[idx].target is a root 8.3 file like FOO.HTML
                        browser.open_file(browser.links[idx].target);
                    }
                }
            } else {
                // normal typing to console
                vga::putc(ch);
            }
        }
        asm volatile("hlt");
    }
}

extern "C" void _start(){ kmain(); }
