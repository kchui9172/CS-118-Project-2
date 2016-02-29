#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h> //for socket
#include <netinet/in.h> //for sockaddr_in
#include <string.h> //for memset
#include <sys/types.h>  //for bind
#include <arpa/inet.h>
#include "packet.c"

//sample call: sender <portNum> CWnd Pl Pc

//Helper Functions:
void printPacket(struct packet toPrint);
void sendData(int socketfd, struct sockaddr_in clientAddress, 
	      socklen_t clientLen,int windowSize,struct packet incoming);
struct fileInfo readFile(char* fName,int socketfd, struct sockaddr_in 
			 clientAddress, socklen_t clientLen);
void sendFIN(int socketfd, struct sockaddr_in clientAddress, socklen_t clientLen);
void sentSuccessful(struct packet p);

struct fileInfo{
  char* buffer;
  int size;
};


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

  //Variables
  int socketfd, portNum, newSocketfd;
  struct sockaddr_in address,clientAddress;
  socklen_t clientLen;
  struct packet incoming;

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
    //args = socket fd, packet, size of packet, flag, address of recevier
    if (recvfrom(socketfd,&incoming, sizeof(incoming),0,(struct sockaddr*) 
		 &clientAddress,&clientLen) <0){
      printf("No packet received\n");
      continue;
    }
    printf("\nReceived a packet!\n");
    //here print packet
    printPacket(incoming);

    //Incoming packet is request
    if (incoming.type == 0){
      sendData(socketfd, clientAddress, clientLen, windowSize, incoming);
      break; //for testing purposes
    }
    if (incoming.type == 2){
      //end of transaction
      printf("\nEnding connection\n");
      break;
    }  
  }
  close(socketfd);
  return 0;
}

void printPacket(struct packet toPrint){
  printf("\nPacket Seq Num: %d\n",toPrint.seqNum);
  char* packType;
  if (toPrint.type == 0){
    packType = "Request";
  }
  else if (toPrint.type == 1){
    packType = "ACK";
  }
  else if (toPrint.type == 2){
    packType = "FIN";
  }
  else{
    packType = "Data";
  }
  printf("Packet type: %s\n",packType);
  printf("Packet size: %d\n", toPrint.size);
}

struct fileInfo readFile(char* fName,int socketfd, struct sockaddr_in 
			 clientAddress, socklen_t clientLen){
  struct fileInfo f;
  struct packet outgoing;
  FILE *fp = fopen(fName,"r");      
  //file doesn't exist
  if (fp == NULL){
    printf("File doesn't exist.\n");
    sendFIN(socketfd,clientAddress,clientLen);
    exit(1);
  }
  
  //find size of file
  fseek(fp,0L,SEEK_END);
  int fileSize = (int) ftell(fp);
  if (fileSize <= 0){
    printf("File size error\n");
    sendFIN(socketfd,clientAddress,clientLen);
    exit(1);
  }
  fseek(fp,0L,SEEK_SET);

  //read in file
  char* buffer = NULL;
  buffer= malloc(sizeof(char)*(fileSize +1));
  int sourceSize = fread(buffer,1,fileSize,fp);
  if (sourceSize == 0){
    printf("File reading error\n");
    sendFIN(socketfd,clientAddress,clientLen);
    exit(1);
  }
  fclose(fp);
  f.size = sourceSize;
  f.buffer = buffer;
  return f;
}

void sendFIN(int socketfd, struct sockaddr_in clientAddress, socklen_t clientLen){
  struct packet out;
  out.type= 2;
  out.size = 0;
  out.seqNum =0;
  printf("\nSending FIN packet to client\n");
  if (sendto(socketfd,&out,sizeof(out),0,(struct sockaddr *) &clientAddress,clientLen)<0){
    printf("Error with sending FIN to receiver\n");
  }
}

void sentSuccessful(struct packet p){
  printf("~~~~~~~~~~~~~~~~~~~~~~\n");
  printf("Successfully sent Packet #%d!\n",((p.seqNum /PACKET_SIZE)+1));
  printf("Sequence Number: %d\n",p.seqNum);
  printf("Packet size: %d\n", p.size);
  if (p.type == 3){
    printf("Packet type: data\n");
  }
  printf("~~~~~~~~~~~~~~~~~~~~~~~\n");
}

void sendData(int socketfd, struct sockaddr_in clientAddress, 
	      socklen_t clientLen,int windowSize,struct packet incoming){
  //struct packet outgoing;
  char* fileName;
  fileName= incoming.data;
  printf("Requested file: %s\n",fileName);
  struct fileInfo info = readFile(fileName, socketfd,clientAddress,clientLen);
  int fileSize = info.size;
  char* buffer = info.buffer;
  
  //divide file into packets of max size 1000 bytes
  int totalNumPackets = fileSize/PACKET_SIZE; 
  if (fileSize % PACKET_SIZE > 0){
    totalNumPackets++; //because need one more packet for extra data
  }

  //Variables to keep track of how much of file sent
  int packetsSent=0;
  int packetsSentTemp = 0; //packets sent but not yet ACKed
  int minPos = 0;
  int maxPos = PACKET_SIZE * (windowSize-1);
  int sequenceNum = 0;
  int filePos = 0; //track where in file sent so far
  int size; //size of packet
  char temp[PACKET_SIZE];
  struct package *front;
  //front = (struct package*) malloc(sizeof(struct package) * windowSize);
  front = NULL;

  printf("\nFile size =%d\n",fileSize);
  printf("\ntotalNumPackets =%d\n",totalNumPackets);

  while (1){
    printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    printf("Packets To Send: %d\n",totalNumPackets);
    printf("Sent: %d\n",packetsSent);
    printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    
    if (packetsSent == totalNumPackets){ //no more packets to send
      printf("Done sending all packets!\n");
      break;
    }
    if (packetsSentTemp < windowSize){       //send packet
      struct packet *outgoing = (struct packet *)malloc(sizeof(struct packet));
      //printf("Sending a packet!\n");
      if (filePos < fileSize){
	//printf("inside send\n");
	size= PACKET_SIZE;
	if (fileSize - filePos < PACKET_SIZE){
	  size = fileSize - filePos;
	}
	//printf("size: %d\n",size);
	bzero(temp,size);
	memcpy(temp, buffer+filePos,size);

	outgoing->type = 3;
	outgoing->seqNum = sequenceNum;
	outgoing->size = size;
	strcpy(outgoing->data,temp);
	
	struct package* pack = (struct package *)malloc(sizeof(struct package));
	pack->p = *outgoing;
	free(outgoing);
	printf("packet type: %d\n",pack->p.type);
	printf("packet seq num: %d\n",pack->p.seqNum);

	if (front == NULL){ //first packet in window
	  printf("first packet evaaa\n");
	  pack->isMinPos = 1;
	  pack->isMaxPos = 0;
	  front = pack;
	}
	else{
	  pack->isMinPos =0;
	  //loop until next == NULL
	  struct package *point;
	  point = front;
	  while (point->next != NULL){
	    point = point->next;
	  }
	  point->next = pack;
	  if (pack->p.seqNum == maxPos){
	    pack->isMaxPos = 1;
	  }
	}
	pack->next = NULL;
	filePos+= size;
	sequenceNum+=size; 
	packetsSentTemp++;
	sendto(socketfd, &pack->p,sizeof(pack->p),0,(struct sockaddr *) 
	       &clientAddress,clientLen);
	sentSuccessful(pack->p);
	//break; //will remove just for testing 

	//pack->timeout.tv_sec = 1; //timeout in 1 s
	//pack->timeout.tv_usec = 0;
	pack->startTime = clock(); 
      }
    }
    if (recvfrom(socketfd,&incoming,sizeof(incoming),0,(struct sockaddr *) 
		 &clientAddress, &clientLen) >=0){ 
	if (incoming.type == 1){ //RECEIVE ACK
	  int packetNumber = (incoming.seqNum / PACKET_SIZE)+1;
	  printf("RECEIVED AN ACK for Packet #%d\n",packetNumber);
	  //printf("incoming seq num: %d\n",incoming.seqNum);
	  //if(1 == 1){
	  if (front->p.seqNum == incoming.seqNum - PACKET_SIZE){ //front always min pos
	    packetsSent++;
	    packetsSentTemp--; //decrement so can shift window
	    front->acked = 1;
	    struct package *point = front;
	    if (front->next != NULL){ //if have following packets 
	      front->next->isMinPos = 1;
	      front = front->next;
	    }
	    free(point); //remove acked package 
	    minPos = front->p.seqNum; //shift window
	    maxPos = maxPos + PACKET_SIZE; 
	  }
	  else{
	    packetsSent++;
	    struct package *point = front;
	    if (point->p.seqNum == incoming.seqNum -PACKET_SIZE){ //found match
	      point->acked = 1;
	    }
	  }
	}
    }

    if (1 == 1){ // change
      clock_t diff= clock() - front->startTime;
      int msec = diff *1000/CLOCKS_PER_SEC;
      printf("elapsed: %d s %d ms\n",msec/1000,msec%1000);
    }
    //else{
      //something timed out?
      //resend something
    //}
    
  }
}
