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

#include "src/transform/binding_remapper.h"

#include <utility>

#include "src/transform/test_helper.h"

namespace tint {
namespace transform {
namespace {

using BindingRemapperTest = TransformTest;

TEST_F(BindingRemapperTest, NoRemappings) {
  auto* src = R"(
[[block]]
struct S {
  a : f32;
};

[[group(2), binding(1)]] var<storage, read> a : S;

[[group(3), binding(2)]] var<storage, read> b : S;

[[stage(compute), workgroup_size(1)]]
fn f() {
}
)";

  auto* expect = src;

  DataMap data;
  data.Add<BindingRemapper::Remappings>(BindingRemapper::BindingPoints{},
                                        BindingRemapper::AccessControls{});
  auto got = Run<BindingRemapper>(src, data);

  EXPECT_EQ(expect, str(got));
}

TEST_F(BindingRemapperTest, RemapBindingPoints) {
  auto* src = R"(
[[block]]
struct S {
  a : f32;
};

[[group(2), binding(1)]] var<storage, read> a : S;

[[group(3), binding(2)]] var<storage, read> b : S;

[[stage(compute), workgroup_size(1)]]
fn f() {
}
)";

  auto* expect = R"(
[[block]]
struct S {
  a : f32;
};

[[group(1), binding(2)]] var<storage, read> a : S;

[[group(3), binding(2)]] var<storage, read> b : S;

[[stage(compute), workgroup_size(1)]]
fn f() {
}
)";

  DataMap data;
  data.Add<BindingRemapper::Remappings>(
      BindingRemapper::BindingPoints{
          {{2, 1}, {1, 2}},  // Remap
          {{4, 5}, {6, 7}},  // Not found
                             // Keep [[group(3), binding(2)]] as is
      },
      BindingRemapper::AccessControls{});
  auto got = Run<BindingRemapper>(src, data);

  EXPECT_EQ(expect, str(got));
}

TEST_F(BindingRemapperTest, RemapAccessControls) {
  auto* src = R"(
[[block]]
struct S {
  a : f32;
};

[[group(2), binding(1)]] var<storage, read> a : S;

[[group(3), binding(2)]] var<storage, write> b : S;

[[group(4), binding(3)]] var<storage, read> c : S;

[[stage(compute), workgroup_size(1)]]
fn f() {
}
)";

  auto* expect = R"(
[[block]]
struct S {
  a : f32;
};

[[group(2), binding(1)]] var<storage, write> a : S;

[[group(3), binding(2)]] var<storage, write> b : S;

[[group(4), binding(3)]] var<storage, read> c : S;

[[stage(compute), workgroup_size(1)]]
fn f() {
}
)";

  DataMap data;
  data.Add<BindingRemapper::Remappings>(
      BindingRemapper::BindingPoints{},
      BindingRemapper::AccessControls{
          {{2, 1}, ast::Access::kWrite},  // Modify access control
          // Keep [[group(3), binding(2)]] as is
          {{4, 3}, ast::Access::kRead},  // Add access control
      });
  auto got = Run<BindingRemapper>(src, data);

  EXPECT_EQ(expect, str(got));
}

// TODO(crbug.com/676): Possibly enable if the spec allows for access
// decorations in type aliases. If not, just remove.
TEST_F(BindingRemapperTest, DISABLED_RemapAccessControlsWithAliases) {
  auto* src = R"(
[[block]]
struct S {
  a : f32;
};

type, read ReadOnlyS = S;

type, write WriteOnlyS = S;

type A = S;

[[group(2), binding(1)]] var<storage> a : ReadOnlyS;

[[group(3), binding(2)]] var<storage> b : WriteOnlyS;

[[group(4), binding(3)]] var<storage> c : A;

[[stage(compute), workgroup_size(1)]]
fn f() {
}
)";

  auto* expect = R"(
[[block]]
struct S {
  a : f32;
};

type, read ReadOnlyS = S;

type, write WriteOnlyS = S;

type A = S;

[[group(2), binding(1)]] var<storage, write> a : S;

[[group(3), binding(2)]] var<storage> b : WriteOnlyS;

[[group(4), binding(3)]] var<storage, write> c : S;

[[stage(compute), workgroup_size(1)]]
fn f() {
}
)";

  DataMap data;
  data.Add<BindingRemapper::Remappings>(
      BindingRemapper::BindingPoints{},
      BindingRemapper::AccessControls{
          {{2, 1}, ast::Access::kWrite},  // Modify access control
          // Keep [[group(3), binding(2)]] as is
          {{4, 3}, ast::Access::kRead},  // Add access control
      });
  auto got = Run<BindingRemapper>(src, data);

  EXPECT_EQ(expect, str(got));
}

TEST_F(BindingRemapperTest, RemapAll) {
  auto* src = R"(
[[block]]
struct S {
  a : f32;
};

[[group(2), binding(1)]] var<storage, read> a : S;

[[group(3), binding(2)]] var<storage, read> b : S;

[[stage(compute), workgroup_size(1)]]
fn f() {
}
)";

  auto* expect = R"(
[[block]]
struct S {
  a : f32;
};

[[group(4), binding(5)]] var<storage, write> a : S;

[[group(6), binding(7)]] var<storage, write> b : S;

[[stage(compute), workgroup_size(1)]]
fn f() {
}
)";

  DataMap data;
  data.Add<BindingRemapper::Remappings>(
      BindingRemapper::BindingPoints{
          {{2, 1}, {4, 5}},
          {{3, 2}, {6, 7}},
      },
      BindingRemapper::AccessControls{
          {{2, 1}, ast::Access::kWrite},
          {{3, 2}, ast::Access::kWrite},
      });
  auto got = Run<BindingRemapper>(src, data);

  EXPECT_EQ(expect, str(got));
}

TEST_F(BindingRemapperTest, BindingCollisionsSameEntryPoint) {
  auto* src = R"(
[[block]]
struct S {
  i : i32;
};

[[group(2), binding(1)]] var<storage, read> a : S;

[[group(3), binding(2)]] var<storage, read> b : S;

[[group(4), binding(3)]] var<storage, read> c : S;

[[group(5), binding(4)]] var<storage, read> d : S;

[[stage(compute), workgroup_size(1)]]
fn f() {
  let x : i32 = (((a.i + b.i) + c.i) + d.i);
}
)";

  auto* expect = R"(
[[block]]
struct S {
  i : i32;
};

[[internal(disable_validation__binding_point_collision), group(1), binding(1)]] var<storage, read> a : S;

[[internal(disable_validation__binding_point_collision), group(1), binding(1)]] var<storage, read> b : S;

[[internal(disable_validation__binding_point_collision), group(5), binding(4)]] var<storage, read> c : S;

[[internal(disable_validation__binding_point_collision), group(5), binding(4)]] var<storage, read> d : S;

[[stage(compute), workgroup_size(1)]]
fn f() {
  let x : i32 = (((a.i + b.i) + c.i) + d.i);
}
)";

  DataMap data;
  data.Add<BindingRemapper::Remappings>(
      BindingRemapper::BindingPoints{
          {{2, 1}, {1, 1}},
          {{3, 2}, {1, 1}},
          {{4, 3}, {5, 4}},
      },
      BindingRemapper::AccessControls{}, true);
  auto got = Run<BindingRemapper>(src, data);

  EXPECT_EQ(expect, str(got));
}

TEST_F(BindingRemapperTest, BindingCollisionsDifferentEntryPoints) {
  auto* src = R"(
[[block]]
struct S {
  i : i32;
};

[[group(2), binding(1)]] var<storage, read> a : S;

[[group(3), binding(2)]] var<storage, read> b : S;

[[group(4), binding(3)]] var<storage, read> c : S;

[[group(5), binding(4)]] var<storage, read> d : S;

[[stage(compute), workgroup_size(1)]]
fn f1() {
  let x : i32 = (a.i + c.i);
}

[[stage(compute), workgroup_size(1)]]
fn f2() {
  let x : i32 = (b.i + d.i);
}
)";

  auto* expect = R"(
[[block]]
struct S {
  i : i32;
};

[[group(1), binding(1)]] var<storage, read> a : S;

[[group(1), binding(1)]] var<storage, read> b : S;

[[group(5), binding(4)]] var<storage, read> c : S;

[[group(5), binding(4)]] var<storage, read> d : S;

[[stage(compute), workgroup_size(1)]]
fn f1() {
  let x : i32 = (a.i + c.i);
}

[[stage(compute), workgroup_size(1)]]
fn f2() {
  let x : i32 = (b.i + d.i);
}
)";

  DataMap data;
  data.Add<BindingRemapper::Remappings>(
      BindingRemapper::BindingPoints{
          {{2, 1}, {1, 1}},
          {{3, 2}, {1, 1}},
          {{4, 3}, {5, 4}},
      },
      BindingRemapper::AccessControls{}, true);
  auto got = Run<BindingRemapper>(src, data);

  EXPECT_EQ(expect, str(got));
}

TEST_F(BindingRemapperTest, NoData) {
  auto* src = R"(
[[block]]
struct S {
  a : f32;
};

[[group(2), binding(1)]] var<storage, read> a : S;
[[group(3), binding(2)]] var<storage, read> b : S;

[[stage(compute), workgroup_size(1)]]
fn f() {}
)";

  auto* expect =
      "error: missing transform data for tint::transform::BindingRemapper";

  auto got = Run<BindingRemapper>(src);

  EXPECT_EQ(expect, str(got));
}

}  // namespace
}  // namespace transform
}  // namespace tint
