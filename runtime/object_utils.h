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

#ifndef ART_RUNTIME_OBJECT_UTILS_H_
#define ART_RUNTIME_OBJECT_UTILS_H_

#include "class_linker-inl.h"
#include "dex_file.h"
#include "monitor.h"
#include "mirror/art_field.h"
#include "mirror/art_method.h"
#include "mirror/class.h"
#include "mirror/dex_cache.h"
#include "mirror/iftable.h"
#include "mirror/string.h"

#include "runtime.h"
#include "sirt_ref.h"

#include <string>

namespace art {

template <typename T>
class ObjectLock {
 public:
  explicit ObjectLock(Thread* self, const SirtRef<T>* object)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      : self_(self), obj_(object) {
    CHECK(object != nullptr);
    CHECK(object->get() != nullptr);
    obj_->get()->MonitorEnter(self_);
  }

  ~ObjectLock() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    obj_->get()->MonitorExit(self_);
  }

  void WaitIgnoringInterrupts() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    Monitor::Wait(self_, obj_->get(), 0, 0, false, kWaiting);
  }

  void Notify() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    obj_->get()->Notify(self_);
  }

  void NotifyAll() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    obj_->get()->NotifyAll(self_);
  }

 private:
  Thread* const self_;
  const SirtRef<T>* const obj_;
  DISALLOW_COPY_AND_ASSIGN(ObjectLock);
};

class ClassHelper {
 public:
  explicit ClassHelper(const mirror::Class* c )
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      : interface_type_list_(NULL),
        klass_(NULL) {
    if (c != NULL) {
      ChangeClass(c);
    }
  }

  void ChangeClass(const mirror::Class* new_c)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    CHECK(new_c != NULL) << "klass_=" << klass_;  // Log what we were changing from if any
    if (!new_c->IsClass()) {
      LOG(FATAL) << "new_c=" << new_c << " cc " << new_c->GetClass() << " ccc "
          << ((new_c->GetClass() != nullptr) ? new_c->GetClass()->GetClass() : NULL);
    }
    klass_ = new_c;
    interface_type_list_ = NULL;
  }

  // The returned const char* is only guaranteed to be valid for the lifetime of the ClassHelper.
  // If you need it longer, copy it into a std::string.
  const char* GetDescriptor() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    CHECK(klass_ != NULL);
    if (UNLIKELY(klass_->IsArrayClass())) {
      return GetArrayDescriptor();
    } else if (UNLIKELY(klass_->IsPrimitive())) {
      return Primitive::Descriptor(klass_->GetPrimitiveType());
    } else if (UNLIKELY(klass_->IsProxyClass())) {
      descriptor_ = GetClassLinker()->GetDescriptorForProxy(klass_);
      return descriptor_.c_str();
    } else {
      const DexFile& dex_file = GetDexFile();
      const DexFile::TypeId& type_id = dex_file.GetTypeId(GetClassDef()->class_idx_);
      return dex_file.GetTypeDescriptor(type_id);
    }
  }

  const char* GetArrayDescriptor() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    std::string result("[");
    const mirror::Class* saved_klass = klass_;
    CHECK(saved_klass != NULL);
    ChangeClass(klass_->GetComponentType());
    result += GetDescriptor();
    ChangeClass(saved_klass);
    descriptor_ = result;
    return descriptor_.c_str();
  }

  const DexFile::ClassDef* GetClassDef() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    DCHECK(klass_ != nullptr);
    uint16_t class_def_idx = klass_->GetDexClassDefIndex();
    if (class_def_idx == DexFile::kDexNoIndex16) {
      return nullptr;
    }
    return &GetDexFile().GetClassDef(class_def_idx);
  }

  uint32_t NumDirectInterfaces() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    DCHECK(klass_ != NULL);
    if (klass_->IsPrimitive()) {
      return 0;
    } else if (klass_->IsArrayClass()) {
      return 2;
    } else if (klass_->IsProxyClass()) {
      return klass_->GetIfTable()->GetLength();
    } else {
      const DexFile::TypeList* interfaces = GetInterfaceTypeList();
      if (interfaces == NULL) {
        return 0;
      } else {
        return interfaces->Size();
      }
    }
  }

  uint16_t GetDirectInterfaceTypeIdx(uint32_t idx)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    DCHECK(klass_ != NULL);
    DCHECK(!klass_->IsPrimitive());
    DCHECK(!klass_->IsArrayClass());
    return GetInterfaceTypeList()->GetTypeItem(idx).type_idx_;
  }

  mirror::Class* GetDirectInterface(uint32_t idx)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    DCHECK(klass_ != NULL);
    DCHECK(!klass_->IsPrimitive());
    if (klass_->IsArrayClass()) {
      if (idx == 0) {
        return GetClassLinker()->FindSystemClass("Ljava/lang/Cloneable;");
      } else {
        DCHECK_EQ(1U, idx);
        return GetClassLinker()->FindSystemClass("Ljava/io/Serializable;");
      }
    } else if (klass_->IsProxyClass()) {
      return klass_->GetIfTable()->GetInterface(idx);
    } else {
      uint16_t type_idx = GetDirectInterfaceTypeIdx(idx);
      mirror::Class* interface = GetDexCache()->GetResolvedType(type_idx);
      if (interface == NULL) {
        interface = GetClassLinker()->ResolveType(GetDexFile(), type_idx, klass_);
        CHECK(interface != NULL || Thread::Current()->IsExceptionPending());
      }
      return interface;
    }
  }

  const char* GetSourceFile() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    std::string descriptor(GetDescriptor());
    const DexFile& dex_file = GetDexFile();
    const DexFile::ClassDef* dex_class_def = GetClassDef();
    CHECK(dex_class_def != NULL);
    return dex_file.GetSourceFile(*dex_class_def);
  }

  std::string GetLocation() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    mirror::DexCache* dex_cache = GetDexCache();
    if (dex_cache != NULL && !klass_->IsProxyClass()) {
      return dex_cache->GetLocation()->ToModifiedUtf8();
    } else {
      // Arrays and proxies are generated and have no corresponding dex file location.
      return "generated class";
    }
  }

  const DexFile& GetDexFile() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return *GetDexCache()->GetDexFile();
  }

  mirror::DexCache* GetDexCache() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return klass_->GetDexCache();
  }

 private:
  const DexFile::TypeList* GetInterfaceTypeList()
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    const DexFile::TypeList* result = interface_type_list_;
    if (result == NULL) {
      const DexFile::ClassDef* class_def = GetClassDef();
      if (class_def != NULL) {
        result =  GetDexFile().GetInterfacesList(*class_def);
        interface_type_list_ = result;
      }
    }
    return result;
  }

  ClassLinker* GetClassLinker() ALWAYS_INLINE {
    return Runtime::Current()->GetClassLinker();
  }

  const DexFile::TypeList* interface_type_list_;
  const mirror::Class* klass_;
  std::string descriptor_;

  DISALLOW_COPY_AND_ASSIGN(ClassHelper);
};

class FieldHelper {
 public:
  FieldHelper() : field_(NULL) {}
  explicit FieldHelper(const mirror::ArtField* f) : field_(f) {}

  void ChangeField(const mirror::ArtField* new_f) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    DCHECK(new_f != NULL);
    field_ = new_f;
  }

  const char* GetName() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    uint32_t field_index = field_->GetDexFieldIndex();
    if (UNLIKELY(field_->GetDeclaringClass()->IsProxyClass())) {
      DCHECK(field_->IsStatic());
      DCHECK_LT(field_index, 2U);
      return field_index == 0 ? "interfaces" : "throws";
    }
    const DexFile& dex_file = GetDexFile();
    return dex_file.GetFieldName(dex_file.GetFieldId(field_index));
  }

  mirror::Class* GetType(bool resolve = true) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    uint32_t field_index = field_->GetDexFieldIndex();
    if (UNLIKELY(field_->GetDeclaringClass()->IsProxyClass())) {
      return GetClassLinker()->FindSystemClass(GetTypeDescriptor());
    }
    const DexFile& dex_file = GetDexFile();
    const DexFile::FieldId& field_id = dex_file.GetFieldId(field_index);
    mirror::Class* type = GetDexCache()->GetResolvedType(field_id.type_idx_);
    if (resolve && (type == NULL)) {
      type = GetClassLinker()->ResolveType(field_id.type_idx_, field_);
      CHECK(type != NULL || Thread::Current()->IsExceptionPending());
    }
    return type;
  }

  const char* GetTypeDescriptor() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    uint32_t field_index = field_->GetDexFieldIndex();
    if (UNLIKELY(field_->GetDeclaringClass()->IsProxyClass())) {
      DCHECK(field_->IsStatic());
      DCHECK_LT(field_index, 2U);
      // 0 == Class[] interfaces; 1 == Class[][] throws;
      return field_index == 0 ? "[Ljava/lang/Class;" : "[[Ljava/lang/Class;";
    }
    const DexFile& dex_file = GetDexFile();
    const DexFile::FieldId& field_id = dex_file.GetFieldId(field_index);
    return dex_file.GetFieldTypeDescriptor(field_id);
  }

  Primitive::Type GetTypeAsPrimitiveType()
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return Primitive::GetType(GetTypeDescriptor()[0]);
  }

  bool IsPrimitiveType() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    Primitive::Type type = GetTypeAsPrimitiveType();
    return type != Primitive::kPrimNot;
  }

  size_t FieldSize() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    Primitive::Type type = GetTypeAsPrimitiveType();
    return Primitive::FieldSize(type);
  }

  // The returned const char* is only guaranteed to be valid for the lifetime of the FieldHelper.
  // If you need it longer, copy it into a std::string.
  const char* GetDeclaringClassDescriptor()
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    uint32_t field_index = field_->GetDexFieldIndex();
    if (UNLIKELY(field_->GetDeclaringClass()->IsProxyClass())) {
      DCHECK(field_->IsStatic());
      DCHECK_LT(field_index, 2U);
      // 0 == Class[] interfaces; 1 == Class[][] throws;
      ClassHelper kh(field_->GetDeclaringClass());
      declaring_class_descriptor_ = kh.GetDescriptor();
      return declaring_class_descriptor_.c_str();
    }
    const DexFile& dex_file = GetDexFile();
    const DexFile::FieldId& field_id = dex_file.GetFieldId(field_index);
    return dex_file.GetFieldDeclaringClassDescriptor(field_id);
  }

 private:
  mirror::DexCache* GetDexCache() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return field_->GetDeclaringClass()->GetDexCache();
  }
  ClassLinker* GetClassLinker() ALWAYS_INLINE {
    return Runtime::Current()->GetClassLinker();
  }
  const DexFile& GetDexFile() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return *GetDexCache()->GetDexFile();
  }
  const mirror::ArtField* field_;
  std::string declaring_class_descriptor_;

  DISALLOW_COPY_AND_ASSIGN(FieldHelper);
};

class MethodHelper {
 public:
  MethodHelper()
     : method_(NULL), shorty_(NULL),
       shorty_len_(0) {}

  explicit MethodHelper(const mirror::ArtMethod* m)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      : method_(NULL), shorty_(NULL), shorty_len_(0) {
    SetMethod(m);
  }

  void ChangeMethod(mirror::ArtMethod* new_m) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    DCHECK(new_m != NULL);
    SetMethod(new_m);
    shorty_ = NULL;
  }

  const mirror::ArtMethod* GetMethod() const {
    return method_;
  }

  const char* GetName() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    const DexFile& dex_file = GetDexFile();
    uint32_t dex_method_idx = method_->GetDexMethodIndex();
    if (LIKELY(dex_method_idx != DexFile::kDexNoIndex)) {
      return dex_file.GetMethodName(dex_file.GetMethodId(dex_method_idx));
    } else {
      Runtime* runtime = Runtime::Current();
      if (method_ == runtime->GetResolutionMethod()) {
        return "<runtime internal resolution method>";
      } else if (method_ == runtime->GetImtConflictMethod()) {
        return "<runtime internal imt conflict method>";
      } else if (method_ == runtime->GetCalleeSaveMethod(Runtime::kSaveAll)) {
        return "<runtime internal callee-save all registers method>";
      } else if (method_ == runtime->GetCalleeSaveMethod(Runtime::kRefsOnly)) {
        return "<runtime internal callee-save reference registers method>";
      } else if (method_ == runtime->GetCalleeSaveMethod(Runtime::kRefsAndArgs)) {
        return "<runtime internal callee-save reference and argument registers method>";
      } else {
        return "<unknown runtime internal method>";
      }
    }
  }

  mirror::String* GetNameAsString() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    const DexFile& dex_file = GetDexFile();
    uint32_t dex_method_idx = method_->GetDexMethodIndex();
    const DexFile::MethodId& method_id = dex_file.GetMethodId(dex_method_idx);
    SirtRef<mirror::DexCache> dex_cache(Thread::Current(), GetDexCache());
    return GetClassLinker()->ResolveString(dex_file, method_id.name_idx_, dex_cache);
  }

  const char* GetShorty() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    const char* result = shorty_;
    if (result == NULL) {
      const DexFile& dex_file = GetDexFile();
      result = dex_file.GetMethodShorty(dex_file.GetMethodId(method_->GetDexMethodIndex()),
                                        &shorty_len_);
      shorty_ = result;
    }
    return result;
  }

  uint32_t GetShortyLength() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    if (shorty_ == NULL) {
      GetShorty();
    }
    return shorty_len_;
  }

  const Signature GetSignature() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    const DexFile& dex_file = GetDexFile();
    uint32_t dex_method_idx = method_->GetDexMethodIndex();
    if (dex_method_idx != DexFile::kDexNoIndex) {
      return dex_file.GetMethodSignature(dex_file.GetMethodId(dex_method_idx));
    } else {
      return Signature::NoSignature();
    }
  }

  const DexFile::ProtoId& GetPrototype() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    const DexFile& dex_file = GetDexFile();
    return dex_file.GetMethodPrototype(dex_file.GetMethodId(method_->GetDexMethodIndex()));
  }

  const DexFile::TypeList* GetParameterTypeList() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    const DexFile::ProtoId& proto = GetPrototype();
    return GetDexFile().GetProtoParameters(proto);
  }

  mirror::Class* GetReturnType(bool resolve = true) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    const DexFile& dex_file = GetDexFile();
    const DexFile::MethodId& method_id = dex_file.GetMethodId(method_->GetDexMethodIndex());
    const DexFile::ProtoId& proto_id = dex_file.GetMethodPrototype(method_id);
    uint16_t return_type_idx = proto_id.return_type_idx_;
    return GetClassFromTypeIdx(return_type_idx, resolve);
  }

  const char* GetReturnTypeDescriptor() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    const DexFile& dex_file = GetDexFile();
    const DexFile::MethodId& method_id = dex_file.GetMethodId(method_->GetDexMethodIndex());
    const DexFile::ProtoId& proto_id = dex_file.GetMethodPrototype(method_id);
    uint16_t return_type_idx = proto_id.return_type_idx_;
    return dex_file.GetTypeDescriptor(dex_file.GetTypeId(return_type_idx));
  }

  int32_t GetLineNumFromDexPC(uint32_t dex_pc) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    if (dex_pc == DexFile::kDexNoIndex) {
      return method_->IsNative() ? -2 : -1;
    } else {
      const DexFile& dex_file = GetDexFile();
      return dex_file.GetLineNumFromPC(method_, dex_pc);
    }
  }

  const char* GetDeclaringClassDescriptor() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    const DexFile& dex_file = GetDexFile();
    uint32_t dex_method_idx = method_->GetDexMethodIndex();
    if (UNLIKELY(dex_method_idx == DexFile::kDexNoIndex)) {
      return "<runtime method>";
    }
    return dex_file.GetMethodDeclaringClassDescriptor(dex_file.GetMethodId(dex_method_idx));
  }

  const char* GetDeclaringClassSourceFile() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return ClassHelper(method_->GetDeclaringClass()).GetSourceFile();
  }

  uint16_t GetClassDefIndex() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return method_->GetDeclaringClass()->GetDexClassDefIndex();
  }

  const DexFile::ClassDef& GetClassDef() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return GetDexFile().GetClassDef(GetClassDefIndex());
  }

  mirror::ClassLoader* GetClassLoader() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return method_->GetDeclaringClass()->GetClassLoader();
  }

  bool IsStatic() const SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return method_->IsStatic();
  }

  bool IsClassInitializer() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return method_->IsConstructor() && IsStatic();
  }

  size_t NumArgs() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    // "1 +" because the first in Args is the receiver.
    // "- 1" because we don't count the return type.
    return (IsStatic() ? 0 : 1) + GetShortyLength() - 1;
  }

  // Get the primitive type associated with the given parameter.
  Primitive::Type GetParamPrimitiveType(size_t param) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    CHECK_LT(param, NumArgs());
    if (IsStatic()) {
      param++;  // 0th argument must skip return value at start of the shorty
    } else if (param == 0) {
      return Primitive::kPrimNot;
    }
    return Primitive::GetType(GetShorty()[param]);
  }

  // Is the specified parameter a long or double, where parameter 0 is 'this' for instance methods.
  bool IsParamALongOrDouble(size_t param) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    Primitive::Type type = GetParamPrimitiveType(param);
    return type == Primitive::kPrimLong || type == Primitive::kPrimDouble;
  }

  // Is the specified parameter a reference, where parameter 0 is 'this' for instance methods.
  bool IsParamAReference(size_t param) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return GetParamPrimitiveType(param) == Primitive::kPrimNot;
  }

  bool HasSameNameAndSignature(MethodHelper* other)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    const DexFile& dex_file = GetDexFile();
    const DexFile::MethodId& mid = dex_file.GetMethodId(method_->GetDexMethodIndex());
    if (GetDexCache() == other->GetDexCache()) {
      const DexFile::MethodId& other_mid =
          dex_file.GetMethodId(other->method_->GetDexMethodIndex());
      return mid.name_idx_ == other_mid.name_idx_ && mid.proto_idx_ == other_mid.proto_idx_;
    }
    const DexFile& other_dex_file = other->GetDexFile();
    const DexFile::MethodId& other_mid =
        other_dex_file.GetMethodId(other->method_->GetDexMethodIndex());
    if (!DexFileStringEquals(&dex_file, mid.name_idx_,
                             &other_dex_file, other_mid.name_idx_)) {
      return false;  // Name mismatch.
    }
    return dex_file.GetMethodSignature(mid) == other_dex_file.GetMethodSignature(other_mid);
  }

  const DexFile::CodeItem* GetCodeItem()
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return GetDexFile().GetCodeItem(method_->GetCodeItemOffset());
  }

  bool IsResolvedTypeIdx(uint16_t type_idx) const
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return method_->GetDexCacheResolvedTypes()->Get(type_idx) != NULL;
  }

  mirror::Class* GetClassFromTypeIdx(uint16_t type_idx, bool resolve = true)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    mirror::Class* type = method_->GetDexCacheResolvedTypes()->Get(type_idx);
    if (type == NULL && resolve) {
      type = GetClassLinker()->ResolveType(type_idx, method_);
      CHECK(type != NULL || Thread::Current()->IsExceptionPending());
    }
    return type;
  }

  const char* GetTypeDescriptorFromTypeIdx(uint16_t type_idx)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    const DexFile& dex_file = GetDexFile();
    return dex_file.GetTypeDescriptor(dex_file.GetTypeId(type_idx));
  }

  mirror::Class* GetDexCacheResolvedType(uint16_t type_idx)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return method_->GetDexCacheResolvedTypes()->Get(type_idx);
  }

  const DexFile& GetDexFile() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return *GetDexCache()->GetDexFile();
  }

  mirror::DexCache* GetDexCache() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return method_->GetDeclaringClass()->GetDexCache();
  }

  mirror::String* ResolveString(uint32_t string_idx) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    mirror::String* s = method_->GetDexCacheStrings()->Get(string_idx);
    if (UNLIKELY(s == NULL)) {
      SirtRef<mirror::DexCache> dex_cache(Thread::Current(), GetDexCache());
      s = GetClassLinker()->ResolveString(GetDexFile(), string_idx, dex_cache);
    }
    return s;
  }

  uint32_t FindDexMethodIndexInOtherDexFile(const DexFile& other_dexfile)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    const DexFile& dexfile = GetDexFile();
    if (&dexfile == &other_dexfile) {
      return method_->GetDexMethodIndex();
    }
    const DexFile::MethodId& mid = dexfile.GetMethodId(method_->GetDexMethodIndex());
    const char* mid_declaring_class_descriptor = dexfile.StringByTypeIdx(mid.class_idx_);
    const DexFile::StringId* other_descriptor =
        other_dexfile.FindStringId(mid_declaring_class_descriptor);
    if (other_descriptor != nullptr) {
      const DexFile::TypeId* other_type_id =
          other_dexfile.FindTypeId(other_dexfile.GetIndexForStringId(*other_descriptor));
      if (other_type_id != nullptr) {
        const char* mid_name = dexfile.GetMethodName(mid);
        const DexFile::StringId* other_name = other_dexfile.FindStringId(mid_name);
        if (other_name != nullptr) {
          uint16_t other_return_type_idx;
          std::vector<uint16_t> other_param_type_idxs;
          bool success = other_dexfile.CreateTypeList(dexfile.GetMethodSignature(mid).ToString(),
                                                      &other_return_type_idx,
                                                      &other_param_type_idxs);
          if (success) {
            const DexFile::ProtoId* other_sig =
                other_dexfile.FindProtoId(other_return_type_idx, other_param_type_idxs);
            if (other_sig != nullptr) {
              const  DexFile::MethodId* other_mid = other_dexfile.FindMethodId(*other_type_id,
                                                                               *other_name,
                                                                               *other_sig);
              if (other_mid != nullptr) {
                return other_dexfile.GetIndexForMethodId(*other_mid);
              }
            }
          }
        }
      }
    }
    return DexFile::kDexNoIndex;
  }

 private:
  // Set the method_ field, for proxy methods looking up the interface method via the resolved
  // methods table.
  void SetMethod(const mirror::ArtMethod* method) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    if (method != NULL) {
      mirror::Class* klass = method->GetDeclaringClass();
      if (UNLIKELY(klass->IsProxyClass())) {
        mirror::ArtMethod* interface_method =
            method->GetDexCacheResolvedMethods()->Get(method->GetDexMethodIndex());
        DCHECK(interface_method != NULL);
        DCHECK(interface_method == GetClassLinker()->FindMethodForProxy(klass, method));
        method = interface_method;
      }
    }
    method_ = method;
  }

  ClassLinker* GetClassLinker() ALWAYS_INLINE {
    return Runtime::Current()->GetClassLinker();
  }

  const mirror::ArtMethod* method_;
  const char* shorty_;
  uint32_t shorty_len_;

  DISALLOW_COPY_AND_ASSIGN(MethodHelper);
};

}  // namespace art

#endif  // ART_RUNTIME_OBJECT_UTILS_H_
