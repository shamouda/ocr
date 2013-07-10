#ifndef __OCR_MACHINE_H_

#define __OCR_MACHINE_H_
#include <stdio.h>
#include <string.h>
#include <iniparser.h>
#include <signal.h>

#include <ocr-mem-platform.h>
#include <ocr-mem-target.h>
#include <ocr-allocator.h>
#include <ocr-comp-platform.h>
#include <ocr-comp-target.h>
#include <ocr-worker.h>
#include <ocr-workpile.h>
#include <ocr-scheduler.h>
#include <ocr-policy-domain.h>
#include <ocr-datablock.h>
#include <ocr-event.h>

typedef enum {
    guid_type,
    memplatform_type,
    memtarget_type,
    allocator_type,
    compplatform_type,
    comptarget_type,
    workpile_type,
    worker_type,
    scheduler_type,
    policydomain_type,
} type_enum;

/* Dependence information (from->to) referenced by refstr */

typedef struct {
    type_enum from;
    type_enum to;
    char *refstr;
} dep_t;

#endif
