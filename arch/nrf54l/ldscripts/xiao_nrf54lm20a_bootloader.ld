/*
 * Based on nrf54lm20b_linker_script.ld from lolren's nrf54-arduino-core 1.0.20, with the memory map
 * moved around andyshinn's nRF54_Bootloader. The bootloader owns 0x0-0x8000, its settings page at
 * 0x1D1000 and the s145 SoftDevice at 0x1DA800-0x1FC57C, and keeps its DFU handoff in RAM at
 * 0x2003FF80-0x20040000.
 *
 * App:                     0x008000-0x149000
 * InternalFS:              0x149000-0x1C9000 (see InternalFileSystem.h)
 * Core persistence pages:  0x1C9000-0x1CC000 (moved down from the SoftDevice's range)
 * Heap and stack go in RAM above the DFU handoff.
 *
 * Licensed under the Apache License 2.0
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)       : ORIGIN = 0x00008000, LENGTH = 0x141000
    FLASH_ZIGBEE (r) : ORIGIN = 0x001C9000, LENGTH = 0x1000
    FLASH_BOND_DB (r) : ORIGIN = 0x001CA000, LENGTH = 0x1000
    FLASH_BOND (r)   : ORIGIN = 0x001CB000, LENGTH = 0x1000
    RAM (rwx)        : ORIGIN = 0x20000000, LENGTH = 0x3FF80
    RAM_HEAP (rwx)   : ORIGIN = 0x20040000, LENGTH = 0x3FD40
}

__stack_size__ = 4K;

/* Only nRF54LM20-compiled objects may satisfy this link. */
PROVIDE(__nrf54_core_target_nrf54lm20 = 0x4C4D3230);

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP (*(.init))
        KEEP (*(.fini))
        . = ALIGN(4);
        _etext = .;
    } >FLASH

    .preinit_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
        . = ALIGN(4);
    } >FLASH

    .init_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
        . = ALIGN(4);
    } >FLASH

    .fini_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
        . = ALIGN(4);
    } >FLASH

    .rodata :
    {
        . = ALIGN(4);
        KEEP(*(.gnu.linkonce.r.nrf54_core_target_guard.*))
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    .zigbee_storage ORIGIN(FLASH_ZIGBEE) (NOLOAD) :
    {
        . = ALIGN(16);
        _szigbee_storage = .;
        KEEP(*(.zigbee_storage))
        KEEP(*(.zigbee_storage*))
        . = ALIGN(16);
        _ezigbee_storage = .;
    } >FLASH_ZIGBEE
    ASSERT(SIZEOF(.zigbee_storage) <= 0x1000,
           "Zigbee persistence exceeds its dedicated 4 KB page")

    .bond_db_storage ORIGIN(FLASH_BOND_DB) (NOLOAD) :
    {
        . = ALIGN(16);
        _sbond_db_storage = .;
        KEEP(*(.bond_db_storage))
        . = ALIGN(16);
        _ebond_db_storage = .;
    } >FLASH_BOND_DB
    ASSERT(SIZEOF(.bond_db_storage) <= 0x1000,
           "BLE multibond database exceeds its dedicated 4 KB page")

    .bond_storage ORIGIN(FLASH_BOND) (NOLOAD) :
    {
        . = ALIGN(4);
        _sbond_storage = .;
        KEEP(*(.bond_base_storage))
        . = ALIGN(4);
    } >FLASH_BOND
    ASSERT(SIZEOF(.bond_storage) <= 0x50,
           "BLE base bond storage exceeds its stable 80-byte slot")

    .bond_cccd_storage ORIGIN(FLASH_BOND) + 0x50 (NOLOAD) :
    {
        . = ALIGN(4);
        KEEP(*(.bond_cccd_storage))
        . = ALIGN(4);
        _ebond_storage = .;
    } >FLASH_BOND
    ASSERT(SIZEOF(.bond_cccd_storage) <= 0x8C,
           "BLE CCCD storage exceeds its stable 140-byte slot")

    .prefs_storage ORIGIN(FLASH_BOND) + 0xDC (NOLOAD) :
    {
        . = ALIGN(4);
        _sprefs_storage = .;
        KEEP(*(.prefs_storage))
        KEEP(*(.prefs_storage*))
        . = ALIGN(4);
        _eprefs_storage = .;
    } >FLASH_BOND
    ASSERT(SIZEOF(.prefs_storage) <= 0xAE0,
           "Preferences storage exceeds its stable 2784-byte slot")

    .eeprom_storage ORIGIN(FLASH_BOND) + 0xBBC (NOLOAD) :
    {
        . = ALIGN(4);
        _seeprom_storage = .;
        KEEP(*(.eeprom_storage))
        KEEP(*(.eeprom_storage*))
        . = ALIGN(4);
        _eeeprom_storage = .;
    } >FLASH_BOND
    ASSERT(SIZEOF(.eeprom_storage) <= 0x40C,
           "EEPROM storage exceeds its stable 1036-byte slot")

    .bond_signing_storage ORIGIN(FLASH_BOND) + 0xFC8 (NOLOAD) :
    {
        . = ALIGN(4);
        _sbond_signing_storage = .;
        KEEP(*(.bond_signing_storage))
        KEEP(*(.bond_signing_storage*))
        . = ALIGN(4);
        _ebond_signing_storage = .;
    } >FLASH_BOND
    ASSERT(SIZEOF(.bond_signing_storage) <= 0x38,
           "BLE signing extension exceeds the stable 56-byte slot")

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
    .ARM : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } >FLASH

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT>FLASH

    _sidata = LOADADDR(.data);

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } >RAM

    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        _snoinit = .;
        *(.noinit)
        *(.noinit*)
        . = ALIGN(4);
        _enoinit = .;
    } >RAM

    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE ( __heap_start__ = . );
        PROVIDE ( end = . );
        PROVIDE ( _end = . );
        . = ALIGN(8);
    } >RAM_HEAP

    __stack_top__ = ORIGIN(RAM_HEAP) + LENGTH(RAM_HEAP);
    __stack_bottom__ = __stack_top__ - __stack_size__;
    __stack_start__ = __stack_top__;
    __stack_end__ = __stack_bottom__;

    PROVIDE(__heap_end__ = __stack_bottom__);

    ASSERT(_end <= __heap_end__, "LM20 static data overlaps the reserved stack")
    ASSERT(__stack_top__ == 0x2007FD40,
           "LM20 RAM must end before VPR saved context")

    . = ALIGN(4);
}
