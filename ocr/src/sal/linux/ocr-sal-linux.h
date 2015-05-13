/*
 * This file is subject to the license agreement located in the file LICENSE
 * and cannot be distributed without it. This notice cannot be
 * removed or modified.
 */

#ifndef __OCR_SAL_LINUX_H__
#define __OCR_SAL_LINUX_H__

#include "ocr-hal.h"

#include <assert.h>
#include <stdio.h>

void sig_handler(u32 sigNum);

extern u32 salPause();

extern void salQuery(u32 flag);

extern void salResume(u32 flag);

extern void registerSignalHandler();

#define sal_abort()   hal_abort()

#define sal_exit(x)   hal_exit(x)

#define sal_assert(x, f, l)  assert(x)

#define sal_print(msg, len)   printf("%s", msg)

#endif /* __OCR_SAL_LINUX_H__ */
