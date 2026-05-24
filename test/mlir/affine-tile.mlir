// ./build/bin/spmcc --pass-pipeline="builtin.module(func.func(affine-loop-tile{tile-size=512}))" ./test/mlir/affine-tile.mlir

module {
    func.func @tile_push_back() -> () {
        %q = "spmc.create"() {element=i32, capacity=16 : ui32} : () -> !spmc.queue<i32, 16>
        affine.for %i = 0 to 4096 {
            %v = arith.index_cast %i : index to i32
            %rc = "spmc.push_back"(%q, %v) : (!spmc.queue<i32, 16>, i32) -> i1
        }
        func.return
    }
}
