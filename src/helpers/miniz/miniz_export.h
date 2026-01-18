#pragma once

// MINIZ_EXPORT is used for shared library exports
// For embedded/static usage, it's empty
#ifndef MINIZ_EXPORT
#define MINIZ_EXPORT
#endif

// For NRF52 platforms, detect 32-bit register usage
#if defined(__arm__) || defined(__thumb__)
#define MINIZ_HAS_64BIT_REGISTERS 0
#else
#define MINIZ_HAS_64BIT_REGISTERS 1
#endif
