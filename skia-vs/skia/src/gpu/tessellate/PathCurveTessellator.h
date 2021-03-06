/*
 * Copyright 2021 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef tessellate_PathCurveTessellator_DEFINED
#define tessellate_PathCurveTessellator_DEFINED

#include "src/gpu/GrVertexChunkArray.h"
#include "src/gpu/tessellate/PathTessellator.h"

class GrCaps;
class GrGpuBuffer;
class GrPipeline;

namespace skgpu {

// Draws an array of "outer curve" patches and, optionally, inner fan triangles for
// GrCubicTessellateShader. Each patch is an independent 4-point curve, representing either a cubic
// or a conic. Quadratics are converted to cubics and triangles are converted to conics with w=Inf.
class PathCurveTessellator : public PathTessellator {
public:
    // If DrawInnerFan is kNo, this class only emits the path's outer curves. In that case the
    // caller is responsible to handle the path's inner fan.
    enum class DrawInnerFan : bool {
        kNo = false,
        kYes
    };

    // Creates a curve tessellator with the shader type best suited for the given path description.
    static PathCurveTessellator* Make(SkArenaAlloc*,
                                      const SkMatrix& viewMatrix,
                                      const SkPMColor4f&,
                                      DrawInnerFan,
                                      int numPathVerbs,
                                      const GrPipeline&,
                                      const GrCaps&);

    void prepare(GrMeshDrawTarget* target,
                 const PathDrawList& pathDrawList,
                 int totalCombinedPathVerbCnt) override {
        this->prepare(target, pathDrawList, totalCombinedPathVerbCnt, nullptr);
    }

    // Implements PathTessellator::prepare(), also sending an additional list of breadcrumb
    // triangles to the GPU. The breadcrumb triangles are implemented as conics with w=Infinity.
    //
    // ALSO NOTE: The breadcrumb triangles do not have a matrix. These need to be pre-transformed by
    // the caller if a CPU-side transformation is desired.
    void prepare(GrMeshDrawTarget*,
                 const PathDrawList&,
                 int totalCombinedPathVerbCnt,
                 const BreadcrumbTriangleList*);

#if SK_GPU_V1
    void draw(GrOpFlushState*) const override;

    // Draws a 4-point instance for each curve. This method is used for drawing convex hulls over
    // each cubic with GrFillCubicHullShader. The caller is responsible for binding its desired
    // pipeline ahead of time.
    void drawHullInstances(GrOpFlushState*, sk_sp<const GrGpuBuffer> vertexBufferIfNeeded) const;
#endif

private:
    PathCurveTessellator(GrPathTessellationShader* shader, DrawInnerFan drawInnerFan)
            : PathTessellator(shader)
            , fDrawInnerFan(drawInnerFan == DrawInnerFan::kYes) {}

    const bool fDrawInnerFan;
    GrVertexChunkArray fVertexChunkArray;

    // If using fixed count, this is the number of vertices we need to emit per instance.
    int fFixedIndexCount;
    sk_sp<const GrGpuBuffer> fFixedCountVertexBuffer;
    sk_sp<const GrGpuBuffer> fFixedCountIndexBuffer;
};

}  // namespace skgpu

#endif  // tessellate_PathCurveTessellator_DEFINED
