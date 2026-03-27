#include "dialect/SpmcDialect.h"

#include <mlir/IR/AsmState.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/Parser/Parser.h>

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <mlir/IR/DialectRegistry.h>
#include <mlir/InitAllDialects.h>
#include <mlir/InitAllPasses.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Pass/PassRegistry.h>
#include <mlir/Tools/mlir-opt/MlirOptMain.h>

int main(int argc, char **argv) {
  mlir::DialectRegistry registry;
  registry.insert<mlir::spmc::SpmcDialect>();
  mlir::registerAllDialects(registry);
  mlir::registerAllPasses();

  mlir::registerAsmPrinterCLOptions();
  mlir::registerMLIRContextCLOptions();

  return mlir::asMainReturnCode(
      mlir::MlirOptMain(argc, argv, "Spmc Pass Driver", registry));
}
