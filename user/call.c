#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void swap(int *a, int *b)
{
  int *tmp = a;
  a = b;
  b = tmp;
}

void main()
{
  int a = 1, b = 2;
  swap(&a, &b);
}
