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

#include "interpreter_common.h"

namespace art {
namespace interpreter {

// Hand select a number of methods to be run in a not yet started runtime without using JNI.
static void UnstartedRuntimeJni(Thread* self, ArtMethod* method,
                                Object* receiver, uint32_t* args, JValue* result)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  std::string name(PrettyMethod(method));
  if (name == "java.lang.ClassLoader dalvik.system.VMStack.getCallingClassLoader()") {
    result->SetL(NULL);
  } else if (name == "java.lang.Class dalvik.system.VMStack.getStackClass2()") {
    NthCallerVisitor visitor(self, 3);
    visitor.WalkStack();
    result->SetL(visitor.caller->GetDeclaringClass());
  } else if (name == "double java.lang.Math.log(double)") {
    JValue value;
    value.SetJ((static_cast<uint64_t>(args[1]) << 32) | args[0]);
    result->SetD(log(value.GetD()));
  } else if (name == "java.lang.String java.lang.Class.getNameNative()") {
    result->SetL(receiver->AsClass()->ComputeName());
  } else if (name == "int java.lang.Float.floatToRawIntBits(float)") {
    result->SetI(args[0]);
  } else if (name == "float java.lang.Float.intBitsToFloat(int)") {
    result->SetI(args[0]);
  } else if (name == "double java.lang.Math.exp(double)") {
    JValue value;
    value.SetJ((static_cast<uint64_t>(args[1]) << 32) | args[0]);
    result->SetD(exp(value.GetD()));
  } else if (name == "java.lang.Object java.lang.Object.internalClone()") {
    result->SetL(receiver->Clone(self));
  } else if (name == "void java.lang.Object.notifyAll()") {
    receiver->NotifyAll(self);
  } else if (name == "int java.lang.String.compareTo(java.lang.String)") {
    String* rhs = reinterpret_cast<Object*>(args[0])->AsString();
    CHECK(rhs != NULL);
    result->SetI(receiver->AsString()->CompareTo(rhs));
  } else if (name == "java.lang.String java.lang.String.intern()") {
    result->SetL(receiver->AsString()->Intern());
  } else if (name == "int java.lang.String.fastIndexOf(int, int)") {
    result->SetI(receiver->AsString()->FastIndexOf(args[0], args[1]));
  } else if (name == "java.lang.Object java.lang.reflect.Array.createMultiArray(java.lang.Class, int[])") {
    result->SetL(Array::CreateMultiArray(self, reinterpret_cast<Object*>(args[0])->AsClass(), reinterpret_cast<Object*>(args[1])->AsIntArray()));
  } else if (name == "java.lang.Object java.lang.Throwable.nativeFillInStackTrace()") {
    ScopedObjectAccessUnchecked soa(self);
    result->SetL(soa.Decode<Object*>(self->CreateInternalStackTrace(soa)));
  } else if (name == "boolean java.nio.ByteOrder.isLittleEndian()") {
    result->SetJ(JNI_TRUE);
  } else if (name == "boolean sun.misc.Unsafe.compareAndSwapInt(java.lang.Object, long, int, int)") {
    Object* obj = reinterpret_cast<Object*>(args[0]);
    jlong offset = (static_cast<uint64_t>(args[2]) << 32) | args[1];
    jint expectedValue = args[3];
    jint newValue = args[4];
    byte* raw_addr = reinterpret_cast<byte*>(obj) + offset;
    volatile int32_t* address = reinterpret_cast<volatile int32_t*>(raw_addr);
    // Note: android_atomic_release_cas() returns 0 on success, not failure.
    int r = android_atomic_release_cas(expectedValue, newValue, address);
    result->SetZ(r == 0);
  } else if (name == "void sun.misc.Unsafe.putObject(java.lang.Object, long, java.lang.Object)") {
    Object* obj = reinterpret_cast<Object*>(args[0]);
    Object* newValue = reinterpret_cast<Object*>(args[3]);
    obj->SetFieldObject(MemberOffset((static_cast<uint64_t>(args[2]) << 32) | args[1]), newValue, false);
  } else {
    LOG(FATAL) << "Attempt to invoke native method in non-started runtime: " << name;
  }
}

static void InterpreterJni(Thread* self, ArtMethod* method, const StringPiece& shorty,
                           Object* receiver, uint32_t* args, JValue* result)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  // TODO: The following enters JNI code using a typedef-ed function rather than the JNI compiler,
  //       it should be removed and JNI compiled stubs used instead.
  ScopedObjectAccessUnchecked soa(self);
  if (method->IsStatic()) {
    if (shorty == "L") {
      typedef jobject (fntype)(JNIEnv*, jclass);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jclass> klass(soa.Env(),
                                   soa.AddLocalReference<jclass>(method->GetDeclaringClass()));
      jobject jresult;
      {
        ScopedThreadStateChange tsc(self, kNative);
        jresult = fn(soa.Env(), klass.get());
      }
      result->SetL(soa.Decode<Object*>(jresult));
    } else if (shorty == "V") {
      typedef void (fntype)(JNIEnv*, jclass);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jclass> klass(soa.Env(),
                                   soa.AddLocalReference<jclass>(method->GetDeclaringClass()));
      ScopedThreadStateChange tsc(self, kNative);
      fn(soa.Env(), klass.get());
    } else if (shorty == "Z") {
      typedef jboolean (fntype)(JNIEnv*, jclass);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jclass> klass(soa.Env(),
                                   soa.AddLocalReference<jclass>(method->GetDeclaringClass()));
      ScopedThreadStateChange tsc(self, kNative);
      result->SetZ(fn(soa.Env(), klass.get()));
    } else if (shorty == "BI") {
      typedef jbyte (fntype)(JNIEnv*, jclass, jint);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jclass> klass(soa.Env(),
                                   soa.AddLocalReference<jclass>(method->GetDeclaringClass()));
      ScopedThreadStateChange tsc(self, kNative);
      result->SetB(fn(soa.Env(), klass.get(), args[0]));
    } else if (shorty == "II") {
      typedef jint (fntype)(JNIEnv*, jclass, jint);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jclass> klass(soa.Env(),
                                   soa.AddLocalReference<jclass>(method->GetDeclaringClass()));
      ScopedThreadStateChange tsc(self, kNative);
      result->SetI(fn(soa.Env(), klass.get(), args[0]));
    } else if (shorty == "LL") {
      typedef jobject (fntype)(JNIEnv*, jclass, jobject);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jclass> klass(soa.Env(),
                                   soa.AddLocalReference<jclass>(method->GetDeclaringClass()));
      ScopedLocalRef<jobject> arg0(soa.Env(),
                                   soa.AddLocalReference<jobject>(reinterpret_cast<Object*>(args[0])));
      jobject jresult;
      {
        ScopedThreadStateChange tsc(self, kNative);
        jresult = fn(soa.Env(), klass.get(), arg0.get());
      }
      result->SetL(soa.Decode<Object*>(jresult));
    } else if (shorty == "IIZ") {
      typedef jint (fntype)(JNIEnv*, jclass, jint, jboolean);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jclass> klass(soa.Env(),
                                   soa.AddLocalReference<jclass>(method->GetDeclaringClass()));
      ScopedThreadStateChange tsc(self, kNative);
      result->SetI(fn(soa.Env(), klass.get(), args[0], args[1]));
    } else if (shorty == "ILI") {
      typedef jint (fntype)(JNIEnv*, jclass, jobject, jint);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jclass> klass(soa.Env(),
                                   soa.AddLocalReference<jclass>(method->GetDeclaringClass()));
      ScopedLocalRef<jobject> arg0(soa.Env(),
                                   soa.AddLocalReference<jobject>(reinterpret_cast<Object*>(args[0])));
      ScopedThreadStateChange tsc(self, kNative);
      result->SetI(fn(soa.Env(), klass.get(), arg0.get(), args[1]));
    } else if (shorty == "SIZ") {
      typedef jshort (fntype)(JNIEnv*, jclass, jint, jboolean);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jclass> klass(soa.Env(),
                                   soa.AddLocalReference<jclass>(method->GetDeclaringClass()));
      ScopedThreadStateChange tsc(self, kNative);
      result->SetS(fn(soa.Env(), klass.get(), args[0], args[1]));
    } else if (shorty == "VIZ") {
      typedef void (fntype)(JNIEnv*, jclass, jint, jboolean);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jclass> klass(soa.Env(),
                                   soa.AddLocalReference<jclass>(method->GetDeclaringClass()));
      ScopedThreadStateChange tsc(self, kNative);
      fn(soa.Env(), klass.get(), args[0], args[1]);
    } else if (shorty == "ZLL") {
      typedef jboolean (fntype)(JNIEnv*, jclass, jobject, jobject);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jclass> klass(soa.Env(),
                                   soa.AddLocalReference<jclass>(method->GetDeclaringClass()));
      ScopedLocalRef<jobject> arg0(soa.Env(),
                                   soa.AddLocalReference<jobject>(reinterpret_cast<Object*>(args[0])));
      ScopedLocalRef<jobject> arg1(soa.Env(),
                                   soa.AddLocalReference<jobject>(reinterpret_cast<Object*>(args[1])));
      ScopedThreadStateChange tsc(self, kNative);
      result->SetZ(fn(soa.Env(), klass.get(), arg0.get(), arg1.get()));
    } else if (shorty == "ZILL") {
      typedef jboolean (fntype)(JNIEnv*, jclass, jint, jobject, jobject);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jclass> klass(soa.Env(),
                                   soa.AddLocalReference<jclass>(method->GetDeclaringClass()));
      ScopedLocalRef<jobject> arg1(soa.Env(),
                                   soa.AddLocalReference<jobject>(reinterpret_cast<Object*>(args[1])));
      ScopedLocalRef<jobject> arg2(soa.Env(),
                                   soa.AddLocalReference<jobject>(reinterpret_cast<Object*>(args[2])));
      ScopedThreadStateChange tsc(self, kNative);
      result->SetZ(fn(soa.Env(), klass.get(), args[0], arg1.get(), arg2.get()));
    } else if (shorty == "VILII") {
      typedef void (fntype)(JNIEnv*, jclass, jint, jobject, jint, jint);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jclass> klass(soa.Env(),
                                   soa.AddLocalReference<jclass>(method->GetDeclaringClass()));
      ScopedLocalRef<jobject> arg1(soa.Env(),
                                   soa.AddLocalReference<jobject>(reinterpret_cast<Object*>(args[1])));
      ScopedThreadStateChange tsc(self, kNative);
      fn(soa.Env(), klass.get(), args[0], arg1.get(), args[2], args[3]);
    } else if (shorty == "VLILII") {
      typedef void (fntype)(JNIEnv*, jclass, jobject, jint, jobject, jint, jint);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jclass> klass(soa.Env(),
                                   soa.AddLocalReference<jclass>(method->GetDeclaringClass()));
      ScopedLocalRef<jobject> arg0(soa.Env(),
                                   soa.AddLocalReference<jobject>(reinterpret_cast<Object*>(args[0])));
      ScopedLocalRef<jobject> arg2(soa.Env(),
                                   soa.AddLocalReference<jobject>(reinterpret_cast<Object*>(args[2])));
      ScopedThreadStateChange tsc(self, kNative);
      fn(soa.Env(), klass.get(), arg0.get(), args[1], arg2.get(), args[3], args[4]);
    } else {
      LOG(FATAL) << "Do something with static native method: " << PrettyMethod(method)
          << " shorty: " << shorty;
    }
  } else {
    if (shorty == "L") {
      typedef jobject (fntype)(JNIEnv*, jobject);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jobject> rcvr(soa.Env(),
                                   soa.AddLocalReference<jobject>(receiver));
      jobject jresult;
      {
        ScopedThreadStateChange tsc(self, kNative);
        jresult = fn(soa.Env(), rcvr.get());
      }
      result->SetL(soa.Decode<Object*>(jresult));
    } else if (shorty == "V") {
      typedef void (fntype)(JNIEnv*, jobject);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jobject> rcvr(soa.Env(),
                                   soa.AddLocalReference<jobject>(receiver));
      ScopedThreadStateChange tsc(self, kNative);
      fn(soa.Env(), rcvr.get());
    } else if (shorty == "LL") {
      typedef jobject (fntype)(JNIEnv*, jobject, jobject);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jobject> rcvr(soa.Env(),
                                   soa.AddLocalReference<jobject>(receiver));
      ScopedLocalRef<jobject> arg0(soa.Env(),
                                   soa.AddLocalReference<jobject>(reinterpret_cast<Object*>(args[0])));
      jobject jresult;
      {
        ScopedThreadStateChange tsc(self, kNative);
        jresult = fn(soa.Env(), rcvr.get(), arg0.get());
      }
      result->SetL(soa.Decode<Object*>(jresult));
      ScopedThreadStateChange tsc(self, kNative);
    } else if (shorty == "III") {
      typedef jint (fntype)(JNIEnv*, jobject, jint, jint);
      fntype* const fn = reinterpret_cast<fntype*>(const_cast<void*>(method->GetNativeMethod()));
      ScopedLocalRef<jobject> rcvr(soa.Env(),
                                   soa.AddLocalReference<jobject>(receiver));
      ScopedThreadStateChange tsc(self, kNative);
      result->SetI(fn(soa.Env(), rcvr.get(), args[0], args[1]));
    } else {
      LOG(FATAL) << "Do something with native method: " << PrettyMethod(method)
          << " shorty: " << shorty;
    }
  }
}

enum InterpreterImplKind {
  kSwitchImpl,            // switch-based interpreter implementation.
  kComputedGotoImplKind   // computed-goto-based interpreter implementation.
};

static const InterpreterImplKind kInterpreterImplKind = kComputedGotoImplKind;

static JValue Execute(Thread* self, MethodHelper& mh, const DexFile::CodeItem* code_item,
                      ShadowFrame& shadow_frame, JValue result_register)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

static inline JValue Execute(Thread* self, MethodHelper& mh, const DexFile::CodeItem* code_item,
                             ShadowFrame& shadow_frame, JValue result_register) {
  DCHECK(shadow_frame.GetMethod() == mh.GetMethod() ||
         shadow_frame.GetMethod()->GetDeclaringClass()->IsProxyClass());
  DCHECK(!shadow_frame.GetMethod()->IsAbstract());
  DCHECK(!shadow_frame.GetMethod()->IsNative());

  if (LIKELY(shadow_frame.GetMethod()->IsPreverified())) {
    // Enter the "without access check" interpreter.
    if (kInterpreterImplKind == kSwitchImpl) {
      return ExecuteSwitchImpl<false>(self, mh, code_item, shadow_frame, result_register);
    } else {
      DCHECK_EQ(kInterpreterImplKind, kComputedGotoImplKind);
      return ExecuteGotoImpl<false>(self, mh, code_item, shadow_frame, result_register);
    }
  } else {
    // Enter the "with access check" interpreter.
    if (kInterpreterImplKind == kSwitchImpl) {
      return ExecuteSwitchImpl<true>(self, mh, code_item, shadow_frame, result_register);
    } else {
      DCHECK_EQ(kInterpreterImplKind, kComputedGotoImplKind);
      return ExecuteGotoImpl<true>(self, mh, code_item, shadow_frame, result_register);
    }
  }
}

void EnterInterpreterFromInvoke(Thread* self, ArtMethod* method, Object* receiver,
                                uint32_t* args, JValue* result) {
  DCHECK_EQ(self, Thread::Current());
  if (UNLIKELY(__builtin_frame_address(0) < self->GetStackEnd())) {
    ThrowStackOverflowError(self);
    return;
  }

  const char* old_cause = self->StartAssertNoThreadSuspension("EnterInterpreterFromInvoke");
  MethodHelper mh(method);
  const DexFile::CodeItem* code_item = mh.GetCodeItem();
  uint16_t num_regs;
  uint16_t num_ins;
  if (code_item != NULL) {
    num_regs =  code_item->registers_size_;
    num_ins = code_item->ins_size_;
  } else if (method->IsAbstract()) {
    self->EndAssertNoThreadSuspension(old_cause);
    ThrowAbstractMethodError(method);
    return;
  } else {
    DCHECK(method->IsNative());
    num_regs = num_ins = ArtMethod::NumArgRegisters(mh.GetShorty());
    if (!method->IsStatic()) {
      num_regs++;
      num_ins++;
    }
  }
  // Set up shadow frame with matching number of reference slots to vregs.
  ShadowFrame* last_shadow_frame = self->GetManagedStack()->GetTopShadowFrame();
  void* memory = alloca(ShadowFrame::ComputeSize(num_regs));
  ShadowFrame* shadow_frame(ShadowFrame::Create(num_regs, last_shadow_frame, method, 0, memory));
  self->PushShadowFrame(shadow_frame);
  self->EndAssertNoThreadSuspension(old_cause);

  size_t cur_reg = num_regs - num_ins;
  if (!method->IsStatic()) {
    CHECK(receiver != NULL);
    shadow_frame->SetVRegReference(cur_reg, receiver);
    ++cur_reg;
  } else if (UNLIKELY(!method->GetDeclaringClass()->IsInitializing())) {
    ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
    SirtRef<mirror::Class> sirt_c(self, method->GetDeclaringClass());
    if (UNLIKELY(!class_linker->EnsureInitialized(sirt_c, true, true))) {
      CHECK(self->IsExceptionPending());
      self->PopShadowFrame();
      return;
    }
    CHECK(sirt_c->IsInitializing());
  }
  const char* shorty = mh.GetShorty();
  for (size_t shorty_pos = 0, arg_pos = 0; cur_reg < num_regs; ++shorty_pos, ++arg_pos, cur_reg++) {
    DCHECK_LT(shorty_pos + 1, mh.GetShortyLength());
    switch (shorty[shorty_pos + 1]) {
      case 'L': {
        Object* o = reinterpret_cast<Object*>(args[arg_pos]);
        shadow_frame->SetVRegReference(cur_reg, o);
        break;
      }
      case 'J': case 'D': {
        uint64_t wide_value = (static_cast<uint64_t>(args[arg_pos + 1]) << 32) | args[arg_pos];
        shadow_frame->SetVRegLong(cur_reg, wide_value);
        cur_reg++;
        arg_pos++;
        break;
      }
      default:
        shadow_frame->SetVReg(cur_reg, args[arg_pos]);
        break;
    }
  }
  if (LIKELY(!method->IsNative())) {
    JValue r = Execute(self, mh, code_item, *shadow_frame, JValue());
    if (result != NULL) {
      *result = r;
    }
  } else {
    // We don't expect to be asked to interpret native code (which is entered via a JNI compiler
    // generated stub) except during testing and image writing.
    if (!Runtime::Current()->IsStarted()) {
      UnstartedRuntimeJni(self, method, receiver, args, result);
    } else {
      InterpreterJni(self, method, shorty, receiver, args, result);
    }
  }
  self->PopShadowFrame();
}

void EnterInterpreterFromDeoptimize(Thread* self, ShadowFrame* shadow_frame, JValue* ret_val)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  JValue value;
  value.SetJ(ret_val->GetJ());  // Set value to last known result in case the shadow frame chain is empty.
  MethodHelper mh;
  while (shadow_frame != NULL) {
    self->SetTopOfShadowStack(shadow_frame);
    mh.ChangeMethod(shadow_frame->GetMethod());
    const DexFile::CodeItem* code_item = mh.GetCodeItem();
    value = Execute(self, mh, code_item, *shadow_frame, value);
    ShadowFrame* old_frame = shadow_frame;
    shadow_frame = shadow_frame->GetLink();
    delete old_frame;
  }
  ret_val->SetJ(value.GetJ());
}

JValue EnterInterpreterFromStub(Thread* self, MethodHelper& mh, const DexFile::CodeItem* code_item,
                                ShadowFrame& shadow_frame) {
  DCHECK_EQ(self, Thread::Current());
  if (UNLIKELY(__builtin_frame_address(0) < self->GetStackEnd())) {
    ThrowStackOverflowError(self);
    return JValue();
  }

  return Execute(self, mh, code_item, shadow_frame, JValue());
}

extern "C" void artInterpreterToInterpreterBridge(Thread* self, MethodHelper& mh,
                                                  const DexFile::CodeItem* code_item,
                                                  ShadowFrame* shadow_frame, JValue* result) {
  if (UNLIKELY(__builtin_frame_address(0) < self->GetStackEnd())) {
    ThrowStackOverflowError(self);
    return;
  }

  self->PushShadowFrame(shadow_frame);
  ArtMethod* method = shadow_frame->GetMethod();
  // Ensure static methods are initialized.
  if (method->IsStatic()) {
    SirtRef<Class> declaringClass(self, method->GetDeclaringClass());
    if (UNLIKELY(!declaringClass->IsInitializing())) {
      if (UNLIKELY(!Runtime::Current()->GetClassLinker()->EnsureInitialized(declaringClass, true,
                                                                            true))) {
        DCHECK(Thread::Current()->IsExceptionPending());
        self->PopShadowFrame();
        return;
      }
      CHECK(declaringClass->IsInitializing());
    }
  }

  if (LIKELY(!method->IsNative())) {
    result->SetJ(Execute(self, mh, code_item, *shadow_frame, JValue()).GetJ());
  } else {
    // We don't expect to be asked to interpret native code (which is entered via a JNI compiler
    // generated stub) except during testing and image writing.
    CHECK(!Runtime::Current()->IsStarted());
    Object* receiver = method->IsStatic() ? nullptr : shadow_frame->GetVRegReference(0);
    uint32_t* args = shadow_frame->GetVRegArgs(method->IsStatic() ? 0 : 1);
    UnstartedRuntimeJni(self, method, receiver, args, result);
  }

  self->PopShadowFrame();
}

}  // namespace interpreter
}  // namespace art
