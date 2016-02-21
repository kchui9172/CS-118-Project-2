#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h> //for socket
#include <netinet/in.h> //for sockaddr_in
#include <string.h> //for memset
#include <sys/types.h>  //for bind
#include <arpa/inet.h>
#include "packet.c"

//sample call: sender <portNum> CWnd Pl Pc

int main(int argc, char *argv[]){  
  if (argc < 5){
    fprintf(stderr,"Need port number, CWnd, Pl, and Pc\n");
    exit(1);
  }
  
  //extract parameters
  int windowSize= atoi(argv[2]);
  double lossProb = atof(argv[3]);
  double corrProb = atof(argv[4]);

  //make sure probabilities are valid
  if (lossProb <0 || lossProb > 1 || corrProb < 0 || corrProb >1){
    fprintf(stderr,"Incorrect values for lossProb and corrProb: must be btwn 0-1\n");
    exit(1);
  }

  if (windowSize < 1){
    fprintf(stderr,"Window size must be at least 1\n");
    exit(1);
  }

  int socketfd, portNum, newSocketfd;
  struct sockaddr_in address,clientAddress;
  socklen_t clientLen;
  struct packet incoming, outgoing;

  //SOCK_DGRAM = UDP
  socketfd = socket(AF_INET, SOCK_DGRAM,0);
  if (socketfd < 0){
    error("Error when creating socket\n");
  }

  memset((char *) &address, 0, sizeof(address));
  portNum = atoi(argv[1]);
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;//bind to default IP addr
  address.sin_port = htons(portNum);
  if(bind(socketfd,(struct sockaddr *) &address, sizeof(address)) < 0){
    error("Error when binding\n");
  }

  //wait for receiver to send message
  printf("Waiting for connection...\n");

  while(1){
    clientLen = sizeof(clientAddress);
    //recvfrom = get UDP datagram from receiver
    //args = socket file descriptor, packet, size of packet, flag, address of 
    //recevier
    if (recvfrom(socketfd,&incoming, sizeof(incoming),0,(struct sockaddr*) &clientAddress,&clientLen) <0){
      printf("no packet received\n");
      continue;
    }
    printf("Received a packet!\n");
    //here print packet
    printf("Data type:%d\n",incoming.type);
    char* fileName = incoming.data;
    printf("Requested file:%s\n",fileName);
    
    FILE *fp = fopen(incoming.data,"r");      
    //file doesn't exist
    if (fp == NULL){
      printf("File doesn't exist.\n");
      //set outgoing packet: FIN and seqNum = 0 to show that error
      outgoing.type = 2;
      outgoing.size = 0;
      outgoing.seqNum = 0;
      if (sendto(socketfd,&outgoing,sizeof(outgoing),0,(struct sockaddr *) &clientAddress,clientLen)<0){
      printf("Error with sending FIN to receiver\n");
      }
    }

    //file exists, read in and divide into packets
    fseek(fp,0L,SEEK_END);
    int fileSize = (int) ftell(fp);
    if (fileSize <= 0){
      printf("File size error\n");
    }
    fseek(fp,0L,SEEK_SET);

    //read in file
    char* buffer = NULL;
    buffer= malloc(sizeof(char)*(fileSize +1));
    int sourceSize = fread(buffer,1,fileSize,fp);
    if (sourceSize == 0){
      printf("File reading error\n");
    }
    fclose(fp);

    //divide file into packets of max size 1000 bytes
    int numPackets = fileSize/1024; 
    if (fileSize % 1024 > 0){
      numPackets++; //because need one more packet for extra data
    }
    
    outgoing.type = 3;
    
  }
  free(buffer);
  close(socketfd);
  return 0;
}

  //divide up file into 1k packets and add header-seq number, dest/send port
  //print message of sending - DATA packet, sequence number, corrupted/not
  //timer value

  //seq number and window size give in unit of bytes -max seq #= 30
