/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <llvm/Support/Threading.h>

#include "compiler_internals.h"
#include "driver/compiler_driver.h"
#include "dataflow_iterator-inl.h"
#include "leb128.h"
#include "mirror/object.h"
#include "pass_driver.h"
#include "runtime.h"
#include "backend.h"
#include "base/logging.h"
#include "base/timing_logger.h"

#if defined(ART_USE_PORTABLE_COMPILER)
#include "dex/portable/mir_to_gbc.h"
#include "llvm/llvm_compilation_unit.h"
#endif

#include "dex/quick/dex_file_to_method_inliner_map.h"

namespace {
#if !defined(ART_USE_PORTABLE_COMPILER)
  pthread_once_t llvm_multi_init = PTHREAD_ONCE_INIT;
#endif
  void InitializeLLVMForQuick() {
    ::llvm::llvm_start_multithreaded();
  }
}

namespace art {
namespace llvm {
::llvm::Module* makeLLVMModuleContents(::llvm::Module* module);
}

LLVMInfo::LLVMInfo() {
#if !defined(ART_USE_PORTABLE_COMPILER)
  pthread_once(&llvm_multi_init, InitializeLLVMForQuick);
#endif
  // Create context, module, intrinsic helper & ir builder
  llvm_context_.reset(new ::llvm::LLVMContext());
  llvm_module_ = new ::llvm::Module("art", *llvm_context_);
  ::llvm::StructType::create(*llvm_context_, "JavaObject");
  art::llvm::makeLLVMModuleContents(llvm_module_);
  intrinsic_helper_.reset(new art::llvm::IntrinsicHelper(*llvm_context_, *llvm_module_));
  ir_builder_.reset(new art::llvm::IRBuilder(*llvm_context_, *llvm_module_, *intrinsic_helper_));
}

LLVMInfo::~LLVMInfo() {
}

extern "C" void ArtInitQuickCompilerContext(art::CompilerDriver& driver) {
  CHECK(driver.GetCompilerContext() == NULL);
}

extern "C" void ArtUnInitQuickCompilerContext(art::CompilerDriver& driver) {
  CHECK(driver.GetCompilerContext() == NULL);
}

/* Default optimizer/debug setting for the compiler. */
static uint32_t kCompilerOptimizerDisableFlags = 0 |  // Disable specific optimizations
  (1 << kLoadStoreElimination) |
  // (1 << kLoadHoisting) |
  // (1 << kSuppressLoads) |
  // (1 << kNullCheckElimination) |
  // (1 << kPromoteRegs) |
  // (1 << kTrackLiveTemps) |
  // (1 << kSafeOptimizations) |
  // (1 << kBBOpt) |
  // (1 << kMatch) |
  // (1 << kPromoteCompilerTemps) |
  // (1 << kSuppressExceptionEdges) |
  0;

static uint32_t kCompilerDebugFlags = 0 |     // Enable debug/testing modes
  // (1 << kDebugDisplayMissingTargets) |
  // (1 << kDebugVerbose) |
  // (1 << kDebugDumpCFG) |
  // (1 << kDebugSlowFieldPath) |
  // (1 << kDebugSlowInvokePath) |
  // (1 << kDebugSlowStringPath) |
  // (1 << kDebugSlowestFieldPath) |
  // (1 << kDebugSlowestStringPath) |
  // (1 << kDebugExerciseResolveMethod) |
  // (1 << kDebugVerifyDataflow) |
  // (1 << kDebugShowMemoryUsage) |
  // (1 << kDebugShowNops) |
  // (1 << kDebugCountOpcodes) |
  // (1 << kDebugDumpCheckStats) |
  // (1 << kDebugDumpBitcodeFile) |
  // (1 << kDebugVerifyBitcode) |
  // (1 << kDebugShowSummaryMemoryUsage) |
  // (1 << kDebugShowFilterStats) |
  // (1 << kDebugTimings) |
  0;

CompilationUnit::CompilationUnit(ArenaPool* pool)
  : compiler_driver(NULL),
    class_linker(NULL),
    dex_file(NULL),
    class_loader(NULL),
    class_def_idx(0),
    method_idx(0),
    code_item(NULL),
    access_flags(0),
    invoke_type(kDirect),
    shorty(NULL),
    disable_opt(0),
    enable_debug(0),
    verbose(false),
    compiler_backend(kNoBackend),
    instruction_set(kNone),
    num_dalvik_registers(0),
    insns(NULL),
    num_ins(0),
    num_outs(0),
    num_regs(0),
    num_compiler_temps(0),
    compiler_flip_match(false),
    arena(pool),
    mir_graph(NULL),
    cg(NULL),
    timings("QuickCompiler", true, false) {
}

CompilationUnit::~CompilationUnit() {
}

// TODO: Add a cumulative version of logging, and combine with dex2oat --dump-timing
void CompilationUnit::StartTimingSplit(const char* label) {
  if (enable_debug & (1 << kDebugTimings)) {
    timings.StartSplit(label);
  }
}

void CompilationUnit::NewTimingSplit(const char* label) {
  if (enable_debug & (1 << kDebugTimings)) {
    timings.NewSplit(label);
  }
}

void CompilationUnit::EndTiming() {
  if (enable_debug & (1 << kDebugTimings)) {
    timings.EndSplit();
    LOG(INFO) << "TIMINGS " << PrettyMethod(method_idx, *dex_file);
    LOG(INFO) << Dumpable<TimingLogger>(timings);
  }
}

static CompiledMethod* CompileMethod(CompilerDriver& compiler,
                                     const CompilerBackend compiler_backend,
                                     const DexFile::CodeItem* code_item,
                                     uint32_t access_flags, InvokeType invoke_type,
                                     uint16_t class_def_idx, uint32_t method_idx,
                                     jobject class_loader, const DexFile& dex_file
#if defined(ART_USE_PORTABLE_COMPILER)
                                     , llvm::LlvmCompilationUnit* llvm_compilation_unit
#endif
) {
  VLOG(compiler) << "Compiling " << PrettyMethod(method_idx, dex_file) << "...";
  if (code_item->insns_size_in_code_units_ >= 0x10000) {
    LOG(INFO) << "Method size exceeds compiler limits: " << code_item->insns_size_in_code_units_
              << " in " << PrettyMethod(method_idx, dex_file);
    return NULL;
  }

  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  CompilationUnit cu(&compiler.GetArenaPool());

  cu.compiler_driver = &compiler;
  cu.class_linker = class_linker;
  cu.instruction_set = compiler.GetInstructionSet();
  cu.compiler_backend = compiler_backend;
  DCHECK((cu.instruction_set == kThumb2) ||
         (cu.instruction_set == kX86) ||
         (cu.instruction_set == kMips));


  /* Adjust this value accordingly once inlining is performed */
  cu.num_dalvik_registers = code_item->registers_size_;
  // TODO: set this from command line
  cu.compiler_flip_match = false;
  bool use_match = !cu.compiler_method_match.empty();
  bool match = use_match && (cu.compiler_flip_match ^
      (PrettyMethod(method_idx, dex_file).find(cu.compiler_method_match) !=
       std::string::npos));
  if (!use_match || match) {
    cu.disable_opt = kCompilerOptimizerDisableFlags;
    cu.enable_debug = kCompilerDebugFlags;
    cu.verbose = VLOG_IS_ON(compiler) ||
        (cu.enable_debug & (1 << kDebugVerbose));
  }

  /*
   * TODO: rework handling of optimization and debug flags.  Should we split out
   * MIR and backend flags?  Need command-line setting as well.
   */

  if (compiler_backend == kPortable) {
    // Fused long branches not currently useful in bitcode.
    cu.disable_opt |=
        (1 << kBranchFusing) |
        (1 << kSuppressExceptionEdges);
  }

  if (cu.instruction_set == kMips) {
    // Disable some optimizations for mips for now
    cu.disable_opt |= (
        (1 << kLoadStoreElimination) |
        (1 << kLoadHoisting) |
        (1 << kSuppressLoads) |
        (1 << kNullCheckElimination) |
        (1 << kPromoteRegs) |
        (1 << kTrackLiveTemps) |
        (1 << kSafeOptimizations) |
        (1 << kBBOpt) |
        (1 << kMatch) |
        (1 << kPromoteCompilerTemps));
  }

  cu.StartTimingSplit("BuildMIRGraph");
  cu.mir_graph.reset(new MIRGraph(&cu, &cu.arena));

  /* Gathering opcode stats? */
  if (kCompilerDebugFlags & (1 << kDebugCountOpcodes)) {
    cu.mir_graph->EnableOpcodeCounting();
  }

  /* Build the raw MIR graph */
  cu.mir_graph->InlineMethod(code_item, access_flags, invoke_type, class_def_idx, method_idx,
                              class_loader, dex_file);

  cu.NewTimingSplit("MIROpt:CheckFilters");
#if !defined(ART_USE_PORTABLE_COMPILER)
  if (cu.mir_graph->SkipCompilation(Runtime::Current()->GetCompilerFilter())) {
    return NULL;
  }
#endif

  /* Create the pass driver and launch it */
  PassDriver driver(&cu);
  driver.Launch();

  if (cu.enable_debug & (1 << kDebugDumpCheckStats)) {
    cu.mir_graph->DumpCheckStats();
  }

  if (kCompilerDebugFlags & (1 << kDebugCountOpcodes)) {
    cu.mir_graph->ShowOpcodeStats();
  }

  /* Reassociate sreg names with original Dalvik vreg names. */
  cu.mir_graph->RemapRegLocations();

  CompiledMethod* result = NULL;

#if defined(ART_USE_PORTABLE_COMPILER)
  if (compiler_backend == kPortable) {
    cu.cg.reset(PortableCodeGenerator(&cu, cu.mir_graph.get(), &cu.arena, llvm_compilation_unit));
  } else {
#endif
    switch (compiler.GetInstructionSet()) {
      case kThumb2:
        cu.cg.reset(ArmCodeGenerator(&cu, cu.mir_graph.get(), &cu.arena));
        break;
      case kMips:
        cu.cg.reset(MipsCodeGenerator(&cu, cu.mir_graph.get(), &cu.arena));
        break;
      case kX86:
        cu.cg.reset(X86CodeGenerator(&cu, cu.mir_graph.get(), &cu.arena));
        break;
      default:
        LOG(FATAL) << "Unexpected instruction set: " << compiler.GetInstructionSet();
    }
#if defined(ART_USE_PORTABLE_COMPILER)
  }
#endif

  cu.cg->Materialize();

  cu.NewTimingSplit("Dedupe");  /* deduping takes up the vast majority of time in GetCompiledMethod(). */
  result = cu.cg->GetCompiledMethod();
  cu.NewTimingSplit("Cleanup");

  if (result) {
    VLOG(compiler) << "Compiled " << PrettyMethod(method_idx, dex_file);
  } else {
    VLOG(compiler) << "Deferred " << PrettyMethod(method_idx, dex_file);
  }

  if (cu.enable_debug & (1 << kDebugShowMemoryUsage)) {
    if (cu.arena.BytesAllocated() > (5 * 1024 *1024)) {
      MemStats mem_stats(cu.arena);
      LOG(INFO) << PrettyMethod(method_idx, dex_file) << " " << Dumpable<MemStats>(mem_stats);
    }
  }

  if (cu.enable_debug & (1 << kDebugShowSummaryMemoryUsage)) {
    LOG(INFO) << "MEMINFO " << cu.arena.BytesAllocated() << " " << cu.mir_graph->GetNumBlocks()
              << " " << PrettyMethod(method_idx, dex_file);
  }

  cu.EndTiming();
  return result;
}

CompiledMethod* CompileOneMethod(CompilerDriver& compiler,
                                 const CompilerBackend backend,
                                 const DexFile::CodeItem* code_item,
                                 uint32_t access_flags,
                                 InvokeType invoke_type,
                                 uint16_t class_def_idx,
                                 uint32_t method_idx,
                                 jobject class_loader,
                                 const DexFile& dex_file,
                                 llvm::LlvmCompilationUnit* llvm_compilation_unit) {
  return CompileMethod(compiler, backend, code_item, access_flags, invoke_type, class_def_idx,
                       method_idx, class_loader, dex_file
#if defined(ART_USE_PORTABLE_COMPILER)
                       , llvm_compilation_unit
#endif
                       );  // NOLINT(whitespace/parens)
}

}  // namespace art

extern "C" art::CompiledMethod*
    ArtQuickCompileMethod(art::CompilerDriver& compiler,
                          const art::DexFile::CodeItem* code_item,
                          uint32_t access_flags, art::InvokeType invoke_type,
                          uint16_t class_def_idx, uint32_t method_idx, jobject class_loader,
                          const art::DexFile& dex_file) {
  // TODO: check method fingerprint here to determine appropriate backend type.  Until then, use build default
  art::CompilerBackend backend = compiler.GetCompilerBackend();
  return art::CompileOneMethod(compiler, backend, code_item, access_flags, invoke_type,
                               class_def_idx, method_idx, class_loader, dex_file,
                               NULL /* use thread llvm_info */);
}
