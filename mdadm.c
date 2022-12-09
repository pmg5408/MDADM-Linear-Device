#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "jbod.h"
#include "mdadm.h"
#include "net.h"

int is_mounted = 0;
int is_written = 0;
int write_flag = 0; 

uint32_t pack_bytes(uint32_t a, uint32_t b, uint32_t c){    // converts the inputs into the desired format
  uint32_t final, t1, t2, t3;
  t1 = a&0xff;
  t2 = (b&0xff) << 8;
  t3 = (c&0xff) << 12; 
  final = t1 | t2 | t3; 
  return final; 
}
int flag = 0; 	// flag to keep a track of whether disk is mounted or dismounted

int mdadm_mount(void) {
	if(flag == 1){		// checks if disk is already mounted
		return -1;
	}
	else{
		int res = jbod_client_operation(pack_bytes(0, 0, JBOD_MOUNT), NULL);  	// mounts the disk
		if (res == 0){
			flag  = 1;
			return 1;
		}
		else {
			return -1;
		}
	}
	return -1;
}

int mdadm_unmount(void) {
	if (flag == 0){		// checks if disk is already unmounted
		return -1;
	}
	else{
		int res = jbod_client_operation(pack_bytes(0, 0, JBOD_UNMOUNT), NULL); 	// Unmounts the disk
		if (res == 0){
			flag = 0;
			return 1;
		}
		else{
			return -1; 
		}
	}
	return -1;
}

int mdadm_write_permission(void){
  if (write_flag == 1){
    return 0;
  }

  else if(write_flag == 0){
    if(jbod_client_operation(pack_bytes(0, 0, JBOD_WRITE_PERMISSION), NULL) == 0){
      write_flag = 1;
      return 0;
    }

    else{
      return -1;
    }  
  }

  else{
    return -1;
  }
}


int mdadm_revoke_write_permission(void){
  if(write_flag == 0){
    return 0;
  }
  else if(write_flag == 1){
    if(jbod_client_operation(pack_bytes(0,0,JBOD_REVOKE_WRITE_PERMISSION), NULL) == 0){
      write_flag = 0;
      return 0;
    }
    else{
      return -1;
    }   
  }
  else{
    return -1;
  }
	return -1;
}


int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {
	uint32_t disk_number = addr / (256*256);		//gets disk number
	uint32_t block_number =  (addr / 256) % 256 ; 	//gets block number
	uint32_t byte_number = addr % 256; 	//gets starting byte number in particular block

	if (flag == 0){		// checks if disk is unmounted
		return -1;
	}

	else{
		if (addr > (256*256*16)){ 	//checks if addr is out of bounds
			return -1;
		}
		else if (addr + len > (256*256*16)) 	// checks if we are trying to read out of the linear address
		{
			return -1;
		}
		else if (len > 2048)	// checks if len exceeds 2048 bytes
		{
			return -1;	
		}
		else if (len > 0 && buf == NULL) 	//checks if len is more than read_buff
		{
			return -1;	
		}
		else if(len == 0 && buf == NULL)
		{
			return len;
		}
		else{ 
			uint8_t temp_buff[256];		// initializing array to read a block
			
			if (((addr + len) / 256) % 256 == block_number){ 		// code to read when reading from within a block	

			//	printf("%d, bk num = %d, disk num = %d\n",cache_lookup(disk_number, block_number, temp_buff),block_number, disk_number);

				if(cache_lookup(disk_number, block_number, temp_buff) != 1){

			//		printf("entering here\n");
					jbod_client_operation(pack_bytes(0, disk_number, JBOD_SEEK_TO_DISK), NULL);		// seek to disk
					jbod_client_operation(pack_bytes(block_number, 0, JBOD_SEEK_TO_BLOCK), NULL); 		// seek to block
					jbod_client_operation(pack_bytes(0, 0, JBOD_READ_BLOCK), temp_buff);

					cache_insert(disk_number, block_number, temp_buff);

				}

				memcpy(buf, temp_buff, len); 		// copying from temp_buff to read_buff
				return len;				
			}
	
			else if(((addr + len) / 256) % 256 > block_number){	// code for reading from multiple blocks

				uint32_t r = len;
				uint32_t a;			// temporary var to store the number of bytes we are reading from a block
				uint32_t b = 0;		// temporary var to store number of bytes already copied in read_buff
				int flag = 1; 		

				jbod_client_operation(pack_bytes(0, disk_number, JBOD_SEEK_TO_DISK), NULL); 		//seek to disk 

				while (r != 0){		// while loop to read all bytes from multiple blocks

					if(cache_lookup(disk_number, block_number, temp_buff) != 1){

						jbod_client_operation(pack_bytes(block_number, 0, JBOD_SEEK_TO_BLOCK), NULL); 		// seek to block
						jbod_client_operation(pack_bytes(0, 0, JBOD_READ_BLOCK), temp_buff);		// read from block  
						cache_insert(disk_number, block_number, temp_buff);

					}

					if (r < 256 && flag != 1){		// reading from the last block 


						memcpy(buf + b, temp_buff + byte_number, r);
						break;

					}

					a = 256 - byte_number;
					memcpy(buf + b, temp_buff + byte_number, a);
					b = b + a;
					r = r - a;
					byte_number = 0; 		// assigning start byte to 0 after we are done reading from the 1st block
					block_number = block_number + 1;	
					flag = flag + 1; 
				 
				}
				return len; 
			}
		
			else if((addr + len / (256 * 256)) > disk_number){			// code to read from multiple disks
				// comments same as above code
				uint32_t num_of_bytes = 256 - byte_number; 
				uint32_t rl = len;
				uint32_t start_buff = 0;

				if(cache_lookup(disk_number, block_number, temp_buff) != 1){

					jbod_client_operation(pack_bytes(0, disk_number, JBOD_SEEK_TO_DISK), NULL);
					jbod_client_operation(pack_bytes(block_number, 0, JBOD_SEEK_TO_BLOCK), NULL);
					jbod_client_operation(pack_bytes(0, 0, JBOD_READ_BLOCK), temp_buff);
					cache_insert(disk_number, block_number, temp_buff);

				}

				memcpy(buf, temp_buff + byte_number, num_of_bytes);

				len = len - num_of_bytes;
				start_buff = num_of_bytes;
				block_number = block_number + 1;

				while (len != 0){

					if((start_buff + addr) / (256 * 256) > disk_number) {
						disk_number = disk_number + 1;
						block_number = 0;

					}

					if(len >= 256){
						
						if(cache_lookup(disk_number, block_number, temp_buff) != 1){

	        				jbod_client_operation(pack_bytes(0, disk_number, JBOD_SEEK_TO_DISK), NULL);
    						jbod_client_operation(pack_bytes(block_number, 0, JBOD_SEEK_TO_BLOCK), NULL);
    						jbod_client_operation(pack_bytes(0, 0, JBOD_READ_BLOCK), temp_buff);
							cache_insert(disk_number, block_number, temp_buff);

						}

						memcpy(buf + start_buff, temp_buff,  256);

						start_buff = start_buff + 256;
						len = len - 256;
						block_number = block_number + 1;

					}

					else{
						
						if(cache_lookup(disk_number, block_number, temp_buff) != 1){
					
							jbod_client_operation(pack_bytes(0, disk_number, JBOD_SEEK_TO_DISK), NULL);
    						jbod_client_operation(pack_bytes(block_number, 0, JBOD_SEEK_TO_BLOCK), NULL);
    						jbod_client_operation(pack_bytes(0, 0, JBOD_READ_BLOCK), temp_buff);
							cache_insert(disk_number, block_number, temp_buff);

						}
						memcpy(buf + start_buff, temp_buff,  len);
						len = 0;
					}
				}
			return rl;
			}
			else{ 
				return 0; 
				}							
			}		
			return 0; 
		}
	return 0;	
}

int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {
// finds disk, block and byte number of start address
  uint32_t disk_num = addr / (256*256);
  uint32_t block_num = (addr / 256) % 256 ; 
  uint32_t byte_num = addr % 256;
  uint32_t x = ((addr + len) / 256) % 256;

// checks for the cases that should fail 
  if(flag == 0){
    return -1;
  }

  else if(addr + len > 16 * 256 * 256){
    return -1; 
  }

  else if(len > 2048){
    return -1; 
  }

  else if (buf == NULL && len != 0){
    return -1;
  }
  
  else if(buf == NULL && len == 0){
    return len; 
  }

  else{
	//uint8_t lookup_buff[256];
	uint8_t lookup_buff[256];
    uint8_t temp_buf[256];
	uint32_t start_buff = 0; 

//to write within a block 
    if (((addr + len) / 256) % 256 == block_num){

      if(write_flag == 1){
		
		if(byte_num != 0){
			mdadm_read(addr - byte_num, byte_num, temp_buf); // reads to temparr on bytes except the ones that need to be written 
		}

		if(byte_num + len != 256){	
			mdadm_read(addr + len, 256 - (byte_num + len), temp_buf + byte_num + len);
		}

        memcpy(temp_buf + byte_num, buf, len);	// stores data from write buff to tempbuff

// seeks to the specific block and write to it
        jbod_client_operation(pack_bytes(0, disk_num, JBOD_SEEK_TO_DISK), NULL);
        jbod_client_operation(pack_bytes(block_num, 0, JBOD_SEEK_TO_BLOCK), NULL);
        jbod_client_operation(pack_bytes(0, 0, JBOD_WRITE_BLOCK), temp_buf);  

	//	cache_update(disk_num, block_num, temp_buf);

		if(cache_lookup(disk_num, block_num, lookup_buff) == 1){
			cache_update(disk_num, block_num, temp_buf);
		}
		else{
			cache_insert(disk_num, block_num, temp_buf);
		}

        return len;
      }
	}

// same disk but across blocks
	else if(x > block_num){

		if(write_flag == 1){

			uint32_t num_of_bytes = 256 - byte_num;
			uint32_t wl = len;

			if(byte_num != 0){
				mdadm_read((addr - byte_num), byte_num, temp_buf);	// reads the contents of first block to temp buff 
			}

			// writes to the specific block
			memcpy(temp_buf + byte_num, buf, num_of_bytes);
        	jbod_client_operation(pack_bytes(0, disk_num, JBOD_SEEK_TO_DISK), NULL);
        	jbod_client_operation(pack_bytes(block_num, 0, JBOD_SEEK_TO_BLOCK), NULL);
        	jbod_client_operation(pack_bytes(0, 0, JBOD_WRITE_BLOCK), temp_buf); 

	//		cache_update(disk_num, block_num, temp_buf);

			if(cache_lookup(disk_num, block_num, lookup_buff) == 1){
				cache_update(disk_num, block_num, temp_buf);
			}
			else{
				cache_insert(disk_num, block_num, temp_buf);
			}	

			// updating all the parameters
			start_buff = num_of_bytes; 
			len = len - num_of_bytes;
			block_num = block_num + 1;

			while (len != 0){	// while loop to write the other bytes after the first one
				
				if (len >= 256){	// code to write when we are writing to complete blocks
					
					memcpy(temp_buf, buf + start_buff, 256);
					jbod_client_operation(pack_bytes(0, disk_num, JBOD_SEEK_TO_DISK), NULL);
					jbod_client_operation(pack_bytes(block_num, 0, JBOD_SEEK_TO_BLOCK), NULL);
					jbod_client_operation(pack_bytes(0, 0, JBOD_WRITE_BLOCK), temp_buf);

	//				cache_update(disk_num, block_num, temp_buf);

				if(cache_lookup(disk_num, block_num, lookup_buff) == 1){
						cache_update(disk_num, block_num, temp_buf);
					}
					else{
						cache_insert(disk_num, block_num, temp_buf);
					}			

					start_buff = start_buff + 256;
					len = len - 256;
					block_num = block_num + 1;

				}

				// writing to the last block 
				else{

					mdadm_read((addr + start_buff + len), (256 - len), temp_buf + len);
					memcpy(temp_buf, buf + start_buff, len);
					jbod_client_operation(pack_bytes(0, disk_num, JBOD_SEEK_TO_DISK), NULL);
					jbod_client_operation(pack_bytes(block_num, 0, JBOD_SEEK_TO_BLOCK), NULL);
					jbod_client_operation(pack_bytes(0, 0, JBOD_WRITE_BLOCK), temp_buf);

		//			cache_update(disk_num, block_num, temp_buf);

					if(cache_lookup(disk_num, block_num, lookup_buff) == 1){
						cache_update(disk_num, block_num, temp_buf);
					}
					else{
						cache_insert(disk_num, block_num, temp_buf);
					}	

					len = 0;
				}
			} 
		return wl;
		}
	}

	// code to write across disks. Comments similar as above
	else if((addr + len / (256 * 256)) > disk_num){

		if(write_flag == 1){

			uint32_t num_of_bytes = 256 - byte_num; 
			uint32_t wl = len;

			mdadm_read(addr - byte_num, byte_num, temp_buf);
			memcpy(temp_buf + byte_num, buf, num_of_bytes);
			jbod_client_operation(pack_bytes(0, disk_num, JBOD_SEEK_TO_DISK), NULL);
			jbod_client_operation(pack_bytes(block_num, 0, JBOD_SEEK_TO_BLOCK), NULL);
			jbod_client_operation(pack_bytes(0, 0, JBOD_WRITE_BLOCK), temp_buf);

	//		cache_update(disk_num, block_num, temp_buf);

			if(cache_lookup(disk_num, block_num, lookup_buff) == 1){
				cache_update(disk_num, block_num, temp_buf);
			}
			else{
				cache_insert(disk_num, block_num, temp_buf);
			}			

			len = len - num_of_bytes;
			start_buff = num_of_bytes;
			block_num = block_num + 1;

			while (len != 0){

				if(len >= 256){

					// checks if the disk number has changed and updates block and disk numbers
					if((start_buff + addr) / (256 * 256) > disk_num) {

					disk_num = disk_num + 1;
					block_num = 0;

					}

					memcpy(temp_buf, buf + start_buff, 256);
					jbod_client_operation(pack_bytes(0, disk_num, JBOD_SEEK_TO_DISK), NULL);
					jbod_client_operation(pack_bytes(block_num, 0, JBOD_SEEK_TO_BLOCK), NULL);
					jbod_client_operation(pack_bytes(0, 0, JBOD_WRITE_BLOCK), temp_buf);

		//			cache_update(disk_num, block_num, temp_buf);

					if(cache_lookup(disk_num, block_num, lookup_buff) == 1){
						cache_update(disk_num, block_num, temp_buf);
					}
					else{
						cache_insert(disk_num, block_num, temp_buf);
					}			

					start_buff = start_buff + 256;
					len = len - 256;
					block_num = block_num + 1;

				}

				else{

					if((start_buff + addr) / (256 * 256) > disk_num) {
					disk_num = disk_num + 1;
					block_num = 0;
					}

					mdadm_read((addr + start_buff + len), (256 - len), temp_buf + len);
					memcpy(temp_buf, buf + start_buff, len);
					jbod_client_operation(pack_bytes(0, disk_num, JBOD_SEEK_TO_DISK), NULL);
					jbod_client_operation(pack_bytes(block_num, 0, JBOD_SEEK_TO_BLOCK), NULL);
					jbod_client_operation(pack_bytes(0, 0, JBOD_WRITE_BLOCK), temp_buf);

		//			cache_update(disk_num, block_num, temp_buf);

					if(cache_lookup(disk_num, block_num, lookup_buff) == 1){
						cache_update(disk_num, block_num, temp_buf);
					}
					else{
						cache_insert(disk_num, block_num, temp_buf);
					}		

					len = 0;

				}
			}	
		return wl;
		}
	}
    else{
        return 0;
    }
  }
	return 0;
}
