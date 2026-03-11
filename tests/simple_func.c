#include <math.h>

double simple_func(double x, double y) {
    if (x * log2(y) == 1024.0) {
        return x + y;
    }
    return -x;
}
