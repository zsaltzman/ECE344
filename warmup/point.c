#include <assert.h>
#include <math.h>
#include "common.h"
#include "point.h"

void
point_translate(struct point *p, double x, double y)
{
  //TBD();
  point_set(p,point_X(p)+x,point_Y(p)+y);
}

double
point_distance(const struct point *p1, const struct point *p2)
{
  //TBD();
  double dist = 0;
  dist = sqrt(pow(point_X(p2)-point_X(p1),2)+pow(point_Y(p2)-point_Y(p1),2));
  return dist;
}

int
point_compare(const struct point *p1, const struct point *p2)
{
  //TBD();
  struct point *origin = malloc(sizeof(struct point));
  point_set(origin,0,0);
  double dist1 = point_distance(p1,origin);
  double dist2 = point_distance(p2,origin);

  if(dist1<dist2)
    return -1;
  else  if (dist1 == dist2)
    return 0;
  else
    return 1;
}
