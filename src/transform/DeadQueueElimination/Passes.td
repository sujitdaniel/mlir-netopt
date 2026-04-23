#ifndef SRC_TRANSFORM_DEADQUEUEELMINATION_PASSES_TD_
#define SRC_TRANSFORM_DEADQUEUEELMINATION_PASSES_TD_

include "mlir/Pass/PassBase.td"

def DeadQueueElimination : Pass<"dead-queue-elimination"> {
    let summary = "Eliminate dead SPMC queues";
    let description = [{
        Performs dead code elimination on unused spmc.create operations.
    }];
    let dependentDialects = ["mlir::spmc::SpmcDialect"];
}

#endif // SRC_TRANSFORM_DEADQUEUEELMINATION_PASSES_TD_
