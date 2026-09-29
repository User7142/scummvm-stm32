/* Linkerskript für das Spielprogramm (MONKEY.BIN) im SDRAM.
 *
 * Der Lader im Flash richtet Takt, SDRAM und MPU ein, kopiert dieses Abbild
 * unverändert nach IMAGE und springt über dessen Vektortabelle hinein. Ein
 * Abbild, das so geladen wird, braucht keine getrennte Ladeadresse für .data.
 *
 * SDRAM (8 MB ab 0xD0000000):
 *   0xD0000000  Bildspeicher L8, 240×320 Byte          (Symbol __fb_l8)
 *   0xD0013000  Bildspeicher RGB565, 240×320×2 Byte    (Symbol __fb_rgb565)
 *   0xD0040000  Abbild: Vektoren, Code, Daten, BSS
 *   danach      Heap bis zum Ende des SDRAM
 * Stack und die Kopie der Vektortabelle liegen im internen SRAM: VTOR darf
 * beim Cortex-M4 nicht in den Bereich 0xD… zeigen, und der Stack ist dort
 * schneller.
 */

ENTRY(Reset_Handler)

MEMORY
{
  IMAGE (rwx) : ORIGIN = 0xD0040000, LENGTH = 0x7C0000
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 192K
  CCM   (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
}

__fb_l8 = 0xD0000000;
__fb_rgb565 = 0xD0013000;
__sdram_end = 0xD0800000;
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  /* Kopfdaten für den Lader: Vektortabelle ganz vorne (SP, Reset_Handler). */
  .isr_vector :
  {
    . = ALIGN(512);
    __image_start = .;
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >IMAGE

  /* Heißer Pfad der Tonausgabe: läuft im internen SRAM statt im SDRAM. Aus
   * dem SDRAM kommt jeder Befehl ungecacht über den 16-Bit-Bus – für die
   * OPL-Emulation im Audio-Interrupt zu langsam (am Board gemessen: sie
   * ließ dem Spiel kaum Rechenzeit). Ladeadresse im Abbild, runtime.c
   * kopiert vor den Konstruktoren (.preinit_array). Steht vor .text, damit
   * dessen *(.text*) diese Objekte nicht zuerst einsammelt. */
  .ram_text :
  {
    . = ALIGN(4);
    __ram_text_start = .;
    *libaudio.a:dbopl.o(.text .text*)
    *libaudio.a:dosbox.o(.text .text*)
    *libaudio.a:chip.o(.text .text*)
    *libaudio.a:fmopl.o(.text .text*)
    *libaudio.a:rate.o(.text .text*)
    *libaudio.a:mixer.o(.text .text*)
    *libaudio.a:adlib.o(.text .text*)
    *board/audio.o(.text .text*)
    *scummvm/stm32-mixer.o(.text .text*)
    . = ALIGN(4);
    __ram_text_end = .;
  } >RAM AT>IMAGE
  __ram_text_load = LOADADDR(.ram_text);

  /* Tabellen der OPL-Emulation (DBOPL, statisch) im SRAM, aus demselben
   * Grund wie .ram_text; steht vor .bss, damit dessen *(.bss*) sie nicht
   * zuerst einsammelt; runtime.c nullt den Bereich vor den Konstruktoren. */
  .ram_bss (NOLOAD) :
  {
    . = ALIGN(4);
    __ram_bss_start = .;
    *libaudio.a:dbopl.o(.bss .bss*)
    . = ALIGN(4);
    __ram_bss_end = .;
  } >RAM

  .text :
  {
    *(.text) *(.text*)
    *(.glue_7) *(.glue_7t) *(.eh_frame)
    KEEP (*(.init)) KEEP (*(.fini))
    . = ALIGN(4);
  } >IMAGE

  .rodata : { *(.rodata) *(.rodata*) . = ALIGN(4); } >IMAGE
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >IMAGE
  .ARM : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } >IMAGE

  .preinit_array : { PROVIDE_HIDDEN (__preinit_array_start = .); KEEP (*(.preinit_array*)) PROVIDE_HIDDEN (__preinit_array_end = .); } >IMAGE
  .init_array : { PROVIDE_HIDDEN (__init_array_start = .); KEEP (*(SORT(.init_array.*))) KEEP (*(.init_array*)) PROVIDE_HIDDEN (__init_array_end = .); } >IMAGE
  .fini_array : { PROVIDE_HIDDEN (__fini_array_start = .); KEEP (*(SORT(.fini_array.*))) KEEP (*(.fini_array*)) PROVIDE_HIDDEN (__fini_array_end = .); } >IMAGE

  /* Ladeadresse = Laufadresse: Die Kopierschleife des Startcodes läuft ins Leere. */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data) *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >IMAGE
  _sidata = _sdata;
  __image_end = .;

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss) *(.bss*) *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >IMAGE

  /* Heap für malloc (_sbrk): vom Ende des BSS bis zum Ende des SDRAM. */
  . = ALIGN(8);
  __heap_start = .;
  PROVIDE(end = .);

  /* Schneller interner RAM: Vektortabelle (Kopie), Logpuffer, Stack. */
  .ram_data (NOLOAD) :
  {
    . = ALIGN(512);
    *(.ram_vectors)
    *(.ram_data*)
    . = ALIGN(8);
  } >RAM

  /* Rest des SRAM ist der Interrupt-Stack (MSP; nach dem Start von FreeRTOS
   * nur noch für Interrupts, die Tasks haben eigene Stacks in .ram_data). */
  ASSERT(_estack - ADDR(.ram_data) - SIZEOF(.ram_data) >= 8K, "SRAM: weniger als 8 KB fuer den Interrupt-Stack")

  /DISCARD/ : { libc.a ( * ) libm.a ( * ) libgcc.a ( * ) }
  .ARM.attributes 0 : { *(.ARM.attributes) }
}
