/**
 * @brief OCR tasks
 * @authors Romain Cledat, Intel Corporation
 * @date 2012-09-21
 * Copyright (c) 2012, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the
 *       distribution.
 *    3. Neither the name of Intel Corporation nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **/

#ifndef __TASK_ALL_H__
#define __TASK_ALL_H__

#include "debug.h"
#include "ocr-task.h"
#include "ocr-utils.h"

typedef enum _taskType_t {
    taskHc_id,
    taskFsim_id,
    taskMax_id
} taskType_t;

const char * task_types [] = {
    "HC",
    "FSIM",
    NULL
};

typedef enum _taskTemplateType_t {
    taskTemplateHc_id,
    taskTemplateFsim_id,
    taskTemplateMax_id
} taskTemplateType_t;

const char * taskTemplate_types [] = {
    "HC",
    "FSIM",
    NULL
};

// HC Task
#include "task/hc/hc-task.h"

// Add other tasks using the same pattern as above

inline ocrTaskFactory_t *newTaskFactory(taskType_t type, ocrParamList_t *typeArg) {
    switch(type) {
    case taskHc_id:
        return newTaskFactoryHc(typeArg);
    default:
        ASSERT(0);
    };
    return NULL;
}

inline ocrTaskTemplateFactory_t *newTaskTemplateFactory(taskTemplateType_t type, ocrParamList_t *typeArg) {
    switch(type) {
    case taskTemplateHc_id:
        return newTaskTemplateFactoryHc(typeArg);
    default:
        ASSERT(0);
        return NULL;
    };
}

#endif /* __TASK_ALL_H__ */