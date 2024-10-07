#include <iostream>
#include "Swap.h"

int main () {
    int a = 10;
    int b = 20;
    std::cout << a << std::endl;
    std::cout << b << std::endl;
    Swap(a, b);
    std::cout << a << std::endl;
    std::cout << b << std::endl;  
    return 0;

}