SKIP: FAILED

static uint var_1 = 0u;

void main_1() {
  while (true) {
    var_1 = 1u;
  }
  return;
}

void main() {
  main_1();
  return;
}
warning: DXIL.dll not found.  Resulting DXIL will not be signed for use in release environments.

error: validation errors
tint_M8paEY:10: error: Loop must have break.
Validation failed.


