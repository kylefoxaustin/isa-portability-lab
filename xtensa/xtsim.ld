/*
 * xtsim.ld - dc233c bare-metal layout for `qemu-system-xtensa -M sim`.
 *
 * QEMU's xtensa `sim` machine (present but unlisted in -M help) gives:
 *   sysram0 : 0x00000000 .. 0x00ffffff   (16 MB, sized by -m)
 *   sysrom0 : 0xfe000000 .. 0xffffffff   (the reset region; writable in qemu)
 * On reset PC = 0xfe000000 and VECBASE = 0x00002000 (queried from the model).
 * The sim machine does NOT honor the ELF entry, so we must place code at those
 * fixed addresses ourselves - the job the Cadence sim LSP would normally do,
 * which the open Zephyr-SDK dc233c toolchain does not ship.
 *
 * picolibc's crt0-semihost.o supplies the reset handler (.reset.*) and the
 * window over/underflow vectors (.vectors); we just place them:
 *   .reset.entry (_start) -> exactly 0xfe000000   (reset PC)
 *   .vectors              -> exactly 0x00002000   (reset VECBASE)
 *   everything else       -> sysram
 * LMA == VMA: qemu -kernel preloads RAM, so _cstart's .data copy is a no-op.
 */
ENTRY(_start)

MEMORY
{
  reset (rwx) : ORIGIN = 0xfe000000, LENGTH = 0x400
  ram   (rwx) : ORIGIN = 0x00000000, LENGTH = 16M
}

SECTIONS
{
  /* Reset vector at 0xfe000000: entry stub FIRST (CPU resets here), then the
   * literal pool (l32r reads backward), then the reset handler body. */
  .reset 0xfe000000 : {
    KEEP (*(.reset.entry))
    . = ALIGN(4);
    KEEP (*(.reset.literal))
    KEEP (*(.reset.text))
  } > reset

  /* Window over/underflow (and exception) vectors at the reset VECBASE. */
  .vectors 0x00002000 : {
    KEEP (*(.vectors))
  } > ram

  . = 0x00010000;

  .text : {
    *(.literal .text .literal.* .text.*)
    *(.rodata .rodata.* .srodata .srodata.*)
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP (*(.init_array))
    PROVIDE_HIDDEN (__init_array_end = .);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(.fini_array))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } > ram

  .data : {
    __data_start = .;
    *(.data .data.* .sdata .sdata.*)
    . = ALIGN(4);
    __data_end = .;
  } > ram
  __data_source = __data_start;                 /* LMA == VMA -> copy is a no-op */
  __data_size   = __data_end - __data_start;

  .bss (NOLOAD) : {
    . = ALIGN(4);
    __bss_start = .;
    *(.dynbss .bss .bss.* .sbss .sbss.* COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > ram
  __bss_size = __bss_end - __bss_start;

  /* TLS: harness uses none, so an empty block keeps picolibc _set_tls happy. */
  __tdata_start = .; __tdata_end = .; __tdata_source = .; __tdata_size = 0;
  __tbss_start = .; __tbss_end = .; __tbss_size = 0; __tbss_offset = 0;
  __tls_base = .; __tls_size = 0; __tls_align = 4;
  PROVIDE (__arm32_tls_tcb_offset = 0);

  . = ALIGN(16);
  __heap_start = __bss_end;
  __heap_end   = 0x00e00000;
  __stack      = 0x00f00000;

  /DISCARD/ : { *(.note.* .comment .eh_frame .xtensa.info .xt.*) }
}
