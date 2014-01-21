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

#ifndef ART_RUNTIME_GC_SPACE_MALLOC_SPACE_H_
#define ART_RUNTIME_GC_SPACE_MALLOC_SPACE_H_

#include "space.h"

#include <valgrind.h>
#include <memcheck/memcheck.h>

namespace art {
namespace gc {

namespace collector {
  class MarkSweep;
}  // namespace collector

namespace space {

// TODO: Remove define macro
#define CHECK_MEMORY_CALL(call, args, what) \
  do { \
    int rc = call args; \
    if (UNLIKELY(rc != 0)) { \
      errno = rc; \
      PLOG(FATAL) << # call << " failed for " << what; \
    } \
  } while (false)

// const bool kUseRosAlloc = true;

// A common parent of DlMallocSpace and RosAllocSpace.
class MallocSpace : public ContinuousMemMapAllocSpace {
 public:
  typedef void(*WalkCallback)(void *start, void *end, size_t num_bytes, void* callback_arg);

  SpaceType GetType() const {
    if (GetGcRetentionPolicy() == kGcRetentionPolicyFullCollect) {
      return kSpaceTypeZygoteSpace;
    } else {
      return kSpaceTypeAllocSpace;
    }
  }

  // Allocate num_bytes without allowing the underlying space to grow.
  virtual mirror::Object* AllocWithGrowth(Thread* self, size_t num_bytes,
                                          size_t* bytes_allocated) = 0;
  // Allocate num_bytes allowing the underlying space to grow.
  virtual mirror::Object* Alloc(Thread* self, size_t num_bytes, size_t* bytes_allocated) = 0;
  // Return the storage space required by obj.
  virtual size_t AllocationSize(const mirror::Object* obj) = 0;
  virtual size_t Free(Thread* self, mirror::Object* ptr) = 0;
  virtual size_t FreeList(Thread* self, size_t num_ptrs, mirror::Object** ptrs) = 0;

#ifndef NDEBUG
  virtual void CheckMoreCoreForPrecondition() {}  // to be overridden in the debug build.
#else
  void CheckMoreCoreForPrecondition() {}  // no-op in the non-debug build.
#endif

  void* MoreCore(intptr_t increment);

  // Hands unused pages back to the system.
  virtual size_t Trim() = 0;

  // Perform a mspace_inspect_all which calls back for each allocation chunk. The chunk may not be
  // in use, indicated by num_bytes equaling zero.
  virtual void Walk(WalkCallback callback, void* arg) = 0;

  // Returns the number of bytes that the space has currently obtained from the system. This is
  // greater or equal to the amount of live data in the space.
  virtual size_t GetFootprint() = 0;

  // Returns the number of bytes that the heap is allowed to obtain from the system via MoreCore.
  virtual size_t GetFootprintLimit() = 0;

  // Set the maximum number of bytes that the heap is allowed to obtain from the system via
  // MoreCore. Note this is used to stop the mspace growing beyond the limit to Capacity. When
  // allocations fail we GC before increasing the footprint limit and allowing the mspace to grow.
  virtual void SetFootprintLimit(size_t limit) = 0;

  // Removes the fork time growth limit on capacity, allowing the application to allocate up to the
  // maximum reserved size of the heap.
  void ClearGrowthLimit() {
    growth_limit_ = NonGrowthLimitCapacity();
  }

  // Override capacity so that we only return the possibly limited capacity
  size_t Capacity() const {
    return growth_limit_;
  }

  // The total amount of memory reserved for the alloc space.
  size_t NonGrowthLimitCapacity() const {
    return GetMemMap()->Size();
  }

  accounting::SpaceBitmap* GetLiveBitmap() const {
    return live_bitmap_.get();
  }

  accounting::SpaceBitmap* GetMarkBitmap() const {
    return mark_bitmap_.get();
  }

  void Dump(std::ostream& os) const;

  void SetGrowthLimit(size_t growth_limit);

  // Swap the live and mark bitmaps of this space. This is used by the GC for concurrent sweeping.
  void SwapBitmaps();

  virtual MallocSpace* CreateInstance(const std::string& name, MemMap* mem_map, void* allocator,
                                      byte* begin, byte* end, byte* limit, size_t growth_limit) = 0;

  // Turn ourself into a zygote space and return a new alloc space
  // which has our unused memory.  When true, the low memory mode
  // argument specifies that the heap wishes the created space to be
  // more aggressive in releasing unused pages.
  MallocSpace* CreateZygoteSpace(const char* alloc_space_name, bool low_memory_mode);

  virtual uint64_t GetBytesAllocated() = 0;
  virtual uint64_t GetObjectsAllocated() = 0;

  // Returns the old mark bitmap.
  accounting::SpaceBitmap* BindLiveToMarkBitmap();
  bool HasBoundBitmaps() const;
  void UnBindBitmaps();

  // Returns the class of a recently freed object.
  mirror::Class* FindRecentFreedObject(const mirror::Object* obj);

  // Used to ensure that failure happens when you free / allocate into an invalidated space. If we
  // don't do this we may get heap corruption instead of a segfault at null.
  virtual void InvalidateAllocator() = 0;

  // Sweep the references in the malloc space.
  void Sweep(bool swap_bitmaps, size_t* freed_objects, size_t* freed_bytes);

 protected:
  MallocSpace(const std::string& name, MemMap* mem_map, byte* begin, byte* end,
              byte* limit, size_t growth_limit);

  static MemMap* CreateMemMap(const std::string& name, size_t starting_size, size_t* initial_size,
                              size_t* growth_limit, size_t* capacity, byte* requested_begin);

  // When true the low memory mode argument specifies that the heap
  // wishes the created allocator to be more aggressive in releasing
  // unused pages.
  virtual void* CreateAllocator(void* base, size_t morecore_start, size_t initial_size,
                                bool low_memory_mode) = 0;

  void RegisterRecentFree(mirror::Object* ptr) EXCLUSIVE_LOCKS_REQUIRED(lock_);

  UniquePtr<accounting::SpaceBitmap> live_bitmap_;
  UniquePtr<accounting::SpaceBitmap> mark_bitmap_;
  UniquePtr<accounting::SpaceBitmap> temp_bitmap_;

  // Recent allocation buffer.
  static constexpr size_t kRecentFreeCount = kDebugSpaces ? (1 << 16) : 0;
  static constexpr size_t kRecentFreeMask = kRecentFreeCount - 1;
  std::pair<const mirror::Object*, mirror::Class*> recent_freed_objects_[kRecentFreeCount];
  size_t recent_free_pos_;

  static size_t bitmap_index_;

  // Used to ensure mutual exclusion when the allocation spaces data structures are being modified.
  Mutex lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;

  // The capacity of the alloc space until such time that ClearGrowthLimit is called.
  // The underlying mem_map_ controls the maximum size we allow the heap to grow to. The growth
  // limit is a value <= to the mem_map_ capacity used for ergonomic reasons because of the zygote.
  // Prior to forking the zygote the heap will have a maximally sized mem_map_ but the growth_limit_
  // will be set to a lower value. The growth_limit_ is used as the capacity of the alloc_space_,
  // however, capacity normally can't vary. In the case of the growth_limit_ it can be cleared
  // one time by a call to ClearGrowthLimit.
  size_t growth_limit_;

  friend class collector::MarkSweep;

 private:
  DISALLOW_COPY_AND_ASSIGN(MallocSpace);
};

// Number of bytes to use as a red zone (rdz). A red zone of this size will be placed before and
// after each allocation. 8 bytes provides long/double alignment.
static constexpr size_t kValgrindRedZoneBytes = 8;

// A specialization of DlMallocSpace/RosAllocSpace that provides information to valgrind wrt allocations.
template <typename BaseMallocSpaceType, typename AllocatorType>
class ValgrindMallocSpace : public BaseMallocSpaceType {
 public:
  virtual mirror::Object* AllocWithGrowth(Thread* self, size_t num_bytes, size_t* bytes_allocated) {
    void* obj_with_rdz = BaseMallocSpaceType::AllocWithGrowth(self, num_bytes + 2 * kValgrindRedZoneBytes,
                                                              bytes_allocated);
    if (obj_with_rdz == NULL) {
      return NULL;
    }
    mirror::Object* result = reinterpret_cast<mirror::Object*>(
        reinterpret_cast<byte*>(obj_with_rdz) + kValgrindRedZoneBytes);
    // Make redzones as no access.
    VALGRIND_MAKE_MEM_NOACCESS(obj_with_rdz, kValgrindRedZoneBytes);
    VALGRIND_MAKE_MEM_NOACCESS(reinterpret_cast<byte*>(result) + num_bytes, kValgrindRedZoneBytes);
    return result;
  }

  virtual mirror::Object* Alloc(Thread* self, size_t num_bytes, size_t* bytes_allocated) {
    void* obj_with_rdz = BaseMallocSpaceType::Alloc(self, num_bytes + 2 * kValgrindRedZoneBytes,
                                                    bytes_allocated);
    if (obj_with_rdz == NULL) {
     return NULL;
    }
    mirror::Object* result = reinterpret_cast<mirror::Object*>(
        reinterpret_cast<byte*>(obj_with_rdz) + kValgrindRedZoneBytes);
    // Make redzones as no access.
    VALGRIND_MAKE_MEM_NOACCESS(obj_with_rdz, kValgrindRedZoneBytes);
    VALGRIND_MAKE_MEM_NOACCESS(reinterpret_cast<byte*>(result) + num_bytes, kValgrindRedZoneBytes);
    return result;
  }

  virtual size_t AllocationSize(const mirror::Object* obj) {
    size_t result = BaseMallocSpaceType::AllocationSize(reinterpret_cast<const mirror::Object*>(
        reinterpret_cast<const byte*>(obj) - kValgrindRedZoneBytes));
    return result - 2 * kValgrindRedZoneBytes;
  }

  virtual size_t Free(Thread* self, mirror::Object* ptr) {
    void* obj_after_rdz = reinterpret_cast<void*>(ptr);
    void* obj_with_rdz = reinterpret_cast<byte*>(obj_after_rdz) - kValgrindRedZoneBytes;
    // Make redzones undefined.
    size_t allocation_size = BaseMallocSpaceType::AllocationSize(
        reinterpret_cast<mirror::Object*>(obj_with_rdz));
    VALGRIND_MAKE_MEM_UNDEFINED(obj_with_rdz, allocation_size);
    size_t freed = BaseMallocSpaceType::Free(self, reinterpret_cast<mirror::Object*>(obj_with_rdz));
    return freed - 2 * kValgrindRedZoneBytes;
  }

  virtual size_t FreeList(Thread* self, size_t num_ptrs, mirror::Object** ptrs) {
    size_t freed = 0;
    for (size_t i = 0; i < num_ptrs; i++) {
      freed += Free(self, ptrs[i]);
    }
    return freed;
  }

  ValgrindMallocSpace(const std::string& name, MemMap* mem_map, AllocatorType allocator, byte* begin,
                      byte* end, byte* limit, size_t growth_limit, size_t initial_size) :
      BaseMallocSpaceType(name, mem_map, allocator, begin, end, limit, growth_limit) {
    VALGRIND_MAKE_MEM_UNDEFINED(mem_map->Begin() + initial_size, mem_map->Size() - initial_size);
  }

  virtual ~ValgrindMallocSpace() {
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ValgrindMallocSpace);
};

}  // namespace space
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_SPACE_MALLOC_SPACE_H_
