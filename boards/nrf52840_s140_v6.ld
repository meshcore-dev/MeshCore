/* Linker script to configure memory regions. */

SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)

MEMORY
{
  FLASH (rx)     : ORIGIN = 0x26000, LENGTH = 0xED000 - 0x26000

  /* To keep data in RAM across resets */
  PERSISTENT_RAM (rwx) : ORIGIN = 0x20006000, LENGTH = 8

  /* SRAM required by Softdevice depend on
   * - Attribute Table Size (Number of Services and Characteristics)
   * - Vendor UUID count
   * - Max ATT MTU
   * - Concurrent connection peripheral + central + secure links
   * - Event Len, HVN queue, Write CMD queue
   */ 
  RAM (rwx) :  ORIGIN = 0x20006000 + 8, LENGTH = 0x20040000 - 0x20006000 - 8
}

SECTIONS
{
  . = ALIGN(4);
  .persistent (NOLOAD) :
  {
    KEEP(*(.persistent_magic))
    KEEP(*(.persistent_data))
    . = ALIGN(4);
  } > PERSISTENT_RAM

  . = ALIGN(4);
  .svc_data :
  {
    PROVIDE(__start_svc_data = .);
    KEEP(*(.svc_data))
    PROVIDE(__stop_svc_data = .);
  } > RAM
  
  .fs_data :
  {
    PROVIDE(__start_fs_data = .);
    KEEP(*(.fs_data))
    PROVIDE(__stop_fs_data = .);
  } > RAM
}

INCLUDE "nrf52_common.ld"
