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

#include "src/transform/remove_phonies.h"

#include <memory>
#include <utility>
#include <vector>

#include "src/transform/test_helper.h"

namespace tint {
namespace transform {
namespace {

using RemovePhoniesTest = TransformTest;

TEST_F(RemovePhoniesTest, EmptyModule) {
  auto* src = "";
  auto* expect = "";

  auto got = Run<RemovePhonies>(src);

  EXPECT_EQ(expect, str(got));
}

TEST_F(RemovePhoniesTest, NoSideEffects) {
  auto* src = R"(
[[group(0), binding(0)]] var t : texture_2d<f32>;

fn f() {
  var v : i32;
  _ = &v;
  _ = 1;
  _ = 1 + 2;
  _ = t;
}
)";

  auto* expect = R"(
[[group(0), binding(0)]] var t : texture_2d<f32>;

fn f() {
  var v : i32;
}
)";

  auto got = Run<RemovePhonies>(src);

  EXPECT_EQ(expect, str(got));
}

TEST_F(RemovePhoniesTest, SingleSideEffects) {
  auto* src = R"(
fn neg(a : i32) -> i32 {
  return -(a);
}

fn add(a : i32, b : i32) -> i32 {
  return (a + b);
}

fn f() {
  _ = neg(1);
  _ = add(2, 3);
  _ = add(neg(4), neg(5));
}
)";

  auto* expect = R"(
fn neg(a : i32) -> i32 {
  return -(a);
}

fn add(a : i32, b : i32) -> i32 {
  return (a + b);
}

fn f() {
  neg(1);
  add(2, 3);
  add(neg(4), neg(5));
}
)";

  auto got = Run<RemovePhonies>(src);

  EXPECT_EQ(expect, str(got));
}

TEST_F(RemovePhoniesTest, MultipleSideEffects) {
  auto* src = R"(
fn neg(a : i32) -> i32 {
  return -(a);
}

fn add(a : i32, b : i32) -> i32 {
  return (a + b);
}

fn xor(a : u32, b : u32) -> u32 {
  return (a ^ b);
}

fn f() {
  _ = (1 + add(2 + add(3, 4), 5)) * add(6, 7) * neg(8);
  _ = add(9, neg(10)) + neg(11);
  _ = xor(12u, 13u) + xor(14u, 15u);
  _ = neg(16) / neg(17) + add(18, 19);
}
)";

  auto* expect = R"(
fn neg(a : i32) -> i32 {
  return -(a);
}

fn add(a : i32, b : i32) -> i32 {
  return (a + b);
}

fn xor(a : u32, b : u32) -> u32 {
  return (a ^ b);
}

fn phony_sink(p0 : i32, p1 : i32, p2 : i32) {
}

fn phony_sink_1(p0 : i32, p1 : i32) {
}

fn phony_sink_2(p0 : u32, p1 : u32) {
}

fn f() {
  phony_sink(add((2 + add(3, 4)), 5), add(6, 7), neg(8));
  phony_sink_1(add(9, neg(10)), neg(11));
  phony_sink_2(xor(12u, 13u), xor(14u, 15u));
  phony_sink(neg(16), neg(17), add(18, 19));
}
)";

  auto got = Run<RemovePhonies>(src);

  EXPECT_EQ(expect, str(got));
}

TEST_F(RemovePhoniesTest, ForLoop) {
  auto* src = R"(
[[block]]
struct S {
  arr : array<i32>;
};

[[group(0), binding(0)]] var<storage, read_write> s : S;

fn x() -> i32 {
  return 0;
}

fn y() -> i32 {
  return 0;
}

fn z() -> i32 {
  return 0;
}

fn f() {
  for (_ = &s.arr; ;_ = &s.arr) {
  }
  for (_ = x(); ;_ = y() + z()) {
  }
}
)";

  auto* expect = R"(
[[block]]
struct S {
  arr : array<i32>;
};

[[group(0), binding(0)]] var<storage, read_write> s : S;

fn x() -> i32 {
  return 0;
}

fn y() -> i32 {
  return 0;
}

fn z() -> i32 {
  return 0;
}

fn phony_sink(p0 : i32, p1 : i32) {
}

fn f() {
  for(; ; ) {
  }
  for(x(); ; phony_sink(y(), z())) {
  }
}
)";

  auto got = Run<RemovePhonies>(src);

  EXPECT_EQ(expect, str(got));
}

}  // namespace
}  // namespace transform
}  // namespace tint
