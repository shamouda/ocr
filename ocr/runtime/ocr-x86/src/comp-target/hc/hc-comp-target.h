/**
 * @brief Compute Platform implemented using pthread
 **/

/*
 * This file is subject to the license agreement located in the file LICENSE
 * and cannot be distributed without it. This notice cannot be
 * removed or modified.
 */

#include "ocr-config.h"
#ifdef ENABLE_COMP_TARGET_HC

#ifndef __COMP_TARGET_HC_H__
#define __COMP_TARGET_HC_H__

#include "ocr-comp-target.h"
#include "ocr-types.h"
#include "ocr-utils.h"

#include <pthread.h>

typedef struct {
    paramListCompTargetInst_t base;
    void (*routine)(void*);
    void* routineArg;
} paramListCompTargetHc_t;

typedef struct {
    ocrCompTarget_t base;

    void (*routine)(void*);
    void* routineArg;
} ocrCompTargetHc_t;

typedef struct {
    ocrCompTargetFactory_t base;
} ocrCompTargetFactoryHc_t;

ocrCompTargetFactory_t* newCompTargetFactoryHc(ocrParamList_t *perType);

#endif /* __COMP_TARGET_HC_H__ */

#endif /* ENABLE_COMP_TARGET_HC */
