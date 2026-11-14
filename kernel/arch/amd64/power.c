#include <stdint.h>
#include <stddef.h>
#include <kilons/io.h>
#include <kilons/power.h>
#include <kilons/console.h>

/* Bootstrap maps physical addresses below 4 GiB. XSDT entries above
 * that boundary still need a future on-demand physical mapping API. */
#define PHYS_LIMIT 0x100000000ULL
#define SLP_EN 0x2000u
#define SLP_TYP_MASK 0x1c00u
#define SCI_EN 1u
static uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1]<<8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)rd16(p) | ((uint32_t)rd16(p+2)<<16); }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p+4)<<32); }
static int eq(const uint8_t *p,const char *s,unsigned n) { for(unsigned i=0;i<n;i++) if(p[i]!=(uint8_t)s[i]) return 0; return 1; }
static int range(uint64_t a,uint32_t n) { return a>=0x400 && n && a<PHYS_LIMIT && n<=PHYS_LIMIT-a; }
static int checksum(const uint8_t *p,uint32_t n) { uint8_t s=0; for(uint32_t i=0;i<n;i++) s=(uint8_t)(s+p[i]); return s==0; }
static const uint8_t *table(uint64_t addr,const char *sig) {
    if(!range(addr,36)) return NULL;
    const uint8_t *p=(const uint8_t *)(uintptr_t)addr;
    uint32_t n=rd32(p+4);
    if(!eq(p,sig,4)||n<36||n>1024*1024||!range(addr,n)||!checksum(p,n)) return NULL;
    return p;
}
static const uint8_t *rsdp(void) {
    uint16_t seg;
    __asm__ volatile("movw 0x40e, %0" : "=r"(seg));
    uint32_t ebda=(uint32_t)seg<<4;
    for(unsigned area=0;area<2;area++) {
        uint32_t start=area==0?ebda:0xe0000;
        uint32_t end=area==0?ebda+1024:0x100000;
        if(!start||end>0x100000) continue;
        for(uint32_t a=start;a+20<=end;a+=16) {
            const uint8_t *p=(const uint8_t *)(uintptr_t)a;
            if(eq(p,"RSD PTR ",8)&&checksum(p,20)) return p;
        }
    }
    return NULL;
}
static const uint8_t *fadt_in_root(uint64_t addr,unsigned stride,const char *sig) {
    if(!range(addr,36)) {
        console_write("ACPI: root table address outside mapped physical range\n");
        return NULL;
    }
    const uint8_t *root=table(addr,sig);
    if(!root) {
        console_write("ACPI: root table signature, length or checksum invalid\n");
        return NULL;
    }
    uint32_t n=rd32(root+4);
    for(uint32_t off=36;off+stride<=n;off+=stride) {
        uint64_t child=stride==8?rd64(root+off):rd32(root+off);
        const uint8_t *f=table(child,"FACP");
        if(f&&rd32(f+4)>=116) return f;
    }
    console_write("ACPI: no valid FADT in root table\n");
    return NULL;
}
static const uint8_t *find_fadt(void) {
    const uint8_t *r=rsdp();
    if(!r) { console_write("ACPI: RSDP not found\n"); return NULL; }
    console_write("ACPI: RSDP found\n");
    if(r[15]>=2 && range((uintptr_t)r,36)) {
        uint32_t len=rd32(r+20);
        if(len>=36&&len<=4096&&range((uintptr_t)r,len)&&checksum(r,len)) {
            const uint8_t *f=fadt_in_root(rd64(r+24),8,"XSDT");
            if(f) { console_write("ACPI: FADT found via XSDT\n"); return f; }
            console_write("ACPI: XSDT lookup failed, trying RSDT\n");
        }
    }
    const uint8_t *f=fadt_in_root(rd32(r+16),4,"RSDT");
    if(f) console_write("ACPI: FADT found via RSDT\n");
    else console_write("ACPI: FADT not found\n");
    return f;
}
/* Minimal, bounded AML _S5 package decoder. Not a general AML interpreter. */
/* ACPI 2.0+ FADT, IA-PC Boot Architecture Flags, bit 1.
 * Older tables carry no keyboard-controller presence information. */
int amd64_i8042_presence(void) {
    const uint8_t *fadt = find_fadt();
    if (!fadt || fadt[8] < 3 || rd32(fadt + 4) < 111)
        return -1;
    return (rd16(fadt + 109) & 2u) != 0;
}

static int aml_integer(const uint8_t **cur,const uint8_t *end,uint32_t *v) {
    const uint8_t *p=*cur;
    if(p>=end) return 0;
    if(*p==0x00||*p==0x01) { *v=*p; *cur=p+1; return 1; }
    if(*p==0x0a&&end-p>=2) { *v=p[1]; *cur=p+2; return 1; }
    if(*p==0x0b&&end-p>=3) { *v=rd16(p+1); *cur=p+3; return 1; }
    if(*p==0x0c&&end-p>=5) { *v=rd32(p+1); *cur=p+5; return 1; }
    return 0;
}
static int s5_types(const uint8_t *dsdt,uint16_t *a,uint16_t *b) {
    const uint8_t *end=dsdt+rd32(dsdt+4);
    for(const uint8_t *p=dsdt+36;end-p>=9;p++) {
        if(*p!=0x08) continue;
        const uint8_t *q=p+1;
        if(q<end&&(*q=='\\'||*q=='^')) q++;
        if(end-q<7||!eq(q,"_S5_",4)||q[4]!=0x12) continue;
        q+=5;
        const uint8_t *pkg=q;
        uint8_t lead=*q++;
        unsigned extra=lead>>6;
        if((size_t)(end-q)<extra) continue;
        uint32_t length=extra?(lead&0x0f):(lead&0x3f);
        for(unsigned i=0;i<extra;i++) length|=(uint32_t)(*q++)<<(4+8*i);
        /* PkgLength counts its own encoding bytes. */
        if(length<1+extra+1||length>(uint32_t)(end-pkg)) continue;
        const uint8_t *pkg_end=pkg+length;
        if(q>=pkg_end||*q++<2) continue;
        uint32_t x,y;
        if(!aml_integer(&q,pkg_end,&x)||!aml_integer(&q,pkg_end,&y)||x>7||y>7) continue;
        *a=(uint16_t)(x<<10); *b=(uint16_t)(y<<10); return 1;
    }
    return 0;
}

static uint16_t pm1_port(const uint8_t *f,uint32_t legacy_off,uint32_t gas_off) {
    uint32_t n=rd32(f+4);
    if(n>=gas_off+12) {
        const uint8_t *g=f+gas_off;
        /* GAS: system I/O, 16-bit register, no bit offset, word access. */
        uint64_t a=rd64(g+4);
        if(g[0]==1 && g[2]==0 && (g[3]==0||g[3]==2) && g[1]>=16 && a && a<=0xffff)
            return (uint16_t)a;
    }
    uint32_t legacy=rd32(f+legacy_off);
    return legacy && legacy<=0xffff?(uint16_t)legacy:0;
}
/* Read-only diagnostics: never enter ACPI mode or write power registers. */
int power_probe(void) {
    const uint8_t *f=find_fadt();
    if(!f) return 0;
    uint64_t dsdt=rd32(f+40);
    if(rd32(f+4)>=148) {
        uint64_t ext=rd64(f+140);
        if(ext && range(ext,36)) dsdt=ext;
    }
    const uint8_t *d=table(dsdt,"DSDT");
    if(!d && dsdt!=rd32(f+40)) d=table(rd32(f+40),"DSDT");
    if(!d) { console_write("ACPI: DSDT unavailable\n"); return 0; }
    uint16_t a,b;
    if(!s5_types(d,&a,&b)) { console_write("ACPI: S5 unavailable\n"); return 0; }
    if(f[89]<2 || !pm1_port(f,64,172)) {
        console_write("ACPI: PM1a control unavailable\n"); return 0;
    }
    console_write("ACPI: S5 soft-off available\n");
    console_write((inw(pm1_port(f,64,172))&SCI_EN)?
        "ACPI: mode enabled\n":"ACPI: mode not enabled yet\n");
    return 1;
}
int power_shutdown(void) {
    const uint8_t *f=find_fadt();
    if(!f) return 0;
    uint64_t dsdt=rd32(f+40);
    if(rd32(f+4)>=148) {
        uint64_t ext=rd64(f+140);
        if(ext && range(ext,36)) dsdt=ext;
    }
    const uint8_t *d=table(dsdt,"DSDT");
    if(!d && dsdt!=rd32(f+40)) d=table(rd32(f+40),"DSDT");
    if(!d) { console_write("ACPI: DSDT not accessible or invalid\n"); return 0; }
    console_write("ACPI: DSDT found\n");
    uint16_t a,b;
    if(!s5_types(d,&a,&b)) { console_write("ACPI: _S5 package not decoded\n"); return 0; }
    console_write("ACPI: _S5 sleep types decoded\n");
    if(f[89]<2) { console_write("ACPI: PM1 control register too short\n"); return 0; }
    uint16_t pm1a=pm1_port(f,64,172),pm1b=pm1_port(f,68,184);
    if(!pm1a) { console_write("ACPI: PM1a control port unavailable\n"); return 0; }
    uint32_t smi=rd32(f+48);
    uint8_t enable=f[52];
    if(!(inw(pm1a)&SCI_EN)&&smi&&smi<=0xffff&&enable) {
        console_write("ACPI: requesting ACPI mode\n");
        outb((uint16_t)smi,enable);
        unsigned wait=1000000;
        while(wait--&&!(inw(pm1a)&SCI_EN)) __asm__ volatile("pause");
    }
    if(!(inw(pm1a)&SCI_EN)) { console_write("ACPI: SCI_EN not set\n"); return 0; }
    console_write("ACPI: sending S5 power-off request\n");
    __asm__ volatile("cli":::"memory");
    uint16_t olda=inw(pm1a);
    outw(pm1a,(uint16_t)((olda&~(SLP_TYP_MASK|SLP_EN))|a|SLP_EN));
    if(pm1b) {
        uint16_t oldb=inw(pm1b);
        outw(pm1b,(uint16_t)((oldb&~(SLP_TYP_MASK|SLP_EN))|b|SLP_EN));
    }
    for(volatile unsigned i=0;i<10000000;i++) __asm__ volatile("pause");
    console_write("ACPI: S5 request did not power off machine\n");
    return 0;
}
