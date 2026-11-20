#ifndef IO_H
#define IO_H

#include "types.h"

/* --- Port I/O --- */
static inline uint8_t inb(uint16_t port)
{
    uint8_t r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t r;
    __asm__ volatile ("inw %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t r;
    __asm__ volatile ("inl %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline void insl(uint16_t port, void* buf, size_t count)
{
    __asm__ volatile ("cld; rep insl"
                      : "+D"(buf), "+c"(count)
                      : "d"(port)
                      : "memory");
}

static inline void outsl(uint16_t port, const void* buf, size_t count)
{
    __asm__ volatile ("cld; rep outsl"
                      : "+S"(buf), "+c"(count)
                      : "d"(port)
                      : "memory");
}

static inline void io_wait(void)
{
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

/* --- Descriptor table loads --- */
static inline void lgdt(void* ptr)
{
    __asm__ volatile ("lgdt (%0)" : : "r"(ptr) : "memory");
}

static inline void lidt(void* ptr)
{
    __asm__ volatile ("lidt (%0)" : : "r"(ptr) : "memory");
}

static inline void ltr(uint16_t sel)
{
    __asm__ volatile ("ltr %0" : : "r"(sel));
}

/* --- Control registers --- */
static inline uintptr_t read_cr0(void)
{
    uintptr_t r;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(r));
    return r;
}

static inline void write_cr0(uintptr_t v)
{
    __asm__ volatile ("mov %0, %%cr0" : : "r"(v) : "memory");
}

static inline uintptr_t read_cr2(void)
{
    uintptr_t r;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(r));
    return r;
}

static inline uintptr_t read_cr3(void)
{
    uintptr_t r;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(r));
    return r;
}

static inline void write_cr3(uintptr_t pml4_phys)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

static inline uintptr_t read_cr4(void)
{
    uintptr_t r;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(r));
    return r;
}

static inline void write_cr4(uintptr_t v)
{
    __asm__ volatile ("mov %0, %%cr4" : : "r"(v) : "memory");
}

/* --- Interrupt flag / halt --- */
static inline void cli(void) { __asm__ volatile ("cli"); }
static inline void sti(void) { __asm__ volatile ("sti"); }
static inline void hlt(void) { __asm__ volatile ("hlt"); }

static inline uint64_t read_flags(void)
{
    uint64_t r;
    __asm__ volatile ("pushfq; pop %0" : "=r"(r));
    return r;
}

static inline void invlpg(uintptr_t addr)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(addr) : "memory");
}

/* --- Model-specific registers --- */
static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val)
{
    uint32_t lo = (uint32_t)(val & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile ("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

/* --- Time-stamp counter --- */
static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

#endif /* IO_H */
