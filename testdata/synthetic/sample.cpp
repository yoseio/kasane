extern "C" char *strcpy(char *, const char *);

int copy_user(char *dst, const char *src) {
  const char *x = src;
  strcpy(dst, x);
  return 0;
}

int id(int x) {
  int y = x;
  return y;
}
