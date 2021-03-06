/*
 * This file is subject to the license agreement located in the file LICENSE
 * and cannot be distributed without it. This notice cannot be
 * removed or modified.
 */


#ifndef __OCR_WORKPILE_H_
#define __OCR_WORKPILE_H_

#include "ocr-runtime-types.h"
#include "ocr-scheduler-heuristic.h"
#include "ocr-types.h"
#include "utils/ocr-utils.h"

struct _ocrPolicyDomain_t;


/****************************************************/
/* PARAMETER LISTS                                  */
/****************************************************/

typedef struct _paramListWorkpileFact_t {
    ocrParamList_t base;
} paramListWorkpileFact_t;

typedef struct _paramListWorkpileInst_t {
    ocrParamList_t base;
} paramListWorkpileInst_t;


/****************************************************/
/* OCR WORKPILE                                     */
/****************************************************/

struct _ocrWorkpile_t;
struct _ocrPolicyDomain_t;

typedef struct _ocrWorkpileFcts_t {
    void (*destruct)(struct _ocrWorkpile_t *self);

    /**
     * @brief Switch runlevel
     *
     * @param[in] self         Pointer to this object
     * @param[in] PD           Policy domain this object belongs to
     * @param[in] runlevel     Runlevel to switch to
     * @param[in] phase        Phase for this runlevel
     * @param[in] properties   Properties (see ocr-runtime-types.h)
     * @param[in] callback     Callback to call when the runlevel switch
     *                         is complete. NULL if no callback is required
     * @param[in] val          Value to pass to the callback
     *
     * @return 0 if the switch command was successful and a non-zero error
     * code otherwise. Note that the return value does not indicate that the
     * runlevel switch occured (the callback will be called when it does) but only
     * that the call to switch runlevel was well formed and will be processed
     * at some point
     */
    u8 (*switchRunlevel)(struct _ocrWorkpile_t* self, struct _ocrPolicyDomain_t *PD, ocrRunlevel_t runlevel,
                         phase_t phase, u32 properties, void (*callback)(struct _ocrPolicyDomain_t*, u64), u64 val);

    /** @brief Interface to extract a task from this pool
     *
     *  This will get a task from the workpile.
     *  @param[in] self         Pointer to this workpile
     *  @param[in] type         Type of pop (regular or steal for eg)
     *  @param[in] cost         Cost function to use to perform the pop
     *
     *  @return GUID of the task extracted from the task pool. After the
     *  call, that task will no longer exist in the pool
     *  @todo cost is not used as of now
     */
    ocrFatGuid_t (*pop)(struct _ocrWorkpile_t *self, ocrWorkPopType_t type,
                        ocrCost_t *cost);

    /** @brief Interface to push a task into the pool
     *  @param[in] self         Pointer to this workpile
     *  @param[in] type         Type of push
     *  @param[in] g            GUID of task to push
     */
    void (*push)(struct _ocrWorkpile_t *self, ocrWorkPushType_t type,
                 ocrFatGuid_t g);
} ocrWorkpileFcts_t;

/*! \brief Abstract class to represent OCR task pool data structures.
 *
 *  This class provides the interface for the underlying implementation to conform.
 *  As we want to support work stealing, we current have pop, push and steal interfaces
 */
typedef struct _ocrWorkpile_t {
    ocrFatGuid_t fguid;
    struct _ocrPolicyDomain_t *pd;
    ocrWorkpileFcts_t fcts;
} ocrWorkpile_t;


/****************************************************/
/* OCR WORKPILE FACTORY                             */
/****************************************************/

typedef struct _ocrWorkpileFactory_t {
    ocrWorkpile_t * (*instantiate)(struct _ocrWorkpileFactory_t * factory, ocrParamList_t *perInstance);
    void (*initialize) (struct _ocrWorkpileFactory_t * factory, struct _ocrWorkpile_t * worker, ocrParamList_t *perInstance);
    void (*destruct)(struct _ocrWorkpileFactory_t * factory);
    ocrWorkpileFcts_t workpileFcts;
} ocrWorkpileFactory_t;

void initializeWorkpileOcr(ocrWorkpileFactory_t * factory, ocrWorkpile_t * self, ocrParamList_t *perInstance);

#endif /* __OCR_WORKPILE_H_ */
