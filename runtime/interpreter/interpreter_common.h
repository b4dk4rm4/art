/*
 * Copyright (C) 2012 The Android Open Source Project
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

#ifndef ART_RUNTIME_INTERPRETER_INTERPRETER_COMMON_H_
#define ART_RUNTIME_INTERPRETER_INTERPRETER_COMMON_H_

#include "interpreter.h"

#include <math.h>

#include "base/logging.h"
#include "class_linker-inl.h"
#include "common_throws.h"
#include "dex_file-inl.h"
#include "dex_instruction-inl.h"
#include "dex_instruction.h"
#include "entrypoints/entrypoint_utils.h"
#include "gc/accounting/card_table-inl.h"
#include "invoke_arg_array_builder.h"
#include "nth_caller_visitor.h"
#include "mirror/art_field-inl.h"
#include "mirror/art_method.h"
#include "mirror/art_method-inl.h"
#include "mirror/class.h"
#include "mirror/class-inl.h"
#include "mirror/object-inl.h"
#include "mirror/object_array-inl.h"
#include "object_utils.h"
#include "ScopedLocalRef.h"
#include "scoped_thread_state_change.h"
#include "thread.h"
#include "well_known_classes.h"

using ::art::mirror::ArtField;
using ::art::mirror::ArtMethod;
using ::art::mirror::Array;
using ::art::mirror::BooleanArray;
using ::art::mirror::ByteArray;
using ::art::mirror::CharArray;
using ::art::mirror::Class;
using ::art::mirror::ClassLoader;
using ::art::mirror::IntArray;
using ::art::mirror::LongArray;
using ::art::mirror::Object;
using ::art::mirror::ObjectArray;
using ::art::mirror::ShortArray;
using ::art::mirror::String;
using ::art::mirror::Throwable;

namespace art {
namespace interpreter {

// External references to both interpreter implementations.

template<bool do_access_check>
extern JValue ExecuteSwitchImpl(Thread* self, MethodHelper& mh,
                                const DexFile::CodeItem* code_item,
                                ShadowFrame& shadow_frame, JValue result_register);

template<bool do_access_check>
extern JValue ExecuteGotoImpl(Thread* self, MethodHelper& mh,
                              const DexFile::CodeItem* code_item,
                              ShadowFrame& shadow_frame, JValue result_register);

static inline void DoMonitorEnter(Thread* self, Object* ref) NO_THREAD_SAFETY_ANALYSIS {
  ref->MonitorEnter(self);
}

static inline void DoMonitorExit(Thread* self, Object* ref) NO_THREAD_SAFETY_ANALYSIS {
  ref->MonitorExit(self);
}

// Invokes the given method. This is part of the invocation support and is used by DoInvoke and
// DoInvokeVirtualQuick functions.
// Returns true on success, otherwise throws an exception and returns false.
template<bool is_range, bool do_assignability_check>
bool DoCall(ArtMethod* method, Thread* self, ShadowFrame& shadow_frame,
            const Instruction* inst, uint16_t inst_data, JValue* result);

// Handles invoke-XXX/range instructions.
// Returns true on success, otherwise throws an exception and returns false.
template<InvokeType type, bool is_range, bool do_access_check>
static inline bool DoInvoke(Thread* self, ShadowFrame& shadow_frame, const Instruction* inst,
                            uint16_t inst_data, JValue* result) {
  const uint32_t method_idx = (is_range) ? inst->VRegB_3rc() : inst->VRegB_35c();
  const uint32_t vregC = (is_range) ? inst->VRegC_3rc() : inst->VRegC_35c();
  Object* receiver = (type == kStatic) ? nullptr : shadow_frame.GetVRegReference(vregC);
  ArtMethod* const method = FindMethodFromCode<type, do_access_check>(method_idx, receiver,
                                                                      shadow_frame.GetMethod(),
                                                                      self);
  if (UNLIKELY(method == nullptr)) {
    CHECK(self->IsExceptionPending());
    result->SetJ(0);
    return false;
  } else if (UNLIKELY(method->IsAbstract())) {
    ThrowAbstractMethodError(method);
    result->SetJ(0);
    return false;
  } else {
    return DoCall<is_range, do_access_check>(method, self, shadow_frame, inst, inst_data, result);
  }
}

// Handles invoke-virtual-quick and invoke-virtual-quick-range instructions.
// Returns true on success, otherwise throws an exception and returns false.
template<bool is_range>
static inline bool DoInvokeVirtualQuick(Thread* self, ShadowFrame& shadow_frame,
                                        const Instruction* inst, uint16_t inst_data,
                                        JValue* result) {
  const uint32_t vregC = (is_range) ? inst->VRegC_3rc() : inst->VRegC_35c();
  Object* const receiver = shadow_frame.GetVRegReference(vregC);
  if (UNLIKELY(receiver == nullptr)) {
    // We lost the reference to the method index so we cannot get a more
    // precised exception message.
    ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
    return false;
  }
  const uint32_t vtable_idx = (is_range) ? inst->VRegB_3rc() : inst->VRegB_35c();
  ArtMethod* const method = receiver->GetClass()->GetVTable()->GetWithoutChecks(vtable_idx);
  if (UNLIKELY(method == nullptr)) {
    CHECK(self->IsExceptionPending());
    result->SetJ(0);
    return false;
  } else if (UNLIKELY(method->IsAbstract())) {
    ThrowAbstractMethodError(method);
    result->SetJ(0);
    return false;
  } else {
    // No need to check since we've been quickened.
    return DoCall<is_range, false>(method, self, shadow_frame, inst, inst_data, result);
  }
}

// Handles iget-XXX and sget-XXX instructions.
// Returns true on success, otherwise throws an exception and returns false.
template<FindFieldType find_type, Primitive::Type field_type, bool do_access_check>
static inline bool DoFieldGet(Thread* self, ShadowFrame& shadow_frame,
                              const Instruction* inst, uint16_t inst_data) {
  const bool is_static = (find_type == StaticObjectRead) || (find_type == StaticPrimitiveRead);
  const uint32_t field_idx = is_static ? inst->VRegB_21c() : inst->VRegC_22c();
  ArtField* f = FindFieldFromCode<find_type, do_access_check>(field_idx, shadow_frame.GetMethod(), self,
                                                              Primitive::FieldSize(field_type));
  if (UNLIKELY(f == nullptr)) {
    CHECK(self->IsExceptionPending());
    return false;
  }
  Object* obj;
  if (is_static) {
    obj = f->GetDeclaringClass();
  } else {
    obj = shadow_frame.GetVRegReference(inst->VRegB_22c(inst_data));
    if (UNLIKELY(obj == nullptr)) {
      ThrowNullPointerExceptionForFieldAccess(shadow_frame.GetCurrentLocationForThrow(), f, true);
      return false;
    }
  }
  uint32_t vregA = is_static ? inst->VRegA_21c(inst_data) : inst->VRegA_22c(inst_data);
  switch (field_type) {
    case Primitive::kPrimBoolean:
      shadow_frame.SetVReg(vregA, f->GetBoolean(obj));
      break;
    case Primitive::kPrimByte:
      shadow_frame.SetVReg(vregA, f->GetByte(obj));
      break;
    case Primitive::kPrimChar:
      shadow_frame.SetVReg(vregA, f->GetChar(obj));
      break;
    case Primitive::kPrimShort:
      shadow_frame.SetVReg(vregA, f->GetShort(obj));
      break;
    case Primitive::kPrimInt:
      shadow_frame.SetVReg(vregA, f->GetInt(obj));
      break;
    case Primitive::kPrimLong:
      shadow_frame.SetVRegLong(vregA, f->GetLong(obj));
      break;
    case Primitive::kPrimNot:
      shadow_frame.SetVRegReference(vregA, f->GetObject(obj));
      break;
    default:
      LOG(FATAL) << "Unreachable: " << field_type;
  }
  return true;
}

// Handles iget-quick, iget-wide-quick and iget-object-quick instructions.
// Returns true on success, otherwise throws an exception and returns false.
template<Primitive::Type field_type>
static inline bool DoIGetQuick(ShadowFrame& shadow_frame, const Instruction* inst, uint16_t inst_data) {
  Object* obj = shadow_frame.GetVRegReference(inst->VRegB_22c(inst_data));
  if (UNLIKELY(obj == nullptr)) {
    // We lost the reference to the field index so we cannot get a more
    // precised exception message.
    ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
    return false;
  }
  MemberOffset field_offset(inst->VRegC_22c());
  const bool is_volatile = false;  // iget-x-quick only on non volatile fields.
  const uint32_t vregA = inst->VRegA_22c(inst_data);
  switch (field_type) {
    case Primitive::kPrimInt:
      shadow_frame.SetVReg(vregA, static_cast<int32_t>(obj->GetField32(field_offset, is_volatile)));
      break;
    case Primitive::kPrimLong:
      shadow_frame.SetVRegLong(vregA, static_cast<int64_t>(obj->GetField64(field_offset, is_volatile)));
      break;
    case Primitive::kPrimNot:
      shadow_frame.SetVRegReference(vregA, obj->GetFieldObject<mirror::Object*>(field_offset, is_volatile));
      break;
    default:
      LOG(FATAL) << "Unreachable: " << field_type;
  }
  return true;
}

// Handles iput-XXX and sput-XXX instructions.
// Returns true on success, otherwise throws an exception and returns false.
template<FindFieldType find_type, Primitive::Type field_type, bool do_access_check>
static inline bool DoFieldPut(Thread* self, const ShadowFrame& shadow_frame,
                              const Instruction* inst, uint16_t inst_data) {
  bool do_assignability_check = do_access_check;
  bool is_static = (find_type == StaticObjectWrite) || (find_type == StaticPrimitiveWrite);
  uint32_t field_idx = is_static ? inst->VRegB_21c() : inst->VRegC_22c();
  ArtField* f = FindFieldFromCode<find_type, do_access_check>(field_idx, shadow_frame.GetMethod(), self,
                                                              Primitive::FieldSize(field_type));
  if (UNLIKELY(f == nullptr)) {
    CHECK(self->IsExceptionPending());
    return false;
  }
  Object* obj;
  if (is_static) {
    obj = f->GetDeclaringClass();
  } else {
    obj = shadow_frame.GetVRegReference(inst->VRegB_22c(inst_data));
    if (UNLIKELY(obj == nullptr)) {
      ThrowNullPointerExceptionForFieldAccess(shadow_frame.GetCurrentLocationForThrow(),
                                              f, false);
      return false;
    }
  }
  uint32_t vregA = is_static ? inst->VRegA_21c(inst_data) : inst->VRegA_22c(inst_data);
  switch (field_type) {
    case Primitive::kPrimBoolean:
      f->SetBoolean(obj, shadow_frame.GetVReg(vregA));
      break;
    case Primitive::kPrimByte:
      f->SetByte(obj, shadow_frame.GetVReg(vregA));
      break;
    case Primitive::kPrimChar:
      f->SetChar(obj, shadow_frame.GetVReg(vregA));
      break;
    case Primitive::kPrimShort:
      f->SetShort(obj, shadow_frame.GetVReg(vregA));
      break;
    case Primitive::kPrimInt:
      f->SetInt(obj, shadow_frame.GetVReg(vregA));
      break;
    case Primitive::kPrimLong:
      f->SetLong(obj, shadow_frame.GetVRegLong(vregA));
      break;
    case Primitive::kPrimNot: {
      Object* reg = shadow_frame.GetVRegReference(vregA);
      if (do_assignability_check && reg != nullptr) {
        Class* field_class = FieldHelper(f).GetType();
        if (!reg->VerifierInstanceOf(field_class)) {
          // This should never happen.
          self->ThrowNewExceptionF(self->GetCurrentLocationForThrow(),
                                   "Ljava/lang/VirtualMachineError;",
                                   "Put '%s' that is not instance of field '%s' in '%s'",
                                   ClassHelper(reg->GetClass()).GetDescriptor(),
                                   ClassHelper(field_class).GetDescriptor(),
                                   ClassHelper(f->GetDeclaringClass()).GetDescriptor());
          return false;
        }
      }
      f->SetObj(obj, reg);
      break;
    }
    default:
      LOG(FATAL) << "Unreachable: " << field_type;
  }
  return true;
}

// Handles iput-quick, iput-wide-quick and iput-object-quick instructions.
// Returns true on success, otherwise throws an exception and returns false.
template<Primitive::Type field_type>
static inline bool DoIPutQuick(const ShadowFrame& shadow_frame, const Instruction* inst, uint16_t inst_data) {
  Object* obj = shadow_frame.GetVRegReference(inst->VRegB_22c(inst_data));
  if (UNLIKELY(obj == nullptr)) {
    // We lost the reference to the field index so we cannot get a more
    // precised exception message.
    ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
    return false;
  }
  MemberOffset field_offset(inst->VRegC_22c());
  const bool is_volatile = false;  // iput-x-quick only on non volatile fields.
  const uint32_t vregA = inst->VRegA_22c(inst_data);
  switch (field_type) {
    case Primitive::kPrimInt:
      obj->SetField32(field_offset, shadow_frame.GetVReg(vregA), is_volatile);
      break;
    case Primitive::kPrimLong:
      obj->SetField64(field_offset, shadow_frame.GetVRegLong(vregA), is_volatile);
      break;
    case Primitive::kPrimNot:
      obj->SetFieldObject(field_offset, shadow_frame.GetVRegReference(vregA), is_volatile);
      break;
    default:
      LOG(FATAL) << "Unreachable: " << field_type;
  }
  return true;
}

// Handles string resolution for const-string and const-string-jumbo instructions. Also ensures the
// java.lang.String class is initialized.
static inline String* ResolveString(Thread* self, MethodHelper& mh, uint32_t string_idx)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  CHECK(!kMovingMethods);
  Class* java_lang_string_class = String::GetJavaLangString();
  if (UNLIKELY(!java_lang_string_class->IsInitialized())) {
    ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
    SirtRef<mirror::Class> sirt_class(self, java_lang_string_class);
    if (UNLIKELY(!class_linker->EnsureInitialized(sirt_class, true, true))) {
      DCHECK(self->IsExceptionPending());
      return nullptr;
    }
  }
  return mh.ResolveString(string_idx);
}

// Handles div-int, div-int/2addr, div-int/li16 and div-int/lit8 instructions.
// Returns true on success, otherwise throws a java.lang.ArithmeticException and return false.
static inline bool DoIntDivide(ShadowFrame& shadow_frame, size_t result_reg,
                               int32_t dividend, int32_t divisor)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  const int32_t kMinInt = std::numeric_limits<int32_t>::min();
  if (UNLIKELY(divisor == 0)) {
    ThrowArithmeticExceptionDivideByZero();
    return false;
  }
  if (UNLIKELY(dividend == kMinInt && divisor == -1)) {
    shadow_frame.SetVReg(result_reg, kMinInt);
  } else {
    shadow_frame.SetVReg(result_reg, dividend / divisor);
  }
  return true;
}

// Handles rem-int, rem-int/2addr, rem-int/li16 and rem-int/lit8 instructions.
// Returns true on success, otherwise throws a java.lang.ArithmeticException and return false.
static inline bool DoIntRemainder(ShadowFrame& shadow_frame, size_t result_reg,
                                  int32_t dividend, int32_t divisor)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  const int32_t kMinInt = std::numeric_limits<int32_t>::min();
  if (UNLIKELY(divisor == 0)) {
    ThrowArithmeticExceptionDivideByZero();
    return false;
  }
  if (UNLIKELY(dividend == kMinInt && divisor == -1)) {
    shadow_frame.SetVReg(result_reg, 0);
  } else {
    shadow_frame.SetVReg(result_reg, dividend % divisor);
  }
  return true;
}

// Handles div-long and div-long-2addr instructions.
// Returns true on success, otherwise throws a java.lang.ArithmeticException and return false.
static inline bool DoLongDivide(ShadowFrame& shadow_frame, size_t result_reg,
                                int64_t dividend, int64_t divisor)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  const int64_t kMinLong = std::numeric_limits<int64_t>::min();
  if (UNLIKELY(divisor == 0)) {
    ThrowArithmeticExceptionDivideByZero();
    return false;
  }
  if (UNLIKELY(dividend == kMinLong && divisor == -1)) {
    shadow_frame.SetVRegLong(result_reg, kMinLong);
  } else {
    shadow_frame.SetVRegLong(result_reg, dividend / divisor);
  }
  return true;
}

// Handles rem-long and rem-long-2addr instructions.
// Returns true on success, otherwise throws a java.lang.ArithmeticException and return false.
static inline bool DoLongRemainder(ShadowFrame& shadow_frame, size_t result_reg,
                                   int64_t dividend, int64_t divisor)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  const int64_t kMinLong = std::numeric_limits<int64_t>::min();
  if (UNLIKELY(divisor == 0)) {
    ThrowArithmeticExceptionDivideByZero();
    return false;
  }
  if (UNLIKELY(dividend == kMinLong && divisor == -1)) {
    shadow_frame.SetVRegLong(result_reg, 0);
  } else {
    shadow_frame.SetVRegLong(result_reg, dividend % divisor);
  }
  return true;
}

// Handles filled-new-array and filled-new-array-range instructions.
// Returns true on success, otherwise throws an exception and returns false.
template <bool is_range, bool do_access_check>
bool DoFilledNewArray(const Instruction* inst, const ShadowFrame& shadow_frame,
                      Thread* self, JValue* result);

// Handles packed-switch instruction.
// Returns the branch offset to the next instruction to execute.
static inline int32_t DoPackedSwitch(const Instruction* inst, const ShadowFrame& shadow_frame,
                                     uint16_t inst_data)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  DCHECK(inst->Opcode() == Instruction::PACKED_SWITCH);
  const uint16_t* switch_data = reinterpret_cast<const uint16_t*>(inst) + inst->VRegB_31t();
  int32_t test_val = shadow_frame.GetVReg(inst->VRegA_31t(inst_data));
  DCHECK_EQ(switch_data[0], static_cast<uint16_t>(Instruction::kPackedSwitchSignature));
  uint16_t size = switch_data[1];
  DCHECK_GT(size, 0);
  const int32_t* keys = reinterpret_cast<const int32_t*>(&switch_data[2]);
  DCHECK(IsAligned<4>(keys));
  int32_t first_key = keys[0];
  const int32_t* targets = reinterpret_cast<const int32_t*>(&switch_data[4]);
  DCHECK(IsAligned<4>(targets));
  int32_t index = test_val - first_key;
  if (index >= 0 && index < size) {
    return targets[index];
  } else {
    // No corresponding value: move forward by 3 (size of PACKED_SWITCH).
    return 3;
  }
}

// Handles sparse-switch instruction.
// Returns the branch offset to the next instruction to execute.
static inline int32_t DoSparseSwitch(const Instruction* inst, const ShadowFrame& shadow_frame,
                                     uint16_t inst_data)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  DCHECK(inst->Opcode() == Instruction::SPARSE_SWITCH);
  const uint16_t* switch_data = reinterpret_cast<const uint16_t*>(inst) + inst->VRegB_31t();
  int32_t test_val = shadow_frame.GetVReg(inst->VRegA_31t(inst_data));
  DCHECK_EQ(switch_data[0], static_cast<uint16_t>(Instruction::kSparseSwitchSignature));
  uint16_t size = switch_data[1];
  DCHECK_GT(size, 0);
  const int32_t* keys = reinterpret_cast<const int32_t*>(&switch_data[2]);
  DCHECK(IsAligned<4>(keys));
  const int32_t* entries = keys + size;
  DCHECK(IsAligned<4>(entries));
  int lo = 0;
  int hi = size - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int32_t foundVal = keys[mid];
    if (test_val < foundVal) {
      hi = mid - 1;
    } else if (test_val > foundVal) {
      lo = mid + 1;
    } else {
      return entries[mid];
    }
  }
  // No corresponding value: move forward by 3 (size of SPARSE_SWITCH).
  return 3;
}

static inline uint32_t FindNextInstructionFollowingException(Thread* self,
                                                             ShadowFrame& shadow_frame,
                                                             uint32_t dex_pc,
                                                             mirror::Object* this_object,
                                                             const instrumentation::Instrumentation* instrumentation)
    ALWAYS_INLINE;

static inline uint32_t FindNextInstructionFollowingException(Thread* self,
                                                             ShadowFrame& shadow_frame,
                                                             uint32_t dex_pc,
                                                             mirror::Object* this_object,
                                                             const instrumentation::Instrumentation* instrumentation)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  self->VerifyStack();
  ThrowLocation throw_location;
  mirror::Throwable* exception = self->GetException(&throw_location);
  bool clear_exception = false;
  uint32_t found_dex_pc = shadow_frame.GetMethod()->FindCatchBlock(exception->GetClass(), dex_pc,
                                                                   &clear_exception);
  if (found_dex_pc == DexFile::kDexNoIndex) {
    instrumentation->MethodUnwindEvent(self, this_object,
                                       shadow_frame.GetMethod(), dex_pc);
  } else {
    instrumentation->ExceptionCaughtEvent(self, throw_location,
                                          shadow_frame.GetMethod(),
                                          found_dex_pc, exception);
    if (clear_exception) {
      self->ClearException();
    }
  }
  return found_dex_pc;
}

static void UnexpectedOpcode(const Instruction* inst, MethodHelper& mh)
  __attribute__((cold, noreturn, noinline));

static void UnexpectedOpcode(const Instruction* inst, MethodHelper& mh)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  LOG(FATAL) << "Unexpected instruction: " << inst->DumpString(&mh.GetDexFile());
  exit(0);  // Unreachable, keep GCC happy.
}

static inline void TraceExecution(const ShadowFrame& shadow_frame, const Instruction* inst,
                                  const uint32_t dex_pc, MethodHelper& mh)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  constexpr bool kTracing = false;
  if (kTracing) {
#define TRACE_LOG std::cerr
    std::ostringstream oss;
    oss << PrettyMethod(shadow_frame.GetMethod())
        << StringPrintf("\n0x%x: ", dex_pc)
        << inst->DumpString(&mh.GetDexFile()) << "\n";
    for (size_t i = 0; i < shadow_frame.NumberOfVRegs(); ++i) {
      uint32_t raw_value = shadow_frame.GetVReg(i);
      Object* ref_value = shadow_frame.GetVRegReference(i);
      oss << StringPrintf(" vreg%d=0x%08X", i, raw_value);
      if (ref_value != NULL) {
        if (ref_value->GetClass()->IsStringClass() &&
            ref_value->AsString()->GetCharArray() != NULL) {
          oss << "/java.lang.String \"" << ref_value->AsString()->ToModifiedUtf8() << "\"";
        } else {
          oss << "/" << PrettyTypeOf(ref_value);
        }
      }
    }
    TRACE_LOG << oss.str() << "\n";
#undef TRACE_LOG
  }
}

static inline bool IsBackwardBranch(int32_t branch_offset) {
  return branch_offset <= 0;
}

// Explicitly instantiate all DoInvoke functions.
#define EXPLICIT_DO_INVOKE_TEMPLATE_DECL(_type, _is_range, _do_check)                      \
  template SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) ALWAYS_INLINE                       \
  bool DoInvoke<_type, _is_range, _do_check>(Thread* self, ShadowFrame& shadow_frame,      \
                                             const Instruction* inst, uint16_t inst_data,  \
                                             JValue* result)

#define EXPLICIT_DO_INVOKE_ALL_TEMPLATE_DECL(_type)       \
  EXPLICIT_DO_INVOKE_TEMPLATE_DECL(_type, false, false);  \
  EXPLICIT_DO_INVOKE_TEMPLATE_DECL(_type, false, true);   \
  EXPLICIT_DO_INVOKE_TEMPLATE_DECL(_type, true, false);   \
  EXPLICIT_DO_INVOKE_TEMPLATE_DECL(_type, true, true);

EXPLICIT_DO_INVOKE_ALL_TEMPLATE_DECL(kStatic);      // invoke-static/range.
EXPLICIT_DO_INVOKE_ALL_TEMPLATE_DECL(kDirect);      // invoke-direct/range.
EXPLICIT_DO_INVOKE_ALL_TEMPLATE_DECL(kVirtual);     // invoke-virtual/range.
EXPLICIT_DO_INVOKE_ALL_TEMPLATE_DECL(kSuper);       // invoke-super/range.
EXPLICIT_DO_INVOKE_ALL_TEMPLATE_DECL(kInterface);   // invoke-interface/range.
#undef EXPLICIT_DO_INVOKE_ALL_TEMPLATE_DECL
#undef EXPLICIT_DO_INVOKE_TEMPLATE_DECL

// Explicitly instantiate all DoFieldGet functions.
#define EXPLICIT_DO_FIELD_GET_TEMPLATE_DECL(_find_type, _field_type, _do_check)                \
  template SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) ALWAYS_INLINE                           \
  bool DoFieldGet<_find_type, _field_type, _do_check>(Thread* self, ShadowFrame& shadow_frame, \
                                                      const Instruction* inst, uint16_t inst_data)

#define EXPLICIT_DO_FIELD_GET_ALL_TEMPLATE_DECL(_find_type, _field_type)  \
    EXPLICIT_DO_FIELD_GET_TEMPLATE_DECL(_find_type, _field_type, false);  \
    EXPLICIT_DO_FIELD_GET_TEMPLATE_DECL(_find_type, _field_type, true);

// iget-XXX
EXPLICIT_DO_FIELD_GET_ALL_TEMPLATE_DECL(InstancePrimitiveRead, Primitive::kPrimBoolean);
EXPLICIT_DO_FIELD_GET_ALL_TEMPLATE_DECL(InstancePrimitiveRead, Primitive::kPrimByte);
EXPLICIT_DO_FIELD_GET_ALL_TEMPLATE_DECL(InstancePrimitiveRead, Primitive::kPrimChar);
EXPLICIT_DO_FIELD_GET_ALL_TEMPLATE_DECL(InstancePrimitiveRead, Primitive::kPrimShort);
EXPLICIT_DO_FIELD_GET_ALL_TEMPLATE_DECL(InstancePrimitiveRead, Primitive::kPrimInt);
EXPLICIT_DO_FIELD_GET_ALL_TEMPLATE_DECL(InstancePrimitiveRead, Primitive::kPrimLong);
EXPLICIT_DO_FIELD_GET_ALL_TEMPLATE_DECL(InstanceObjectRead, Primitive::kPrimNot);

// sget-XXX
EXPLICIT_DO_FIELD_GET_ALL_TEMPLATE_DECL(StaticPrimitiveRead, Primitive::kPrimBoolean);
EXPLICIT_DO_FIELD_GET_ALL_TEMPLATE_DECL(StaticPrimitiveRead, Primitive::kPrimByte);
EXPLICIT_DO_FIELD_GET_ALL_TEMPLATE_DECL(StaticPrimitiveRead, Primitive::kPrimChar);
EXPLICIT_DO_FIELD_GET_ALL_TEMPLATE_DECL(StaticPrimitiveRead, Primitive::kPrimShort);
EXPLICIT_DO_FIELD_GET_ALL_TEMPLATE_DECL(StaticPrimitiveRead, Primitive::kPrimInt);
EXPLICIT_DO_FIELD_GET_ALL_TEMPLATE_DECL(StaticPrimitiveRead, Primitive::kPrimLong);
EXPLICIT_DO_FIELD_GET_ALL_TEMPLATE_DECL(StaticObjectRead, Primitive::kPrimNot);

#undef EXPLICIT_DO_FIELD_GET_ALL_TEMPLATE_DECL
#undef EXPLICIT_DO_FIELD_GET_TEMPLATE_DECL

// Explicitly instantiate all DoFieldPut functions.
#define EXPLICIT_DO_FIELD_PUT_TEMPLATE_DECL(_find_type, _field_type, _do_check)                      \
  template SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) ALWAYS_INLINE                                 \
  bool DoFieldPut<_find_type, _field_type, _do_check>(Thread* self, const ShadowFrame& shadow_frame, \
                                                      const Instruction* inst, uint16_t inst_data)

#define EXPLICIT_DO_FIELD_PUT_ALL_TEMPLATE_DECL(_find_type, _field_type)  \
    EXPLICIT_DO_FIELD_PUT_TEMPLATE_DECL(_find_type, _field_type, false);  \
    EXPLICIT_DO_FIELD_PUT_TEMPLATE_DECL(_find_type, _field_type, true);

// iput-XXX
EXPLICIT_DO_FIELD_PUT_ALL_TEMPLATE_DECL(InstancePrimitiveWrite, Primitive::kPrimBoolean);
EXPLICIT_DO_FIELD_PUT_ALL_TEMPLATE_DECL(InstancePrimitiveWrite, Primitive::kPrimByte);
EXPLICIT_DO_FIELD_PUT_ALL_TEMPLATE_DECL(InstancePrimitiveWrite, Primitive::kPrimChar);
EXPLICIT_DO_FIELD_PUT_ALL_TEMPLATE_DECL(InstancePrimitiveWrite, Primitive::kPrimShort);
EXPLICIT_DO_FIELD_PUT_ALL_TEMPLATE_DECL(InstancePrimitiveWrite, Primitive::kPrimInt);
EXPLICIT_DO_FIELD_PUT_ALL_TEMPLATE_DECL(InstancePrimitiveWrite, Primitive::kPrimLong);
EXPLICIT_DO_FIELD_PUT_ALL_TEMPLATE_DECL(InstanceObjectWrite, Primitive::kPrimNot);

// sput-XXX
EXPLICIT_DO_FIELD_PUT_ALL_TEMPLATE_DECL(StaticPrimitiveWrite, Primitive::kPrimBoolean);
EXPLICIT_DO_FIELD_PUT_ALL_TEMPLATE_DECL(StaticPrimitiveWrite, Primitive::kPrimByte);
EXPLICIT_DO_FIELD_PUT_ALL_TEMPLATE_DECL(StaticPrimitiveWrite, Primitive::kPrimChar);
EXPLICIT_DO_FIELD_PUT_ALL_TEMPLATE_DECL(StaticPrimitiveWrite, Primitive::kPrimShort);
EXPLICIT_DO_FIELD_PUT_ALL_TEMPLATE_DECL(StaticPrimitiveWrite, Primitive::kPrimInt);
EXPLICIT_DO_FIELD_PUT_ALL_TEMPLATE_DECL(StaticPrimitiveWrite, Primitive::kPrimLong);
EXPLICIT_DO_FIELD_PUT_ALL_TEMPLATE_DECL(StaticObjectWrite, Primitive::kPrimNot);

#undef EXPLICIT_DO_FIELD_PUT_ALL_TEMPLATE_DECL
#undef EXPLICIT_DO_FIELD_PUT_TEMPLATE_DECL

// Explicitly instantiate all DoInvokeVirtualQuick functions.
#define EXPLICIT_DO_INVOKE_VIRTUAL_QUICK_TEMPLATE_DECL(_is_range)                    \
  template SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) ALWAYS_INLINE                 \
  bool DoInvokeVirtualQuick<_is_range>(Thread* self, ShadowFrame& shadow_frame,      \
                                       const Instruction* inst, uint16_t inst_data,  \
                                       JValue* result)

EXPLICIT_DO_INVOKE_VIRTUAL_QUICK_TEMPLATE_DECL(false);  // invoke-virtual-quick.
EXPLICIT_DO_INVOKE_VIRTUAL_QUICK_TEMPLATE_DECL(true);   // invoke-virtual-quick-range.
#undef EXPLICIT_INSTANTIATION_DO_INVOKE_VIRTUAL_QUICK

// Explicitly instantiate all DoIGetQuick functions.
#define EXPLICIT_DO_IGET_QUICK_TEMPLATE_DECL(_field_type)                            \
  template SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) ALWAYS_INLINE                 \
  bool DoIGetQuick<_field_type>(ShadowFrame& shadow_frame, const Instruction* inst,  \
                                uint16_t inst_data)

EXPLICIT_DO_IGET_QUICK_TEMPLATE_DECL(Primitive::kPrimInt);    // iget-quick.
EXPLICIT_DO_IGET_QUICK_TEMPLATE_DECL(Primitive::kPrimLong);   // iget-wide-quick.
EXPLICIT_DO_IGET_QUICK_TEMPLATE_DECL(Primitive::kPrimNot);    // iget-object-quick.
#undef EXPLICIT_DO_IGET_QUICK_TEMPLATE_DECL

// Explicitly instantiate all DoIPutQuick functions.
#define EXPLICIT_DO_IPUT_QUICK_TEMPLATE_DECL(_field_type)                                  \
  template SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) ALWAYS_INLINE                       \
  bool DoIPutQuick<_field_type>(const ShadowFrame& shadow_frame, const Instruction* inst,  \
                                uint16_t inst_data)

EXPLICIT_DO_IPUT_QUICK_TEMPLATE_DECL(Primitive::kPrimInt);    // iget-quick.
EXPLICIT_DO_IPUT_QUICK_TEMPLATE_DECL(Primitive::kPrimLong);   // iget-wide-quick.
EXPLICIT_DO_IPUT_QUICK_TEMPLATE_DECL(Primitive::kPrimNot);    // iget-object-quick.
#undef EXPLICIT_DO_IPUT_QUICK_TEMPLATE_DECL

}  // namespace interpreter
}  // namespace art

#endif  // ART_RUNTIME_INTERPRETER_INTERPRETER_COMMON_H_
