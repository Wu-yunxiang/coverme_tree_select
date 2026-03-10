#include <math.h>
void foo_raw(double db_x, double y){
  int x=(int) floor(db_x);
  if (x<=3)
    return;

  if (y<=5.1)
    return;

  return;

}


/*void foo(double * X){
  int x=(int)std::floor(X[0]);
  double y= X[1];
  foo_raw(x,y);
}*/
