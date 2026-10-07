// Smoke test: the three spmc ops in generic form, with the signatures the
// dialect defines (push_back returns i1; pop_front returns (i1, value)).
//   ./build/bin/spmcc ./test/mlir/spmc-ops.mlir

func.func @spmc_ops() -> () {
    %val = arith.constant 42 : i32
    %q = "spmc.create"() {element=i32, capacity=16 : ui32} : () -> !spmc.queue<i32, 16>
    %rc = "spmc.push_back"(%q, %val) : (!spmc.queue<i32, 16>, i32) -> i1
    %ok, %elt = "spmc.pop_front"(%q) : (!spmc.queue<i32, 16>) -> (i1, i32)
    func.return
}
