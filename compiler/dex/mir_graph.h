/*
 * Copyright (C) 2013 The Android Open Source Project
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

#ifndef ART_COMPILER_DEX_MIR_GRAPH_H_
#define ART_COMPILER_DEX_MIR_GRAPH_H_

#include "dex_file.h"
#include "dex_instruction.h"
#include "compiler_ir.h"
#include "arena_bit_vector.h"
#include "growable_array.h"

namespace art {

enum InstructionAnalysisAttributePos {
  kUninterestingOp = 0,
  kArithmeticOp,
  kFPOp,
  kSingleOp,
  kDoubleOp,
  kIntOp,
  kLongOp,
  kBranchOp,
  kInvokeOp,
  kArrayOp,
  kHeavyweightOp,
  kSimpleConstOp,
  kMoveOp,
  kSwitch
};

#define AN_NONE (1 << kUninterestingOp)
#define AN_MATH (1 << kArithmeticOp)
#define AN_FP (1 << kFPOp)
#define AN_LONG (1 << kLongOp)
#define AN_INT (1 << kIntOp)
#define AN_SINGLE (1 << kSingleOp)
#define AN_DOUBLE (1 << kDoubleOp)
#define AN_FLOATMATH (1 << kFPOp)
#define AN_BRANCH (1 << kBranchOp)
#define AN_INVOKE (1 << kInvokeOp)
#define AN_ARRAYOP (1 << kArrayOp)
#define AN_HEAVYWEIGHT (1 << kHeavyweightOp)
#define AN_SIMPLECONST (1 << kSimpleConstOp)
#define AN_MOVE (1 << kMoveOp)
#define AN_SWITCH (1 << kSwitch)
#define AN_COMPUTATIONAL (AN_MATH | AN_ARRAYOP | AN_MOVE | AN_SIMPLECONST)

enum DataFlowAttributePos {
  kUA = 0,
  kUB,
  kUC,
  kAWide,
  kBWide,
  kCWide,
  kDA,
  kIsMove,
  kSetsConst,
  kFormat35c,
  kFormat3rc,
  kNullCheckSrc0,        // Null check of uses[0].
  kNullCheckSrc1,        // Null check of uses[1].
  kNullCheckSrc2,        // Null check of uses[2].
  kNullCheckOut0,        // Null check out outgoing arg0.
  kDstNonNull,           // May assume dst is non-null.
  kRetNonNull,           // May assume retval is non-null.
  kNullTransferSrc0,     // Object copy src[0] -> dst.
  kNullTransferSrcN,     // Phi null check state transfer.
  kRangeCheckSrc1,       // Range check of uses[1].
  kRangeCheckSrc2,       // Range check of uses[2].
  kRangeCheckSrc3,       // Range check of uses[3].
  kFPA,
  kFPB,
  kFPC,
  kCoreA,
  kCoreB,
  kCoreC,
  kRefA,
  kRefB,
  kRefC,
  kUsesMethodStar,       // Implicit use of Method*.
  kDoLVN,                // Worth computing local value numbers.
};

#define DF_NOP                  0ULL
#define DF_UA                   (1ULL << kUA)
#define DF_UB                   (1ULL << kUB)
#define DF_UC                   (1ULL << kUC)
#define DF_A_WIDE               (1ULL << kAWide)
#define DF_B_WIDE               (1ULL << kBWide)
#define DF_C_WIDE               (1ULL << kCWide)
#define DF_DA                   (1ULL << kDA)
#define DF_IS_MOVE              (1ULL << kIsMove)
#define DF_SETS_CONST           (1ULL << kSetsConst)
#define DF_FORMAT_35C           (1ULL << kFormat35c)
#define DF_FORMAT_3RC           (1ULL << kFormat3rc)
#define DF_NULL_CHK_0           (1ULL << kNullCheckSrc0)
#define DF_NULL_CHK_1           (1ULL << kNullCheckSrc1)
#define DF_NULL_CHK_2           (1ULL << kNullCheckSrc2)
#define DF_NULL_CHK_OUT0        (1ULL << kNullCheckOut0)
#define DF_NON_NULL_DST         (1ULL << kDstNonNull)
#define DF_NON_NULL_RET         (1ULL << kRetNonNull)
#define DF_NULL_TRANSFER_0      (1ULL << kNullTransferSrc0)
#define DF_NULL_TRANSFER_N      (1ULL << kNullTransferSrcN)
#define DF_RANGE_CHK_1          (1ULL << kRangeCheckSrc1)
#define DF_RANGE_CHK_2          (1ULL << kRangeCheckSrc2)
#define DF_RANGE_CHK_3          (1ULL << kRangeCheckSrc3)
#define DF_FP_A                 (1ULL << kFPA)
#define DF_FP_B                 (1ULL << kFPB)
#define DF_FP_C                 (1ULL << kFPC)
#define DF_CORE_A               (1ULL << kCoreA)
#define DF_CORE_B               (1ULL << kCoreB)
#define DF_CORE_C               (1ULL << kCoreC)
#define DF_REF_A                (1ULL << kRefA)
#define DF_REF_B                (1ULL << kRefB)
#define DF_REF_C                (1ULL << kRefC)
#define DF_UMS                  (1ULL << kUsesMethodStar)
#define DF_LVN                  (1ULL << kDoLVN)

#define DF_HAS_USES             (DF_UA | DF_UB | DF_UC)

#define DF_HAS_DEFS             (DF_DA)

#define DF_HAS_NULL_CHKS        (DF_NULL_CHK_0 | \
                                 DF_NULL_CHK_1 | \
                                 DF_NULL_CHK_2 | \
                                 DF_NULL_CHK_OUT0)

#define DF_HAS_RANGE_CHKS       (DF_RANGE_CHK_1 | \
                                 DF_RANGE_CHK_2 | \
                                 DF_RANGE_CHK_3)

#define DF_HAS_NR_CHKS          (DF_HAS_NULL_CHKS | \
                                 DF_HAS_RANGE_CHKS)

#define DF_A_IS_REG             (DF_UA | DF_DA)
#define DF_B_IS_REG             (DF_UB)
#define DF_C_IS_REG             (DF_UC)
#define DF_IS_GETTER_OR_SETTER  (DF_IS_GETTER | DF_IS_SETTER)
#define DF_USES_FP              (DF_FP_A | DF_FP_B | DF_FP_C)
#define DF_NULL_TRANSFER        (DF_NULL_TRANSFER_0 | DF_NULL_TRANSFER_N)
enum OatMethodAttributes {
  kIsLeaf,            // Method is leaf.
  kHasLoop,           // Method contains simple loop.
};

#define METHOD_IS_LEAF          (1 << kIsLeaf)
#define METHOD_HAS_LOOP         (1 << kHasLoop)

// Minimum field size to contain Dalvik v_reg number.
#define VREG_NUM_WIDTH 16

#define INVALID_SREG (-1)
#define INVALID_VREG (0xFFFFU)
#define INVALID_REG (0xFF)
#define INVALID_OFFSET (0xDEADF00FU)

/* SSA encodings for special registers */
#define SSA_METHOD_BASEREG (-2)
/* First compiler temp basereg, grows smaller */
#define SSA_CTEMP_BASEREG (SSA_METHOD_BASEREG - 1)

#define MIR_IGNORE_NULL_CHECK           (1 << kMIRIgnoreNullCheck)
#define MIR_NULL_CHECK_ONLY             (1 << kMIRNullCheckOnly)
#define MIR_IGNORE_RANGE_CHECK          (1 << kMIRIgnoreRangeCheck)
#define MIR_RANGE_CHECK_ONLY            (1 << kMIRRangeCheckOnly)
#define MIR_INLINED                     (1 << kMIRInlined)
#define MIR_INLINED_PRED                (1 << kMIRInlinedPred)
#define MIR_CALLEE                      (1 << kMIRCallee)
#define MIR_IGNORE_SUSPEND_CHECK        (1 << kMIRIgnoreSuspendCheck)
#define MIR_DUP                         (1 << kMIRDup)

#define BLOCK_NAME_LEN 80

typedef uint16_t BasicBlockId;
static const BasicBlockId NullBasicBlockId = 0;

/*
 * In general, vreg/sreg describe Dalvik registers that originated with dx.  However,
 * it is useful to have compiler-generated temporary registers and have them treated
 * in the same manner as dx-generated virtual registers.  This struct records the SSA
 * name of compiler-introduced temporaries.
 */
struct CompilerTemp {
  int32_t s_reg;
};

// When debug option enabled, records effectiveness of null and range check elimination.
struct Checkstats {
  int32_t null_checks;
  int32_t null_checks_eliminated;
  int32_t range_checks;
  int32_t range_checks_eliminated;
};

// Dataflow attributes of a basic block.
struct BasicBlockDataFlow {
  ArenaBitVector* use_v;
  ArenaBitVector* def_v;
  ArenaBitVector* live_in_v;
  ArenaBitVector* phi_v;
  int32_t* vreg_to_ssa_map;
  ArenaBitVector* ending_null_check_v;
};

/*
 * Normalized use/def for a MIR operation using SSA names rather than vregs.  Note that
 * uses/defs retain the Dalvik convention that long operations operate on a pair of 32-bit
 * vregs.  For example, "ADD_LONG v0, v2, v3" would have 2 defs (v0/v1) and 4 uses (v2/v3, v4/v5).
 * Following SSA renaming, this is the primary struct used by code generators to locate
 * operand and result registers.  This is a somewhat confusing and unhelpful convention that
 * we may want to revisit in the future.
 */
struct SSARepresentation {
  int16_t num_uses;
  int16_t num_defs;
  int32_t* uses;
  bool* fp_use;
  int32_t* defs;
  bool* fp_def;
};

/*
 * The Midlevel Intermediate Representation node, which may be largely considered a
 * wrapper around a Dalvik byte code.
 */
struct MIR {
  /*
   * TODO: remove embedded DecodedInstruction to save space, keeping only opcode.  Recover
   * additional fields on as-needed basis.  Question: how to support MIR Pseudo-ops; probably
   * need to carry aux data pointer.
   */
  DecodedInstruction dalvikInsn;
  uint16_t width;                 // Note: width can include switch table or fill array data.
  NarrowDexOffset offset;         // Offset of the instruction in code units.
  uint16_t optimization_flags;
  int16_t m_unit_index;           // From which method was this MIR included
  MIR* next;
  SSARepresentation* ssa_rep;
  union {
    // Incoming edges for phi node.
    BasicBlockId* phi_incoming;
    // Establish link between two halves of throwing instructions.
    MIR* throw_insn;
  } meta;
};

struct SuccessorBlockInfo;

struct BasicBlock {
  BasicBlockId id;
  BasicBlockId dfs_id;
  NarrowDexOffset start_offset;     // Offset in code units.
  BasicBlockId fall_through;
  BasicBlockId taken;
  BasicBlockId i_dom;               // Immediate dominator.
  uint16_t nesting_depth;
  BBType block_type:4;
  BlockListType successor_block_list_type:4;
  bool visited:1;
  bool hidden:1;
  bool catch_entry:1;
  bool explicit_throw:1;
  bool conditional_branch:1;
  bool terminated_by_return:1;  // Block ends with a Dalvik return opcode.
  bool dominates_return:1;      // Is a member of return extended basic block.
  bool use_lvn:1;               // Run local value numbering on this block.
  MIR* first_mir_insn;
  MIR* last_mir_insn;
  BasicBlockDataFlow* data_flow_info;
  ArenaBitVector* dominators;
  ArenaBitVector* i_dominated;      // Set nodes being immediately dominated.
  ArenaBitVector* dom_frontier;     // Dominance frontier.
  GrowableArray<BasicBlockId>* predecessors;
  GrowableArray<SuccessorBlockInfo*>* successor_blocks;
};

/*
 * The "blocks" field in "successor_block_list" points to an array of elements with the type
 * "SuccessorBlockInfo".  For catch blocks, key is type index for the exception.  For swtich
 * blocks, key is the case value.
 */
struct SuccessorBlockInfo {
  BasicBlockId block;
  int key;
};

/*
 * Whereas a SSA name describes a definition of a Dalvik vreg, the RegLocation describes
 * the type of an SSA name (and, can also be used by code generators to record where the
 * value is located (i.e. - physical register, frame, spill, etc.).  For each SSA name (SReg)
 * there is a RegLocation.
 * A note on SSA names:
 *   o SSA names for Dalvik vRegs v0..vN will be assigned 0..N.  These represent the "vN_0"
 *     names.  Negative SSA names represent special values not present in the Dalvik byte code.
 *     For example, SSA name -1 represents an invalid SSA name, and SSA name -2 represents the
 *     the Method pointer.  SSA names < -2 are reserved for future use.
 *   o The vN_0 names for non-argument Dalvik should in practice never be used (as they would
 *     represent the read of an undefined local variable).  The first definition of the
 *     underlying Dalvik vReg will result in a vN_1 name.
 *
 * FIXME: The orig_sreg field was added as a workaround for llvm bitcode generation.  With
 * the latest restructuring, we should be able to remove it and rely on s_reg_low throughout.
 */
struct RegLocation {
  RegLocationType location:3;
  unsigned wide:1;
  unsigned defined:1;   // Do we know the type?
  unsigned is_const:1;  // Constant, value in mir_graph->constant_values[].
  unsigned fp:1;        // Floating point?
  unsigned core:1;      // Non-floating point?
  unsigned ref:1;       // Something GC cares about.
  unsigned high_word:1;  // High word of pair?
  unsigned home:1;      // Does this represent the home location?
  VectorLengthType vec_len:3;  // Is this value in a vector register, and how big is it?
  uint8_t low_reg;      // First physical register.
  uint8_t high_reg;     // 2nd physical register (if wide).
  int16_t s_reg_low;    // SSA name for low Dalvik word.
  int16_t orig_sreg;    // TODO: remove after Bitcode gen complete
                        // and consolidate usage w/ s_reg_low.

  bool IsVectorScalar() const { return vec_len == kVectorLength4 || vec_len == kVectorLength8;}
};

/*
 * Collection of information describing an invoke, and the destination of
 * the subsequent MOVE_RESULT (if applicable).  Collected as a unit to enable
 * more efficient invoke code generation.
 */
struct CallInfo {
  int num_arg_words;    // Note: word count, not arg count.
  RegLocation* args;    // One for each word of arguments.
  RegLocation result;   // Eventual target of MOVE_RESULT.
  int opt_flags;
  InvokeType type;
  uint32_t dex_idx;
  uint32_t index;       // Method idx for invokes, type idx for FilledNewArray.
  uintptr_t direct_code;
  uintptr_t direct_method;
  RegLocation target;    // Target of following move_result.
  bool skip_this;
  bool is_range;
  DexOffset offset;      // Offset in code units.
};


const RegLocation bad_loc = {kLocDalvikFrame, 0, 0, 0, 0, 0, 0, 0, 0, kVectorNotUsed,
                             INVALID_REG, INVALID_REG, INVALID_SREG, INVALID_SREG};

class MIRGraph {
 public:
  MIRGraph(CompilationUnit* cu, ArenaAllocator* arena);
  ~MIRGraph();

  /*
   * Examine the graph to determine whether it's worthwile to spend the time compiling
   * this method.
   */
  bool SkipCompilation(Runtime::CompilerFilter compiler_filter);

  /*
   * Parse dex method and add MIR at current insert point.  Returns id (which is
   * actually the index of the method in the m_units_ array).
   */
  void InlineMethod(const DexFile::CodeItem* code_item, uint32_t access_flags,
                    InvokeType invoke_type, uint16_t class_def_idx,
                    uint32_t method_idx, jobject class_loader, const DexFile& dex_file);

  /* Find existing block */
  BasicBlock* FindBlock(DexOffset code_offset) {
    return FindBlock(code_offset, false, false, NULL);
  }

  const uint16_t* GetCurrentInsns() const {
    return current_code_item_->insns_;
  }

  const uint16_t* GetInsns(int m_unit_index) const {
    return m_units_[m_unit_index]->GetCodeItem()->insns_;
  }

  int GetNumBlocks() const {
    return num_blocks_;
  }

  size_t GetNumDalvikInsns() const {
    return cu_->code_item->insns_size_in_code_units_;
  }

  ArenaBitVector* GetTryBlockAddr() const {
    return try_block_addr_;
  }

  BasicBlock* GetEntryBlock() const {
    return entry_block_;
  }

  BasicBlock* GetExitBlock() const {
    return exit_block_;
  }

  BasicBlock* GetBasicBlock(int block_id) const {
    return (block_id == NullBasicBlockId) ? NULL : block_list_.Get(block_id);
  }

  size_t GetBasicBlockListCount() const {
    return block_list_.Size();
  }

  GrowableArray<BasicBlock*>* GetBlockList() {
    return &block_list_;
  }

  GrowableArray<BasicBlockId>* GetDfsOrder() {
    return dfs_order_;
  }

  GrowableArray<BasicBlockId>* GetDfsPostOrder() {
    return dfs_post_order_;
  }

  GrowableArray<BasicBlockId>* GetDomPostOrder() {
    return dom_post_order_traversal_;
  }

  int GetDefCount() const {
    return def_count_;
  }

  ArenaAllocator* GetArena() {
    return arena_;
  }

  void EnableOpcodeCounting() {
    opcode_count_ = static_cast<int*>(arena_->Alloc(kNumPackedOpcodes * sizeof(int),
                                                    ArenaAllocator::kAllocMisc));
  }

  void ShowOpcodeStats();

  DexCompilationUnit* GetCurrentDexCompilationUnit() const {
    return m_units_[current_method_];
  }

  /**
   * @brief Dump a CFG into a dot file format.
   * @param dir_prefix the directory the file will be created in.
   * @param all_blocks does the dumper use all the basic blocks or use the reachable blocks.
   * @param suffix does the filename require a suffix or not (default = nullptr).
   */
  void DumpCFG(const char* dir_prefix, bool all_blocks, const char* suffix = nullptr);

  void InitRegLocations();

  void RemapRegLocations();

  void DumpRegLocTable(RegLocation* table, int count);

  void BasicBlockOptimization();

  bool IsConst(int32_t s_reg) const {
    return is_constant_v_->IsBitSet(s_reg);
  }

  bool IsConst(RegLocation loc) const {
    return loc.orig_sreg < 0 ? false : IsConst(loc.orig_sreg);
  }

  int32_t ConstantValue(RegLocation loc) const {
    DCHECK(IsConst(loc));
    return constant_values_[loc.orig_sreg];
  }

  int32_t ConstantValue(int32_t s_reg) const {
    DCHECK(IsConst(s_reg));
    return constant_values_[s_reg];
  }

  int64_t ConstantValueWide(RegLocation loc) const {
    DCHECK(IsConst(loc));
    return (static_cast<int64_t>(constant_values_[loc.orig_sreg + 1]) << 32) |
        Low32Bits(static_cast<int64_t>(constant_values_[loc.orig_sreg]));
  }

  bool IsConstantNullRef(RegLocation loc) const {
    return loc.ref && loc.is_const && (ConstantValue(loc) == 0);
  }

  int GetNumSSARegs() const {
    return num_ssa_regs_;
  }

  void SetNumSSARegs(int new_num) {
     /*
      * TODO: It's theoretically possible to exceed 32767, though any cases which did
      * would be filtered out with current settings.  When orig_sreg field is removed
      * from RegLocation, expand s_reg_low to handle all possible cases and remove DCHECK().
      */
    DCHECK_EQ(new_num, static_cast<int16_t>(new_num));
    num_ssa_regs_ = new_num;
  }

  unsigned int GetNumReachableBlocks() const {
    return num_reachable_blocks_;
  }

  int GetUseCount(int vreg) const {
    return use_counts_.Get(vreg);
  }

  int GetRawUseCount(int vreg) const {
    return raw_use_counts_.Get(vreg);
  }

  int GetSSASubscript(int ssa_reg) const {
    return ssa_subscripts_->Get(ssa_reg);
  }

  RegLocation GetRawSrc(MIR* mir, int num) {
    DCHECK(num < mir->ssa_rep->num_uses);
    RegLocation res = reg_location_[mir->ssa_rep->uses[num]];
    return res;
  }

  RegLocation GetRawDest(MIR* mir) {
    DCHECK_GT(mir->ssa_rep->num_defs, 0);
    RegLocation res = reg_location_[mir->ssa_rep->defs[0]];
    return res;
  }

  RegLocation GetDest(MIR* mir) {
    RegLocation res = GetRawDest(mir);
    DCHECK(!res.wide);
    return res;
  }

  RegLocation GetSrc(MIR* mir, int num) {
    RegLocation res = GetRawSrc(mir, num);
    DCHECK(!res.wide);
    return res;
  }

  RegLocation GetDestWide(MIR* mir) {
    RegLocation res = GetRawDest(mir);
    DCHECK(res.wide);
    return res;
  }

  RegLocation GetSrcWide(MIR* mir, int low) {
    RegLocation res = GetRawSrc(mir, low);
    DCHECK(res.wide);
    return res;
  }

  RegLocation GetBadLoc() {
    return bad_loc;
  }

  int GetMethodSReg() {
    return method_sreg_;
  }

  bool MethodIsLeaf() {
    return attributes_ & METHOD_IS_LEAF;
  }

  RegLocation GetRegLocation(int index) {
    DCHECK((index >= 0) && (index > num_ssa_regs_));
    return reg_location_[index];
  }

  RegLocation GetMethodLoc() {
    return reg_location_[method_sreg_];
  }

  bool IsBackedge(BasicBlock* branch_bb, BasicBlockId target_bb_id) {
    return ((target_bb_id != NullBasicBlockId) &&
            (GetBasicBlock(target_bb_id)->start_offset <= branch_bb->start_offset));
  }

  bool IsBackwardsBranch(BasicBlock* branch_bb) {
    return IsBackedge(branch_bb, branch_bb->taken) || IsBackedge(branch_bb, branch_bb->fall_through);
  }

  void CountBranch(DexOffset target_offset) {
    if (target_offset <= current_offset_) {
      backward_branches_++;
    } else {
      forward_branches_++;
    }
  }

  int GetBranchCount() {
    return backward_branches_ + forward_branches_;
  }

  bool IsPseudoMirOp(Instruction::Code opcode) {
    return static_cast<int>(opcode) >= static_cast<int>(kMirOpFirst);
  }

  bool IsPseudoMirOp(int opcode) {
    return opcode >= static_cast<int>(kMirOpFirst);
  }

  void DumpCheckStats();
  MIR* FindMoveResult(BasicBlock* bb, MIR* mir);
  int SRegToVReg(int ssa_reg) const;
  void VerifyDataflow();
  void CheckForDominanceFrontier(BasicBlock* dom_bb, const BasicBlock* succ_bb);
  bool EliminateNullChecksAndInferTypes(BasicBlock *bb);
  /*
   * Type inference handling helpers.  Because Dalvik's bytecode is not fully typed,
   * we have to do some work to figure out the sreg type.  For some operations it is
   * clear based on the opcode (i.e. ADD_FLOAT v0, v1, v2), but for others (MOVE), we
   * may never know the "real" type.
   *
   * We perform the type inference operation by using an iterative  walk over
   * the graph, propagating types "defined" by typed opcodes to uses and defs in
   * non-typed opcodes (such as MOVE).  The Setxx(index) helpers are used to set defined
   * types on typed opcodes (such as ADD_INT).  The Setxx(index, is_xx) form is used to
   * propagate types through non-typed opcodes such as PHI and MOVE.  The is_xx flag
   * tells whether our guess of the type is based on a previously typed definition.
   * If so, the defined type takes precedence.  Note that it's possible to have the same sreg
   * show multiple defined types because dx treats constants as untyped bit patterns.
   * The return value of the Setxx() helpers says whether or not the Setxx() action changed
   * the current guess, and is used to know when to terminate the iterative walk.
   */
  bool SetFp(int index, bool is_fp);
  bool SetFp(int index);
  bool SetCore(int index, bool is_core);
  bool SetCore(int index);
  bool SetRef(int index, bool is_ref);
  bool SetRef(int index);
  bool SetWide(int index, bool is_wide);
  bool SetWide(int index);
  bool SetHigh(int index, bool is_high);
  bool SetHigh(int index);

  void AppendMIR(BasicBlock* bb, MIR* mir);
  void PrependMIR(BasicBlock* bb, MIR* mir);
  void InsertMIRAfter(BasicBlock* bb, MIR* current_mir, MIR* new_mir);
  char* GetDalvikDisassembly(const MIR* mir);
  void ReplaceSpecialChars(std::string& str);
  std::string GetSSAName(int ssa_reg);
  std::string GetSSANameWithConst(int ssa_reg, bool singles_only);
  void GetBlockName(BasicBlock* bb, char* name);
  const char* GetShortyFromTargetIdx(int);
  void DumpMIRGraph();
  CallInfo* NewMemCallInfo(BasicBlock* bb, MIR* mir, InvokeType type, bool is_range);
  BasicBlock* NewMemBB(BBType block_type, int block_id);
  MIR* AdvanceMIR(BasicBlock** p_bb, MIR* mir);
  BasicBlock* NextDominatedBlock(BasicBlock* bb);
  bool LayoutBlocks(BasicBlock* bb);

  /**
   * @brief Perform the initial preparation for the Method Uses.
   */
  void InitializeMethodUses();

  /**
   * @brief Perform the initial preparation for the Constant Propagation.
   */
  void InitializeConstantPropagation();

  /**
   * @brief Perform the initial preparation for the SSA Transformation.
   */
  void InitializeSSATransformation();

  /**
   * @brief Insert a the operands for the Phi nodes.
   * @param bb the considered BasicBlock.
   * @return true
   */
  bool InsertPhiNodeOperands(BasicBlock* bb);

  /**
   * @brief Perform constant propagation on a BasicBlock.
   * @param bb the considered BasicBlock.
   */
  void DoConstantPropagation(BasicBlock* bb);

  /**
   * @brief Count the uses in the BasicBlock
   * @param bb the BasicBlock
   */
  void CountUses(struct BasicBlock* bb);

  /**
   * @brief Initialize the data structures with Null Check data
   * @param bb the considered BasicBlock
   */
  void NullCheckEliminationInit(BasicBlock* bb);

  /**
   * @brief Check if the temporary ssa register vector is allocated
   */
  void CheckSSARegisterVector();

  /**
   * @brief Combine BasicBlocks
   * @param the BasicBlock we are considering
   */
  void CombineBlocks(BasicBlock* bb);

  void ClearAllVisitedFlags();
  /*
   * IsDebugBuild sanity check: keep track of the Dex PCs for catch entries so that later on
   * we can verify that all catch entries have native PC entries.
   */
  std::set<uint32_t> catches_;

  // TODO: make these private.
  RegLocation* reg_location_;                         // Map SSA names to location.
  GrowableArray<CompilerTemp*> compiler_temps_;
  SafeMap<unsigned int, unsigned int> block_id_map_;  // Block collapse lookup cache.

  static const uint64_t oat_data_flow_attributes_[kMirOpLast];
  static const char* extended_mir_op_names_[kMirOpLast - kMirOpFirst];
  static const uint32_t analysis_attributes_[kMirOpLast];

 private:
  int FindCommonParent(int block1, int block2);
  void ComputeSuccLineIn(ArenaBitVector* dest, const ArenaBitVector* src1,
                         const ArenaBitVector* src2);
  void HandleLiveInUse(ArenaBitVector* use_v, ArenaBitVector* def_v,
                       ArenaBitVector* live_in_v, int dalvik_reg_id);
  void HandleDef(ArenaBitVector* def_v, int dalvik_reg_id);
  void CompilerInitializeSSAConversion();
  bool DoSSAConversion(BasicBlock* bb);
  bool InvokeUsesMethodStar(MIR* mir);
  int ParseInsn(const uint16_t* code_ptr, DecodedInstruction* decoded_instruction);
  bool ContentIsInsn(const uint16_t* code_ptr);
  BasicBlock* SplitBlock(DexOffset code_offset, BasicBlock* orig_block,
                         BasicBlock** immed_pred_block_p);
  BasicBlock* FindBlock(DexOffset code_offset, bool split, bool create,
                        BasicBlock** immed_pred_block_p);
  void ProcessTryCatchBlocks();
  BasicBlock* ProcessCanBranch(BasicBlock* cur_block, MIR* insn, DexOffset cur_offset, int width,
                               int flags, const uint16_t* code_ptr, const uint16_t* code_end);
  BasicBlock* ProcessCanSwitch(BasicBlock* cur_block, MIR* insn, DexOffset cur_offset, int width,
                               int flags);
  BasicBlock* ProcessCanThrow(BasicBlock* cur_block, MIR* insn, DexOffset cur_offset, int width,
                              int flags, ArenaBitVector* try_block_addr, const uint16_t* code_ptr,
                              const uint16_t* code_end);
  int AddNewSReg(int v_reg);
  void HandleSSAUse(int* uses, int dalvik_reg, int reg_index);
  void HandleSSADef(int* defs, int dalvik_reg, int reg_index);
  void DataFlowSSAFormat35C(MIR* mir);
  void DataFlowSSAFormat3RC(MIR* mir);
  bool FindLocalLiveIn(BasicBlock* bb);
  bool InferTypeAndSize(BasicBlock* bb, MIR* mir, bool changed);
  bool VerifyPredInfo(BasicBlock* bb);
  BasicBlock* NeedsVisit(BasicBlock* bb);
  BasicBlock* NextUnvisitedSuccessor(BasicBlock* bb);
  void MarkPreOrder(BasicBlock* bb);
  void RecordDFSOrders(BasicBlock* bb);
  void ComputeDFSOrders();
  void ComputeDefBlockMatrix();
  void ComputeDomPostOrderTraversal(BasicBlock* bb);
  void ComputeDominators();
  void InsertPhiNodes();
  void DoDFSPreOrderSSARename(BasicBlock* block);
  void SetConstant(int32_t ssa_reg, int value);
  void SetConstantWide(int ssa_reg, int64_t value);
  int GetSSAUseCount(int s_reg);
  bool BasicBlockOpt(BasicBlock* bb);
  bool BuildExtendedBBList(struct BasicBlock* bb);
  bool FillDefBlockMatrix(BasicBlock* bb);
  void InitializeDominationInfo(BasicBlock* bb);
  bool ComputeblockIDom(BasicBlock* bb);
  bool ComputeBlockDominators(BasicBlock* bb);
  bool SetDominators(BasicBlock* bb);
  bool ComputeBlockLiveIns(BasicBlock* bb);
  bool ComputeDominanceFrontier(BasicBlock* bb);

  void CountChecks(BasicBlock* bb);
  void AnalyzeBlock(BasicBlock* bb, struct MethodStats* stats);
  bool ComputeSkipCompilation(struct MethodStats* stats, bool skip_default);

  CompilationUnit* const cu_;
  GrowableArray<int>* ssa_base_vregs_;
  GrowableArray<int>* ssa_subscripts_;
  // Map original Dalvik virtual reg i to the current SSA name.
  int* vreg_to_ssa_map_;            // length == method->registers_size
  int* ssa_last_defs_;              // length == method->registers_size
  ArenaBitVector* is_constant_v_;   // length == num_ssa_reg
  int* constant_values_;            // length == num_ssa_reg
  // Use counts of ssa names.
  GrowableArray<uint32_t> use_counts_;      // Weighted by nesting depth
  GrowableArray<uint32_t> raw_use_counts_;  // Not weighted
  unsigned int num_reachable_blocks_;
  GrowableArray<BasicBlockId>* dfs_order_;
  GrowableArray<BasicBlockId>* dfs_post_order_;
  GrowableArray<BasicBlockId>* dom_post_order_traversal_;
  int* i_dom_list_;
  ArenaBitVector** def_block_matrix_;    // num_dalvik_register x num_blocks.
  ArenaBitVector* temp_block_v_;
  ArenaBitVector* temp_dalvik_register_v_;
  ArenaBitVector* temp_ssa_register_v_;  // num_ssa_regs.
  static const int kInvalidEntry = -1;
  GrowableArray<BasicBlock*> block_list_;
  ArenaBitVector* try_block_addr_;
  BasicBlock* entry_block_;
  BasicBlock* exit_block_;
  int num_blocks_;
  const DexFile::CodeItem* current_code_item_;
  GrowableArray<uint16_t> dex_pc_to_block_map_;  // FindBlock lookup cache.
  std::vector<DexCompilationUnit*> m_units_;     // List of methods included in this graph
  typedef std::pair<int, int> MIRLocation;       // Insert point, (m_unit_ index, offset)
  std::vector<MIRLocation> method_stack_;        // Include stack
  int current_method_;
  DexOffset current_offset_;                     // Offset in code units
  int def_count_;                                // Used to estimate size of ssa name storage.
  int* opcode_count_;                            // Dex opcode coverage stats.
  int num_ssa_regs_;                             // Number of names following SSA transformation.
  std::vector<BasicBlockId> extended_basic_blocks_;  // Heads of block "traces".
  int method_sreg_;
  unsigned int attributes_;
  Checkstats* checkstats_;
  ArenaAllocator* arena_;
  int backward_branches_;
  int forward_branches_;
};

}  // namespace art

#endif  // ART_COMPILER_DEX_MIR_GRAPH_H_
