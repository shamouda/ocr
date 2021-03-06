#include "perfs.h"
#include "ocr.h"

// DESC: Create a producer event and 'FAN_OUT' consumer event depending on it.
// TIME: Satisfying an event that has 'FAN_OUT' dependences
// FREQ: Done 'NB_ITERS' times.
//
// VARIABLES
// - NB_ITERS
// - FAN_OUT

#define PRODUCER_EVENT_TYPE_IS_LATCH 1
#define PRODUCER_EVENT_TYPE  OCR_EVENT_LATCH_T
#define CONSUMER_EVENT_TYPE  OCR_EVENT_LATCH_T

// Cannot clean-up iteration for non-persistent events
#define CLEAN_UP_ITERATION   0

#define TIME_SATISFY 0
#define TIME_ADD_DEP 1
#define TIME_CONSUMER_CREATE 0
#define TIME_CONSUMER_DESTRUCT 0

#include "event2FanOutEvent.ctpl"
