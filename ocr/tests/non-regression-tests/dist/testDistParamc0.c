/*
 * This file is subject to the license agreement located in the file LICENSE
 * and cannot be distributed without it. This notice cannot be
 * removed or modified.
 */
#include "ocr.h"
#include "extensions/ocr-affinity.h"

/**
 * DESC: OCR-DIST - create a remote EDT with paramc
 */

ocrGuid_t remoteEdt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]) {
    ASSERT(paramc == 2);
    ASSERT(paramv[0] == 333);
    ASSERT(paramv[1] == 555);
    PRINTF("[remote] RemoteEdt: paramv checked\n");
    ocrShutdown();
    return NULL_GUID;
}

ocrGuid_t mainEdt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]) {
    u64 affinityCount;
    ocrAffinityCount(AFFINITY_PD, &affinityCount);
    ASSERT(affinityCount >= 1);
    ocrGuid_t affinities[affinityCount];
    ocrAffinityGet(AFFINITY_PD, &affinityCount, affinities);
    ocrGuid_t edtAffinity = affinities[affinityCount-1];

    ocrGuid_t remoteEdtTemplateGuid;
    ocrEdtTemplateCreate(&remoteEdtTemplateGuid, remoteEdt, 2, 0);
    ocrHint_t edtHint;
    ocrHintInit( &edtHint, OCR_HINT_EDT_T );
    ocrSetHintValue( & edtHint, OCR_HINT_EDT_AFFINITY, ocrAffinityToHintValue( edtAffinity) );
    ocrGuid_t edtGuid;
    u64 edtParamv[2] = {333,555};
    ocrEdtCreate(&edtGuid, remoteEdtTemplateGuid, 2, (u64*) &edtParamv, 0, NULL,
        EDT_PROP_NONE, &edtHint, NULL);
    return NULL_GUID;
}
