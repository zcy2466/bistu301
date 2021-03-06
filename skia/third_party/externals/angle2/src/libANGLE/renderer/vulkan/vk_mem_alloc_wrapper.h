//
// Copyright 2020 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// vma_allocator_wrapper.h:
//    Hides VMA functions so we can use separate warning sets.
//

#ifndef LIBANGLE_RENDERER_VULKAN_VK_MEM_ALLOC_WRAPPER_H_
#define LIBANGLE_RENDERER_VULKAN_VK_MEM_ALLOC_WRAPPER_H_

#include "common/vulkan/vk_headers.h"

VK_DEFINE_HANDLE(VmaAllocator)
VK_DEFINE_HANDLE(VmaAllocation)
VK_DEFINE_HANDLE(VmaPool)

namespace vma
{
VkResult InitAllocator(VkPhysicalDevice physicalDevice,
                       VkDevice device,
                       VkInstance instance,
                       uint32_t apiVersion,
                       VkDeviceSize preferredLargeHeapBlockSize,
                       VmaAllocator *pAllocator);

void DestroyAllocator(VmaAllocator allocator);

VkResult CreatePool(VmaAllocator allocator,
                    uint32_t memoryTypeIndex,
                    bool buddyAlgorithm,
                    VkDeviceSize blockSize,
                    VmaPool *pPool);
void DestroyPool(VmaAllocator allocator, VmaPool pool);

void FreeMemory(VmaAllocator allocator, VmaAllocation allocation);

VkResult CreateBuffer(VmaAllocator allocator,
                      const VkBufferCreateInfo *pBufferCreateInfo,
                      VkMemoryPropertyFlags requiredFlags,
                      VkMemoryPropertyFlags preferredFlags,
                      bool persistentlyMappedBuffers,
                      uint32_t *pMemoryTypeIndexOut,
                      VkBuffer *pBuffer,
                      VmaAllocation *pAllocation);

VkResult FindMemoryTypeIndexForBufferInfo(VmaAllocator allocator,
                                          const VkBufferCreateInfo *pBufferCreateInfo,
                                          VkMemoryPropertyFlags requiredFlags,
                                          VkMemoryPropertyFlags preferredFlags,
                                          bool persistentlyMappedBuffers,
                                          uint32_t *pMemoryTypeIndexOut);

void GetMemoryTypeProperties(VmaAllocator allocator,
                             uint32_t memoryTypeIndex,
                             VkMemoryPropertyFlags *pFlags);

VkResult MapMemory(VmaAllocator allocator, VmaAllocation allocation, void **ppData);

void UnmapMemory(VmaAllocator allocator, VmaAllocation allocation);

void FlushAllocation(VmaAllocator allocator,
                     VmaAllocation allocation,
                     VkDeviceSize offset,
                     VkDeviceSize size);

void InvalidateAllocation(VmaAllocator allocator,
                          VmaAllocation allocation,
                          VkDeviceSize offset,
                          VkDeviceSize size);

void BuildStatsString(VmaAllocator allocator, char **statsString, VkBool32 detailedMap);
void FreeStatsString(VmaAllocator allocator, char *statsString);

}  // namespace vma

#endif  // LIBANGLE_RENDERER_VULKAN_VK_MEM_ALLOC_WRAPPER_H_
