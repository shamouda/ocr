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

#include <assert.h>
#include <stdlib.h>

#include "ocr-scheduler.h"

extern ocr_scheduler_t * hc_scheduler_constructor();
extern ocr_scheduler_t * xe_scheduler_constructor();
extern ocr_scheduler_t * ce_scheduler_constructor();
extern ocr_scheduler_t * hc_placed_scheduler_constructor();

ocr_scheduler_t * newScheduler(ocr_scheduler_kind schedulerType) {
    switch(schedulerType) {
    case OCR_SCHEDULER_WST:
        return hc_scheduler_constructor();
    case OCR_SCHEDULER_XE:
        return xe_scheduler_constructor();
    case OCR_SCHEDULER_CE:
        return ce_scheduler_constructor();
    case OCR_PLACED_SCHEDULER:
        assert ( 0 && "delete support for OCR_PLACED_SCHEDULER" );
        return NULL;

    case OCR_SCHEDULER_RANDOMVICTIM_LOCALPUSH:
        return hc_randomvictim_localpush_scheduler_constructor();
    case OCR_SCHEDULER_RANDOMVICTIM_DATALOCALITYPUSH:
        return hc_randomvictim_datalocalitypush_scheduler_constructor();
    case OCR_SCHEDULER_RANDOMVICTIM_EVENTLOCALITYPUSH:
        return hc_randomvictim_eventlocalitypush_scheduler_constructor();
    case OCR_SCHEDULER_CYCLICVICTIM_LOCALPUSH:
        return hc_cyclicvictim_localpush_scheduler_constructor();
    case OCR_SCHEDULER_CYCLICVICTIM_DATALOCALITYPUSH:
        return hc_cyclicvictim_datalocalitypush_scheduler_constructor();
    case OCR_SCHEDULER_CYCLICVICTIM_EVENTLOCALITYPUSH:
        return hc_cyclicvictim_eventlocalitypush_scheduler_constructor();
    case OCR_SCHEDULER_HIERCYCLICVICTIM_LOCALPUSH:
        return hc_hiercyclicvictim_localpush_scheduler_constructor();
    case OCR_SCHEDULER_HIERCYCLICVICTIM_DATALOCALITYPUSH:
        return hc_hiercyclicvictim_datalocalitypush_scheduler_constructor();
    case OCR_SCHEDULER_HIERCYCLICVICTIM_EVENTLOCALITYPUSH:
        return hc_hiercyclicvictim_eventlocalitypush_scheduler_constructor();
    case OCR_SCHEDULER_HIERRANDOMVICTIM_LOCALPUSH:
        return hc_hierrandomvictim_localpush_scheduler_constructor();
    case OCR_SCHEDULER_HIERRANDOMVICTIM_DATALOCALITYPUSH:
        return hc_hierrandomvictim_datalocalitypush_scheduler_constructor();
    case OCR_SCHEDULER_HIERRANDOMVICTIM_EVENTLOCALITYPUSH:
        return hc_hierrandomvictim_eventlocalitypush_scheduler_constructor();
    case OCR_SCHEDULER_SOCKETONLYVICTIM_USERSOCKETPUSH:
        return hc_socketonlyvictim_usersocketpush_scheduler_constructor();
    default:
        assert(false && "Unrecognized scheduler kind");
        break;
    }

    return NULL;
}