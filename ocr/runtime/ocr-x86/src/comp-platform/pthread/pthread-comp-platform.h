/**
 * @brief Compute Platform implemented using pthread
 **/

/*
 * This file is subject to the license agreement located in the file LICENSE
 * and cannot be distributed without it. This notice cannot be
 * removed or modified.
 */
#ifndef __COMP_PLATFORM_PTHREAD_H__
#define __COMP_PLATFORM_PTHREAD_H__

#include "ocr-config.h"
#ifdef ENABLE_COMP_PLATFORM_PTHREAD

#include "ocr-comp-platform.h"
#include "ocr-comp-target.h"
#include "ocr-worker.h"
#include "ocr-types.h"
#include "utils/ocr-utils.h"
#ifdef ENABLE_WORKPILE_CE
#include "utils/deque.h"
#endif

#include <pthread.h>


typedef struct {
    ocrCompPlatformFactory_t base;
    u64 stackSize;
} ocrCompPlatformFactoryPthread_t;

typedef struct {
    ocrCompPlatform_t base;
    pthread_t osThread;
    u64 stackSize;
    s32 binding;
    bool isMaster;
} ocrCompPlatformPthread_t;

typedef struct {
    paramListCompPlatformInst_t base;
    u64 stackSize;

	s32 coreCount;
	s32 segmentCount;
	s32 freeCoresPerSegment;
	s32 specialBinding;
    s32 binding;
	s32 threadsPerCore;
	s32 compPerCore;

} paramListCompPlatformPthread_t;

extern ocrCompPlatformFactory_t* newCompPlatformFactoryPthread(ocrParamList_t *perType);

#endif /* ENABLE_COMP_PLATFORM_PTHREAD */
#endif /* __COMP_PLATFORM_PTHREAD_H__ */
