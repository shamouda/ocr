/**
 * @brief OCR tasks
 **/

/*
 * This file is subject to the license agreement located in the file LICENSE
 * and cannot be distributed without it. This notice cannot be
 * removed or modified.
 */


#ifndef __TASK_ALL_H__
#define __TASK_ALL_H__

#include "debug.h"
#include "ocr-config.h"
#include "ocr-task.h"
#include "utils/ocr-utils.h"

typedef enum _taskType_t {
#ifdef ENABLE_TASK_HC
    taskHc_id,
#endif
    taskMax_id
} taskType_t;

const char * task_types [] = {
#ifdef ENABLE_TASK_HC
    "HC",
#endif
    NULL
};

typedef enum _taskTemplateType_t {
#ifdef ENABLE_TASKTEMPLATE_HC
    taskTemplateHc_id,
#endif
    taskTemplateMax_id
} taskTemplateType_t;

const char * taskTemplate_types [] = {
#ifdef ENABLE_TASKTEMPLATE_HC
    "HC",
#endif
    NULL
};

// HC Task
#include "task/hc/hc-task.h"

// Add other tasks using the same pattern as above

inline ocrTaskFactory_t *newTaskFactory(taskType_t type, ocrParamList_t *typeArg) {
    switch(type) {
#ifdef ENABLE_TASK_HC
    case taskHc_id:
        return newTaskFactoryHc(typeArg, (u32)type);
#endif
    default:
        ASSERT(0);
    };
    return NULL;
}

inline ocrTaskTemplateFactory_t *newTaskTemplateFactory(taskTemplateType_t type, ocrParamList_t *typeArg) {
    switch(type) {
#ifdef ENABLE_TASKTEMPLATE_HC
    case taskTemplateHc_id:
        return newTaskTemplateFactoryHc(typeArg, (u32)type);
#endif
    default:
        ASSERT(0);
        return NULL;
    };
}

#endif /* __TASK_ALL_H__ */
