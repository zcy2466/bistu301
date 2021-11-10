// GENERATED FILE - DO NOT EDIT.
// Generated by generate_entry_points.py using data from gl.xml.
//
// Copyright 2020 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// validationGL15_autogen.h:
//   Validation functions for the OpenGL 1.5 entry points.

#ifndef LIBANGLE_VALIDATION_GL15_AUTOGEN_H_
#define LIBANGLE_VALIDATION_GL15_AUTOGEN_H_

#include "common/PackedEnums.h"

namespace gl
{
class Context;

bool ValidateGetBufferSubData(const Context *context,
                              GLenum target,
                              GLintptr offset,
                              GLsizeiptr size,
                              const void *data);
bool ValidateGetQueryObjectiv(const Context *context,
                              QueryID idPacked,
                              GLenum pname,
                              const GLint *params);
bool ValidateMapBuffer(const Context *context, BufferBinding targetPacked, GLenum access);
}  // namespace gl

#endif  // LIBANGLE_VALIDATION_GL15_AUTOGEN_H_