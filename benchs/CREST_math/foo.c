#include <stdio.h>
#include <math.h>

int foo_raw(double a_d, double b_d, double c_d, double d_d, double e_d){
  int a = (int)floor(a_d);
  int b = (int)floor(b_d);
  int c = (int)floor(c_d);
  int d = (int)floor(d_d);
  int e = (int)floor(e_d);
  if(!(a>=0 && b>=0 && c>=0 && d>=0 && e>=0)){
    return 0;
  }
  if (3*a + 3*(b - 5*c) + (b+c) - a == d - 17*e + 170) {
    //printf("Condition met!\n");
    return 1;
  } else {
    // printf("Condition not met\n");
    return 0;
  }
}

void foo(double * X){
  foo_raw(X[0], X[1], X[2], X[3], X[4]);
}


/* From Crest Bench
#include <crest.h>

int main(void) {
  int a, b, c, d, e;
  CREST_int(a);
  CREST_int(b);
  CREST_int(c);
  CREST_int(d);
  CREST_int(e);
  if (3*a + 3*(b - 5*c) + (b+c) - a == d - 17*e) {
    return 1;
  } else {
    return 0;
  }
}

*/
