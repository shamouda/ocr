/**
 * @brief Configuration for the OCR runtime
 **/

/*
 * This file is subject to the license agreement located in the file LICENSE
 * and cannot be distributed without it. This notice cannot be
 * removed or modified.
 */


#ifndef __OCR_CONFIG_H__
#define __OCR_CONFIG_H__

// Define this if building the PD builder program
// If this is defined, this will exclude everything
// that does not contribute to building the policy domain
//#define ENABLE_BUILDER_ONLY

// Allocator
#define ENABLE_ALLOCATOR_TLSF
#define ENABLE_ALLOCATOR_SIMPLE

// CommApi
#define ENABLE_COMM_API_HANDLELESS

// Comm-platform
#define ENABLE_COMM_PLATFORM_CE

// Comp-platform
//#define ENABLE_COMP_PLATFORM_PTHREAD
#define ENABLE_COMP_PLATFORM_FSIM

// Comp-target
#define ENABLE_COMP_TARGET_PASSTHROUGH

// Scheduler Objects
#define ENABLE_SCHEDULER_OBJECT_NULL

// Scheduler Heuristic
#define ENABLE_SCHEDULER_HEURISTIC_NULL

// Datablock
#define ENABLE_DATABLOCK_REGULAR
#define ENABLE_DATABLOCK_LOCKABLE

// Event
#define ENABLE_EVENT_HC

// External things (mostly needed by the INI parser)

// GUID provider
#define ENABLE_GUID_PTR

// HAL layer to use
#define HAL_FSIM_CE

// SAL layer to use
#define SAL_FSIM_CE

// Mem-platform
#define ENABLE_MEM_PLATFORM_FSIM

// Mem-target
#define ENABLE_MEM_TARGET_SHARED

// Policy domain
#define ENABLE_POLICY_DOMAIN_CE
#define ENABLE_MSG_SEQID

// Scheduler
#define ENABLE_SCHEDULER_CE

// SAL layer to use
#define SAL_FSIM_CE

// Sysboot layer to use
#define ENABLE_SYSBOOT_FSIM

// Task
#define ENABLE_TASK_HC

// Task template
#define ENABLE_TASKTEMPLATE_HC

// Worker
#define ENABLE_WORKER_CE

// Workpile
#define ENABLE_WORKPILE_CE

// Enable legacy support
//#define ENABLE_EXTENSION_LEGACY
#endif /* __OCR_CONFIG_H__ */


