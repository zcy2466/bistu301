var<private> I : i32;

[[stage(compute), workgroup_size(1)]]
fn main() {
  I = 123; // constant
  I = 100 + 20 + 3; // dynamic
}
