#include <stdint.h>
#include <stddef.h>
#include <kilons/io.h>
#include <kilons/power.h>

/* Bootstrap page tables identity-map the first GiB. Only accept ACPI tables
 * in that range until a proper physical mapping facility is available. */
#define PHYS_LIMIT 0x40000000ULL
#define SLP_EN (1u << 13)
#define SCI_EN 1u
static uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)rd16(p) | ((uint32_t)rd16(p+2) << 16); }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p+4) << 32); }
static int equal(const uint8_t *p, const char *s, unsigned n) {
    for (unsigned i=0;i<n;i++) if (p[i]!=(uint8_t)s[i]) return 0;
    return 1;
}
static int range(uint64_t addr, uint32_t len) {
    return addr >= 0x400 && len && addr < PHYS_LIMIT && len <= PHYS_LIMIT-addr;
}
static int checksum(const uint8_t *p, uint32_t n) {
    uint8_t sum=0;
    for (uint32_t i=0;i<n;i++) sum=(uint8_t)(sum+p[i]);
    return sum==0;
}
static const uint8_t *table(uint64_t addr, const char *signature) {
    if (!range(addr,36)) return NULL;
    const uint8_t *p=(const uint8_t *)(uintptr_t)addr;
    uint32_t n=rd32(p+4);
    if (!equal(p,signature,4) || n<36 || n>1024*1024 || !range(addr,n) || !checksum(p,n)) return NULL;
    return p;
}
static const uint8_t *rsdp(void) {
    /* ACPI 1.0 EBDA first KiB, then BIOS area. */
    uint16_t ebda_segment;
    __asm__ volatile("movw 0x40e, %0" : "=r"(ebda_segment));
    uint32_t ebda=(uint32_t)ebda_segment<<4;
    for (unsigned area=0;area<2;area++) {
        uint32_t start=area==0 ? ebda : 0xe0000;
        uint32_t end=area==0 ? ebda+1024 : 0x100000;
        if (!start || end>0x100000) continue;
        for (uint32_t a=start;a+20<=end;a+=16) {
            const uint8_t *p=(const uint8_t *)(uintptr_t)a;
            if (equal(p,"RSD PTR ",8) && checksum(p,20)) return p;
        }
    }
    return NULL;
}
static const uint8_t *find_fadt(void) {
    const uint8_t *r=rsdp();
    if (!r) return NULL;
    /* Prefer XSDT if extended RSDP checksum is valid. */
    uint64_t addr=rd32(r+16);
    unsigned entry_size=4;
    const char *sig="RSDT";
    if (r[15]>=2 && range((uintptr_t)r,36)) {
        uint32_t len=rd32(r+20);
        if (len>=36 && len<=4096 && range((uintptr_t)r,len) && checksum(r,len) && rd64(r+24)<PHYS_LIMIT) {
            addr=rd64(r+24); entry_size=8; sig="XSDT";
        }
    }
    const uint8_t *root=table(addr,sig);
    if (!root) return NULL;
    uint32_t n=rd32(root+4);
    for (uint32_t i=36;i+entry_size<=n;i+=entry_size) {
        uint64_t child=entry_size==8 ? rd64(root+i) : rd32(root+i);
        const uint8_t *f=table(child,"FACP");
        if (f && rd32(f+4)>=116) return f;
    }
    return NULL;
}
/* Find Name(_S5, Package(...)) in DSDT AML, and decode its first two
 * integer package elements. This is a limited parser, not a full AML VM. */
static int aml_integer(const uint8_t **cursor, const uint8_t *end, uint32_t *out) {
    const uint8_t *p=*cursor;
    if (p>=end) return 0;
    if (*p==0x00 || *p==0x01) { *out=*p++; *cursor=p; return 1; }
    if (*p==0x0a && p+2<=end) { *out=p[1]; *cursor=p+2; return 1; }
    if (*p==0x0b && p+3<=end) { *out=rd16(p+1); *cursor=p+3; return 1; }
    if (*p==0x0c && p+5<=end) { *out=rd32(p+1); *cursor=p+5; return 1; }
    return 0;
}
static int s5_types(const uint8_t *dsdt, uint16_t *a, uint16_t *b) {
    uint32_t len=rd32(dsdt+4);
    const uint8_t *end=dsdt+len;
    for (const uint8_t *p=dsdt+36;p+9<end;p++) {
        if (*p!=0x08) continue; /* NameOp */
        const uint8_t *q=p+1;
        if (*q=='\\' || *q=='^') q++;
        if (q+6>=end || !equal(q,"_S5_",4) || q[4]!=0x12) continue;
        q+=5; /* PackageOp */
        uint8_t lead=*q++;
        unsigned extra=lead>>6;
        uint32_t pkglen=extra ? (lead&0x0f) : (lead&0x3f);
        if (q+extra>=end) continue;
        for (unsigned i=0;i<extra;i++) pkglen|=(uint32_t)(*q++) << (4+8*i);
        if (pkglen<2 || *q++<2) continue;
        uint32_t v1,v2;
        if (!aml_integer(&q,end,&v1) || !aml_integer(&q,end,&v2) || v1>7 || v2>7) continue;
        *a=(uint16_t)(v1<<10); *b=(uint16_t)(v2<<10); return 1;
    }
    return 0;
}
static void outw(uint16_t port,uint16_t value) {
    __asm__ volatile("outw %0,%1"::"a"(value),"Nd"(port));
}
int power_shutdown(void) {
    const uint8_t *f=find_fadt();
    if (!f) return 0;
    uint32_t dsdt=rd32(f+40);
    const uint8_t *d=table(dsdt,"DSDT");
    if (!d) return 0;
    uint16_t a,b;
    if (!s5_types(d,&a,&b)) return 0;
    uint32_t pm1a=rd32(f+64),pm1b=rd32(f+68);
    if (!pm1a || pm1a>0xffff || pm1b>0xffff || f[89]<2) return 0;
    /* Firmware may require ACPI enable before PM1 control is usable. */
    uint32_t smi=rd32(f+48);
    uint8_t enable=f[52];
    if (!(inw((uint16_t)pm1a)&SCI_EN) && smi && smi<=0xffff && enable) {
        outb((uint16_t)smi,enable);
        unsigned wait=1000000;
        while (wait-- && !(inw((uint16_t)pm1a)&SCI_EN)) __asm__ volatile("pause");
    }
    if (!(inw((uint16_t)pm1a)&SCI_EN)) return 0;
    __asm__ volatile("cli" ::: "memory");
    outw((uint16_t)pm1a,(uint16_t)(a|SLP_EN));
    if (pm1b) outw((uint16_t)pm1b,(uint16_t)(b|SLP_EN));
    /* If firmware does not power off, caller will report failure and halt. */
    for (volatile unsigned i=0;i<10000000;i++) __asm__ volatile("pause");
    return 0;
}
