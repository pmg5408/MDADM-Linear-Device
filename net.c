#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n (len) bytes from fd; returns true on success and false on failure. 
It may need to call the system call "read" multiple times to reach the given size len. 
*/
static bool nread(int fd, int len, uint8_t *buf) {
  bool ret = true;
  int count = 0;

  while (count < len){  // loops till all the bites are copied
    int x = read(fd, buf+count, len-count); 
    if (x < 0){
      ret = false;
    }
    count += x;   // used to keep a track of bytes copied
  }
  return ret;
}

/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf) {
  bool ret = true;
  int count = 0;
  
  while (count < len){    // loops till all the bites are copied
    int x = write(fd, buf+count, len-count);
    if (x < 0){
      ret = false;
    }
    count += x;   // used to keep a track of bytes copied
  }
  return ret;
}

/* Through this function call the client attempts to receive a packet from sd 
(i.e., receiving a response from the server.). It happens after the client previously 
forwarded a jbod operation call via a request message to the server.  
It returns true on success and false on failure. 
The values of the parameters (including op, ret, block) will be returned to the caller of this function: 

op - the address to store the jbod "opCode"  
ret - the address to store the info code (lowest bit represendVals the return value of the server side calling the corresponding jbod_operation function. 2nd lowest bit represendVal whether data block exists after HEADER_LEN.)
block - holds the recieveVal block content if existing (e.g., when the op command is JBOD_READ_BLOCK)

In your implementation, you can read the packet header first (i.e., read HEADER_LEN bytes first), 
and then use the length field in the header to determine whether it is needed to read 
a block of data from the server. You may use the above nread function here.  
*/
static bool recv_packet(int sd, uint32_t *op, uint8_t *ret, uint8_t *block) {
  uint8_t header[HEADER_LEN];
  
  bool retValue;

  retValue = nread(sd, HEADER_LEN, header);

  if (!retValue){
    return false;
  }

  // code to unpack the packet recieved and retrieve info from it
  uint8_t infocode;
  uint16_t opCode;
  memcpy(&opCode, header, 4);
  memcpy(&infocode, header + 4, 1);
  *ret = infocode;
  *op = ntohl(opCode);

  if (infocode == 2){
    retValue = nread(sd, JBOD_BLOCK_SIZE, block);
  }
  return retValue;
}


/* The client attempts to send a jbod request packet to sd (i.e., the server socket here); 
returns true on success and false on failure. 

op - the opCode. 
block- when the command is JBOD_WRITE_BLOCK, the block will contain data to write to the server jbod system;
otherwise it is NULL.

The above information (when applicable) has to be wrapped into a jbod request packet (format specified in readme).
You may call the above nwrite function to do the actual sending.  
*/
static bool send_packet(int sd, uint32_t op, uint8_t *block) {
  bool ret;
  uint32_t comm;
  uint8_t packet[JBOD_BLOCK_SIZE + HEADER_LEN];
  uint32_t opCode = htonl(op);

  memcpy(packet, &opCode, 4);

  comm = ((op >> 12) & 0x3f);
  
  uint8_t infocode = 0;

  // packing all the required info and sending the packet to the server
  if (comm == JBOD_WRITE_BLOCK){
    infocode = 2;
    memcpy(packet + 4, &infocode, 1);
    memcpy(packet + 5, block, JBOD_BLOCK_SIZE);
    ret = nwrite(sd,JBOD_BLOCK_SIZE+HEADER_LEN, packet);
  }
  else{
    memcpy(packet + 4, &infocode, 1);
    ret = nwrite(sd, HEADER_LEN, packet);
  }
  return ret;
}

/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. 
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
*/
bool jbod_connect(const char *ip, uint16_t port) {
  struct sockaddr_in caddr;
  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(JBOD_PORT);

  if (inet_aton(JBOD_SERVER, &caddr.sin_addr) == 0){    // converts the ip address in unix language
    return false;
  }
  
  cli_sd = socket(AF_INET, SOCK_STREAM, 0);   // creates a socket
  if (cli_sd == -1){
    return false;
  }

  if (connect(cli_sd, (const struct sockaddr *)&caddr, sizeof(caddr)) == -1){   // connects the socket to the server
    return false;
  }
  return true;
}


/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {
  close(cli_sd);    // closes the socket
  cli_sd = -1;
}

/* sends the JBOD operation to the server (use the send_packet function) and receives 
(use the recv_packet function) and processes the response. 

The meaning of each parameter is the same as in the original jbod_operation function. 
return: 0 means success, -1 means failure.
*/
int jbod_client_operation(uint32_t op, uint8_t *block) {
  bool sendVal = send_packet(cli_sd, op, block);    // sends the packet
  if (!sendVal){
    return -1;
  }
  uint8_t ret;
  bool recieveVal = recv_packet(cli_sd, &op, &ret, block);    // recieves the packet
  if (!recieveVal){
    return -1;
  }
  else{
    return 0;
  }
}
