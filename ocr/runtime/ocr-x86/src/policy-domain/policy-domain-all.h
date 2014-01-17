/*
 * This file is subject to the license agreement located in the file LICENSE
 * and cannot be distributed without it. This notice cannot be
 * removed or modified.
 */

#ifndef __POLICY_DOMAIN_ALL_H_
#define __POLICY_DOMAIN_ALL_H_

#include "debug.h"
#include "ocr-config.h"
#include "ocr-policy-domain.h"
#include "utils/ocr-utils.h"

typedef enum _policyDomainType_t {
    policyDomainHc_id,
    policyDomainXe_id,
    policyDomainCe_id,
    policyDomainFsimMasterCE_id,
    policyDomainHcPlaced_id,
    policyDomainHcLeafPlace_id,
    policyDomainHcMasterLeafPlace_id,
    policyDomainMax_id
} policyDomainType_t;

const char * policyDomain_types [] = {
    "HC",
    "XE",
    "CE",
    "MasterCE",
    "HCPlaced",
    "HCLeafPlace",
    "HCMasterLeafPlace",
    NULL
};

#include "policy-domain/hc/hc-policy.h"
#include "policy-domain/ce/ce-policy.h"

inline ocrPolicyDomainFactory_t * newPolicyDomainFactory(policyDomainType_t type, ocrParamList_t *perType) {
    switch(type) {
#ifdef ENABLE_POLICY_DOMAIN_HC
    case policyDomainHc_id:
        return newPolicyDomainFactoryHc(perType);
#endif
#ifdef ENABLE_POLICY_DOMAIN_XE
    case policyDomainXe_id:
        return newPolicyDomainFactoryXe(perType);
#endif
#ifdef ENABLE_POLICY_DOMAIN_CE
    case policyDomainCe_id:
        return newPolicyDomainFactoryCe(perType);
#endif
#if 0
    case policyDomainFsimMasterCE_id:
//        return newPolicyDomainFactoryFsimMasterCE(perType);
    case policyDomainHcPlaced_id:
//        return newPolicyDomainFactoryHcPlaced(perType);
    case policyDomainHcLeafPlace_id:
//        return newPolicyDomainFactoryHcLeafPlace(perType);
    case policyDomainHcMasterLeafPlace_id:
//        return newPolicyDomainFactoryHcMasterLeafPlace(perType);
#endif
    default:
        ASSERT(0);
    }
    return NULL;
}

#endif /* __POLICY_DOMAIN_ALL_H_ */
