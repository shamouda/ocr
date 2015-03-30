#include "perfs.h"
#include "ocr.h"

// DESC: One worker creates all the tasks that have 'PARAMC_SIZE' paramc arguments.
//       Sink EDT depends on all tasks through the output-event of a finish EDT.
// TIME: Completion of all tasks
// FREQ: Create 'NB_INSTANCES' EDTs once

#define PARAMC_SIZE 5

#include "edtExecuteFinishSync0.ctpl"
