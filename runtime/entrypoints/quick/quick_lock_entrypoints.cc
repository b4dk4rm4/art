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
#include "common_throws.h"
#include "mirror/object-inl.h"

namespace art {

extern "C" int artLockObjectFromCode(mirror::Object* obj, Thread* self, mirror::ArtMethod** sp)
    EXCLUSIVE_LOCK_FUNCTION(monitor_lock_) {
  FinishCalleeSaveFrameSetup(self, sp, Runtime::kRefsOnly);
  if (UNLIKELY(obj == NULL)) {
    ThrowLocation throw_location(self->GetCurrentLocationForThrow());
    ThrowNullPointerException(&throw_location,
                              "Null reference used for synchronization (monitor-enter)");
    return -1;  // Failure.
  } else {
    if (kIsDebugBuild) {
      // GC may move the obj, need Sirt for the following DCHECKs.
      SirtRef<mirror::Object> sirt_obj(self, obj);
      obj->MonitorEnter(self);  // May block
      CHECK(self->HoldsLock(sirt_obj.get()));
      CHECK(!self->IsExceptionPending());
    } else {
      obj->MonitorEnter(self);  // May block
    }
    return 0;  // Success.
    // Only possible exception is NPE and is handled before entry
  }
}

extern "C" int artUnlockObjectFromCode(mirror::Object* obj, Thread* self, mirror::ArtMethod** sp)
    UNLOCK_FUNCTION(monitor_lock_) {
  FinishCalleeSaveFrameSetup(self, sp, Runtime::kRefsOnly);
  if (UNLIKELY(obj == NULL)) {
    ThrowLocation throw_location(self->GetCurrentLocationForThrow());
    ThrowNullPointerException(&throw_location,
                              "Null reference used for synchronization (monitor-exit)");
    return -1;  // Failure.
  } else {
    // MonitorExit may throw exception.
    return obj->MonitorExit(self) ? 0 /* Success */ : -1 /* Failure */;
  }
}

}  // namespace art
