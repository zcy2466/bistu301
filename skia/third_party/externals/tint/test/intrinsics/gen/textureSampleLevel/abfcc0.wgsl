// Copyright 2021 The Tint Authors.
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

////////////////////////////////////////////////////////////////////////////////
// File generated by tools/intrinsic-gen
// using the template:
//   test/intrinsics/intrinsics.wgsl.tmpl
// and the intrinsic defintion file:
//   src/intrinsics.def
//
// Do not modify this file directly
////////////////////////////////////////////////////////////////////////////////

[[group(1), binding(0)]] var arg_0: texture_3d<f32>;
[[group(1), binding(1)]] var arg_1: sampler;

// fn textureSampleLevel(texture: texture_3d<f32>, sampler: sampler, coords: vec3<f32>, level: f32) -> vec4<f32>
fn textureSampleLevel_abfcc0() {
  var res: vec4<f32> = textureSampleLevel(arg_0, arg_1, vec3<f32>(), 1.0);
}

[[stage(vertex)]]
fn vertex_main() -> [[builtin(position)]] vec4<f32> {
  textureSampleLevel_abfcc0();
  return vec4<f32>();
}

[[stage(fragment)]]
fn fragment_main() {
  textureSampleLevel_abfcc0();
}

[[stage(compute), workgroup_size(1)]]
fn compute_main() {
  textureSampleLevel_abfcc0();
}
