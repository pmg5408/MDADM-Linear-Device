#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "cache.h"
#include "jbod.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int num_queries = 0;
static int num_hits = 0;
int insert_flag = 0;
int remaining;
cache_entry_t *c;

int cache_create(int num_entries) {

    if(num_entries < 2 || num_entries > 4096){    // cache has to be greater than 2 and less than 4096
      return -1;
    }

    else if (cache != NULL){
      return -1;
    }

    //allocating memory in heap and making changes to all the parameters
    else{
      cache = calloc(num_entries, sizeof(cache_entry_t)); 
      c = cache;
      cache_size = num_entries;
      remaining = num_entries;
      return 1;
    }
    return -1;
}

int cache_destroy(void) {
    if(cache == NULL){
      return -1;
    }
    // freeing memory and making necessary changes to the parameters
    else{
      free(cache);
      cache = NULL;
      cache_size = 0; 
      insert_flag = 0;
      return 1;
    }
    return -1;
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
  if (insert_flag == 0){
      return -1;
  }

  else if(cache == NULL || buf == NULL){
    return -1;
  }

  else if(block_num < 0 || block_num >= 256){
    return -1;
  }

  else if(disk_num < 0 || disk_num >= 16){
    return -1;
  }

  else{

    num_queries += 1; 
    for(int j = 0; j <cache_size; j++){   // iterating through cache to see if it already exists
      if(cache[j].block_num == block_num && cache[j].disk_num == disk_num){

        memcpy(buf, cache[j].block, JBOD_BLOCK_SIZE);   // copying contents and making necessary changes to parameters
        cache[j].num_accesses += 1;
        num_hits += 1;
        return 1;

      }
    }
  } 
  return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {

  for(cache_entry_t *i = cache; i < cache + cache_size; i++){   //iterating through cache to find the memory block

    if(i->block_num == block_num && i->disk_num == disk_num){

      memcpy(i->block, buf, JBOD_BLOCK_SIZE);   // updating the content in cach everytime we write something on the disk
      i->num_accesses += 1;
      return;

    }
  }  
  return; 
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
//  printf("here\n");
  insert_flag = 1;
  if(buf == NULL){
    return -1;
  }

  else if(block_num < 0 || block_num >= 256){
    return -1;
  }

  else if(disk_num < 0 || disk_num >= 16){
    return -1;
  }

  else if(cache == NULL){
    return -1;
  }

  else{
      int x = 0; 
      int min = 0;
      int a = 0;

      for(int i = 0; i < cache_size; i ++){
      
        if(cache[i].valid == true && block_num == cache[i].block_num && disk_num == cache[i].disk_num){
          return -1;
        }
      }

      for(int i = 0; i < cache_size; i++){

        // inserting block in cache if cache has empty space
        if(cache[i].valid != true){

          cache[i].disk_num = disk_num;
          cache[i].block_num = block_num;
          memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
          cache[i].num_accesses = 1;
          a++;
          cache[i].valid = true;
          return 1;
        
        }

        // finding the block in cache with minimum accesses for LFU
        else{

          if(cache[i].num_accesses < min){

            min = cache[i].num_accesses;
            x = i;

          }
        }
      }

      // replacing block selected by LFU with new block
      cache[x].disk_num = disk_num;
      cache[x].block_num = block_num;
      memcpy(cache[x].block, buf, JBOD_BLOCK_SIZE);
      cache[x].num_accesses = 1; 
      cache[x].valid = true;
      return 1;
    } 
  return -1;
}

bool cache_enabled(void) {
	return cache != NULL && cache_size > 0;
}

void cache_print_hit_rate(void) {
	fprintf(stderr, "num_hits: %d, num_queries: %d\n", num_hits, num_queries);
	fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}
