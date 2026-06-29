/**
 * BSD 2-Clause License
 *
 * Copyright (c) 2019-2020, Erik Moqvist
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef DETOOLS_H
#define DETOOLS_H

/*
 * Configuration.
 *
 * Define any of the defines below to 0 to disable given feature.
 *
 * MeshCore note: upstream defaults are all 1. We flip FILE_IO, LZMA and
 * HEATSHRINK off here so this vendored copy is safe-by-default on bare-metal
 * targets (no liblzma, no malloc/heatshrink, no <stdio> file IO) regardless of
 * build flags. MeshCore .mota deltas use --codec sequential --compression crle,
 * which is fully self-contained in detools.c (NONE + CRLE only). Re-enable a
 * feature by passing -DDETOOLS_CONFIG_..=1. See detools/README.meshcore.txt.
 */

#ifndef DETOOLS_CONFIG_FILE_IO
#    define DETOOLS_CONFIG_FILE_IO                 0
#endif

#ifndef DETOOLS_CONFIG_COMPRESSION_NONE
#    define DETOOLS_CONFIG_COMPRESSION_NONE        1
#endif

#ifndef DETOOLS_CONFIG_COMPRESSION_LZMA
#    define DETOOLS_CONFIG_COMPRESSION_LZMA        0
#endif

#ifndef DETOOLS_CONFIG_COMPRESSION_CRLE
#    define DETOOLS_CONFIG_COMPRESSION_CRLE        1
#endif

#ifndef DETOOLS_CONFIG_COMPRESSION_HEATSHRINK
#    define DETOOLS_CONFIG_COMPRESSION_HEATSHRINK  0
#endif

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define DETOOLS_VERSION                  "0.53.0"

/* Error codes. */
#define DETOOLS_OK                                        0
#define DETOOLS_NOT_IMPLEMENTED                           1
#define DETOOLS_NOT_DONE                                  2
#define DETOOLS_BAD_PATCH_TYPE                            3
#define DETOOLS_BAD_COMPRESSION                           4
#define DETOOLS_INTERNAL_ERROR                            5
#define DETOOLS_LZMA_INIT                                 6
#define DETOOLS_LZMA_DECODE                               7
#define DETOOLS_OUT_OF_MEMORY                             8
#define DETOOLS_CORRUPT_PATCH                             9
#define DETOOLS_IO_FAILED                                10
#define DETOOLS_ALREADY_DONE                             11
#define DETOOLS_FILE_OPEN_FAILED                         12
#define DETOOLS_FILE_CLOSE_FAILED                        13
#define DETOOLS_FILE_READ_FAILED                         14
#define DETOOLS_FILE_WRITE_FAILED                        15
#define DETOOLS_FILE_SEEK_FAILED                         16
#define DETOOLS_FILE_TELL_FAILED                         17
#define DETOOLS_SHORT_HEADER                             18
#define DETOOLS_NOT_ENOUGH_PATCH_DATA                    19
#define DETOOLS_HEATSHRINK_SINK                          20
#define DETOOLS_HEATSHRINK_POLL                          21
#define DETOOLS_STEP_SET_FAILED                          22
#define DETOOLS_STEP_GET_FAILED                          23
#define DETOOLS_ALREADY_FAILED                           24
#define DETOOLS_CORRUPT_PATCH_OVERFLOW                   25
#define DETOOLS_CORRUPT_PATCH_CRLE_KIND                  26
#define DETOOLS_HEATSHRINK_HEADER                        27

/**
 * Read callback.
 *
 * @param[in] arg_p User data passed to detools_apply_patch_init().
 * @param[out] buf_p Buffer to read into.
 * @param[in] size Number of bytes to read.
 *
 * @return zero(0) or negative error code.
 */
typedef int (*detools_read_t)(void *arg_p, uint8_t *buf_p, size_t size);

/**
 * Write callback.
 *
 * @param[in] arg_p User data passed to detools_apply_patch_init().
 * @param[in] buf_p Buffer to write.
 * @param[in] size Number of bytes to write.
 *
 * @return zero(0) or negative error code.
 */
typedef int (*detools_write_t)(void *arg_p, const uint8_t *buf_p, size_t size);

/**
 * Seek from current position callback.
 *
 * @param[in] arg_p User data passed to detools_apply_patch_init().
 * @param[in] offset Offset to seek to from current position.
 *
 * @return zero(0) or negative error code.
 */
typedef int (*detools_seek_t)(void *arg_p, int offset);

/**
 * Memory read callback.
 *
 * @param[in] arg_p User data passed to detools_apply_patch_init().
 * @param[out] dst_p Buffer to read into.
 * @param[in] src Address to read from.
 * @param[in] size Number of bytes to read.
 *
 * @return zero(0) or negative error code.
 */
typedef int (*detools_mem_read_t)(void *arg_p,
                                  void *dst_p,
                                  uintptr_t src,
                                  size_t size);

/**
 * Memory write callback.
 *
 * @param[in] arg_p User data passed to detools_apply_patch_init().
 * @param[in] dst Address to write to.
 * @param[in] addr src_p Buffer to write from.
 * @param[in] size Number of bytes to write.
 *
 * @return zero(0) or negative error code.
 */
typedef int (*detools_mem_write_t)(void *arg_p,
                                   uintptr_t dst,
                                   void *src_p,
                                   size_t size);

/**
 * Memory erase callback.
 *
 * @param[in] arg_p User data passed to detools_apply_patch_init().
 * @param[in] addr Address to erase from.
 * @param[in] size Number of bytes to erase.
 *
 * @return zero(0) or negative error code.
 */
typedef int (*detools_mem_erase_t)(void *arg_p, uintptr_t addr, size_t size);

/**
 * State read callback.
 *
 * @param[in] arg_p User data passed to detools_apply_patch_init().
 * @param[out] buf_p Buffer to read into.
 * @param[in] size Number of bytes to read.
 *
 * @return zero(0) or negative error code.
 */
typedef int (*detools_state_read_t)(void *arg_p, void *buf_p, size_t size);

/**
 * State write callback.
 *
 * @param[in] arg_p User data passed to detools_apply_patch_init().
 * @param[in] buf_p Buffer to write.
 * @param[in] size Number of bytes to write.
 *
 * @return zero(0) or negative error code.
 */
typedef int (*detools_state_write_t)(void *arg_p, const void *buf_p, size_t size);

/**
 * Step set callback.
 *
 * @param[in] arg_p User data passed to detools_apply_patch_init().
 * @param[in] step Step to set. Later read by the step get callback.
 *
 * @return zero(0) or negative error code.
 */
typedef int (*detools_step_set_t)(void *arg_p, int step);

/**
 * Step get callback.
 *
 * @param[in] arg_p User data passed to detools_apply_patch_init().
 * @param[out] step_p Outputs the most recently set step by the set
 *                    callback, or zero(0) if not yet set.
 *
 * @return zero(0) or negative error code.
 */
typedef int (*detools_step_get_t)(void *arg_p, int *step_p);

struct detools_apply_patch_size_t {
    int state;
    int value;
    int offset;
    bool is_signed;
};

struct detools_apply_patch_patch_reader_none_t {
    size_t patch_size;
    size_t patch_offset;
};

#if DETOOLS_CONFIG_COMPRESSION_LZMA == 1

#include <lzma.h>

struct detools_apply_patch_patch_reader_lzma_t {
    lzma_stream stream;
    uint8_t *input_p;
    uint8_t *output_p;
    size_t output_size;
};

#endif

#if DETOOLS_CONFIG_COMPRESSION_HEATSHRINK == 1

#include "heatshrink_decoder.h"

struct detools_apply_patch_patch_reader_heatshrink_t {
    int8_t window_sz2;
    int8_t lookahead_sz2;
    heatshrink_decoder *decoder_p;
#if HEATSHRINK_DYNAMIC_ALLOC == 0
    heatshrink_decoder decoder;
#endif
};

#endif

enum detools_unpack_usize_state_t {
    detools_unpack_usize_state_first_t = 0,
    detools_unpack_usize_state_consecutive_t
};

struct detools_unpack_usize_t {
    enum detools_unpack_usize_state_t state;
    int value;
    int offset;
};

enum detools_crle_state_t {
    detools_crle_state_idle_t = 0,
    detools_crle_state_scattered_size_t,
    detools_crle_state_scattered_data_t,
    detools_crle_state_repeated_repetitions_t,
    detools_crle_state_repeated_data_t,
    detools_crle_state_repeated_data_read_t
};

struct detools_apply_patch_patch_reader_crle_t {
    enum detools_crle_state_t state;
    union {
        struct {
            size_t number_of_bytes_left;
            struct detools_unpack_usize_t size;
        } scattered;
        struct {
            uint8_t value;
            size_t number_of_bytes_left;
            struct detools_unpack_usize_t size;
        } repeated;
    } kind;
};

struct detools_apply_patch_patch_reader_t {
    struct detools_apply_patch_chunk_t *patch_chunk_p;
    struct detools_apply_patch_size_t size;
    union {
#if DETOOLS_CONFIG_COMPRESSION_NONE == 1
        struct detools_apply_patch_patch_reader_none_t none;
#endif
#if DETOOLS_CONFIG_COMPRESSION_LZMA == 1
        struct detools_apply_patch_patch_reader_lzma_t lzma;
#endif
#if DETOOLS_CONFIG_COMPRESSION_CRLE == 1
        struct detools_apply_patch_patch_reader_crle_t crle;
#endif
#if DETOOLS_CONFIG_COMPRESSION_HEATSHRINK == 1
        struct detools_apply_patch_patch_reader_heatshrink_t heatshrink;
#endif
    } compression;
    int (*destroy)(struct detools_apply_patch_patch_reader_t *self_p);
    int (*decompress)(struct detools_apply_patch_patch_reader_t *self_p,
                      uint8_t *buf_p,
                      size_t *size_p);
};

struct detools_apply_patch_chunk_t {
    const uint8_t *buf_p;
    size_t size;
    size_t offset;
};

enum detools_apply_patch_state_t {
    detools_apply_patch_state_init_t = 0,
    detools_apply_patch_state_dfpatch_size_t,
    detools_apply_patch_state_diff_size_t,
    detools_apply_patch_state_diff_data_t,
    detools_apply_patch_state_extra_size_t,
    detools_apply_patch_state_extra_data_t,
    detools_apply_patch_state_adjustment_t,
    detools_apply_patch_state_done_t,
    detools_apply_patch_state_failed_t
};

enum detools_apply_patch_init_state_t {
    detools_apply_patch_init_state_fixed_header_t = 0,
    detools_apply_patch_init_state_to_size_t
};

/**
 * The apply patch data structure.
 */
struct detools_apply_patch_t {
    detools_read_t from_read;
    detools_seek_t from_seek;
    size_t patch_size;
    detools_write_t to_write;
    void *arg_p;
    enum detools_apply_patch_state_t state;
    enum detools_apply_patch_init_state_t init_state;
    int compression;
    size_t patch_offset;
    size_t to_offset;
    size_t to_size;
    int from_offset;
    size_t chunk_size;
    struct detools_apply_patch_patch_reader_t patch_reader;
    struct detools_apply_patch_chunk_t chunk;
    struct detools_apply_patch_size_t size;
};

enum detools_apply_patch_in_place_init_state_t {
    detools_apply_patch_in_place_init_state_fixed_header_t = 0,
    detools_apply_patch_in_place_init_state_memory_size_t,
    detools_apply_patch_in_place_init_state_segment_size_t,
    detools_apply_patch_in_place_init_state_shift_size_t,
    detools_apply_patch_in_place_init_state_from_size_t,
    detools_apply_patch_in_place_init_state_to_size_t
};

/**
 * The in-place apply patch data structure.
 */
struct detools_apply_patch_in_place_t {
    detools_mem_read_t mem_read;
    detools_mem_write_t mem_write;
    detools_mem_erase_t mem_erase;
    detools_step_set_t step_set;
    detools_step_get_t step_get;
    size_t patch_size;
    void *arg_p;
    enum detools_apply_patch_state_t state;
    enum detools_apply_patch_in_place_init_state_t init_state;
    int compression;
    int ongoing_step;
    size_t to_pos;
    size_t to_size;
    size_t from_size;
    size_t memory_size;
    size_t segment_size;
    size_t shift_size;
    size_t chunk_size;
    struct {
        size_t index;
        int from_offset;
        size_t to_offset;
        size_t to_size;
        size_t to_pos;
    } segment;
    struct detools_apply_patch_patch_reader_t patch_reader;
    struct detools_apply_patch_chunk_t chunk;
    struct detools_apply_patch_size_t size;
};

/**
 * Initialize given apply patch object.
 *
 * @param[out] self_p Apply patch object to initialize.
 * @param[in] from_read Callback to read from-data.
 * @param[in] from_seek Callback to seek from current position in from-data.
 * @param[in] patch_size Patch size in bytes. Not used if
 *                       `detools_apply_patch_restore()` is called
 *                       immediately after this function.
 * @param[in] to_write Destination callback.
 * @param[in] arg_p Argument passed to the callbacks.
 *
 * @return zero(0) or negative error code.
 */
int detools_apply_patch_init(struct detools_apply_patch_t *self_p,
                             detools_read_t from_read,
                             detools_seek_t from_seek,
                             size_t patch_size,
                             detools_write_t to_write,
                             void *arg_p);

/**
 * Dump given apply patch object state. Call
 * `detools_apply_patch_restore()` to restore an apply patch object to
 * the dumped state.
 *
 * @param[in] self_p Apply patch object to dump.
 * @param[in] write Write callback.
 *
 * @return zero(0) or negative error code.
 */
int detools_apply_patch_dump(struct detools_apply_patch_t *self_p,
                             detools_state_write_t state_write);

/**
 * Restore given apply patch object to given dumped
 * state.
 *
 * `detools_apply_patch_get_to_offset()` and
 * `detools_apply_patch_get_patch_offset()` are often called after
 * this function to restore the to and patch streams.
 *
 * @param[in,out] self_p Initialized apply patch object to restore.
 * @param[in] read Callback to read the dumped state.
 *
 * @return zero(0) or negative error code.
 */
int detools_apply_patch_restore(struct detools_apply_patch_t *self_p,
                                detools_state_read_t state_read);

/**
 * Get the current to stream offset. Often used to restore the to
 * stream after restore.
 *
 * @param[in] self_p Apply patch object.
 *
 * @return The current to stream offset.
 */
size_t detools_apply_patch_get_to_offset(struct detools_apply_patch_t *self_p);

/**
 * Get the current patch stream offset. Often used to restore the
 * patch stream after restore.
 *
 * @param[in] self_p Apply patch object.
 *
 * @return The current patch stream offset.
 */
size_t detools_apply_patch_get_patch_offset(struct detools_apply_patch_t *self_p);

/**
 * Call this function repeatedly until all patch data has been
 * processed or an error occurres. Call detools_apply_patch_finalize()
 * to finalize the patching, even if an error occurred.
 *
 * @param[in,out] self_p Initialized apply patch object.
 * @param[in] patch_p Next chunk of the patch.
 * @param[in] size Patch buffer size.
 *
 * @return zero(0) or negative error code.
 */
int detools_apply_patch_process(struct detools_apply_patch_t *self_p,
                                const uint8_t *patch_p,
                                size_t size);

/**
 * Call once after all data has been processed to finalize the
 * patching. The value returned from this function should be ignored
 * if an error occurred in detools_apply_patch_process().
 *
 * @param[in,out] self_p Initialized apply patch object.
 *
 * @return Size of to-data in bytes if the patch was applied
 *         successfully, or negative error code.
 */
int detools_apply_patch_finalize(struct detools_apply_patch_t *self_p);

/**
 * Initialize given in-place apply patch object.
 *
 * @param[out] self_p In-place apply patch object to initialize.
 * @param[in] mem_read Callback to read data.
 * @param[in] mem_write Callback to write data.
 * @param[in] mem_erase Callback to erase data.
 * @param[in] step_set Callback to set the step.
 * @param[in] step_get Callback to get the step.
 * @param[in] patch_size Patch size in bytes.
 * @param[in] arg_p Argument passed to the callbacks.
 *
 * @return zero(0) or negative error code.
 */
int detools_apply_patch_in_place_init(
    struct detools_apply_patch_in_place_t *self_p,
    detools_mem_read_t mem_read,
    detools_mem_write_t mem_write,
    detools_mem_erase_t mem_erase,
    detools_step_set_t step_set,
    detools_step_get_t step_get,
    size_t patch_size,
    void *arg_p);

/**
 * Call this function repeatedly until all patch data has been
 * processed or an error occurres. Call
 * detools_apply_patch_in_place_finalize() to finalize the patching,
 * even if an error occurred.
 *
 * @param[in,out] self_p Initialized apply patch object.
 * @param[in] patch_p Next chunk of the patch.
 * @param[in] size Patch buffer size.
 *
 * @return zero(0) or negative error code.
 */
int detools_apply_patch_in_place_process(
    struct detools_apply_patch_in_place_t *self_p,
    const uint8_t *patch_p,
    size_t size);

/**
 * Call once after all data has been processed to finalize the
 * patching. The value returned from this function should be ignored
 * if an error occurred in detools_apply_patch_in_place_process().
 *
 * @param[in,out] self_p Initialized apply patch object.
 *
 * @return Size of to-data in bytes if the patch was applied
 *         successfully, or negative error code.
 */
int detools_apply_patch_in_place_finalize(
    struct detools_apply_patch_in_place_t *self_p);

/**
 * Apply given patch using read, write and seek callbacks.
 *
 * @param[in] from_read Source read callback.
 * @param[in] from_seek Source seek callback.
 * @param[in] patch_read Patch read callback.
 * @param[in] patch_size Patch size in bytes.
 * @param[in] to_write Destination write callback.
 * @param[in] arg_p Argument passed to all callbacks.
 *
 * @return Size of to-data in bytes or negative error code.
 */
int detools_apply_patch_callbacks(detools_read_t from_read,
                                  detools_seek_t from_seek,
                                  detools_read_t patch_read,
                                  size_t patch_size,
                                  detools_write_t to_write,
                                  void *arg_p);

/**
 * Apply given in-place patch using read, write and erase callbacks.
 *
 * @param[in] mem_read Callback to read data.
 * @param[in] mem_write Callback to write data.
 * @param[in] mem_erase Callback to erase data.
 * @param[in] step_set Callback to set the step.
 * @param[in] step_get Callback to get the step.
 * @param[in] patch_read Patch read callback.
 * @param[in] patch_size Patch size in bytes.
 * @param[in] arg_p Argument passed to the callbacks.
 *
 * @return Size of to-data in bytes or negative error code.
 */
int detools_apply_patch_in_place_callbacks(detools_mem_read_t mem_read,
                                           detools_mem_write_t mem_write,
                                           detools_mem_erase_t mem_erase,
                                           detools_step_set_t step_set,
                                           detools_step_get_t step_get,
                                           detools_read_t patch_read,
                                           size_t patch_size,
                                           void *arg_p);

#if DETOOLS_CONFIG_FILE_IO == 1

/**
 * Apply given patch file to given from file and write the output to
 * given to file.
 *
 * @param[in] from_p Source file name.
 * @param[in] patch_p Patch file name.
 * @param[in] to_p Destination file name.
 *
 * @return Size of to-data in bytes or negative error code.
 */
int detools_apply_patch_filenames(const char *from_p,
                                  const char *patch_p,
                                  const char *to_p);

/**
 * Apply given patch file to given memory file.
 *
 * @param[in] memory_p Memory file name.
 * @param[in] patch_p Patch file name.
 * @param[in] step_set Callback to set the step.
 * @param[in] step_get Callback to get the step.
 *
 * @return Size of to-data in bytes or negative error code.
 */
int detools_apply_patch_in_place_filenames(const char *memory_p,
                                           const char *patch_p,
                                           detools_step_set_t step_set,
                                           detools_step_get_t step_get);

#endif

/**
 * Get the error string for given error code.
 *
 * @param[in] Error code.
 *
 * @return Error string.
 */
const char *detools_error_as_string(int error);

#endif
