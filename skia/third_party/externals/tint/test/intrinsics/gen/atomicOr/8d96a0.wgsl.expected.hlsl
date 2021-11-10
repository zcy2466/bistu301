int atomicOr_1(RWByteAddressBuffer buffer, uint offset, int value) {
  int original_value = 0;
  buffer.InterlockedOr(offset, value, original_value);
  return original_value;
}

RWByteAddressBuffer sb_rw : register(u0, space0);

void atomicOr_8d96a0() {
  int res = atomicOr_1(sb_rw, 0u, 1);
}

void fragment_main() {
  atomicOr_8d96a0();
  return;
}

[numthreads(1, 1, 1)]
void compute_main() {
  atomicOr_8d96a0();
  return;
}
