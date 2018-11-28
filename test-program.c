#include <stdio.h>
#include <stdlib.h>
#include <time.h>
 
void my_inner_func(void)
{
    printf ("my_inner_func.\n");
}

void my_func(void)
{
    printf ("my_func before my_inner_func.\n");
    my_inner_func();
    printf ("my_func after my_inner_func.\n");
}
 

int main(int argc, char ** argv)
{
    printf("in main.\n");
    my_func();
}

