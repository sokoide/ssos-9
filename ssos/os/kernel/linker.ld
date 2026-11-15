ENTRY(entry)

__ssosstack_size = SS_MAX_TASKS * SS_TASK_STACK;

MEMORY
{
	ROM (rx):		ORIGIN = 0x00010000, LENGTH = 192K
	RAM (rwx):		ORIGIN = 0x00040000, LENGTH = 80K
	SSOSRAM (rwx):	ORIGIN = 0x00150000, LENGTH = 10944K
}

SECTIONS
{
	_start = .;

	.text : {
		__text_start = .;
		*(.text)
		*(.rela.text)
		*(.rodata)
		__text_end = .;
	} > ROM
	__text_size = __text_end - __text_start;

	.data : {
		__data_start = .;
		*(.data)
		__data_end = .;
	} > RAM
	__data_size = __data_end - __data_start;

	.bss : {
		__bss_start = .;
		*(.bss)
		*(COMMON)
		__bss_end = .;
	} > RAM
	__bss_size = __bss_end - __bss_start;

	/* Heap marker required by newlib sbrk (pulled in by sprintf). */
	end = .;
	_end = .;

	.ssos : {
		. = ALIGN(4);
		__ssosstack_start = .;
		. = . + __ssosstack_size;
		__ssosstack_end = .;
	} > SSOSRAM
	__ssosram_start = __ssosstack_end;
	__ssosram_end = SS_RAM_END;
	__ssosram_size = __ssosram_end - __ssosram_start;
	ASSERT(__ssosram_start < __ssosram_end, "SSOS RAM arena is empty")
	ASSERT(__ssosram_end <= ORIGIN(SSOSRAM) + LENGTH(SSOSRAM),
	       "SSOS RAM end exceeds supported 12 MiB address range")
	/* The IPL (boot/main.s) reads 207 sectors from 0x010000, so the last
	 * byte it loads is 0x010000 + 207*1024 = 0x43c00.  .data lands at
	 * ORIGIN(RAM) with no separate load address, so anything past that
	 * limit would boot with a partially unloaded .data. */
	ASSERT(LOADADDR(.data) + SIZEOF(.data) <= 0x43c00,
	       "kernel .data exceeds IPL load span (207 sectors from 0x010000)")
}
