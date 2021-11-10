// Copyright 2020 The Dawn Authors
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

#include "tests/DawnTest.h"

#include "common/Math.h"
#include "utils/ComboRenderPipelineDescriptor.h"
#include "utils/TestUtils.h"
#include "utils/WGPUHelpers.h"

#define EXPECT_LAZY_CLEAR(N, statement)                                                       \
    do {                                                                                      \
        if (UsesWire()) {                                                                     \
            statement;                                                                        \
        } else {                                                                              \
            size_t lazyClearsBefore = dawn_native::GetLazyClearCountForTesting(device.Get()); \
            statement;                                                                        \
            size_t lazyClearsAfter = dawn_native::GetLazyClearCountForTesting(device.Get());  \
            EXPECT_EQ(N, lazyClearsAfter - lazyClearsBefore);                                 \
        }                                                                                     \
    } while (0)

namespace {

    struct BufferZeroInitInCopyT2BSpec {
        wgpu::Extent3D textureSize;
        uint64_t bufferOffset;
        uint64_t extraBytes;
        uint32_t bytesPerRow;
        uint32_t rowsPerImage;
        uint32_t lazyClearCount;
    };

}  // anonymous namespace

class BufferZeroInitTest : public DawnTest {
  protected:
    std::vector<const char*> GetRequiredFeatures() override {
        std::vector<const char*> requiredFeatures = {};
        if (SupportsFeatures({"timestamp-query"})) {
            requiredFeatures.push_back("timestamp-query");
        }
        return requiredFeatures;
    }

  public:
    wgpu::Buffer CreateBuffer(uint64_t size,
                              wgpu::BufferUsage usage,
                              bool mappedAtCreation = false) {
        wgpu::BufferDescriptor descriptor;
        descriptor.size = size;
        descriptor.usage = usage;
        descriptor.mappedAtCreation = mappedAtCreation;
        return device.CreateBuffer(&descriptor);
    }

    void MapAsyncAndWait(wgpu::Buffer buffer,
                         wgpu::MapMode mapMode,
                         uint64_t offset,
                         uint64_t size) {
        ASSERT(mapMode == wgpu::MapMode::Read || mapMode == wgpu::MapMode::Write);

        bool done = false;
        buffer.MapAsync(
            mapMode, offset, size,
            [](WGPUBufferMapAsyncStatus status, void* userdata) {
                ASSERT_EQ(WGPUBufferMapAsyncStatus_Success, status);
                *static_cast<bool*>(userdata) = true;
            },
            &done);

        while (!done) {
            WaitABit();
        }
    }

    wgpu::Texture CreateAndInitializeTexture(const wgpu::Extent3D& size,
                                             wgpu::TextureFormat format,
                                             wgpu::Color color = {0.f, 0.f, 0.f, 0.f}) {
        wgpu::TextureDescriptor descriptor;
        descriptor.size = size;
        descriptor.format = format;
        descriptor.usage = wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::CopySrc |
                           wgpu::TextureUsage::RenderAttachment |
                           wgpu::TextureUsage::StorageBinding;
        wgpu::Texture texture = device.CreateTexture(&descriptor);

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();

        for (uint32_t arrayLayer = 0; arrayLayer < size.depthOrArrayLayers; ++arrayLayer) {
            wgpu::TextureViewDescriptor viewDescriptor;
            viewDescriptor.format = format;
            viewDescriptor.dimension = wgpu::TextureViewDimension::e2D;
            viewDescriptor.baseArrayLayer = arrayLayer;
            viewDescriptor.arrayLayerCount = 1u;

            utils::ComboRenderPassDescriptor renderPassDescriptor(
                {texture.CreateView(&viewDescriptor)});
            renderPassDescriptor.cColorAttachments[0].clearColor = color;
            wgpu::RenderPassEncoder renderPass = encoder.BeginRenderPass(&renderPassDescriptor);
            renderPass.EndPass();
        }

        wgpu::CommandBuffer commandBuffer = encoder.Finish();
        queue.Submit(1, &commandBuffer);

        return texture;
    }

    void TestBufferZeroInitInCopyTextureToBuffer(const BufferZeroInitInCopyT2BSpec& spec) {
        constexpr wgpu::TextureFormat kTextureFormat = wgpu::TextureFormat::R32Float;
        ASSERT(utils::GetTexelBlockSizeInBytes(kTextureFormat) * spec.textureSize.width %
                   kTextureBytesPerRowAlignment ==
               0);

        constexpr wgpu::Color kClearColor = {0.5f, 0.5f, 0.5f, 0.5f};
        wgpu::Texture texture =
            CreateAndInitializeTexture(spec.textureSize, kTextureFormat, kClearColor);

        const wgpu::ImageCopyTexture imageCopyTexture =
            utils::CreateImageCopyTexture(texture, 0, {0, 0, 0});

        const uint64_t bufferSize = spec.bufferOffset + spec.extraBytes +
                                    utils::RequiredBytesInCopy(spec.bytesPerRow, spec.rowsPerImage,
                                                               spec.textureSize, kTextureFormat);
        wgpu::Buffer buffer =
            CreateBuffer(bufferSize, wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst);
        const wgpu::ImageCopyBuffer imageCopyBuffer = utils::CreateImageCopyBuffer(
            buffer, spec.bufferOffset, spec.bytesPerRow, spec.rowsPerImage);

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyTextureToBuffer(&imageCopyTexture, &imageCopyBuffer, &spec.textureSize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();
        EXPECT_LAZY_CLEAR(spec.lazyClearCount, queue.Submit(1, &commandBuffer));

        const uint64_t expectedValueCount = bufferSize / sizeof(float);
        std::vector<float> expectedValues(expectedValueCount, 0.f);

        for (uint32_t slice = 0; slice < spec.textureSize.depthOrArrayLayers; ++slice) {
            const uint64_t baseOffsetBytesPerSlice =
                spec.bufferOffset + spec.bytesPerRow * spec.rowsPerImage * slice;
            for (uint32_t y = 0; y < spec.textureSize.height; ++y) {
                const uint64_t baseOffsetBytesPerRow =
                    baseOffsetBytesPerSlice + spec.bytesPerRow * y;
                const uint64_t baseOffsetFloatCountPerRow = baseOffsetBytesPerRow / sizeof(float);
                for (uint32_t x = 0; x < spec.textureSize.width; ++x) {
                    expectedValues[baseOffsetFloatCountPerRow + x] = 0.5f;
                }
            }
        }

        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_FLOAT_RANGE_EQ(expectedValues.data(), buffer, 0,
                                                           expectedValues.size()));
    }

    void TestBufferZeroInitInBindGroup(wgpu::ShaderModule module,
                                       uint64_t bufferOffset,
                                       uint64_t boundBufferSize,
                                       const std::vector<uint32_t>& expectedBufferData) {
        wgpu::ComputePipelineDescriptor pipelineDescriptor;
        pipelineDescriptor.layout = nullptr;
        pipelineDescriptor.compute.module = module;
        pipelineDescriptor.compute.entryPoint = "main";
        wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&pipelineDescriptor);

        const uint64_t bufferSize = expectedBufferData.size() * sizeof(uint32_t);
        wgpu::Buffer buffer =
            CreateBuffer(bufferSize, wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc |
                                         wgpu::BufferUsage::Storage | wgpu::BufferUsage::Uniform);
        wgpu::Texture outputTexture =
            CreateAndInitializeTexture({1u, 1u, 1u}, wgpu::TextureFormat::RGBA8Unorm);

        wgpu::BindGroup bindGroup = utils::MakeBindGroup(
            device, pipeline.GetBindGroupLayout(0),
            {{0, buffer, bufferOffset, boundBufferSize}, {1u, outputTexture.CreateView()}});

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::ComputePassEncoder computePass = encoder.BeginComputePass();
        computePass.SetBindGroup(0, bindGroup);
        computePass.SetPipeline(pipeline);
        computePass.Dispatch(1u);
        computePass.EndPass();
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_RANGE_EQ(expectedBufferData.data(), buffer, 0,
                                                         expectedBufferData.size()));

        constexpr RGBA8 kExpectedColor = {0, 255, 0, 255};
        EXPECT_PIXEL_RGBA8_EQ(kExpectedColor, outputTexture, 0u, 0u);
    }

    wgpu::RenderPipeline CreateRenderPipelineForTest(
        const char* vertexShader,
        uint32_t vertexBufferCount = 1u,
        wgpu::VertexFormat vertexFormat = wgpu::VertexFormat::Float32x4) {
        constexpr wgpu::TextureFormat kColorAttachmentFormat = wgpu::TextureFormat::RGBA8Unorm;

        wgpu::ShaderModule vsModule = utils::CreateShaderModule(device, vertexShader);

        wgpu::ShaderModule fsModule = utils::CreateShaderModule(device, R"(
            [[stage(fragment)]]
            fn main([[location(0)]] i_color : vec4<f32>) -> [[location(0)]] vec4<f32> {
                return i_color;
            })");

        ASSERT(vertexBufferCount <= 1u);
        utils::ComboRenderPipelineDescriptor descriptor;
        descriptor.vertex.module = vsModule;
        descriptor.cFragment.module = fsModule;
        descriptor.primitive.topology = wgpu::PrimitiveTopology::PointList;
        descriptor.vertex.bufferCount = vertexBufferCount;
        descriptor.cBuffers[0].arrayStride = Align(utils::VertexFormatSize(vertexFormat), 4);
        descriptor.cBuffers[0].attributeCount = 1;
        descriptor.cAttributes[0].format = vertexFormat;
        descriptor.cTargets[0].format = kColorAttachmentFormat;
        return device.CreateRenderPipeline(&descriptor);
    }

    void ExpectLazyClearSubmitAndCheckOutputs(wgpu::CommandEncoder encoder,
                                              wgpu::Buffer buffer,
                                              uint64_t bufferSize,
                                              wgpu::Texture colorAttachment) {
        wgpu::CommandBuffer commandBuffer = encoder.Finish();
        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        // Although we just bind a part of the buffer, we still expect the whole buffer to be
        // lazily initialized to 0.
        const std::vector<uint32_t> expectedBufferData(bufferSize / sizeof(uint32_t), 0);
        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_RANGE_EQ(expectedBufferData.data(), buffer, 0,
                                                         expectedBufferData.size()));

        const RGBA8 kExpectedPixelValue = {0, 255, 0, 255};
        EXPECT_PIXEL_RGBA8_EQ(kExpectedPixelValue, colorAttachment, 0, 0);
    }

    void TestBufferZeroInitAsVertexBuffer(uint64_t vertexBufferOffset) {
        constexpr wgpu::TextureFormat kColorAttachmentFormat = wgpu::TextureFormat::RGBA8Unorm;

        wgpu::RenderPipeline renderPipeline = CreateRenderPipelineForTest(R"(
            struct VertexOut {
                [[location(0)]] color : vec4<f32>;
                [[builtin(position)]] position : vec4<f32>;
            };

            [[stage(vertex)]] fn main([[location(0)]] pos : vec4<f32>) -> VertexOut {
                var output : VertexOut;
                if (all(pos == vec4<f32>(0.0, 0.0, 0.0, 0.0))) {
                    output.color = vec4<f32>(0.0, 1.0, 0.0, 1.0);
                } else {
                    output.color = vec4<f32>(1.0, 0.0, 0.0, 1.0);
                }
                output.position = vec4<f32>(0.0, 0.0, 0.0, 1.0);
                return output;
            })");

        constexpr uint64_t kVertexAttributeSize = sizeof(float) * 4;
        const uint64_t vertexBufferSize = kVertexAttributeSize + vertexBufferOffset;
        wgpu::Buffer vertexBuffer =
            CreateBuffer(vertexBufferSize, wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopySrc |
                                               wgpu::BufferUsage::CopyDst);
        wgpu::Texture colorAttachment =
            CreateAndInitializeTexture({1, 1, 1}, kColorAttachmentFormat);
        utils::ComboRenderPassDescriptor renderPassDescriptor({colorAttachment.CreateView()});

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::RenderPassEncoder renderPass = encoder.BeginRenderPass(&renderPassDescriptor);

        // Bind the buffer with offset == vertexBufferOffset and size kVertexAttributeSize as the
        // vertex buffer.
        renderPass.SetVertexBuffer(0, vertexBuffer, vertexBufferOffset, kVertexAttributeSize);
        renderPass.SetPipeline(renderPipeline);
        renderPass.Draw(1);
        renderPass.EndPass();

        ExpectLazyClearSubmitAndCheckOutputs(encoder, vertexBuffer, vertexBufferSize,
                                             colorAttachment);
    }

    void TestBufferZeroInitAsIndexBuffer(uint64_t indexBufferOffset) {
        constexpr wgpu::TextureFormat kColorAttachmentFormat = wgpu::TextureFormat::RGBA8Unorm;

        wgpu::RenderPipeline renderPipeline =
            CreateRenderPipelineForTest(R"(
            struct VertexOut {
                [[location(0)]] color : vec4<f32>;
                [[builtin(position)]] position : vec4<f32>;
            };

            [[stage(vertex)]]
            fn main([[builtin(vertex_index)]] VertexIndex : u32) -> VertexOut {
                var output : VertexOut;
                if (VertexIndex == 0u) {
                    output.color = vec4<f32>(0.0, 1.0, 0.0, 1.0);
                } else {
                    output.color = vec4<f32>(1.0, 0.0, 0.0, 1.0);
                }
                output.position = vec4<f32>(0.0, 0.0, 0.0, 1.0);
                return output;
            })",
                                        0 /* vertexBufferCount */);

        // The buffer size cannot be less than 4
        const uint64_t indexBufferSize = sizeof(uint32_t) + indexBufferOffset;
        wgpu::Buffer indexBuffer =
            CreateBuffer(indexBufferSize, wgpu::BufferUsage::Index | wgpu::BufferUsage::CopySrc |
                                              wgpu::BufferUsage::CopyDst);

        wgpu::Texture colorAttachment =
            CreateAndInitializeTexture({1, 1, 1}, kColorAttachmentFormat);
        utils::ComboRenderPassDescriptor renderPassDescriptor({colorAttachment.CreateView()});

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::RenderPassEncoder renderPass = encoder.BeginRenderPass(&renderPassDescriptor);
        renderPass.SetPipeline(renderPipeline);

        // Bind the buffer with offset == indexBufferOffset and size sizeof(uint32_t) as the index
        // buffer.
        renderPass.SetIndexBuffer(indexBuffer, wgpu::IndexFormat::Uint16, indexBufferOffset,
                                  sizeof(uint32_t));
        renderPass.DrawIndexed(1);
        renderPass.EndPass();

        ExpectLazyClearSubmitAndCheckOutputs(encoder, indexBuffer, indexBufferSize,
                                             colorAttachment);
    }

    void TestBufferZeroInitAsIndirectBufferForDrawIndirect(uint64_t indirectBufferOffset) {
        constexpr wgpu::TextureFormat kColorAttachmentFormat = wgpu::TextureFormat::RGBA8Unorm;
        constexpr wgpu::Color kClearColorGreen = {0.f, 1.f, 0.f, 1.f};

        // As long as the vertex shader is executed once, the output color will be red.
        wgpu::RenderPipeline renderPipeline =
            CreateRenderPipelineForTest(R"(
            struct VertexOut {
                [[location(0)]] color : vec4<f32>;
                [[builtin(position)]] position : vec4<f32>;
            };

            [[stage(vertex)]] fn main() -> VertexOut {
                var output : VertexOut;
                output.color = vec4<f32>(1.0, 0.0, 0.0, 1.0);
                output.position = vec4<f32>(0.0, 0.0, 0.0, 1.0);
                return output;
            })",
                                        0 /* vertexBufferCount */);

        // Clear the color attachment to green.
        wgpu::Texture colorAttachment =
            CreateAndInitializeTexture({1, 1, 1}, kColorAttachmentFormat, kClearColorGreen);
        utils::ComboRenderPassDescriptor renderPassDescriptor({colorAttachment.CreateView()});
        renderPassDescriptor.cColorAttachments[0].loadOp = wgpu::LoadOp::Load;

        const uint64_t bufferSize = kDrawIndirectSize + indirectBufferOffset;
        wgpu::Buffer indirectBuffer =
            CreateBuffer(bufferSize, wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::Indirect);

        // The indirect buffer should be lazily cleared to 0, so we actually draw nothing and the
        // color attachment will keep its original color (green) after we end the render pass.
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::RenderPassEncoder renderPass = encoder.BeginRenderPass(&renderPassDescriptor);
        renderPass.SetPipeline(renderPipeline);
        renderPass.DrawIndirect(indirectBuffer, indirectBufferOffset);
        renderPass.EndPass();

        ExpectLazyClearSubmitAndCheckOutputs(encoder, indirectBuffer, bufferSize, colorAttachment);
    }

    void TestBufferZeroInitAsIndirectBufferForDrawIndexedIndirect(uint64_t indirectBufferOffset) {
        constexpr wgpu::TextureFormat kColorAttachmentFormat = wgpu::TextureFormat::RGBA8Unorm;
        constexpr wgpu::Color kClearColorGreen = {0.f, 1.f, 0.f, 1.f};

        // As long as the vertex shader is executed once, the output color will be red.
        wgpu::RenderPipeline renderPipeline =
            CreateRenderPipelineForTest(R"(
            struct VertexOut {
                [[location(0)]] color : vec4<f32>;
                [[builtin(position)]] position : vec4<f32>;
            };

            [[stage(vertex)]] fn main() -> VertexOut {
                var output : VertexOut;
                output.color = vec4<f32>(1.0, 0.0, 0.0, 1.0);
                output.position = vec4<f32>(0.0, 0.0, 0.0, 1.0);
                return output;
            })",
                                        0 /* vertexBufferCount */);
        wgpu::Buffer indexBuffer =
            utils::CreateBufferFromData<uint32_t>(device, wgpu::BufferUsage::Index, {0});

        // Clear the color attachment to green.
        wgpu::Texture colorAttachment =
            CreateAndInitializeTexture({1, 1, 1}, kColorAttachmentFormat, kClearColorGreen);
        utils::ComboRenderPassDescriptor renderPassDescriptor({colorAttachment.CreateView()});
        renderPassDescriptor.cColorAttachments[0].loadOp = wgpu::LoadOp::Load;

        const uint64_t bufferSize = kDrawIndexedIndirectSize + indirectBufferOffset;
        wgpu::Buffer indirectBuffer =
            CreateBuffer(bufferSize, wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::Indirect);

        // The indirect buffer should be lazily cleared to 0, so we actually draw nothing and the
        // color attachment will keep its original color (green) after we end the render pass.
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::RenderPassEncoder renderPass = encoder.BeginRenderPass(&renderPassDescriptor);
        renderPass.SetPipeline(renderPipeline);
        renderPass.SetIndexBuffer(indexBuffer, wgpu::IndexFormat::Uint16);
        renderPass.DrawIndexedIndirect(indirectBuffer, indirectBufferOffset);
        renderPass.EndPass();

        ExpectLazyClearSubmitAndCheckOutputs(encoder, indirectBuffer, bufferSize, colorAttachment);
    }

    void TestBufferZeroInitAsIndirectBufferForDispatchIndirect(uint64_t indirectBufferOffset) {
        constexpr wgpu::TextureFormat kColorAttachmentFormat = wgpu::TextureFormat::RGBA8Unorm;
        constexpr wgpu::Color kClearColorGreen = {0.f, 1.f, 0.f, 1.f};

        // As long as the comptue shader is executed once, the pixel color of outImage will be set
        // to red.
        const char* computeShader = R"(
            [[group(0), binding(0)]] var outImage : texture_storage_2d<rgba8unorm, write>;

            [[stage(compute), workgroup_size(1)]] fn main() {
                textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(1.0, 0.0, 0.0, 1.0));
            })";

        wgpu::ComputePipelineDescriptor pipelineDescriptor;
        pipelineDescriptor.layout = nullptr;
        pipelineDescriptor.compute.module = utils::CreateShaderModule(device, computeShader);
        pipelineDescriptor.compute.entryPoint = "main";
        wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&pipelineDescriptor);

        // Clear the color of outputTexture to green.
        wgpu::Texture outputTexture =
            CreateAndInitializeTexture({1u, 1u, 1u}, kColorAttachmentFormat, kClearColorGreen);
        wgpu::BindGroup bindGroup = utils::MakeBindGroup(device, pipeline.GetBindGroupLayout(0),
                                                         {{0, outputTexture.CreateView()}});

        const uint64_t bufferSize = kDispatchIndirectSize + indirectBufferOffset;
        wgpu::Buffer indirectBuffer =
            CreateBuffer(bufferSize, wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::Indirect);

        // The indirect buffer should be lazily cleared to 0, so we actually don't execute the
        // compute shader and the output texture should keep its original color (green).
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::ComputePassEncoder computePass = encoder.BeginComputePass();
        computePass.SetBindGroup(0, bindGroup);
        computePass.SetPipeline(pipeline);
        computePass.DispatchIndirect(indirectBuffer, indirectBufferOffset);
        computePass.EndPass();

        ExpectLazyClearSubmitAndCheckOutputs(encoder, indirectBuffer, bufferSize, outputTexture);
    }
};

// Test that calling writeBuffer to overwrite the entire buffer doesn't need to lazily initialize
// the destination buffer.
TEST_P(BufferZeroInitTest, WriteBufferToEntireBuffer) {
    constexpr uint32_t kBufferSize = 8u;
    constexpr wgpu::BufferUsage kBufferUsage =
        wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer buffer = CreateBuffer(kBufferSize, kBufferUsage);

    constexpr std::array<uint32_t, kBufferSize / sizeof(uint32_t)> kExpectedData = {
        {0x02020202u, 0x02020202u}};
    EXPECT_LAZY_CLEAR(0u, queue.WriteBuffer(buffer, 0, kExpectedData.data(), kBufferSize));

    EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_RANGE_EQ(kExpectedData.data(), buffer, 0,
                                                     kBufferSize / sizeof(uint32_t)));
}

// Test that calling writeBuffer to overwrite a part of buffer needs to lazily initialize the
// destination buffer.
TEST_P(BufferZeroInitTest, WriteBufferToSubBuffer) {
    constexpr uint32_t kBufferSize = 8u;
    constexpr wgpu::BufferUsage kBufferUsage =
        wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst;

    constexpr uint32_t kCopyValue = 0x02020202u;

    // offset == 0
    {
        wgpu::Buffer buffer = CreateBuffer(kBufferSize, kBufferUsage);

        constexpr uint32_t kCopyOffset = 0u;
        EXPECT_LAZY_CLEAR(1u,
                          queue.WriteBuffer(buffer, kCopyOffset, &kCopyValue, sizeof(kCopyValue)));

        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_EQ(kCopyValue, buffer, kCopyOffset));
        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_EQ(0, buffer, kBufferSize - sizeof(kCopyValue)));
    }

    // offset > 0
    {
        wgpu::Buffer buffer = CreateBuffer(kBufferSize, kBufferUsage);

        constexpr uint32_t kCopyOffset = 4u;
        EXPECT_LAZY_CLEAR(1u,
                          queue.WriteBuffer(buffer, kCopyOffset, &kCopyValue, sizeof(kCopyValue)));

        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_EQ(0, buffer, 0));
        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_EQ(kCopyValue, buffer, kCopyOffset));
    }
}

// Test that the code path of CopyBufferToBuffer clears the source buffer correctly when it is the
// first use of the source buffer.
TEST_P(BufferZeroInitTest, CopyBufferToBufferSource) {
    constexpr uint64_t kBufferSize = 16u;
    constexpr wgpu::BufferUsage kBufferUsage =
        wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst;
    wgpu::BufferDescriptor bufferDescriptor;
    bufferDescriptor.size = kBufferSize;
    bufferDescriptor.usage = kBufferUsage;

    constexpr std::array<uint8_t, kBufferSize> kInitialData = {
        {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};

    wgpu::Buffer dstBuffer =
        utils::CreateBufferFromData(device, kInitialData.data(), kBufferSize, kBufferUsage);

    constexpr std::array<uint32_t, kBufferSize / sizeof(uint32_t)> kExpectedData = {{0, 0, 0, 0}};

    // Full copy from the source buffer
    {
        wgpu::Buffer srcBuffer = device.CreateBuffer(&bufferDescriptor);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToBuffer(srcBuffer, 0, dstBuffer, 0, kBufferSize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_RANGE_EQ(kExpectedData.data(), srcBuffer, 0,
                                                         kBufferSize / sizeof(uint32_t)));
    }

    // Partial copy from the source buffer
    // srcOffset == 0
    {
        constexpr uint64_t kSrcOffset = 0;
        constexpr uint64_t kCopySize = kBufferSize / 2;

        wgpu::Buffer srcBuffer = device.CreateBuffer(&bufferDescriptor);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToBuffer(srcBuffer, kSrcOffset, dstBuffer, 0, kCopySize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_RANGE_EQ(kExpectedData.data(), srcBuffer, 0,
                                                         kBufferSize / sizeof(uint32_t)));
    }

    // srcOffset > 0 and srcOffset + copySize == srcBufferSize
    {
        constexpr uint64_t kSrcOffset = kBufferSize / 2;
        constexpr uint64_t kCopySize = kBufferSize - kSrcOffset;

        wgpu::Buffer srcBuffer = device.CreateBuffer(&bufferDescriptor);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToBuffer(srcBuffer, kSrcOffset, dstBuffer, 0, kCopySize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_RANGE_EQ(kExpectedData.data(), srcBuffer, 0,
                                                         kBufferSize / sizeof(uint32_t)));
    }

    // srcOffset > 0 and srcOffset + copySize < srcBufferSize
    {
        constexpr uint64_t kSrcOffset = kBufferSize / 4;
        constexpr uint64_t kCopySize = kBufferSize / 2;

        wgpu::Buffer srcBuffer = device.CreateBuffer(&bufferDescriptor);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToBuffer(srcBuffer, kSrcOffset, dstBuffer, 0, kCopySize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_RANGE_EQ(kExpectedData.data(), srcBuffer, 0,
                                                         kBufferSize / sizeof(uint32_t)));
    }
}

// Test that the code path of CopyBufferToBuffer clears the destination buffer correctly when it is
// the first use of the destination buffer.
TEST_P(BufferZeroInitTest, CopyBufferToBufferDestination) {
    constexpr uint64_t kBufferSize = 16u;
    constexpr wgpu::BufferUsage kBufferUsage =
        wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst;
    wgpu::BufferDescriptor bufferDescriptor;
    bufferDescriptor.size = kBufferSize;
    bufferDescriptor.usage = kBufferUsage;

    const std::array<uint8_t, kBufferSize> kInitialData = {
        {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};
    wgpu::Buffer srcBuffer =
        utils::CreateBufferFromData(device, kInitialData.data(), kBufferSize, kBufferUsage);

    // Full copy from the source buffer doesn't need lazy initialization at all.
    {
        wgpu::Buffer dstBuffer = device.CreateBuffer(&bufferDescriptor);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToBuffer(srcBuffer, 0, dstBuffer, 0, kBufferSize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(0u, queue.Submit(1, &commandBuffer));

        EXPECT_LAZY_CLEAR(
            0u, EXPECT_BUFFER_U32_RANGE_EQ(reinterpret_cast<const uint32_t*>(kInitialData.data()),
                                           dstBuffer, 0, kBufferSize / sizeof(uint32_t)));
    }

    // Partial copy from the source buffer needs lazy initialization.
    // offset == 0
    {
        constexpr uint32_t kDstOffset = 0;
        constexpr uint32_t kCopySize = kBufferSize / 2;

        wgpu::Buffer dstBuffer = device.CreateBuffer(&bufferDescriptor);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToBuffer(srcBuffer, 0, dstBuffer, kDstOffset, kCopySize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        std::array<uint8_t, kBufferSize> expectedData;
        expectedData.fill(0);
        for (uint32_t index = kDstOffset; index < kDstOffset + kCopySize; ++index) {
            expectedData[index] = kInitialData[index - kDstOffset];
        }

        EXPECT_LAZY_CLEAR(
            0u, EXPECT_BUFFER_U32_RANGE_EQ(reinterpret_cast<uint32_t*>(expectedData.data()),
                                           dstBuffer, 0, kBufferSize / sizeof(uint32_t)));
    }

    // offset > 0 and dstOffset + CopySize == kBufferSize
    {
        constexpr uint32_t kDstOffset = kBufferSize / 2;
        constexpr uint32_t kCopySize = kBufferSize - kDstOffset;

        wgpu::Buffer dstBuffer = device.CreateBuffer(&bufferDescriptor);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToBuffer(srcBuffer, 0, dstBuffer, kDstOffset, kCopySize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        std::array<uint8_t, kBufferSize> expectedData;
        expectedData.fill(0);
        for (uint32_t index = kDstOffset; index < kDstOffset + kCopySize; ++index) {
            expectedData[index] = kInitialData[index - kDstOffset];
        }

        EXPECT_LAZY_CLEAR(
            0u, EXPECT_BUFFER_U32_RANGE_EQ(reinterpret_cast<uint32_t*>(expectedData.data()),
                                           dstBuffer, 0, kBufferSize / sizeof(uint32_t)));
    }

    // offset > 0 and dstOffset + CopySize < kBufferSize
    {
        constexpr uint32_t kDstOffset = kBufferSize / 4;
        constexpr uint32_t kCopySize = kBufferSize / 2;

        wgpu::Buffer dstBuffer = device.CreateBuffer(&bufferDescriptor);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToBuffer(srcBuffer, 0, dstBuffer, kDstOffset, kCopySize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        std::array<uint8_t, kBufferSize> expectedData;
        expectedData.fill(0);
        for (uint32_t index = kDstOffset; index < kDstOffset + kCopySize; ++index) {
            expectedData[index] = kInitialData[index - kDstOffset];
        }

        EXPECT_LAZY_CLEAR(
            0u, EXPECT_BUFFER_U32_RANGE_EQ(reinterpret_cast<uint32_t*>(expectedData.data()),
                                           dstBuffer, 0, kBufferSize / sizeof(uint32_t)));
    }
}

// Test that the code path of readable buffer mapping clears the buffer correctly when it is the
// first use of the buffer.
TEST_P(BufferZeroInitTest, MapAsync_Read) {
    constexpr uint32_t kBufferSize = 16u;
    constexpr wgpu::BufferUsage kBufferUsage =
        wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;

    constexpr wgpu::MapMode kMapMode = wgpu::MapMode::Read;

    // Map the whole buffer
    {
        wgpu::Buffer buffer = CreateBuffer(kBufferSize, kBufferUsage);
        EXPECT_LAZY_CLEAR(1u, MapAsyncAndWait(buffer, kMapMode, 0, kBufferSize));

        const uint32_t* mappedDataUint = static_cast<const uint32_t*>(buffer.GetConstMappedRange());
        for (uint32_t i = 0; i < kBufferSize / sizeof(uint32_t); ++i) {
            EXPECT_EQ(0u, mappedDataUint[i]);
        }
        buffer.Unmap();
    }

    // Map a range of a buffer
    {
        wgpu::Buffer buffer = CreateBuffer(kBufferSize, kBufferUsage);

        constexpr uint64_t kOffset = 8u;
        constexpr uint64_t kSize = 8u;
        EXPECT_LAZY_CLEAR(1u, MapAsyncAndWait(buffer, kMapMode, kOffset, kSize));

        const uint32_t* mappedDataUint =
            static_cast<const uint32_t*>(buffer.GetConstMappedRange(kOffset));
        for (uint32_t i = 0; i < kSize / sizeof(uint32_t); ++i) {
            EXPECT_EQ(0u, mappedDataUint[i]);
        }
        buffer.Unmap();

        EXPECT_LAZY_CLEAR(0u, MapAsyncAndWait(buffer, kMapMode, 0, kBufferSize));
        mappedDataUint = static_cast<const uint32_t*>(buffer.GetConstMappedRange());
        for (uint32_t i = 0; i < kBufferSize / sizeof(uint32_t); ++i) {
            EXPECT_EQ(0u, mappedDataUint[i]);
        }
        buffer.Unmap();
    }
}

// Test that the code path of writable buffer mapping clears the buffer correctly when it is the
// first use of the buffer.
TEST_P(BufferZeroInitTest, MapAsync_Write) {
    constexpr uint32_t kBufferSize = 16u;
    constexpr wgpu::BufferUsage kBufferUsage =
        wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc;

    constexpr wgpu::MapMode kMapMode = wgpu::MapMode::Write;

    constexpr std::array<uint32_t, kBufferSize / sizeof(uint32_t)> kExpectedData = {{0, 0, 0, 0}};

    // Map the whole buffer
    {
        wgpu::Buffer buffer = CreateBuffer(kBufferSize, kBufferUsage);
        EXPECT_LAZY_CLEAR(1u, MapAsyncAndWait(buffer, kMapMode, 0, kBufferSize));
        buffer.Unmap();

        EXPECT_LAZY_CLEAR(
            0u, EXPECT_BUFFER_U32_RANGE_EQ(reinterpret_cast<const uint32_t*>(kExpectedData.data()),
                                           buffer, 0, kExpectedData.size()));
    }

    // Map a range of a buffer
    {
        wgpu::Buffer buffer = CreateBuffer(kBufferSize, kBufferUsage);

        constexpr uint64_t kOffset = 8u;
        constexpr uint64_t kSize = 8u;
        EXPECT_LAZY_CLEAR(1u, MapAsyncAndWait(buffer, kMapMode, kOffset, kSize));
        buffer.Unmap();

        EXPECT_LAZY_CLEAR(
            0u, EXPECT_BUFFER_U32_RANGE_EQ(reinterpret_cast<const uint32_t*>(kExpectedData.data()),
                                           buffer, 0, kExpectedData.size()));
    }
}

// Test that the code path of creating a buffer with BufferDescriptor.mappedAtCreation == true
// clears the buffer correctly at the creation of the buffer.
TEST_P(BufferZeroInitTest, MappedAtCreation) {
    constexpr uint32_t kBufferSize = 16u;

    constexpr std::array<uint32_t, kBufferSize / sizeof(uint32_t)> kExpectedData = {{0, 0, 0, 0}};

    // Buffer with MapRead usage
    {
        constexpr wgpu::BufferUsage kBufferUsage = wgpu::BufferUsage::MapRead;

        wgpu::Buffer buffer;
        EXPECT_LAZY_CLEAR(1u, buffer = CreateBuffer(kBufferSize, kBufferUsage, true));
        const uint8_t* mappedData = static_cast<const uint8_t*>(buffer.GetConstMappedRange());
        EXPECT_EQ(0, memcmp(mappedData, kExpectedData.data(), kBufferSize));
        buffer.Unmap();

        MapAsyncAndWait(buffer, wgpu::MapMode::Read, 0, kBufferSize);
        mappedData = static_cast<const uint8_t*>(buffer.GetConstMappedRange());
        EXPECT_EQ(0, memcmp(mappedData, kExpectedData.data(), kBufferSize));
        buffer.Unmap();
    }

    // Buffer with MapRead usage and upload the buffer (from CPU and GPU)
    {
        constexpr std::array<uint32_t, kBufferSize / sizeof(uint32_t)> kExpectedFinalData = {
            {10, 20, 30, 40}};

        constexpr wgpu::BufferUsage kBufferUsage =
            wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;

        wgpu::Buffer buffer;
        EXPECT_LAZY_CLEAR(1u, buffer = CreateBuffer(kBufferSize, kBufferUsage, true));

        // Update data from the CPU side.
        uint32_t* mappedData = static_cast<uint32_t*>(buffer.GetMappedRange());
        mappedData[2] = kExpectedFinalData[2];
        mappedData[3] = kExpectedFinalData[3];
        buffer.Unmap();

        // Update data from the GPU side.
        wgpu::Buffer uploadBuffer = utils::CreateBufferFromData(
            device, wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst,
            {kExpectedFinalData[0], kExpectedFinalData[1]});

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToBuffer(uploadBuffer, 0, buffer, 0, 2 * sizeof(uint32_t));
        wgpu::CommandBuffer commandBuffer = encoder.Finish();
        EXPECT_LAZY_CLEAR(0u, queue.Submit(1, &commandBuffer));

        // Check the content of the buffer on the CPU side
        MapAsyncAndWait(buffer, wgpu::MapMode::Read, 0, kBufferSize);
        const uint32_t* constMappedData =
            static_cast<const uint32_t*>(buffer.GetConstMappedRange());
        EXPECT_EQ(0, memcmp(kExpectedFinalData.data(), constMappedData, kBufferSize));
    }

    // Buffer with MapWrite usage
    {
        constexpr wgpu::BufferUsage kBufferUsage =
            wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc;

        wgpu::Buffer buffer;
        EXPECT_LAZY_CLEAR(1u, buffer = CreateBuffer(kBufferSize, kBufferUsage, true));

        const uint8_t* mappedData = static_cast<const uint8_t*>(buffer.GetConstMappedRange());
        EXPECT_EQ(0, memcmp(mappedData, kExpectedData.data(), kBufferSize));
        buffer.Unmap();

        EXPECT_LAZY_CLEAR(
            0u, EXPECT_BUFFER_U32_RANGE_EQ(kExpectedData.data(), buffer, 0, kExpectedData.size()));
    }

    // Buffer with neither MapRead nor MapWrite usage
    {
        constexpr wgpu::BufferUsage kBufferUsage = wgpu::BufferUsage::CopySrc;

        wgpu::Buffer buffer;
        EXPECT_LAZY_CLEAR(1u, buffer = CreateBuffer(kBufferSize, kBufferUsage, true));

        const uint8_t* mappedData = static_cast<const uint8_t*>(buffer.GetConstMappedRange());
        EXPECT_EQ(0, memcmp(mappedData, kExpectedData.data(), kBufferSize));
        buffer.Unmap();

        EXPECT_LAZY_CLEAR(
            0u, EXPECT_BUFFER_U32_RANGE_EQ(kExpectedData.data(), buffer, 0, kExpectedData.size()));
    }
}

// Test that the code path of CopyBufferToTexture clears the source buffer correctly when it is the
// first use of the buffer.
TEST_P(BufferZeroInitTest, CopyBufferToTexture) {
    constexpr wgpu::Extent3D kTextureSize = {16u, 16u, 1u};

    constexpr wgpu::TextureFormat kTextureFormat = wgpu::TextureFormat::R32Uint;

    wgpu::Texture texture = CreateAndInitializeTexture(kTextureSize, kTextureFormat);
    const wgpu::ImageCopyTexture imageCopyTexture =
        utils::CreateImageCopyTexture(texture, 0, {0, 0, 0});

    const uint32_t rowsPerImage = kTextureSize.height;
    const uint32_t requiredBufferSizeForCopy = utils::RequiredBytesInCopy(
        kTextureBytesPerRowAlignment, rowsPerImage, kTextureSize, kTextureFormat);

    constexpr wgpu::BufferUsage kBufferUsage =
        wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst;

    // bufferOffset == 0
    {
        constexpr uint64_t kOffset = 0;
        const uint32_t totalBufferSize = requiredBufferSizeForCopy + kOffset;
        wgpu::Buffer buffer = CreateBuffer(totalBufferSize, kBufferUsage);
        const wgpu::ImageCopyBuffer imageCopyBuffer = utils::CreateImageCopyBuffer(
            buffer, kOffset, kTextureBytesPerRowAlignment, kTextureSize.height);

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToTexture(&imageCopyBuffer, &imageCopyTexture, &kTextureSize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();
        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        const std::vector<uint32_t> expectedValues(totalBufferSize / sizeof(uint32_t), 0);
        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_RANGE_EQ(expectedValues.data(), buffer, 0,
                                                         totalBufferSize / sizeof(uint32_t)));
    }

    // bufferOffset > 0
    {
        constexpr uint64_t kOffset = 8u;
        const uint32_t totalBufferSize = requiredBufferSizeForCopy + kOffset;
        wgpu::Buffer buffer = CreateBuffer(totalBufferSize, kBufferUsage);
        const wgpu::ImageCopyBuffer imageCopyBuffer = utils::CreateImageCopyBuffer(
            buffer, kOffset, kTextureBytesPerRowAlignment, kTextureSize.height);

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToTexture(&imageCopyBuffer, &imageCopyTexture, &kTextureSize);
        wgpu::CommandBuffer commandBuffer = encoder.Finish();
        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commandBuffer));

        const std::vector<uint32_t> expectedValues(totalBufferSize / sizeof(uint32_t), 0);
        EXPECT_LAZY_CLEAR(0u, EXPECT_BUFFER_U32_RANGE_EQ(expectedValues.data(), buffer, 0,
                                                         totalBufferSize / sizeof(uint32_t)));
    }
}

// Test that the code path of CopyTextureToBuffer clears the destination buffer correctly when it is
// the first use of the buffer and the texture is a 2D non-array texture.
TEST_P(BufferZeroInitTest, Copy2DTextureToBuffer) {
    constexpr wgpu::Extent3D kTextureSize = {64u, 8u, 1u};

    // bytesPerRow == texelBlockSizeInBytes * copySize.width && bytesPerRow * copySize.height ==
    // buffer.size
    {
        TestBufferZeroInitInCopyTextureToBuffer(
            {kTextureSize, 0u, 0u, kTextureBytesPerRowAlignment, kTextureSize.height, 0u});
    }

    // bytesPerRow > texelBlockSizeInBytes * copySize.width
    {
        constexpr uint64_t kBytesPerRow = kTextureBytesPerRowAlignment * 2;
        TestBufferZeroInitInCopyTextureToBuffer(
            {kTextureSize, 0u, 0u, kBytesPerRow, kTextureSize.height, 1u});
    }

    // bufferOffset > 0
    {
        constexpr uint64_t kBufferOffset = 16u;
        TestBufferZeroInitInCopyTextureToBuffer({kTextureSize, kBufferOffset, 0u,
                                                 kTextureBytesPerRowAlignment, kTextureSize.height,
                                                 1u});
    }

    // bytesPerRow * copySize.height < buffer.size
    {
        constexpr uint64_t kExtraBufferSize = 16u;
        TestBufferZeroInitInCopyTextureToBuffer({kTextureSize, 0u, kExtraBufferSize,
                                                 kTextureBytesPerRowAlignment, kTextureSize.height,
                                                 1u});
    }
}

// Test that the code path of CopyTextureToBuffer clears the destination buffer correctly when it is
// the first use of the buffer and the texture is a 2D array texture.
TEST_P(BufferZeroInitTest, Copy2DArrayTextureToBuffer) {
    // TODO(crbug.com/dawn/593): This test uses glTextureView() which is not supported on OpenGL ES.
    DAWN_TEST_UNSUPPORTED_IF(IsOpenGLES());

    constexpr wgpu::Extent3D kTextureSize = {64u, 4u, 3u};

    // bytesPerRow == texelBlockSizeInBytes * copySize.width && rowsPerImage == copySize.height &&
    // bytesPerRow * (rowsPerImage * (copySize.depthOrArrayLayers - 1) + copySize.height) ==
    // buffer.size
    {
        TestBufferZeroInitInCopyTextureToBuffer(
            {kTextureSize, 0u, 0u, kTextureBytesPerRowAlignment, kTextureSize.height, 0u});
    }

    // rowsPerImage > copySize.height
    {
        constexpr uint64_t kRowsPerImage = kTextureSize.height + 1u;
        TestBufferZeroInitInCopyTextureToBuffer(
            {kTextureSize, 0u, 0u, kTextureBytesPerRowAlignment, kRowsPerImage, 1u});
    }

    // bytesPerRow * rowsPerImage * copySize.depthOrArrayLayers < buffer.size
    {
        constexpr uint64_t kExtraBufferSize = 16u;
        TestBufferZeroInitInCopyTextureToBuffer({kTextureSize, 0u, kExtraBufferSize,
                                                 kTextureBytesPerRowAlignment, kTextureSize.height,
                                                 1u});
    }
}

// Test that the buffer will be lazy initialized correctly when its first use is to be bound as a
// uniform buffer.
TEST_P(BufferZeroInitTest, BoundAsUniformBuffer) {
    // TODO(crbug.com/dawn/661): Diagnose and fix this backend validation failure on GLES.
    DAWN_SUPPRESS_TEST_IF(IsOpenGLES() && IsBackendValidationEnabled());

    constexpr uint32_t kBoundBufferSize = 16u;
    wgpu::ShaderModule module = utils::CreateShaderModule(device, R"(
        [[block]] struct UBO {
            value : vec4<u32>;
        };
        [[group(0), binding(0)]] var<uniform> ubo : UBO;
        [[group(0), binding(1)]] var outImage : texture_storage_2d<rgba8unorm, write>;

        [[stage(compute), workgroup_size(1)]] fn main() {
            if (all(ubo.value == vec4<u32>(0u, 0u, 0u, 0u))) {
                textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(0.0, 1.0, 0.0, 1.0));
            } else {
                textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(1.0, 0.0, 0.0, 1.0));
            }
        }
    )");

    // Bind the whole buffer
    {
        const std::vector<uint32_t> expected(kBoundBufferSize / sizeof(uint32_t), 0u);
        TestBufferZeroInitInBindGroup(module, 0, kBoundBufferSize, expected);
    }

    // Bind a range of a buffer
    {
        constexpr uint32_t kOffset = 256u;
        constexpr uint32_t kExtraBytes = 16u;
        const std::vector<uint32_t> expected(
            (kBoundBufferSize + kOffset + kExtraBytes) / sizeof(uint32_t), 0u);
        TestBufferZeroInitInBindGroup(module, kOffset, kBoundBufferSize, expected);
    }
}

// Test that the buffer will be lazy initialized correctly when its first use is to be bound as a
// read-only storage buffer.
TEST_P(BufferZeroInitTest, BoundAsReadonlyStorageBuffer) {
    // TODO(crbug.com/dawn/661): Diagnose and fix this backend validation failure on GLES.
    DAWN_SUPPRESS_TEST_IF(IsOpenGLES() && IsBackendValidationEnabled());

    constexpr uint32_t kBoundBufferSize = 16u;
    wgpu::ShaderModule module = utils::CreateShaderModule(device, R"(
        [[block]] struct SSBO {
            value : vec4<u32>;
        };
        [[group(0), binding(0)]] var<storage, read> ssbo : SSBO;
        [[group(0), binding(1)]] var outImage : texture_storage_2d<rgba8unorm, write>;

        [[stage(compute), workgroup_size(1)]] fn main() {
            if (all(ssbo.value == vec4<u32>(0u, 0u, 0u, 0u))) {
                textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(0.0, 1.0, 0.0, 1.0));
            } else {
                textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(1.0, 0.0, 0.0, 1.0));
            }
        }
    )");

    // Bind the whole buffer
    {
        const std::vector<uint32_t> expected(kBoundBufferSize / sizeof(uint32_t), 0u);
        TestBufferZeroInitInBindGroup(module, 0, kBoundBufferSize, expected);
    }

    // Bind a range of a buffer
    {
        constexpr uint32_t kOffset = 256u;
        constexpr uint32_t kExtraBytes = 16u;
        const std::vector<uint32_t> expected(
            (kBoundBufferSize + kOffset + kExtraBytes) / sizeof(uint32_t), 0u);
        TestBufferZeroInitInBindGroup(module, kOffset, kBoundBufferSize, expected);
    }
}

// Test that the buffer will be lazy initialized correctly when its first use is to be bound as a
// storage buffer.
TEST_P(BufferZeroInitTest, BoundAsStorageBuffer) {
    // TODO(crbug.com/dawn/661): Diagnose and fix this backend validation failure on GLES.
    DAWN_SUPPRESS_TEST_IF(IsOpenGLES() && IsBackendValidationEnabled());

    constexpr uint32_t kBoundBufferSize = 32u;
    wgpu::ShaderModule module = utils::CreateShaderModule(device, R"(
        [[block]] struct SSBO {
            value : array<vec4<u32>, 2>;
        };
        [[group(0), binding(0)]] var<storage, read_write> ssbo : SSBO;
        [[group(0), binding(1)]] var outImage : texture_storage_2d<rgba8unorm, write>;

        [[stage(compute), workgroup_size(1)]] fn main() {
            if (all(ssbo.value[0] == vec4<u32>(0u, 0u, 0u, 0u)) &&
                all(ssbo.value[1] == vec4<u32>(0u, 0u, 0u, 0u))) {
                textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(0.0, 1.0, 0.0, 1.0));
            } else {
                textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(1.0, 0.0, 0.0, 1.0));
            }

            storageBarrier();

            ssbo.value[0].x = 10u;
            ssbo.value[1].y = 20u;
        }
    )");

    // Bind the whole buffer
    {
        std::vector<uint32_t> expected(kBoundBufferSize / sizeof(uint32_t), 0u);
        expected[0] = 10u;
        expected[5] = 20u;
        TestBufferZeroInitInBindGroup(module, 0, kBoundBufferSize, expected);
    }

    // Bind a range of a buffer
    {
        constexpr uint32_t kOffset = 256u;
        constexpr uint32_t kExtraBytes = 16u;
        std::vector<uint32_t> expected(
            (kBoundBufferSize + kOffset + kExtraBytes) / sizeof(uint32_t), 0u);
        expected[kOffset / sizeof(uint32_t)] = 10u;
        expected[kOffset / sizeof(uint32_t) + 5u] = 20u;
        TestBufferZeroInitInBindGroup(module, kOffset, kBoundBufferSize, expected);
    }
}

// Test the buffer will be lazily initialized correctly when its first use is in SetVertexBuffer.
TEST_P(BufferZeroInitTest, SetVertexBuffer) {
    // Bind the whole buffer as a vertex buffer.
    {
        constexpr uint64_t kVertexBufferOffset = 0u;
        TestBufferZeroInitAsVertexBuffer(kVertexBufferOffset);
    }

    // Bind the buffer as a vertex buffer with a non-zero offset.
    {
        constexpr uint64_t kVertexBufferOffset = 16u;
        TestBufferZeroInitAsVertexBuffer(kVertexBufferOffset);
    }
}

// Test for crbug.com/dawn/837.
// Test that the padding after a buffer allocation is initialized to 0.
// This test makes an unaligned vertex buffer which should be padded in the backend
// allocation. It then tries to index off the end of the vertex buffer in an indexed
// draw call. A backend which implements robust buffer access via clamping should
// still see zeros at the end of the buffer.
TEST_P(BufferZeroInitTest, PaddingInitialized) {
    DAWN_SUPPRESS_TEST_IF(IsANGLE());  // TODO(crbug.com/dawn/1084).

    constexpr wgpu::TextureFormat kColorAttachmentFormat = wgpu::TextureFormat::RGBA8Unorm;
    // A small sub-4-byte format means a single vertex can fit entirely within the padded buffer,
    // touching some of the padding. Test a small format, as well as larger formats.
    for (wgpu::VertexFormat vertexFormat :
         {wgpu::VertexFormat::Unorm8x2, wgpu::VertexFormat::Float16x2,
          wgpu::VertexFormat::Float32x2}) {
        wgpu::RenderPipeline renderPipeline =
            CreateRenderPipelineForTest(R"(
            struct VertexOut {
                [[location(0)]] color : vec4<f32>;
                [[builtin(position)]] position : vec4<f32>;
            };

            [[stage(vertex)]] fn main([[location(0)]] pos : vec2<f32>) -> VertexOut {
                var output : VertexOut;
                if (all(pos == vec2<f32>(0.0, 0.0))) {
                    output.color = vec4<f32>(0.0, 1.0, 0.0, 1.0);
                } else {
                    output.color = vec4<f32>(1.0, 0.0, 0.0, 1.0);
                }
                output.position = vec4<f32>(0.0, 0.0, 0.0, 1.0);
                return output;
            })",
                                        /* vertexBufferCount */ 1u, vertexFormat);

        // Create an index buffer the indexes off the end of the vertex buffer.
        wgpu::Buffer indexBuffer =
            utils::CreateBufferFromData<uint32_t>(device, wgpu::BufferUsage::Index, {1});

        const uint32_t vertexFormatSize = utils::VertexFormatSize(vertexFormat);

        // Create an 8-bit texture to use to initialize buffer contents.
        wgpu::TextureDescriptor initTextureDesc = {};
        initTextureDesc.size = {vertexFormatSize + 4, 1, 1};
        initTextureDesc.format = wgpu::TextureFormat::R8Unorm;
        initTextureDesc.usage = wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst;
        wgpu::ImageCopyTexture zeroTextureSrc =
            utils::CreateImageCopyTexture(device.CreateTexture(&initTextureDesc), 0, {0, 0, 0});
        {
            wgpu::TextureDataLayout layout =
                utils::CreateTextureDataLayout(0, wgpu::kCopyStrideUndefined);
            std::vector<uint8_t> data(initTextureDesc.size.width);
            queue.WriteTexture(&zeroTextureSrc, data.data(), data.size(), &layout,
                               &initTextureDesc.size);
        }

        for (uint32_t extraBytes : {0, 1, 2, 3, 4}) {
            // Create a vertex buffer to hold a single vertex attribute.
            // Uniform usage is added to force even more padding on D3D12.
            // The buffer is internally padded and allocated as a larger buffer.
            const uint32_t vertexBufferSize = vertexFormatSize + extraBytes;
            for (uint32_t vertexBufferOffset = 0; vertexBufferOffset <= vertexBufferSize;
                 vertexBufferOffset += 4u) {
                wgpu::Buffer vertexBuffer = CreateBuffer(
                    vertexBufferSize, wgpu::BufferUsage::Vertex | wgpu::BufferUsage::Uniform |
                                          wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst);

                // "Fully" initialize the buffer with a copy from an 8-bit texture, touching
                // everything except the padding. From the point-of-view of the API, all
                // |vertexBufferSize| bytes are initialized. Note: Uses CopyTextureToBuffer because
                // it does not require 4-byte alignment.
                {
                    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
                    wgpu::ImageCopyBuffer dst =
                        utils::CreateImageCopyBuffer(vertexBuffer, 0, wgpu::kCopyStrideUndefined);
                    wgpu::Extent3D extent = {vertexBufferSize, 1, 1};
                    encoder.CopyTextureToBuffer(&zeroTextureSrc, &dst, &extent);

                    wgpu::CommandBuffer commandBuffer = encoder.Finish();
                    EXPECT_LAZY_CLEAR(0u, queue.Submit(1, &commandBuffer));
                }

                wgpu::CommandEncoder encoder = device.CreateCommandEncoder();

                wgpu::Texture colorAttachment =
                    CreateAndInitializeTexture({1, 1, 1}, kColorAttachmentFormat);
                utils::ComboRenderPassDescriptor renderPassDescriptor(
                    {colorAttachment.CreateView()});

                wgpu::RenderPassEncoder renderPass = encoder.BeginRenderPass(&renderPassDescriptor);

                renderPass.SetVertexBuffer(0, vertexBuffer, vertexBufferOffset);
                renderPass.SetIndexBuffer(indexBuffer, wgpu::IndexFormat::Uint32);

                renderPass.SetPipeline(renderPipeline);
                renderPass.DrawIndexed(1);
                renderPass.EndPass();

                wgpu::CommandBuffer commandBuffer = encoder.Finish();

                EXPECT_LAZY_CLEAR(0u, queue.Submit(1, &commandBuffer));

                constexpr RGBA8 kExpectedPixelValue = {0, 255, 0, 255};
                EXPECT_PIXEL_RGBA8_EQ(kExpectedPixelValue, colorAttachment, 0, 0);
            }
        }
    }
}

// Test the buffer will be lazily initialized correctly when its first use is in SetIndexBuffer.
TEST_P(BufferZeroInitTest, SetIndexBuffer) {
    // Bind the whole buffer as an index buffer.
    {
        constexpr uint64_t kIndexBufferOffset = 0u;
        TestBufferZeroInitAsIndexBuffer(kIndexBufferOffset);
    }

    // Bind the buffer as an index buffer with a non-zero offset.
    {
        constexpr uint64_t kIndexBufferOffset = 16u;
        TestBufferZeroInitAsIndexBuffer(kIndexBufferOffset);
    }
}

// Test the buffer will be lazily initialized correctly when its first use is an indirect buffer for
// DrawIndirect.
TEST_P(BufferZeroInitTest, IndirectBufferForDrawIndirect) {
    // Bind the whole buffer as an indirect buffer.
    {
        constexpr uint64_t kOffset = 0u;
        TestBufferZeroInitAsIndirectBufferForDrawIndirect(kOffset);
    }

    // Bind the buffer as an indirect buffer with a non-zero offset.
    {
        constexpr uint64_t kOffset = 8u;
        TestBufferZeroInitAsIndirectBufferForDrawIndirect(kOffset);
    }
}

// Test the buffer will be lazily initialized correctly when its first use is an indirect buffer for
// DrawIndexedIndirect.
TEST_P(BufferZeroInitTest, IndirectBufferForDrawIndexedIndirect) {
    // Bind the whole buffer as an indirect buffer.
    {
        constexpr uint64_t kOffset = 0u;
        TestBufferZeroInitAsIndirectBufferForDrawIndexedIndirect(kOffset);
    }

    // Bind the buffer as an indirect buffer with a non-zero offset.
    {
        constexpr uint64_t kOffset = 8u;
        TestBufferZeroInitAsIndirectBufferForDrawIndexedIndirect(kOffset);
    }
}

// Test the buffer will be lazily initialized correctly when its first use is an indirect buffer for
// DispatchIndirect.
TEST_P(BufferZeroInitTest, IndirectBufferForDispatchIndirect) {
    // TODO(crbug.com/dawn/661): Diagnose and fix this backend validation failure on GLES.
    DAWN_SUPPRESS_TEST_IF(IsOpenGLES() && IsBackendValidationEnabled());

    // Bind the whole buffer as an indirect buffer.
    {
        constexpr uint64_t kOffset = 0u;
        TestBufferZeroInitAsIndirectBufferForDispatchIndirect(kOffset);
    }

    // Bind the buffer as an indirect buffer with a non-zero offset.
    {
        constexpr uint64_t kOffset = 8u;
        TestBufferZeroInitAsIndirectBufferForDispatchIndirect(kOffset);
    }
}

// Test the buffer will be lazily initialized correctly when its first use is in resolveQuerySet
TEST_P(BufferZeroInitTest, ResolveQuerySet) {
    // Timestamp query is not supported on OpenGL
    DAWN_TEST_UNSUPPORTED_IF(IsOpenGL());

    // TODO(crbug.com/dawn/545): Crash occurs if we only call WriteTimestamp in a command encoder
    // without any copy commands on Metal on AMD GPU.
    DAWN_SUPPRESS_TEST_IF(IsMetal() && IsAMD());

    // Skip if timestamp feature is not supported on device
    DAWN_TEST_UNSUPPORTED_IF(!SupportsFeatures({"timestamp-query"}));

    // crbug.com/dawn/940: Does not work on Mac 11.0+. Backend validation changed.
    DAWN_TEST_UNSUPPORTED_IF(IsMacOS() && !IsMacOS(10));

    constexpr uint64_t kBufferSize = 16u;
    constexpr wgpu::BufferUsage kBufferUsage =
        wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopyDst;

    wgpu::QuerySetDescriptor descriptor;
    descriptor.count = 2u;
    descriptor.type = wgpu::QueryType::Timestamp;
    wgpu::QuerySet querySet = device.CreateQuerySet(&descriptor);

    // Resolve data to the whole buffer doesn't need lazy initialization.
    {
        constexpr uint32_t kQueryCount = 2u;
        constexpr uint64_t kDestinationOffset = 0u;

        wgpu::Buffer destination = CreateBuffer(kBufferSize, kBufferUsage);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.WriteTimestamp(querySet, 0);
        encoder.WriteTimestamp(querySet, 1);
        encoder.ResolveQuerySet(querySet, 0, kQueryCount, destination, kDestinationOffset);
        wgpu::CommandBuffer commands = encoder.Finish();

        EXPECT_LAZY_CLEAR(0u, queue.Submit(1, &commands));
    }

    // Resolve data to partial of the buffer needs lazy initialization.
    // destinationOffset == 0 and destinationOffset + 8 * queryCount < kBufferSize
    {
        constexpr uint32_t kQueryCount = 1u;
        constexpr uint64_t kDestinationOffset = 0u;

        wgpu::Buffer destination = CreateBuffer(kBufferSize, kBufferUsage);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.WriteTimestamp(querySet, 0);
        encoder.ResolveQuerySet(querySet, 0, kQueryCount, destination, kDestinationOffset);
        wgpu::CommandBuffer commands = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commands));
    }

    // destinationOffset > 0 and destinationOffset + 8 * queryCount <= kBufferSize
    {
        constexpr uint32_t kQueryCount = 1;
        constexpr uint64_t kDestinationOffset = 256u;

        wgpu::Buffer destination = CreateBuffer(kBufferSize + kDestinationOffset, kBufferUsage);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.WriteTimestamp(querySet, 0);
        encoder.ResolveQuerySet(querySet, 0, kQueryCount, destination, kDestinationOffset);
        wgpu::CommandBuffer commands = encoder.Finish();

        EXPECT_LAZY_CLEAR(1u, queue.Submit(1, &commands));
    }
}

DAWN_INSTANTIATE_TEST(BufferZeroInitTest,
                      D3D12Backend({"nonzero_clear_resources_on_creation_for_testing"}),
                      MetalBackend({"nonzero_clear_resources_on_creation_for_testing"}),
                      OpenGLBackend({"nonzero_clear_resources_on_creation_for_testing"}),
                      OpenGLESBackend({"nonzero_clear_resources_on_creation_for_testing"}),
                      VulkanBackend({"nonzero_clear_resources_on_creation_for_testing"}));
