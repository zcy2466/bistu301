// Copyright 2018 The Dawn Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "dawn_native/ComputePassEncoder.h"

#include "dawn_native/Buffer.h"
#include "dawn_native/CommandEncoder.h"
#include "dawn_native/CommandValidation.h"
#include "dawn_native/Commands.h"
#include "dawn_native/ComputePipeline.h"
#include "dawn_native/Device.h"
#include "dawn_native/ObjectType_autogen.h"
#include "dawn_native/PassResourceUsageTracker.h"
#include "dawn_native/QuerySet.h"

namespace dawn_native {

    ComputePassEncoder::ComputePassEncoder(DeviceBase* device,
                                           CommandEncoder* commandEncoder,
                                           EncodingContext* encodingContext)
        : ProgrammablePassEncoder(device, encodingContext), mCommandEncoder(commandEncoder) {
    }

    ComputePassEncoder::ComputePassEncoder(DeviceBase* device,
                                           CommandEncoder* commandEncoder,
                                           EncodingContext* encodingContext,
                                           ErrorTag errorTag)
        : ProgrammablePassEncoder(device, encodingContext, errorTag),
          mCommandEncoder(commandEncoder) {
    }

    ComputePassEncoder* ComputePassEncoder::MakeError(DeviceBase* device,
                                                      CommandEncoder* commandEncoder,
                                                      EncodingContext* encodingContext) {
        return new ComputePassEncoder(device, commandEncoder, encodingContext, ObjectBase::kError);
    }

    ObjectType ComputePassEncoder::GetType() const {
        return ObjectType::ComputePassEncoder;
    }

    void ComputePassEncoder::APIEndPass() {
        if (mEncodingContext->TryEncode(
                this,
                [&](CommandAllocator* allocator) -> MaybeError {
                    if (IsValidationEnabled()) {
                        DAWN_TRY(ValidateProgrammableEncoderEnd());
                    }

                    allocator->Allocate<EndComputePassCmd>(Command::EndComputePass);

                    return {};
                },
                "encoding EndPass()")) {
            mEncodingContext->ExitComputePass(this, mUsageTracker.AcquireResourceUsage());
        }
    }

    void ComputePassEncoder::APIDispatch(uint32_t x, uint32_t y, uint32_t z) {
        mEncodingContext->TryEncode(
            this,
            [&](CommandAllocator* allocator) -> MaybeError {
                if (IsValidationEnabled()) {
                    DAWN_TRY(mCommandBufferState.ValidateCanDispatch());

                    uint32_t workgroupsPerDimension =
                        GetDevice()->GetLimits().v1.maxComputeWorkgroupsPerDimension;

                    DAWN_INVALID_IF(
                        x > workgroupsPerDimension,
                        "Dispatch size X (%u) exceeds max compute workgroups per dimension (%u).",
                        x, workgroupsPerDimension);

                    DAWN_INVALID_IF(
                        y > workgroupsPerDimension,
                        "Dispatch size Y (%u) exceeds max compute workgroups per dimension (%u).",
                        y, workgroupsPerDimension);

                    DAWN_INVALID_IF(
                        z > workgroupsPerDimension,
                        "Dispatch size Z (%u) exceeds max compute workgroups per dimension (%u).",
                        z, workgroupsPerDimension);
                }

                // Record the synchronization scope for Dispatch, which is just the current
                // bindgroups.
                AddDispatchSyncScope();

                DispatchCmd* dispatch = allocator->Allocate<DispatchCmd>(Command::Dispatch);
                dispatch->x = x;
                dispatch->y = y;
                dispatch->z = z;

                return {};
            },
            "encoding Dispatch (x: %u, y: %u, z: %u)", x, y, z);
    }

    void ComputePassEncoder::APIDispatchIndirect(BufferBase* indirectBuffer,
                                                 uint64_t indirectOffset) {
        mEncodingContext->TryEncode(
            this,
            [&](CommandAllocator* allocator) -> MaybeError {
                if (IsValidationEnabled()) {
                    DAWN_TRY(GetDevice()->ValidateObject(indirectBuffer));
                    DAWN_TRY(ValidateCanUseAs(indirectBuffer, wgpu::BufferUsage::Indirect));
                    DAWN_TRY(mCommandBufferState.ValidateCanDispatch());

                    // Indexed dispatches need a compute-shader based validation to check that the
                    // dispatch sizes aren't too big. Disallow them as unsafe until the validation
                    // is implemented.
                    DAWN_INVALID_IF(
                        GetDevice()->IsToggleEnabled(Toggle::DisallowUnsafeAPIs),
                        "DispatchIndirect is disallowed because it doesn't validate that the "
                        "dispatch size is valid yet.");

                    DAWN_INVALID_IF(indirectOffset % 4 != 0,
                                    "Indirect offset (%u) is not a multiple of 4.", indirectOffset);

                    DAWN_INVALID_IF(
                        indirectOffset >= indirectBuffer->GetSize() ||
                            indirectOffset + kDispatchIndirectSize > indirectBuffer->GetSize(),
                        "Indirect offset (%u) and dispatch size (%u) exceeds the indirect buffer "
                        "size (%u).",
                        indirectOffset, kDispatchIndirectSize, indirectBuffer->GetSize());
                }

                // Record the synchronization scope for Dispatch, both the bindgroups and the
                // indirect buffer.
                SyncScopeUsageTracker scope;
                scope.BufferUsedAs(indirectBuffer, wgpu::BufferUsage::Indirect);
                mUsageTracker.AddReferencedBuffer(indirectBuffer);
                AddDispatchSyncScope(std::move(scope));

                DispatchIndirectCmd* dispatch =
                    allocator->Allocate<DispatchIndirectCmd>(Command::DispatchIndirect);
                dispatch->indirectBuffer = indirectBuffer;
                dispatch->indirectOffset = indirectOffset;

                return {};
            },
            "encoding DispatchIndirect with %s", indirectBuffer);
    }

    void ComputePassEncoder::APISetPipeline(ComputePipelineBase* pipeline) {
        mEncodingContext->TryEncode(
            this,
            [&](CommandAllocator* allocator) -> MaybeError {
                if (IsValidationEnabled()) {
                    DAWN_TRY(GetDevice()->ValidateObject(pipeline));
                }

                mCommandBufferState.SetComputePipeline(pipeline);

                SetComputePipelineCmd* cmd =
                    allocator->Allocate<SetComputePipelineCmd>(Command::SetComputePipeline);
                cmd->pipeline = pipeline;

                return {};
            },
            "encoding SetPipeline with %s", pipeline);
    }

    void ComputePassEncoder::APISetBindGroup(uint32_t groupIndexIn,
                                             BindGroupBase* group,
                                             uint32_t dynamicOffsetCount,
                                             const uint32_t* dynamicOffsets) {
        mEncodingContext->TryEncode(
            this,
            [&](CommandAllocator* allocator) -> MaybeError {
                BindGroupIndex groupIndex(groupIndexIn);

                if (IsValidationEnabled()) {
                    DAWN_TRY(ValidateSetBindGroup(groupIndex, group, dynamicOffsetCount,
                                                  dynamicOffsets));
                }

                mUsageTracker.AddResourcesReferencedByBindGroup(group);

                RecordSetBindGroup(allocator, groupIndex, group, dynamicOffsetCount,
                                   dynamicOffsets);
                mCommandBufferState.SetBindGroup(groupIndex, group);

                return {};
            },
            "encoding SetBindGroup with %s at index %u", group, groupIndexIn);
    }

    void ComputePassEncoder::APIWriteTimestamp(QuerySetBase* querySet, uint32_t queryIndex) {
        mEncodingContext->TryEncode(
            this,
            [&](CommandAllocator* allocator) -> MaybeError {
                if (IsValidationEnabled()) {
                    DAWN_TRY(GetDevice()->ValidateObject(querySet));
                    DAWN_TRY(ValidateTimestampQuery(querySet, queryIndex));
                }

                mCommandEncoder->TrackQueryAvailability(querySet, queryIndex);

                WriteTimestampCmd* cmd =
                    allocator->Allocate<WriteTimestampCmd>(Command::WriteTimestamp);
                cmd->querySet = querySet;
                cmd->queryIndex = queryIndex;

                return {};
            },
            "encoding WriteTimestamp to %s.", querySet);
    }

    void ComputePassEncoder::AddDispatchSyncScope(SyncScopeUsageTracker scope) {
        PipelineLayoutBase* layout = mCommandBufferState.GetPipelineLayout();
        for (BindGroupIndex i : IterateBitSet(layout->GetBindGroupLayoutsMask())) {
            scope.AddBindGroup(mCommandBufferState.GetBindGroup(i));
        }
        mUsageTracker.AddDispatch(scope.AcquireSyncScopeUsage());
    }

}  // namespace dawn_native
