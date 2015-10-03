#include "common.h"

int
main(int argc, char *argv[])
{
  int i = 1;
  while(i<argc)
  {
    printf("%s\n",argv[i]);
    i++;
  }
	return 0;
}
