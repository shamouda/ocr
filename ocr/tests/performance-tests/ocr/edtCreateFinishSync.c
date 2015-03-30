#include "perfs.h"
#include "ocr.h"

// DESC: One worker creates all the tasks. Sink EDT depends on
//       all tasks through the output-event of a finish EDT.
// TIME: Creation of all tasks
// FREQ: Create 'NB_INSTANCES' EDTs once

ocrGuid_t terminateEdt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]) {
    timestamp_t * timers = (timestamp_t *) depv[1].ptr;
    summary_throughput_timer(&timers[0], &timers[1], NB_INSTANCES);
    ocrShutdown(); // This is the last EDT to execute, terminate
    return NULL_GUID;
}

ocrGuid_t workEdt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]) {
    return NULL_GUID;
}

ocrGuid_t finishEdt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]) {

    timestamp_t * dbPtr = (timestamp_t *) depv[0].ptr;

    get_time(&dbPtr[0]);

    ocrGuid_t workEdtTemplateGuid;
    ocrEdtTemplateCreate(&workEdtTemplateGuid, workEdt, 0, 0);

    int i = 0;
    while (i < NB_INSTANCES) {
        ocrGuid_t workEdtGuid;
        ocrEdtCreate(&workEdtGuid, workEdtTemplateGuid,
                     0, NULL, 0, NULL_GUID, EDT_PROP_NONE, NULL_GUID, NULL);
        i++;
    }
    ocrEdtTemplateDestroy(workEdtTemplateGuid);

    get_time(&dbPtr[1]);

    return NULL_GUID;
}

ocrGuid_t mainEdt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]) {

    ocrGuid_t terminateEdtTemplateGuid;
    ocrEdtTemplateCreate(&terminateEdtTemplateGuid, terminateEdt, 0, 2);

    ocrGuid_t terminateEdtGuid;
    ocrEdtCreate(&terminateEdtGuid, terminateEdtTemplateGuid,
                 0, NULL, 2, NULL_GUID, EDT_PROP_NONE, NULL_GUID, NULL);

    timestamp_t * dbPtr;
    ocrGuid_t dbGuid;
    ocrDbCreate(&dbGuid, (void **)&dbPtr, (sizeof(timestamp_t)*2), 0, NULL_GUID, NO_ALLOC);
    ocrAddDependence(dbGuid, terminateEdtGuid, 1, DB_MODE_RO);

    ocrGuid_t oEvtGuid;
    ocrGuid_t finishEdtTemplateGuid;
    ocrEdtTemplateCreate(&finishEdtTemplateGuid, finishEdt, 0, 1);
    ocrGuid_t finishEdtGuid;
    ocrEdtCreate(&finishEdtGuid, finishEdtTemplateGuid,
                 0, NULL, 1, NULL_GUID,  EDT_PROP_FINISH, NULL_GUID, &oEvtGuid);

    ocrAddDependence(oEvtGuid, terminateEdtGuid, 0, DB_MODE_RO);
    ocrAddDependence(dbGuid, finishEdtGuid, 0, DB_MODE_RO);

    return NULL_GUID;
}
