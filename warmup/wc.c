#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "common.h"
#include "wc.h"

typedef struct node{
  int count;
  char *word;
  struct node *next;
}table_entry;

struct wc {
  table_entry **word_table;
  long table_size;
};

void wc_populate(struct wc * wc,char *input);

struct wc *
wc_init(char *word_array, long size)
{
	struct wc *wc;

	wc = (struct wc *)malloc(sizeof(struct wc));
	assert(wc);
	wc->word_table = (table_entry **)malloc(size * sizeof(table_entry *));
	assert(wc->word_table);
	wc->table_size = size;
	long i = 0;
	while (i<size)
	  {
	  wc->word_table[i] = (table_entry *)malloc(sizeof(table_entry));
	  wc->word_table[i]->next = NULL;
	  wc->word_table[i]->word = NULL;
	  i++;
	  }
	wc_populate(wc,word_array);
	return wc;
}

//this is our hash function for the hash table.
//all credit to www.cse.yorku.ca/~oz/hash.html
int hash(char *str, long table_size)
{
  int hash = 5381;
  int c = 0;

  while ((c = *str++)!=0)
     {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
     }
   if(hash<0)
     hash *= -1;
   hash = hash%table_size;
   return hash;
}

//adds a word to the hash table at a location determined by the hash function.
int table_add(struct wc *wc, char *str, int index)
{
  //if there's no valid entry, set one and return
  if(wc->word_table[index]->word == NULL)
    {
      wc->word_table[index]->count = 1;
      wc->word_table[index]->word = str;
      return 1;
    }
  //otherwise we're going to have to check to see if our word is here.
  table_entry *current_index = wc->word_table[index]; //head of that list of table_entries
  table_entry *prev = NULL; //if the entry for the word doesn't exist we're going to have to add it to the end
  while(current_index != NULL)
    {
      //if the word already exists
      if(strcmp(str,current_index->word)==0)
	{
	  current_index->count = current_index->count + 1;
	  free(str); //this string doesn't need to exist if it has already been recorded.
	  return 1;
	}
      prev = current_index;
      current_index = current_index -> next;
    }
  //if we get here it means we didn't find it, so we have to add it.
  prev->next = (table_entry *)malloc(sizeof(table_entry));
  prev->next->next = NULL;
  prev->next->count = 1;
  prev->next->word = str;
  return 1;
}


//parses our input and records the results in the hash table.
void wc_populate(struct wc *wc, char *input)
{
  char *table_word = NULL;
  int h; //used to hold the value of table_word's hash
  int i = 0; //used to index for the while loop.
  int j = 0; //used in the while loop that reads a word into table_word.
  int loc_last_word = 0; //used to keep track of where the last word ended.
  int word_size = 0; //used to keep track of how long the current word is.
  while (input[i]!= '\0') //NOTE: this assumes input is a null-terminated string
    {
      if(isspace(input[i]))
	{
	  if(word_size != 0)
	  {
	    table_word = (char *)malloc((word_size+1)*sizeof(char)); //allocate an array of characters the size of the word, plus the null character at the end.
	    assert(table_word);
	    //read the word into table_word
	    while (j<word_size)
	      {
		table_word[j] = input[loc_last_word+j];
		j++;
	      }
	    table_word[j] = '\0'; //add the null
	    h = hash(table_word, wc->table_size);
	    table_add(wc,table_word,h);
	  }
	    loc_last_word = i+1; //set the location of the last word to the letter after the space.
	    j = 0;
	    word_size = 0;
	}
      else
	{
	  word_size++;
	}
      i++; 
    }
}

//prints the list starting with te
void print_table_entry(table_entry *te)
{
  if(te->next != NULL)
    print_table_entry(te->next);
  
   printf("%s:%d\n",te->word,te->count);
}

void
wc_output(struct wc *wc)
{
  long i = 0;
  while(i<wc->table_size)
    {
      if(wc->word_table[i]->word != NULL)
	print_table_entry(wc->word_table[i]);
      i++;
    }
}

//delete all records associated with the head of a table_entry list.
void table_entry_delete(table_entry *te)
{
  if(te->next != NULL)
    table_entry_delete(te->next);
  free(te->word);
  free(te);
}

void
wc_destroy(struct wc *wc)
{
  int i = 0;
  while(i < wc->table_size)
    {
      table_entry_delete(wc->word_table[i]);
      i++;
    }
  free(wc->word_table);
  free(wc);
}
