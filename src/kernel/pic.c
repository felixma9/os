#include "pic.h"

#define PIC1_CMD    0x20
#define PIC1_DATA   0x21
#define PIC2_CMD    0xA0
#define PIC2_DATA   0xA1

#define PIC_EOI     0x20

#define ICW1_INIT   0x11    // start initialization sequence, expect ICW4
#define ICW4_8086   0x01    // 8086 mode (not MCS-80)

static void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void io_wait()
{
    // Writing to an unused port burns a few cycles — gives older hardware
    // time to process the previous command before the next one arrives.
    outb(0x80, 0);
}

void pic_init()
{
    // Save masks so we can restore them after remapping
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    // ICW1: start initialization
    outb(PIC1_CMD, ICW1_INIT); io_wait();
    outb(PIC2_CMD, ICW1_INIT); io_wait();

    // ICW2: vector offsets — IRQ0-7 → vectors 32-39, IRQ8-15 → vectors 40-47
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();

    // ICW3: tell master/slave how they're wired together
    outb(PIC1_DATA, 0x04); io_wait(); // master: slave on IRQ2 (bit mask)
    outb(PIC2_DATA, 0x02); io_wait(); // slave: its cascade identity is IRQ2

    // ICW4: 8086 mode
    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();

    // Restore saved masks
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

// Called at the end of every hardware IRQ handler to tell the PIC the
// interrupt has been handled and it can send the next one.
void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}
