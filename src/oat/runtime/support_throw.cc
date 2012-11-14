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

#include "callee_save_frame.h"
#include "object.h"
#include "object_utils.h"
#include "runtime_support.h"
#include "thread.h"
#include "well_known_classes.h"

namespace art {

// Used to implement MOVE_EXCEPTION.
extern "C" void* GetAndClearException(Thread* self) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  DCHECK(self->IsExceptionPending());
  Throwable* exception = self->GetException();
  self->ClearException();
  return exception;
}

// Deliver an exception that's pending on thread helping set up a callee save frame on the way.
extern "C" void artDeliverPendingExceptionFromCode(Thread* thread, AbstractMethod** sp)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  FinishCalleeSaveFrameSetup(thread, sp, Runtime::kSaveAll);
  thread->DeliverException();
}

// Called by generated call to throw an exception.
extern "C" void artDeliverExceptionFromCode(Throwable* exception, Thread* thread,
                                            AbstractMethod** sp)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  /*
   * exception may be NULL, in which case this routine should
   * throw NPE.  NOTE: this is a convenience for generated code,
   * which previously did the null check inline and constructed
   * and threw a NPE if NULL.  This routine responsible for setting
   * exception_ in thread and delivering the exception.
   */
  FinishCalleeSaveFrameSetup(thread, sp, Runtime::kSaveAll);
  if (exception == NULL) {
    thread->ThrowNewException("Ljava/lang/NullPointerException;", "throw with null exception");
  } else {
    thread->SetException(exception);
  }
  thread->DeliverException();
}

// Called by generated call to throw a NPE exception.
extern "C" void artThrowNullPointerExceptionFromCode(Thread* self,
                                                     AbstractMethod** sp)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  FinishCalleeSaveFrameSetup(self, sp, Runtime::kSaveAll);
  uint32_t dex_pc;
  AbstractMethod* throw_method = self->GetCurrentMethod(&dex_pc);
  ThrowNullPointerExceptionFromDexPC(throw_method, dex_pc);
  self->DeliverException();
}

// Called by generated call to throw an arithmetic divide by zero exception.
extern "C" void artThrowDivZeroFromCode(Thread* thread,
                                        AbstractMethod** sp)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  FinishCalleeSaveFrameSetup(thread, sp, Runtime::kSaveAll);
  thread->ThrowNewException("Ljava/lang/ArithmeticException;", "divide by zero");
  thread->DeliverException();
}

// Called by generated call to throw an array index out of bounds exception.
extern "C" void artThrowArrayBoundsFromCode(int index, int limit, Thread* thread,
                                            AbstractMethod** sp)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  FinishCalleeSaveFrameSetup(thread, sp, Runtime::kSaveAll);
  thread->ThrowNewExceptionF("Ljava/lang/ArrayIndexOutOfBoundsException;",
                             "length=%d; index=%d", limit, index);
  thread->DeliverException();
}

extern "C" void artThrowStackOverflowFromCode(Thread* self, AbstractMethod** sp)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  FinishCalleeSaveFrameSetup(self, sp, Runtime::kSaveAll);
  CHECK(!self->IsHandlingStackOverflow()) << "Recursive stack overflow.";
  // Remove extra entry pushed onto second stack during method tracing.
  if (Runtime::Current()->IsMethodTracingActive()) {
    InstrumentationMethodUnwindFromCode(self);
  }
  self->SetStackEndForStackOverflow();  // Allow space on the stack for constructor to execute.
  JNIEnvExt* env = self->GetJniEnv();
  std::string msg("stack size ");
  msg += PrettySize(self->GetStackSize());
  // Use low-level JNI routine and pre-baked error class to avoid class linking operations that
  // would consume more stack.
  int rc = ::art::ThrowNewException(env, WellKnownClasses::java_lang_StackOverflowError,
                                    msg.c_str(), NULL);
  if (rc != JNI_OK) {
    // TODO: ThrowNewException failed presumably because of an OOME, we continue to throw the OOME
    //       or die in the CHECK below. We may want to throw a pre-baked StackOverflowError
    //       instead.
    LOG(ERROR) << "Couldn't throw new StackOverflowError because JNI ThrowNew failed.";
    CHECK(self->IsExceptionPending());
  }
  self->ResetDefaultStackEnd();  // Return to default stack size.
  self->DeliverException();
}

extern "C" void artThrowNoSuchMethodFromCode(int32_t method_idx, Thread* self,
                                             AbstractMethod** sp)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  FinishCalleeSaveFrameSetup(self, sp, Runtime::kSaveAll);
  AbstractMethod* method = self->GetCurrentMethod();
  ThrowNoSuchMethodError(method_idx, method);
  self->DeliverException();
}

}  // namespace art
