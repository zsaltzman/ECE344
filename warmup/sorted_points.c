#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "point.h"
#include "sorted_points.h"

typedef struct node {
  struct point *val;
  struct node *next;
}point_entry;


struct sorted_points {
  int size;
  point_entry *head;
};

struct sorted_points *
sp_init()
{
	struct sorted_points *sp;

	sp = (struct sorted_points *)malloc(sizeof(struct sorted_points));
       	assert(sp);
        sp->head = NULL;
	sp->size = 0;
	return sp;
}

//DO NOT USE THIS FUNCTION IF YOU WANT TO PRESERVE THE LIST, IT IS UNSAFE.
void delete_point_destructive(point_entry *pe)
{
  if (pe != NULL)
    {
      pe->next = NULL;
      free(pe->val);
      free(pe);
    }
}

void print_list(struct sorted_points *sp)
{
  printf("contents of %p:\n",sp);
  point_entry *pe = sp->head;
  while (pe != NULL)
    {
      printf("%f,%f\n",point_X(pe->val),point_Y(pe->val));
      pe = pe->next;
    }
  printf("end of list\n");
}
void delete_point(struct sorted_points *sp, point_entry *pe, point_entry *pe_prev)
{
  if (pe != NULL)
    {
      if (pe_prev != NULL)
	pe_prev->next = pe->next;
      else
	sp->head = pe->next; //if there is nothing before pe, it must be head.
      
      pe->next = NULL;
      free(pe->val);
      free(pe);
    }
  
}
void delete_all_points(point_entry *pe)
{
  if (pe != NULL)
   {
     if (pe->next != NULL)
       delete_all_points(pe->next);
     delete_point_destructive(pe);
   }
  
}

void
sp_destroy(struct sorted_points *sp)
{
  delete_all_points(sp->head);
  sp->head = NULL;
  free(sp);
}

int
sp_add_point(struct sorted_points *sp, double x, double y)
{
  //	TBD();
  //create the point entry, starting with the point
  struct point *p = (struct point *)malloc(sizeof(struct point));
  if (p == NULL)
    return 0;
  point_set(p,x,y);
  point_entry *pe = (point_entry *)malloc(sizeof(point_entry));
  assert(pe);
  if (pe == NULL)
   {
     free(p);
     return 0;
   }
  
  pe->val = p;
  pe->next = NULL;
  //figure out where entry should go in list
  //if it's an empty list, it goes at the beginning
  if (sp->head == NULL)
    {
      pe->next = NULL;
      sp->head = pe;
      return 1;
    }
  point_entry *pe_prev = NULL;
  point_entry *pe_search = sp->head;
  struct point *origin = (struct point *)malloc(sizeof(struct point));
  point_set(origin,0,0);
  //otherwise we can assume it's safe to dereference head.
  
  while(pe_search != NULL)
    {
      //if the distance between pe_search pe is equal
      if (point_distance(pe_search->val,origin) == point_distance(pe->val,origin))
	{
	  if (point_X(pe_search->val) == point_X(pe->val) && point_Y(pe_search->val) >= point_Y(pe->val))
	    {
	      pe->next = pe_search;
	      if (pe_prev != NULL)
		pe_prev->next = pe;
	      else
		sp->head = pe;
	      free(origin);
	      return 1;
	    }
	  while (pe_search != NULL && point_distance(pe_search->val,origin) == point_distance(pe->val,origin) && point_X(pe_search->val) < x)
	    {
	      pe_prev = pe_search;
	      pe_search = pe_search->next;
	    }
	  pe->next = pe_search;
	  if (pe_prev != NULL)
	    pe_prev->next = pe;
	  else
	    sp->head = pe;
	  free(origin);
	  return 1;
	}
      if (point_distance(pe_search->val,origin) > point_distance(pe->val,origin))
	{
	  pe->next = pe_search;
	  if (pe_prev != NULL)
	    pe_prev->next = pe;
	  else
	    sp->head = pe;
	  free(origin);
	  return 1;
	}
      pe_prev = pe_search;
      pe_search = pe_search->next;
    }
  //we got to the end of the list and didn't find a greater or equal x value, i.e., element should go at the end.
  pe_prev->next = pe;
  free(origin);
  return 1;
}

int
sp_remove_first(struct sorted_points *sp, struct point *ret)
{
  //	TBD();
  if (sp->head == NULL)
    return 0;
  // printf("%f,%f\n",point_X(sp->head->val),point_Y(sp->head->val));
  point_set(ret,point_X(sp->head->val),point_Y(sp->head->val));
  delete_point(sp,sp->head,NULL); //nothing before head, so pass NULL
  return 1;
}

int
sp_remove_last(struct sorted_points *sp, struct point *ret)
{
  point_entry *p = sp->head;
  point_entry *prev = NULL;
  if (p == NULL)
    return 0;
  while (p->next != NULL)
    {
      prev = p;
      p = p->next;
    }
  point_set(ret,point_X(p->val),point_Y(p->val));
  delete_point(sp,p,prev);
  return 1;
}

int
sp_remove_by_index(struct sorted_points *sp, int index, struct point *ret)
{
  //	TBD();
  int i = 0;
  point_entry *p = sp->head;
  point_entry *prev = NULL;
  if (p == NULL)
    return 0;
  while (i!=index && p != NULL)
    {
      i++;
      prev = p;
      p = p->next;
    }
  if (p==NULL)
    return 0;
  else if (i==index)
    {
      point_set(ret,point_X(p->val),point_Y(p->val));
      delete_point(sp,p,prev);
    }
  return 1;
}






int sp_delete_duplicates(struct sorted_points *sp)
{
  //TBD();
  point_entry *p = sp->head;
  point_entry *p_next = NULL;
  int count =  0;
  if (p != NULL)
    p_next = p->next;
  while (p_next != NULL)
    {
      if (point_X(p->val) == point_X(p_next->val) &&  point_Y(p->val) == point_Y(p_next->val))
	{
	  delete_point(sp, p_next, p);//NOTE: delete_point sets the next of p automatically.
	  count++;
	  if (p->next == NULL) //if we moved p to the end of the list by deleting, end the function.
	    return count;
	  p_next = p->next; //we don't increment p here because there may be multiple duplicates of the same entry.  
	} 
      else
	{
	  p = p_next;
	  p_next = p_next->next;  
	}
    }
  return count;
}
