// RUN: /Volumes/workplace/mlir-netsec/build/bin/spmcc --dead-queue-elimination %s | FileCheck %s

// Test 1: Simple dead queue case - should be eliminated
// CHECK-LABEL: @dead_queue_is_eliminated
// CHECK-NOT: spmc.create
func.func @dead_queue_is_eliminated() -> () {
    %unused_queue = "spmc.create"() {element=i32, capacity=16 : ui32} : () -> !spmc.queue<i32, 16>
    func.return
}

// Test 6: Queue in control flow - should be eliminated if unused
// CHECK-LABEL: @queue_in_control_flow
// CHECK-NOT: spmc.create
func.func @queue_in_control_flow(%cond: i1) -> () {
    cf.cond_br %cond, ^bb1, ^bb2
    ^bb1:
        %q = "spmc.create"() {element=i32, capacity=16 : ui32} : () -> !spmc.queue<i32, 16>
        func.return
    ^bb2:
        func.return
}

// Test 8: Queue as function parameter - should NOT be eliminated
// CHECK-LABEL: @queue_is_function_parameter
func.func @queue_is_function_parameter(%q: !spmc.queue<i32, 16>) -> () {
    func.return
}
