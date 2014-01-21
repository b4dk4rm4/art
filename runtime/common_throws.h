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

#ifndef ART_RUNTIME_COMMON_THROWS_H_
#define ART_RUNTIME_COMMON_THROWS_H_

#include "base/mutex.h"
#include "invoke_type.h"

namespace art {
namespace mirror {
  class ArtField;
  class ArtMethod;
  class Class;
  class Object;
}  // namespace mirror
class Signature;
class StringPiece;
class ThrowLocation;

// AbstractMethodError

void ThrowAbstractMethodError(const mirror::ArtMethod* method)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

// ArithmeticException

void ThrowArithmeticExceptionDivideByZero() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

// ArrayIndexOutOfBoundsException

void ThrowArrayIndexOutOfBoundsException(int index, int length)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

// ArrayStoreException

void ThrowArrayStoreException(const mirror::Class* element_class,
                              const mirror::Class* array_class)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

// ClassCircularityError

void ThrowClassCircularityError(mirror::Class* c)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

// ClassCastException

void ThrowClassCastException(const mirror::Class* dest_type, const mirror::Class* src_type)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

void ThrowClassCastException(const ThrowLocation* throw_location, const char* msg)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

// ClassFormatError

void ThrowClassFormatError(const mirror::Class* referrer, const char* fmt, ...)
    __attribute__((__format__(__printf__, 2, 3)))
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

// IllegalAccessError

void ThrowIllegalAccessErrorClass(mirror::Class* referrer, mirror::Class* accessed)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIllegalAccessErrorClassForMethodDispatch(mirror::Class* referrer, mirror::Class* accessed,
                                                   const mirror::ArtMethod* caller,
                                                   const mirror::ArtMethod* called,
                                                   InvokeType type)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIllegalAccessErrorMethod(mirror::Class* referrer, mirror::ArtMethod* accessed)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIllegalAccessErrorField(mirror::Class* referrer, mirror::ArtField* accessed)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIllegalAccessErrorFinalField(const mirror::ArtMethod* referrer,
                                       mirror::ArtField* accessed)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIllegalAccessError(mirror::Class* referrer, const char* fmt, ...)
    __attribute__((__format__(__printf__, 2, 3)))
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

// IllegalArgumentException

void ThrowIllegalArgumentException(const ThrowLocation* throw_location, const char* msg)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

// IncompatibleClassChangeError

void ThrowIncompatibleClassChangeError(InvokeType expected_type, InvokeType found_type,
                                       mirror::ArtMethod* method,
                                       const mirror::ArtMethod* referrer)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIncompatibleClassChangeErrorClassForInterfaceDispatch(const mirror::ArtMethod* interface_method,
                                                                mirror::Object* this_object,
                                                                const mirror::ArtMethod* referrer)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIncompatibleClassChangeErrorField(const mirror::ArtField* resolved_field, bool is_static,
                                            const mirror::ArtMethod* referrer)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

void ThrowIncompatibleClassChangeError(const mirror::Class* referrer, const char* fmt, ...)
    __attribute__((__format__(__printf__, 2, 3)))
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

// IOException

void ThrowIOException(const char* fmt, ...) __attribute__((__format__(__printf__, 1, 2)))
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

// LinkageError

void ThrowLinkageError(const mirror::Class* referrer, const char* fmt, ...)
    __attribute__((__format__(__printf__, 2, 3)))
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

// NegativeArraySizeException

void ThrowNegativeArraySizeException(int size)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

void ThrowNegativeArraySizeException(const char* msg)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;


// NoSuchFieldError

void ThrowNoSuchFieldError(const StringPiece& scope, mirror::Class* c,
                           const StringPiece& type, const StringPiece& name)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

// NoSuchMethodError

void ThrowNoSuchMethodError(InvokeType type, mirror::Class* c, const StringPiece& name,
                            const Signature& signature)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

void ThrowNoSuchMethodError(uint32_t method_idx)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

// NullPointerException

void ThrowNullPointerExceptionForFieldAccess(const ThrowLocation& throw_location,
                                             mirror::ArtField* field,
                                             bool is_read)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

void ThrowNullPointerExceptionForMethodAccess(const ThrowLocation& throw_location,
                                              uint32_t method_idx,
                                              InvokeType type)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

void ThrowNullPointerExceptionForMethodAccess(const ThrowLocation& throw_location,
                                              mirror::ArtMethod* method,
                                              InvokeType type)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

void ThrowNullPointerExceptionFromDexPC(const ThrowLocation& throw_location)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

void ThrowNullPointerException(const ThrowLocation* throw_location, const char* msg)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

// RuntimeException

void ThrowRuntimeException(const char* fmt, ...)
    __attribute__((__format__(__printf__, 1, 2)))
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

// VerifyError

void ThrowVerifyError(const mirror::Class* referrer, const char* fmt, ...)
    __attribute__((__format__(__printf__, 2, 3)))
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) COLD_ATTR;

}  // namespace art

#endif  // ART_RUNTIME_COMMON_THROWS_H_
