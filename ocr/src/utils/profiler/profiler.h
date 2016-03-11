/* Modified in 2014 by Romain Cledat (now at Intel). The original
 * license (BSD) is below. This file is also subject to the license
 * aggrement located in the file LICENSE and cannot be distributed
 * without it. This notice cannot be removed or modified
 */

/* Copyright (c) 2011, Romain Cledat
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Georgia Institute of Technology nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL ROMAIN CLEDAT BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __PROFILER_H__
#define __PROFILER_H__

#include "ocr-config.h"
#ifdef OCR_RUNTIME_PROFILER
#include "ocr-types.h"

#ifndef ENABLE_COMP_PLATFORM_PTHREAD
#error "The current runtime profiler is only compatible with the pthread comp-platform at this time"
#endif

#include "profilerAutoGenRT.h"
#endif /* OCR_RUNTIME_PROFILER */

#include "extensions/ocr-profiler-internal.h"

#endif /* __PROFILER_H__ */
