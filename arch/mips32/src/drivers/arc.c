/*
 * Copyright (C) 2005 Ondrej Palkovsky
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <arch/drivers/arc.h>
#include <arch/mm/page.h>
#include <print.h>
#include <arch.h>
#include <arch/byteorder.h>
#include <arch/mm/frame.h>
#include <mm/frame.h>

/* This is a good joke, SGI HAS different types than NT bioses... */
/* Here is the SGI type */
static char *basetypes[] = {
	"ExceptionBlock",
	"SystemParameterBlock",
	"FreeContiguous",
	"FreeMemory",
	"BadMemory",
	"LoadedProgram",
	"FirmwareTemporary",
	"FirmwarePermanent"
};

static char *ctypes[] = {
	"ARC_type",
	"CPU_type",
	"FPU_type",
	"PrimaryICache",
	"PrimaryDCache",
	"SecondaryICache",
	"SecondaryDCache",
	"SecondaryCache",
	"Memory",
	"EISAAdapter",
	"TCAdapter",
	"SCSIAdapter",
	"DTIAdapter",
	"MultiFunctionAdapter",
	"DiskController",
	"TapeController",
	"CDROMController",
	"WORMController",
	"SerialController",
	"NetworkController",
	"DisplayController",
	"ParallelController",
	"PointerController",
	"KeyboardController",
	"AudioController",
	"OtherController",
	"DiskPeripheral",
	"FloppyDiskPeripheral",
	"TapePeripheral",
	"ModemPeripheral",
	"MonitorPeripheral",
	"PrinterPeripheral",
	"PointerPeripheral",
	"KeyboardPeripheral",
	"TerminalPeripheral",
	"OtherPeripheral",
	"LinePeripheral",
	"NetworkPeripheral"
	"OtherPeripheral",
	"XTalkAdapter",
	"PCIAdapter",
	"GIOAdapter",
	"TPUAdapter",
	"Anonymous"
};

static arc_sbp *sbp = (arc_sbp *)PA2KA(0x1000);
static arc_func_vector_t *arc_entry; 

static void _arc_putchar(char ch);

/** Initialize ARC structure
 *
 * @return 0 - ARC OK, -1 - ARC does not exist
 */
int arc_init(void)
{
	if (sbp->signature != ARC_MAGIC) {
		sbp = NULL;
		return -1;
	}
	arc_entry = sbp->firmwarevector;

	arc_putchar('A');
	arc_putchar('R');
	arc_putchar('C');
	arc_putchar('\n');
}

/** Return true if ARC is available */
int arc_enabled(void)
{
	return sbp != NULL;
}

static void arc_print_component(arc_component *c)
{
	int i;

	printf("%s: ",ctypes[c->type]);
	for (i=0;i < c->identifier_len;i++)
		putchar(c->identifier[i]);
	putchar('\n');
}

void arc_print_devices(void)
{
	arc_component *c,*next;

	if (!arc_enabled())
		return;

	c = arc_entry->getchild(NULL);
	while (c) {
		arc_print_component(c);
		next = arc_entry->getchild(c);
		while (!next) {
			next = arc_entry->getpeer(c);
			if (!next)
				c = arc_entry->getparent(c);
			if (!c)
				return;
		}
		c = next;
	}
}

void arc_print_memory_map(void)
{
	arc_memdescriptor_t *desc;

	if (!arc_enabled())
		return;

	printf("Memory map:\n");

	desc = arc_entry->getmemorydescriptor(NULL);
	while (desc) {
		printf("%s: %d (size: %dKB)\n",basetypes[desc->type],
		       desc->basepage * ARC_FRAME,
		       desc->basecount*ARC_FRAME/1024);
		desc = arc_entry->getmemorydescriptor(desc);
	}
}

/** Print charactor to console */
void arc_putchar(char ch)
{
	__u32 cnt;
	ipl_t ipl;

	/* TODO: Should be spinlock? */
	ipl = interrupts_disable();
	arc_entry->write(1, &ch, 1, &cnt);
	interrupts_restore(ipl);
	
}

/** Try to get character, return character or -1 if not available */
int arc_getchar(void)
{
	char ch;
	__u32 count;
	long result;

	if (arc_entry->getreadstatus(0))
		return -1;
	result = arc_entry->read(0, &ch, 1, &count);
	if (result || count!=1) {
		cpu_halt();
		return -1;
	}
	if (ch == '\r')
		return '\n';
	return ch;
}

/* Initialize frame zones from ARC firmware. 
 * In the future we may use even the FirmwareTemporary regions,
 * currently we use the FreeMemory (what about the LoadedProgram?)
 */
void arc_frame_init(void)
{
	arc_memdescriptor_t *desc;
	int total = 0;

	desc = arc_entry->getmemorydescriptor(NULL);
	while (desc) {
		if (desc->type == FreeMemory ||
		    desc->type == FreeContiguous) {
			total += desc->basecount*ARC_FRAME;
			zone_create_in_region(desc->basepage*ARC_FRAME,
					      desc->basecount*ARC_FRAME);
		}
		desc = arc_entry->getmemorydescriptor(desc);
	}

	config.memory_size = total;
}

