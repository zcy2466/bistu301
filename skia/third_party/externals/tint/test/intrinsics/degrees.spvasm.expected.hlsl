void main_1() {
  float a = 0.0f;
  float b = 0.0f;
  a = 42.0f;
  b = (a * 57.295780182f);
  return;
}

[numthreads(1, 1, 1)]
void main() {
  main_1();
  return;
}
