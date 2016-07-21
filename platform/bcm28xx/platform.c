/*
 * Copyright (c) 2015 Travis Geiselbrecht
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <reg.h>
#include <err.h>
#include <debug.h>
#include <trace.h>

#include <dev/uart.h>
#include <arch.h>
#include <lk/init.h>
#include <kernel/vm.h>
#include <kernel/spinlock.h>
#include <dev/timer/arm_generic.h>
#include <dev/display.h>

#include <platform.h>
#include <platform/interrupts.h>
#include <platform/bcm28xx.h>
#include <platform/videocore.h>

#if BCM2836
#include <arch/arm.h>
#include <arch/arm/mmu.h>

/* initial memory mappings. parsed by start.S */
struct mmu_initial_mapping mmu_initial_mappings[] = {
    /* 1GB of sdram space */
    {
        .phys = SDRAM_BASE,
        .virt = KERNEL_BASE,
        .size = MEMSIZE,
        .flags = 0,
        .name = "memory"
    },

    /* peripherals */
    {
        .phys = BCM_PERIPH_BASE_PHYS,
        .virt = BCM_PERIPH_BASE_VIRT,
        .size = BCM_PERIPH_SIZE,
        .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
        .name = "bcm peripherals"
    },

    /* identity map to let the boot code run */
    {
        .phys = SDRAM_BASE,
        .virt = SDRAM_BASE,
        .size = 16*1024*1024,
        .flags = MMU_INITIAL_MAPPING_TEMPORARY
    },
    /* null entry to terminate the list */
    { 0 }
};

#define DEBUG_UART 0

#elif BCM2837
#include <libfdt.h>
#include <arch/arm64.h>
#include <arch/arm64/mmu.h>

/* initial memory mappings. parsed by start.S */
struct mmu_initial_mapping mmu_initial_mappings[] = {
 /* 1GB of sdram space */
 {
     .phys = SDRAM_BASE,
     .virt = KERNEL_BASE,
     .size = MEMORY_APERTURE_SIZE,
     .flags = 0,
     .name = "memory"
 },

 /* peripherals */
 {
     .phys = BCM_PERIPH_BASE_PHYS,
     .virt = BCM_PERIPH_BASE_VIRT,
     .size = BCM_PERIPH_SIZE,
     .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
     .name = "bcm peripherals"
 },

 /* null entry to terminate the list */
 { 0 }
};

#define DEBUG_UART 1

#else
#error Unknown BCM28XX Variant
#endif

extern void intc_init(void);
extern void arm_reset(void);

static fb_mbox_t framebuff_descriptor __ALIGNED(64);

static uint8_t * vbuff;

static pmm_arena_t arena = {
    .name = "sdram",
    .base = SDRAM_BASE,
    .size = MEMSIZE,
    .flags = PMM_ARENA_FLAG_KMAP,
};

void platform_init_mmu_mappings(void)
{
}

void platform_early_init(void)
{
    uart_init_early();

    intc_init();

#if BCM2837
    arm_generic_timer_init(INTERRUPT_ARM_LOCAL_CNTPNSIRQ, 0);

   /* look for a flattened device tree just before the kernel */
    const void *fdt = (void *)KERNEL_BASE;
    int err = fdt_check_header(fdt);
    if (err >= 0) {
        /* walk the nodes, looking for 'memory' */
        int depth = 0;
        int offset = 0;
        for (;;) {
            offset = fdt_next_node(fdt, offset, &depth);
            if (offset < 0)
                break;

            /* get the name */
            const char *name = fdt_get_name(fdt, offset, NULL);
            if (!name)
                continue;

            /* look for the 'memory' property */
            if (strcmp(name, "memory") == 0) {
                printf("Found memory in fdt\n");
                int lenp;
                const void *prop_ptr = fdt_getprop(fdt, offset, "reg", &lenp);
                if (prop_ptr && lenp == 0x10) {
                    /* we're looking at a memory descriptor */
                    //uint64_t base = fdt64_to_cpu(*(uint64_t *)prop_ptr);
                    uint64_t len = fdt64_to_cpu(*((const uint64_t *)prop_ptr + 1));

                    /* trim size on certain platforms */
#if ARCH_ARM
                    if (len > 1024*1024*1024U) {
                        len = 1024*1024*1024; /* only use the first 1GB on ARM32 */
                        printf("trimming memory to 1GB\n");
                    }
#endif

                    /* set the size in the pmm arena */
                    arena.size = len;
                }
            }
        }
    }

#elif BCM2836
    arm_generic_timer_init(INTERRUPT_ARM_LOCAL_CNTPNSIRQ, 1000000);
#else
#error Unknown BCM28XX Variant
#endif

    /* add the main memory arena */
    pmm_add_arena(&arena);

#if BCM2837
    /* reserve the first 64k of ram, which should be holding the fdt */
    struct list_node list = LIST_INITIAL_VALUE(list);
    pmm_alloc_range(MEMBASE, 0x80000 / PAGE_SIZE, &list);
#endif

#if WITH_SMP
#if BCM2837
    uintptr_t sec_entry = &arm_reset - KERNEL_ASPACE_BASE;
    unsigned long long *spin_table = (void *)(KERNEL_ASPACE_BASE + 0xd8);

    for (uint i = 1; i <= 3; i++) {
    //    spin_table[i] = sec_entry;
 //       __asm__ __volatile__ ("" : : : "memory");
        arch_clean_cache_range(0xffff000000000000,256);
        __asm__ __volatile__("sev");
    }
#else
    /* start the other cpus */
    uintptr_t sec_entry = (uintptr_t)&arm_reset;
    sec_entry -= (KERNEL_BASE - MEMBASE);
    for (uint i = 1; i <= 3; i++) {
        *REG32(ARM_LOCAL_BASE + 0x8c + 0x10 * i) = sec_entry;
    }
#endif
#endif
}



void platform_init(void)
{
    uart_init();

    /* Get framebuffer for jraphics */

    framebuff_descriptor.phys_width  = 1920;
    framebuff_descriptor.phys_height = 1080;
    framebuff_descriptor.virt_width  = 1920;
    framebuff_descriptor.virt_height = 1080;
    framebuff_descriptor.pitch       = 0;
    framebuff_descriptor.depth       = 32;
    framebuff_descriptor.virt_x_offs = 0;
    framebuff_descriptor.virt_y_offs = 0;
    framebuff_descriptor.fb_p        = 0;
    framebuff_descriptor.fb_size     = 0;

    if (!get_vcore_framebuffer(&framebuff_descriptor)) {
        printf ("fb returned at 0x%08x of %d bytes in size\n",framebuff_descriptor.fb_p
                                                             ,framebuff_descriptor.fb_size);

        vbuff = (uint8_t *)((framebuff_descriptor.fb_p & 0x3fffffff) + KERNEL_BASE);
        printf("video buffer at %llx\n",vbuff);
        printf("pitch: %d\n",framebuff_descriptor.pitch);
    }
}



void target_init(void)
{
 
    uint32_t * temp;

    
    uint32_t addr;

    temp = (uint32_t *)get_vcore_single(0x00010005,8,8);
    if (temp) printf ("ARM memory base:0x%08x len:0x%08x\n",temp[0],temp[1]);
 
    temp = (uint32_t *)get_vcore_single(0x00010006,8,8);
    if (temp) printf ("VC  memory base:0x%08x len:0x%08x\n",temp[0],temp[1]);

    temp = (uint32_t *)get_vcore_single(0x00010004,8,8);
    if (temp) printf ("SERIAL # %08x%08x\n",temp[1],temp[0]);
 
}

static void flush(void){
    arch_clean_cache_range(vbuff,framebuff_descriptor.fb_size);
}

status_t display_get_framebuffer(struct display_framebuffer *fb)
{
    fb->image.pixels = (void *)vbuff;

    fb->format = DISPLAY_FORMAT_ARGB_8888;
    fb->image.format = IMAGE_FORMAT_ARGB_8888;
    fb->image.rowbytes = framebuff_descriptor.phys_width * 4;

    fb->image.width = framebuff_descriptor.phys_width;
    fb->image.height = framebuff_descriptor.phys_height;
    fb->image.stride = framebuff_descriptor.phys_width;
    fb->flush = flush;

    return NO_ERROR;
}

status_t display_get_info(struct display_info *info)
{
    info->format = DISPLAY_FORMAT_ARGB_8888;
    info->width = framebuff_descriptor.phys_width;
    info->height = framebuff_descriptor.phys_height;

    /*if (ltdc_handle.LayerCfg[active_layer].PixelFormat == LTDC_PIXEL_FORMAT_ARGB8888) {
        info->format = DISPLAY_FORMAT_ARGB_8888;
    } else if (ltdc_handle.LayerCfg[active_layer].PixelFormat == LTDC_PIXEL_FORMAT_RGB565) {
        info->format = DISPLAY_FORMAT_RGB_565;
    } else {
        panic("unhandled pixel format\n");
        return ERR_NOT_FOUND;
    }

    info->width = BSP_LCD_GetXSize();
    info->height = BSP_LCD_GetYSize();
*/
    return NO_ERROR;
}

status_t display_present(struct display_image *image, uint starty, uint endy)
{
  TRACEF("display_present - not implemented");
  DEBUG_ASSERT(false);
  return NO_ERROR;
}


void platform_dputc(char c)
{
    if (c == '\n')
        uart_putc(DEBUG_UART, '\r');
    uart_putc(DEBUG_UART, c);
}

int platform_dgetc(char *c, bool wait)
{
    int ret = uart_getc(DEBUG_UART, wait);
    if (ret == -1)
        return -1;
    *c = ret;
    return 0;
}

