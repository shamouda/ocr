/*
 * This file is subject to the license agreement located in the file LICENSE
 * and cannot be distributed without it. This notice cannot be
 * removed or modified.
 */

#include "ocr.h"
#define N 16

/**
 * DESC: Creates a top-level finish-edt which forks 16 edts, writing in a
 * shared data block. Then the sink edt checks everybody wrote to the db and
 * terminates.
 */

// This edt is triggered when the output event of the other edt is satisfied by the runtime
ocrGuid_t terminateEdt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]) {
    // TODO shouldn't be doing that... but need more support from output events to get a 'return' value from an edt
    ASSERT(!(ocrGuidIsNull(depv[0].guid)));
    u64 * array = (u64*)depv[0].ptr;
    u64 i = 0;
    while (i < N) {
        ASSERT(array[i] == i);
        i++;
    }
    ocrShutdown(); // This is the last EDT to execute, terminate
    return NULL_GUID;
}

ocrGuid_t updaterEdt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]) {
    // Retrieve id
    ASSERT(paramc == 1);
    u64 id = paramv[0];
    ASSERT ((id>=0) && (id < N));
    u64 * dbPtr = (u64 *) depv[0].ptr;
    dbPtr[id] = id;
    return NULL_GUID;
}

ocrGuid_t computeEdt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]) {
    ocrGuid_t updaterEdtTemplateGuid;
    ocrEdtTemplateCreate(&updaterEdtTemplateGuid, updaterEdt, 1 /*paramc*/, 1/*depc*/);
    u64 i = 0;
    while (i < N) {
        // Pass down the index to write to and the db guid through params
        // (Could also be done through dependences)
        u64 nparamv = i;
        // Pass the guid we got fron depv to the updaterEdt through depv
        ocrGuid_t updaterEdtGuid;
        ocrEdtCreate(&updaterEdtGuid, updaterEdtTemplateGuid, EDT_PARAM_DEF, &nparamv, EDT_PARAM_DEF, &(depv[0].guid), 0, NULL_HINT, NULL);
        i++;
    }
    return depv[0].guid;
}

ocrGuid_t mainEdt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]) {
    ocrGuid_t finishEdtOutputEventGuid;
    ocrGuid_t computeEdtGuid;
    ocrGuid_t computeEdtTemplateGuid;
    ocrEdtTemplateCreate(&computeEdtTemplateGuid, computeEdt, 0 /*paramc*/, 1 /*depc*/);
    ocrEdtCreate(&computeEdtGuid, computeEdtTemplateGuid, EDT_PARAM_DEF, /*paramv=*/NULL, EDT_PARAM_DEF, /*depv=*/NULL,
                 /*properties=*/ EDT_PROP_FINISH, NULL_HINT, /*outEvent=*/&finishEdtOutputEventGuid);

    // Build a data-block to be shared with sub-edts
    u64 * array;
    ocrGuid_t dbGuid;
    ocrDbCreate(&dbGuid,(void **) &array, sizeof(u64)*N, DB_PROP_NONE, NULL_HINT, NO_ALLOC);

    ocrGuid_t terminateEdtGuid;
    ocrGuid_t terminateEdtTemplateGuid;
    ocrEdtTemplateCreate(&terminateEdtTemplateGuid, terminateEdt, 0 /*paramc*/, 2 /*depc*/);
    ocrEdtCreate(&terminateEdtGuid, terminateEdtTemplateGuid, EDT_PARAM_DEF, /*paramv=*/NULL, EDT_PARAM_DEF, /*depv=*/NULL,
                 /*properties=*/0, NULL_HINT, /*outEvent=*/NULL);
    ocrAddDependence(dbGuid, terminateEdtGuid, 0, DB_MODE_CONST);
    ocrAddDependence(finishEdtOutputEventGuid, terminateEdtGuid, 1, DB_MODE_CONST);

    // Use an event to channel the db guid to the main edt
    // Could also pass it directly as a depv
    ocrGuid_t dbEventGuid;
    ocrEventCreate(&dbEventGuid, OCR_EVENT_STICKY_T, true);

    ocrAddDependence(dbEventGuid, computeEdtGuid, 0, DB_MODE_CONST);

    ocrEventSatisfy(dbEventGuid, dbGuid);

    return NULL_GUID;
}
