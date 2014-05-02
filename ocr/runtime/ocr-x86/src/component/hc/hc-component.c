/*
 * This file is subject to the license agreement located in the file LICENSE
 * and cannot be distributed without it. This notice cannot be
 * removed or modified.
 */

#include "debug.h"
#include "ocr-config.h"
#include "component/hc/hc-component.h"
#include "component/component-all.h"
#include "ocr-errors.h"
#include "ocr-sysboot.h"

#ifdef ENABLE_COMPONENT_HC_STATE

ocrComponent_t* newComponentHcState(ocrComponentFactory_t * factory, ocrFatGuid_t hints, u32 properties) {
    u32 i;

    ocrComponentFactoryHcState_t * derivedFactory = (ocrComponentFactoryHcState_t*)factory;
    u32 numWorkers = derivedFactory->maxWorkers;
    ocrPolicyDomain_t *pd = NULL;
    getCurrentEnv(&pd, NULL, NULL, NULL);
    ocrComponentHcState_t * component = (ocrComponentHcState_t *)runtimeChunkAlloc((sizeof(ocrComponentHcState_t) + (numWorkers * sizeof(ocrComponent_t*))), (void *)1);
    component->base.fcts = factory->fcts;
    component->components = (ocrComponent_t **)((u64)component + sizeof(ocrComponentHcState_t));
    component->numWorkers = numWorkers;
    ocrComponentFactory_t * fact = pd->componentFactories[componentHcWork_id];
    ocrFatGuid_t hint = {NULL_GUID, NULL};
    for (i = 0; i < numWorkers; i++) {
        hint.guid = (ocrGuid_t)i;
        component->components[i] = fact->instantiate(fact, hint, 0);
    }
    return (ocrComponent_t*)component;
}

u8 hcStateComponentCreate(ocrComponent_t *self, ocrLocation_t loc, ocrFatGuid_t* component, ocrFatGuid_t hints, u32 properties) {
//TODO
ASSERT(0);
return OCR_ENOTSUP;
}

u8 hcStateComponentInsert(ocrComponent_t *self, ocrLocation_t loc, ocrFatGuid_t component, ocrFatGuid_t hints, u32 properties) {
    ASSERT(component.guid != NULL_GUID);
    ocrComponentHcState_t * compHcState = (ocrComponentHcState_t*)self;
    ASSERT((u32)loc < compHcState->numWorkers);
    ocrComponent_t * comp = compHcState->components[loc];
    comp->fcts.insert(comp, loc, component, hints, properties);
    return 0;
}

u8 hcStateComponentRemove(ocrComponent_t *self, ocrLocation_t loc, ocrFatGuid_t *component, ocrFatGuid_t hints, u32 properties) {
    u32 i;
    ASSERT(component && component->guid == NULL_GUID);
    ocrComponentHcState_t * compHcState = (ocrComponentHcState_t*)self;
    //First try to pop from owned deque and then try to steal from others
    ocrComponent_t * comp = compHcState->components[loc];
    comp->fcts.remove(comp, loc, component, hints, properties);
    if (component->guid == NULL_GUID) {
        for (i = 1; i < compHcState->numWorkers; i++) {
            u32 victim = ((u32)loc + i) % compHcState->numWorkers;
            ocrComponent_t *compVictim = compHcState->components[victim];
            compVictim->fcts.remove(compVictim, loc, component, hints, properties);
            if (component->guid != NULL_GUID) {
                break;
            }
        }
    }
    return 0;
}

u8 hcStateComponentMove(ocrComponent_t *self, ocrLocation_t loc, ocrComponent_t *destination, ocrFatGuid_t component, ocrFatGuid_t hints, u32 properties) {
//TODO
ASSERT(0);
return OCR_ENOTSUP;
}

u8 hcStateComponentSplit(ocrComponent_t *self, ocrLocation_t loc, u32 count, u32 *chunks, ocrFatGuid_t *components, ocrFatGuid_t hints, u32 properties) {
//TODO
ASSERT(0);
return OCR_ENOTSUP;
}

u8 hcStateComponentMerge(ocrComponent_t *self, ocrLocation_t loc, u32 count, ocrFatGuid_t *components, ocrFatGuid_t hints, u32 properties) {
//TODO
ASSERT(0);
return OCR_ENOTSUP;
}

u32 hcStateComponentCount(ocrComponent_t *self, ocrLocation_t loc) {
//TODO
ASSERT(0);
return OCR_ENOTSUP;
}


ocrComponentFactory_t* newOcrComponentFactoryHcState(ocrParamList_t *perType) {
    ocrComponentFactory_t* base = (ocrComponentFactory_t*) runtimeChunkAlloc(
        sizeof(ocrComponentFactoryHcState_t), (void *)1);

    base->instantiate = &newComponentHcState;
    base->destruct = NULL; //&destructComponentFactoryHcState;

    base->fcts.create = FUNC_ADDR(u8 (*)(ocrComponent_t*, ocrLocation_t, ocrFatGuid_t*, ocrFatGuid_t, u32), hcStateComponentCreate);
    base->fcts.insert = FUNC_ADDR(u8 (*)(ocrComponent_t*, ocrLocation_t, ocrFatGuid_t, ocrFatGuid_t, u32), hcStateComponentInsert);
    base->fcts.remove = FUNC_ADDR(u8 (*)(ocrComponent_t*, ocrLocation_t, ocrFatGuid_t*, ocrFatGuid_t, u32), hcStateComponentRemove);
    base->fcts.move = FUNC_ADDR(u8 (*)(ocrComponent_t*, ocrLocation_t, ocrComponent_t*, ocrFatGuid_t, ocrFatGuid_t, u32), hcStateComponentMove);
    base->fcts.split = FUNC_ADDR(u8 (*)(ocrComponent_t*, ocrLocation_t, u32, u32*, ocrFatGuid_t*, ocrFatGuid_t, u32), hcStateComponentSplit);
    base->fcts.merge = FUNC_ADDR(u8 (*)(ocrComponent_t*, ocrLocation_t, u32, ocrFatGuid_t*, ocrFatGuid_t, u32), hcStateComponentMerge);
    base->fcts.count = FUNC_ADDR(u32 (*)(ocrComponent_t*, ocrLocation_t), hcStateComponentCount);

    paramListComponentFactHcState_t * paramDerived = (paramListComponentFactHcState_t*)perType;
    ocrComponentFactoryHcState_t * derived = (ocrComponentFactoryHcState_t*)base;
    derived->maxWorkers = paramDerived->maxWorkers;
    ASSERT(derived->maxWorkers);
    return base;
}

#endif /* ENABLE_COMPONENT_HC_STATE */

#ifdef ENABLE_COMPONENT_HC_WORK

ocrComponent_t* newComponentHcWork(ocrComponentFactory_t * factory, ocrFatGuid_t hints, u32 properties) {
    //ocrComponentFactoryHcWork_t * derivedFactory = (ocrComponentFactoryHcWork_t*)factory;
    //u32 dequeInitSize = derivedFactory->dequeInitSize;
    ocrPolicyDomain_t *pd = NULL;
    getCurrentEnv(&pd, NULL, NULL, NULL);
    ocrComponentHcWork_t * component = (ocrComponentHcWork_t *)runtimeChunkAlloc(sizeof(ocrComponentHcWork_t), (void *)1);
    component->base.fcts = factory->fcts;
    component->base.mapping = (ocrLocation_t)hints.guid;
    component->deque = newWorkStealingDeque(pd, (void *) NULL_GUID);
    return (ocrComponent_t*)component;
}

u8 hcWorkComponentCreate(ocrComponent_t *self, ocrLocation_t loc, ocrFatGuid_t* component, ocrFatGuid_t hints, u32 properties) {
//TODO
ASSERT(0);
return OCR_ENOTSUP;
}

u8 hcWorkComponentInsert(ocrComponent_t *self, ocrLocation_t loc, ocrFatGuid_t component, ocrFatGuid_t hints, u32 properties) {
    ASSERT((properties & OCR_COMP_PROP_TYPE) == OCR_COMP_PROP_TYPE_EDT);
    ocrComponentHcWork_t * hcWorkComp = (ocrComponentHcWork_t*)self;
    ASSERT(loc == hcWorkComp->base.mapping);
    hcWorkComp->deque->pushAtTail(hcWorkComp->deque, (void*)component.guid, 0);
    return 0;
}

u8 hcWorkComponentRemove(ocrComponent_t *self, ocrLocation_t loc, ocrFatGuid_t *component, ocrFatGuid_t hints, u32 properties) {
    ocrComponentHcWork_t * hcWorkComp = (ocrComponentHcWork_t*)self;
    ocrGuid_t el;
    if (loc == hcWorkComp->base.mapping) {
        el = (ocrGuid_t)hcWorkComp->deque->popFromTail(hcWorkComp->deque, 0);
    } else {
        el = (ocrGuid_t)hcWorkComp->deque->popFromHead(hcWorkComp->deque, 1);
    }
    if (el != NULL_GUID)
        component->guid = el;
    return 0;
}

u8 hcWorkComponentMove(ocrComponent_t *self, ocrLocation_t loc, struct _ocrComponent_t *destination, ocrFatGuid_t component, ocrFatGuid_t hints, u32 properties) {
//TODO
ASSERT(0);
return OCR_ENOTSUP;
}

u8 hcWorkComponentSplit(ocrComponent_t *self, ocrLocation_t loc, u32 count, u32 *chunks, ocrFatGuid_t *components, ocrFatGuid_t hints, u32 properties) {
//TODO
ASSERT(0);
return OCR_ENOTSUP;
}

u8 hcWorkComponentMerge(ocrComponent_t *self, ocrLocation_t loc, u32 count, ocrFatGuid_t *components, ocrFatGuid_t hints, u32 properties) {
//TODO
ASSERT(0);
return OCR_ENOTSUP;
}

u32 hcWorkComponentCount(ocrComponent_t *self, ocrLocation_t loc) {
//TODO
ASSERT(0);
return OCR_ENOTSUP;
}

ocrComponentFactory_t* newOcrComponentFactoryHcWork(ocrParamList_t *perType) {
    ocrComponentFactory_t* base = (ocrComponentFactory_t*) runtimeChunkAlloc(
        sizeof(ocrComponentFactoryHcWork_t), (void *)1);

    base->instantiate = &newComponentHcWork;
    base->destruct = NULL; //&destructComponentFactoryHcWork;

    base->fcts.create = FUNC_ADDR(u8 (*)(ocrComponent_t*, ocrLocation_t, ocrFatGuid_t*, ocrFatGuid_t, u32), hcWorkComponentCreate);
    base->fcts.insert = FUNC_ADDR(u8 (*)(ocrComponent_t*, ocrLocation_t, ocrFatGuid_t, ocrFatGuid_t, u32), hcWorkComponentInsert);
    base->fcts.remove = FUNC_ADDR(u8 (*)(ocrComponent_t*, ocrLocation_t, ocrFatGuid_t*, ocrFatGuid_t, u32), hcWorkComponentRemove);
    base->fcts.move = FUNC_ADDR(u8 (*)(ocrComponent_t*, ocrLocation_t, ocrComponent_t*, ocrFatGuid_t, ocrFatGuid_t, u32), hcWorkComponentMove);
    base->fcts.split = FUNC_ADDR(u8 (*)(ocrComponent_t*, ocrLocation_t, u32, u32*, ocrFatGuid_t*, ocrFatGuid_t, u32), hcWorkComponentSplit);
    base->fcts.merge = FUNC_ADDR(u8 (*)(ocrComponent_t*, ocrLocation_t, u32, ocrFatGuid_t*, ocrFatGuid_t, u32), hcWorkComponentMerge);
    base->fcts.count = FUNC_ADDR(u32 (*)(ocrComponent_t*, ocrLocation_t), hcWorkComponentCount);

    paramListComponentFactHcWork_t * paramDerived = (paramListComponentFactHcWork_t*)perType;
    ocrComponentFactoryHcWork_t * derived = (ocrComponentFactoryHcWork_t*)base;
    derived->dequeInitSize = paramDerived->dequeInitSize;
    return base;
}

#endif /* ENABLE_COMPONENT_HC_WORK */