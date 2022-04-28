/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2019, NEC Europe Ltd., NEC Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * THIS HEADER MAY NOT BE EXTRACTED OR MODIFIED IN ANY WAY.
 */

#ifndef KVM_VMMINFO_H
#error Do not include _bootparams.h directly!
#endif

#include <string.h>
#include <uk/print.h>
#include <uk/plat/config.h>
#include <kvm/config.h>

#define BOOT_PARAM_BASE (0x7000)

static inline void _bp_get_cmdline(struct boot_params *bp)
{
        __u64 cmdline_addr;
        char *bp_cmdline;
        size_t bp_cmdline_len = bp->hdr.cmdline_size;

        cmdline_addr = bp->hdr.cmd_line_ptr;
        cmdline_addr |= (__u64)bp->ext_ramdisk_size << 32;
        bp_cmdline = (char *)cmdline_addr;
        uk_pr_info("command line at 0x%lx\n", cmdline_addr);
        uk_pr_info("command line size 0x%lx\n", bp_cmdline_len);

        if (!bp_cmdline) {
                uk_pr_info("No command line provided\n");
                strncpy(cmdline, CONFIG_UK_NAME, sizeof(cmdline));
                return;
        }

        if (bp_cmdline_len >= sizeof(cmdline)) {
                bp_cmdline_len = sizeof(cmdline) - 1;
                uk_pr_info("Command line too long, truncated\n");
        }
        memcpy(cmdline, bp_cmdline, bp_cmdline_len);
        /* ensure null termination */
        cmdline[bp_cmdline_len] = '\0';

        uk_pr_info("Command line: %s\n", cmdline);
}

static inline void _bp_init_mem(struct boot_params *bp)
{
        int i;
        size_t max_addr;
        struct boot_e820_entry *e820_entry = NULL;

        uk_pr_info("boot_params: %d entries in e820\n", bp->e820_entries);
        for (i=0; i < bp->e820_entries; i++) {
                uk_pr_info("  e820 entry %d:\n", i);
                uk_pr_info("    addr: 0x%lx\n", bp->e820_table[i].addr);
                uk_pr_info("    size: 0x%lx\n", bp->e820_table[i].size);
                uk_pr_info("    type: 0x%x\n", bp->e820_table[i].type);
        }

        for (i = 0; i < bp->e820_entries; i++) {
                uk_pr_info("Checking e820 entry %d\n", i);
                if (bp->e820_table[i].addr == PLATFORM_MEM_START
                    && bp->e820_table[i].type == 0x1) {
                        e820_entry = &bp->e820_table[i];
                        break;
                }
        }
        if (!e820_entry)
                UK_CRASH("Could not find suitable memory region!\n");

        uk_pr_info("Using e820 memory region %d\n", i);
        max_addr = e820_entry->addr + e820_entry->size;
        if (max_addr > PLATFORM_MAX_MEM_ADDR)
                max_addr = PLATFORM_MAX_MEM_ADDR;
        UK_ASSERT((size_t)__END <= max_addr);

        _libkvmplat_cfg.heap.start = ALIGN_UP((uintptr_t)__END, __PAGE_SIZE);
        _libkvmplat_cfg.heap.end   = (uintptr_t) max_addr - __STACK_SIZE;
        _libkvmplat_cfg.heap.len   = _libkvmplat_cfg.heap.end
                                     - _libkvmplat_cfg.heap.start;
        _libkvmplat_cfg.bstack.start = _libkvmplat_cfg.heap.end;
        _libkvmplat_cfg.bstack.end   = max_addr;
        _libkvmplat_cfg.bstack.len   = __STACK_SIZE;
}

static inline void _bp_init_initrd(struct boot_params *bp)
{
	uintptr_t heap0_start, heap0_end;
	uintptr_t heap1_start, heap1_end;
	size_t    heap0_len,   heap1_len;

	if (bp->hdr.ramdisk_size == 0 || !bp->hdr.ramdisk_image) {
		uk_pr_debug("No initrd present or initrd is empty\n");
		goto no_initrd;
	}

	_libkvmplat_cfg.initrd.start = (uintptr_t) bp->hdr.ramdisk_image;
	_libkvmplat_cfg.initrd.len = (size_t) bp->hdr.ramdisk_size;
	_libkvmplat_cfg.initrd.end = _libkvmplat_cfg.initrd.start
		+ _libkvmplat_cfg.initrd.len;

	/*
	 * Check if initrd is part of heap
	 * In such a case, we figure out the remaining pieces as heap
	 */
	if (_libkvmplat_cfg.heap.len == 0) {
		/* We do not have a heap */
		goto out;
	}
	heap0_start = 0;
	heap0_end   = 0;
	heap1_start = 0;
	heap1_end   = 0;
	if (RANGE_OVERLAP(_libkvmplat_cfg.heap.start,
			  _libkvmplat_cfg.heap.len,
			  _libkvmplat_cfg.initrd.start,
			  _libkvmplat_cfg.initrd.len)) {
		if (IN_RANGE(_libkvmplat_cfg.initrd.start,
			     _libkvmplat_cfg.heap.start,
			     _libkvmplat_cfg.heap.len)) {
			/* Start of initrd within heap range;
			 * Use the prepending left piece as heap */
			heap0_start = _libkvmplat_cfg.heap.start;
			heap0_end   = ALIGN_DOWN(_libkvmplat_cfg.initrd.start,
						 __PAGE_SIZE);
		}
		if (IN_RANGE(_libkvmplat_cfg.initrd.start,

			     _libkvmplat_cfg.heap.start,
			     _libkvmplat_cfg.heap.len)) {
			/* End of initrd within heap range;
			 * Use the remaining left piece as heap */
			heap1_start = ALIGN_UP(_libkvmplat_cfg.initrd.end,
					       __PAGE_SIZE);
			heap1_end   = _libkvmplat_cfg.heap.end;
		}
	} else {
		/* Initrd is not overlapping with heap */
		heap0_start = _libkvmplat_cfg.heap.start;
		heap0_end   = _libkvmplat_cfg.heap.end;
	}
	heap0_len = heap0_end - heap0_start;
	heap1_len = heap1_end - heap1_start;

	/*
	 * Update heap regions
	 * We make sure that in we start filling left heap pieces at
	 * `_libkvmplat_cfg.heap`. Any additional piece will then be
	 * placed to `_libkvmplat_cfg.heap2`.
	 */
	if (heap0_len == 0) {
		/* Heap piece 0 is empty, use piece 1 as only */
		if (heap1_len != 0) {
			_libkvmplat_cfg.heap.start = heap1_start;
			_libkvmplat_cfg.heap.end   = heap1_end;
			_libkvmplat_cfg.heap.len   = heap1_len;
		} else {
			_libkvmplat_cfg.heap.start = 0;
			_libkvmplat_cfg.heap.end   = 0;
			_libkvmplat_cfg.heap.len   = 0;
		}
		 _libkvmplat_cfg.heap2.start = 0;
		 _libkvmplat_cfg.heap2.end   = 0;
		 _libkvmplat_cfg.heap2.len   = 0;
	} else {
		/* Heap piece 0 has memory */
		_libkvmplat_cfg.heap.start = heap0_start;
		_libkvmplat_cfg.heap.end   = heap0_end;
		_libkvmplat_cfg.heap.len   = heap0_len;
		if (heap1_len != 0) {
			_libkvmplat_cfg.heap2.start = heap1_start;
			_libkvmplat_cfg.heap2.end   = heap1_end;
			_libkvmplat_cfg.heap2.len   = heap1_len;
		} else {
			_libkvmplat_cfg.heap2.start = 0;
			_libkvmplat_cfg.heap2.end   = 0;
			_libkvmplat_cfg.heap2.len   = 0;
		}
	}

	/*
	 * Double-check that initrd is not overlapping with previously allocated
	 * boot stack. We crash in such a case because we assume that multiboot
	 * places the initrd close to the beginning of the heap region. One need
	 * to assign just more memory in order to avoid this crash.
	 */
	if (RANGE_OVERLAP(_libkvmplat_cfg.heap.start,
			  _libkvmplat_cfg.heap.len,
			  _libkvmplat_cfg.initrd.start,
			  _libkvmplat_cfg.initrd.len))
		UK_CRASH("Not enough space at end of memory for boot stack\n");
out:
	return;

no_initrd:
	_libkvmplat_cfg.initrd.start = 0;
	_libkvmplat_cfg.initrd.end   = 0;
	_libkvmplat_cfg.initrd.len   = 0;
	_libkvmplat_cfg.heap2.start  = 0;
	_libkvmplat_cfg.heap2.end    = 0;
	_libkvmplat_cfg.heap2.len    = 0;
	return;
}

static inline void process_vmminfo(void *arg __unused)
{
        /* Location of boot parameters is currently hardcoded to 0x7000
         * in Firecracker, but this might change at later point.
         */
        struct boot_params *bp = (struct boot_params *)BOOT_PARAM_BASE;

        uk_pr_info("     boot params: %p\n", bp);
        _bp_init_mem(bp);
        _bp_get_cmdline(bp);
        _bp_init_initrd(bp);
}

