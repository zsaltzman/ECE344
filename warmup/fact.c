#include "common.h"
#include <string.h>
#include <ctype.h>
int fact(int order);
int main(int argc, char *argv[])
{
  //TBD();
  int argLen = strlen(argv[1]);
  char *arg = argv[1];
  int parse = 1;
  int i = 0;
  // printf("%s\n",arg);
  //printf("%d\n",argLen);
  if(argc<2)
    parse = 0;
  while(i<argLen && parse)
  {
    if (!isdigit(arg[i]))
      {
	printf("Huh?\n");
	parse = 0;
      }
    i++;
  }
  if (parse)
    {
      int order = atoi(arg);
      if(order>12)
	printf("Overflow\n");
      else
	printf("%d\n", fact(order));
    }
     return 0;
}

int fact(int  order)
{
  if (order <= 0)
    printf("Huh?\n");
  if (order == 1)
    return 1;
  else
    return fact(order-1)*order;
 
}
