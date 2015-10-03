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

	wc_populate(wc,word_array);
	return wc;
}

//this is our hash function for the hash table
int hash(char *word, long size)
{
  return -1;
}

//adds a word to the hash table.
int table_add(struct wc *wc, char *str, int index)
{
  //if there's no entry, add one and return
  if(wc->word_table[index] == NULL)
    {
      wc->word_table[index] = (table_entry *)malloc(sizeof(table_entry *));
      wc->word_table[index]->next = NULL;
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
	  return 1;
	}
      prev = current_index;
      current_index = current_index -> next;
    }
  //if we get here it means we didn't find it, so we have to add it.
  prev->next = (table_entry *)malloc(sizeof(table_entry *));
  prev->next->next = NULL;
  prev->next->count = 1;
  prev->next->word = str;
  return 1;
}


//parses our input and records the results in the hash table.
//NOTE THAT TABLE_WORD SHOULD BE MORE INTELLIGENTLY PARSED.
void wc_populate(struct wc *wc, char *input)
{
  char *table_word = (char *)malloc(sizeof(char *)); ; //used to hold the word we will add to the hash table. note that 500 is an arbitrary value
  int h; //used to hold the value of table_word's hash
  int j; //used to index into table_word;
  for (int i = 0;input[i]!= '\0';i++) //NOTE: this assumes input is a null-terminated string
    {
      if(isspace(input[i]))
	{
	  h = hash(table_word, wc->table_size);
	  table_add(wc,table_word,h); //put word at the hash location in wc.
	  j = 0;
	  free(table_word);
	  table_word = (char *)malloc(sizeof(char *));
	}
      else
	{
	  table_word[j] = input[i];
	  j++;
	}
        
    }
}

//prints the list starting with te
void print_table_entry(table_entry *te)
{
  if(te->next != NULL)
    print_table_entry(te->next);
  printf("%s:%d",te->word,te->count);
}

void
wc_output(struct wc *wc)
{
  for (long i = 0;i<wc->table_size;i++)
    {
      if(wc->word_table[i] != NULL)
	print_table_entry(wc->word_table[i]);
    }
}

//delete all records associated with the head of a table_entry list.
void table_entry_delete(table_entry *te)
{
  if(te->next != NULL)
    table_entry_delete(te->next);
  free(te);
}

void
wc_destroy(struct wc *wc)
{
  for(int i = 0;i<wc->table_size;i++)
    {
      if (wc->word_table[i] != NULL)
	table_entry_delete(wc->word_table[i]);
    }
  free(wc);
}
