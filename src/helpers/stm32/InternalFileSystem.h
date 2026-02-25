/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 hathach for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef INTERNALFILESYSTEM_H_
#define INTERNALFILESYSTEM_H_

#include "Adafruit_LittleFS.h"

#ifndef LFS_FLASH_TOTAL_SIZE             /* Flash size can be configured in platformio.ini */
#define LFS_FLASH_TOTAL_SIZE (16 * 2048) /* defaults to 32k flash */
#endif
#define LFS_BLOCK_SIZE      (2048)
#define LFS_FLASH_ADDR_BASE (FLASH_END_ADDR - LFS_FLASH_TOTAL_SIZE + 1)

class InternalFileSystem : public Adafruit_LittleFS {
public:
  InternalFileSystem(void);

  // overwrite to also perform low level format (sector erase of whole flash region)
  bool begin(void);
};

extern InternalFileSystem InternalFS;

#endif /* INTERNALFILESYSTEM_H_ */
