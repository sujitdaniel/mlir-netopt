#include "LoopStateAccessAnalysis.h"

#include "src/dialect/SpmcOps.h"

#include <mlir/Dialect/Affine/IR/AffineOps.h>
#include <optional>

namespace mlir {
namespace spmc {

LoopStateAccessAnalysis::LoopStateAccessAnalysis(Operation *op) {
  op->walk([&](affine::AffineForOp forOp) { analyzeLoop(forOp); });
}

void LoopStateAccessAnalysis::analyzeLoop(affine::AffineForOp forOp) {
  LoopAccessInfo info;

  checkSpmcOps(forOp, info);

  info.isParallelizable = !info.hasSpmc;

  loopInfoMap.emplace_or_assign(forOp.getOperation(), std::move(info));
}

void LoopStateAccessAnalysis::checkSpmcOps(affine::AffineForOp forOp,
                                           LoopAccessInfo &info) {
  forOp.walk([&](Operation *op) {
    if (isa<spmc::PushOp, spmc::PopOp>(op)) {
      info.hasSpmc = true;
      info.blockingOps.push_back(op);
    }
  });
}

bool LoopStateAccessAnalysis::isParallelizable(
    affine::AffineForOp forOp) const {
  auto it = loopInfoMap.find(forOp.getOperation());
  if (it == loopInfoMap.end()) {
    return false;
  }
  return it->second.isParallelizable;
}

const std::optional<LoopAccessInfo>
LoopStateAccessAnalysis::getAccessInfo(affine::AffineForOp forOp) const {
  auto it = loopInfoMap.find(forOp.getOperation());
  if (it == loopInfoMap.end()) {
    return std::nullopt;
  }
  return it->second;
}

} // namespace spmc
} // namespace mlir
