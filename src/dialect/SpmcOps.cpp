#include "src/dialect/SpmcOps.h"
#include "src/dialect/SpmcTypes.h"

#include <llvm/Support/Casting.h>
#include <llvm/Support/LogicalResult.h>
#include <llvm/Support/raw_ostream.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/Diagnostics.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/Interfaces/InferTypeOpInterface.h>

namespace mlir {
namespace spmc {

llvm::LogicalResult CreateOp::inferReturnTypes(
    MLIRContext *context, std::optional<Location> location, ValueRange operands,
    DictionaryAttr attributes, OpaqueProperties properties, RegionRange regions,
    llvm::SmallVectorImpl<Type> &inferredReturnTypes) {
  CreateOp::Adaptor adaptor(operands, attributes, properties, regions);
  Type eltType = adaptor.getElement();
  unsigned capacity = adaptor.getCapacity();

  QueueType qType = QueueType::get(context, eltType, capacity);
  inferredReturnTypes.emplace_back(qType);
  return success();
}

llvm::LogicalResult
PopOp::inferReturnTypes(MLIRContext *context, std::optional<Location> location,
                        ValueRange operands, DictionaryAttr attributes,
                        OpaqueProperties properties, RegionRange regions,
                        llvm::SmallVectorImpl<Type> &inferredReturnTypes) {
  PopOp::Adaptor adaptor(operands, attributes, properties, regions);
  QueueType qType =
      llvm::dyn_cast<spmc::QueueType>(adaptor.getQueue().getType());

  if (!qType)
    return emitOptionalError(location, "Expected queue operand");
  inferredReturnTypes.emplace_back(IntegerType::get(context, 1));
  inferredReturnTypes.emplace_back(qType.getElement());
  return success();
}

} // namespace spmc
} // namespace mlir
