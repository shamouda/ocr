/* Copyright (c) 2012, Rice University

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

   1.  Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
   2.  Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided
   with the distribution.
   3.  Neither the name of Intel Corporation
   nor the names of its contributors may be used to endorse or
   promote products derived from this software without specific
   prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef __OCR_SCHEDULER_H__
#define __OCR_SCHEDULER_H__

#include "ocr-mappable.h"
#include "ocr-types.h"
#include "ocr-utils.h"


/****************************************************/
/* PARAMETER LISTS                                  */
/****************************************************/

typedef struct _paramListSchedulerFact_t {
    ocrParamList_t base;
} paramListSchedulerFact_t;

typedef struct _paramListSchedulerInst_t {
    ocrParamList_t base;
} paramListSchedulerInst_t;


/****************************************************/
/* OCR SCHEDULER                                    */
/****************************************************/

struct _ocrScheduler_t;
struct _ocrCost_t;
struct _ocrPolicyCtx_t;

typedef struct _ocrSchedulerFcts_t {
    void (*destruct)(struct _ocrScheduler_t *self);

    void (*start)(struct _ocrScheduler_t *self, struct _ocrPolicyDomain_t * PD);

    void (*stop)(struct _ocrScheduler_t *self);

    u8 (*yield)(struct _ocrScheduler_t *self, ocrGuid_t workerGuid,
                       ocrGuid_t yieldingEdtGuid, ocrGuid_t eventToYieldForGuid,
                       ocrGuid_t * returnGuid, struct _ocrPolicyCtx_t *context);
    /**
     * @brief Requests EDTs from this scheduler
     * @see ocrPolicyDomain_t
     */
    u8 (*takeEdt)(struct _ocrScheduler_t *self, struct _ocrCost_t *cost, u32 *count,
                  ocrGuid_t *edts, struct _ocrPolicyCtx_t *context);

    u8 (*giveEdt)(struct _ocrScheduler_t *self, u32 count,
                  ocrGuid_t *edts, struct _ocrPolicyCtx_t *context);

    // TODO: We will need to add the DB functions here
} ocrSchedulerFcts_t;

struct _ocrWorker_t;
struct _ocrWorkpile_t;

/*! \brief Represents OCR schedulers.
 *
 *  Currently, we allow scheduler interface to have work taken from them or given to them
 */
typedef struct _ocrScheduler_t {
    ocrMappable_t module;
    ocrGuid_t guid;

    struct _ocrWorker_t **workers;
    struct _ocrWorkpile_t **workpiles;
    u64 workerCount;
    u64 workpileCount;

    ocrSchedulerFcts_t *fctPtrs;
} ocrScheduler_t;


/****************************************************/
/* OCR SCHEDULER FACTORY                            */
/****************************************************/

typedef struct _ocrSchedulerFactory_t {
    ocrMappable_t module;
    ocrScheduler_t* (*instantiate) (struct _ocrSchedulerFactory_t * factory,
                                    ocrParamList_t *perInstance);

    void (*destruct)(struct _ocrSchedulerFactory_t * factory);

    ocrSchedulerFcts_t schedulerFcts;
} ocrSchedulerFactory_t;

#endif /* __OCR_SCHEDULER_H__ */