// GENERATED FILE - DO NOT EDIT.
// Generated by gen_restricted_traces.py using data from restricted_traces.json
//
// Copyright 2020 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// restricted_traces_autogen: Types and enumerations for trace tests.

#ifndef ANGLE_RESTRICTED_TRACES_AUTOGEN_H_
#define ANGLE_RESTRICTED_TRACES_AUTOGEN_H_

#include <EGL/egl.h>
#include <KHR/khrplatform.h>
#include <cstdint>
#include <vector>

#include "restricted_traces_export.h"

namespace trace_angle
{
using GenericProc = void (*)();
using LoadProc    = GenericProc(KHRONOS_APIENTRY *)(const char *);
ANGLE_TRACE_LOADER_EXPORT void LoadEGL(LoadProc loadProc);
ANGLE_TRACE_LOADER_EXPORT void LoadGLES(LoadProc loadProc);

static constexpr size_t kTraceInfoMaxNameLen = 128;

static constexpr uint32_t kDefaultReplayContextClientMajorVersion = 3;
static constexpr uint32_t kDefaultReplayContextClientMinorVersion = 1;
static constexpr uint32_t kDefaultReplayDrawSurfaceColorSpace     = EGL_COLORSPACE_LINEAR;

struct TraceInfo
{
    char name[kTraceInfoMaxNameLen];
    uint32_t contextClientMajorVersion;
    uint32_t contextClientMinorVersion;
    uint32_t frameEnd;
    uint32_t frameStart;
    uint32_t drawSurfaceWidth;
    uint32_t drawSurfaceHeight;
    uint32_t drawSurfaceColorSpace;
    uint32_t displayPlatformType;
    uint32_t displayDeviceType;
    int configRedBits;
    int configBlueBits;
    int configGreenBits;
    int configAlphaBits;
    int configDepthBits;
    int configStencilBits;
    bool isBinaryDataCompressed;
    bool areClientArraysEnabled;
    bool isBindGeneratesResourcesEnabled;
    bool isWebGLCompatibilityEnabled;
    bool isRobustResourceInitEnabled;
};

ANGLE_TRACE_EXPORT const TraceInfo &GetTraceInfo(const char *traceName);
}  // namespace trace_angle

#endif  // ANGLE_RESTRICTED_TRACES_AUTOGEN_H_
