/*
 * This file is subject to the license agreement located in the file LIXENSE
 * and cannot be distributed without it. This notice cannot be
 * removed or modified.
 */

#include "ocr-config.h"
#ifdef ENABLE_SCHEDULER_XE

#include "debug.h"
#include "ocr-policy-domain.h"
#include "ocr-runtime-types.h"
#include "ocr-sysboot.h"
#include "ocr-workpile.h"
#include "scheduler/xe/xe-scheduler.h"

/******************************************************/
/* OCR-XE SCHEDULER                                   */
/******************************************************/

void xeSchedulerDestruct(ocrScheduler_t * self) {
}

void xeSchedulerBegin(ocrScheduler_t * self, ocrPolicyDomain_t * PD) {
}

void xeSchedulerStart(ocrScheduler_t * self, ocrPolicyDomain_t * PD) {
}

void xeSchedulerStop(ocrScheduler_t * self) {
}

void xeSchedulerFinish(ocrScheduler_t *self) {
}

u8 xeSchedulerTake (ocrScheduler_t *self, u32 *count, ocrFatGuid_t *edts) {
    return 0;
}

u8 xeSchedulerGive (ocrScheduler_t* base, u32* count, ocrFatGuid_t* edts) {
    return 0;
}

ocrScheduler_t* newSchedulerXe(ocrSchedulerFactory_t * factory, ocrParamList_t *perInstance) {
    ocrSchedulerXe_t* derived = (ocrSchedulerXe_t*) runtimeChunkAlloc(
        sizeof(ocrSchedulerXe_t), NULL);
    
    ocrScheduler_t* base = (ocrScheduler_t*)derived;
    base->fguid.guid = UNINITIALIZED_GUID;
    base->fguid.metaDataPtr = base;
    base->pd = NULL;
    base->workpiles = NULL;
    base->workpileCount = 0;
    base->fcts = factory->schedulerFcts;
    
    return base;
}

void destructSchedulerFactoryXe(ocrSchedulerFactory_t * factory) {
    runtimeChunkFree((u64)factory, NULL);
}

ocrSchedulerFactory_t * newOcrSchedulerFactoryXe(ocrParamList_t *perType) {
    ocrSchedulerFactoryXe_t* derived = (ocrSchedulerFactoryXe_t*) runtimeChunkAlloc(
        sizeof(ocrSchedulerFactoryXe_t), NULL);
    
    ocrSchedulerFactory_t* base = (ocrSchedulerFactory_t*) derived;
    base->instantiate = &newSchedulerXe;
    base->destruct = &destructSchedulerFactoryXe;
    base->schedulerFcts.begin = &xeSchedulerBegin;
    base->schedulerFcts.start = &xeSchedulerStart;
    base->schedulerFcts.stop = &xeSchedulerStop;
    base->schedulerFcts.finish = &xeSchedulerFinish;
    base->schedulerFcts.destruct = &xeSchedulerDestruct;
    base->schedulerFcts.takeEdt = &xeSchedulerTake;
    base->schedulerFcts.giveEdt = &xeSchedulerGive;
    return base;
}

#endif /* ENABLE_SCHEDULER_XE */
