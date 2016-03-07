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
 void sendData(int socketfd, struct sockaddr_in clientAddress, 
	       socklen_t clientLen,int windowSize,struct packet incoming);
 void printPacket(struct packet toprint);
 struct fileInfo readFile(char* fname,int socketfd, struct sockaddr_in 
			  clientaddress, socklen_t clientlen);
 void sendFIN(int socketfd, struct sockaddr_in clientaddress, socklen_t clientlen);
 void sentSuccessful(struct packet p);
 void retransmitSuccessful(struct packet p);

 struct fileInfo{
   char* buffer;
   int size;
 };


int main(int argc, char *argv[]){  
  if (argc < 5){
    fprintf(stderr,"need port number, cwnd, pl, and pc\n");
    exit(1);
  }
  
  //extract parameters
  int windowSize= atoi(argv[2]);
  double lossprob = atof(argv[3]);
  double corrprob = atof(argv[4]);

  //make sure probabilities are valid
  if (lossprob <0 || lossprob > 1 || corrprob < 0 || corrprob >1){
    fprintf(stderr,"incorrect values for lossprob and corrprob: must be btwn 0-1\n");
    exit(1);
  }

  if (windowSize < 1){
    fprintf(stderr,"window size must be at least 1\n");
    exit(1);
  }

  //variables
  int socketfd, portnum, newsocketfd;
  struct sockaddr_in address,clientAddress;
  socklen_t clientLen;
  struct packet incoming;

  //sock_dgram = udp
  socketfd = socket(AF_INET, SOCK_DGRAM,0);
  if (socketfd < 0){
    error("error when creating socket\n");
  }

  memset((char *) &address, 0, sizeof(address));
  portnum = atoi(argv[1]);
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;//bind to default ip addr
  address.sin_port = htons(portnum);
  if(bind(socketfd,(struct sockaddr *) &address, sizeof(address)) < 0){
    error("error when binding\n");
  }

  //wait for receiver to send message
  printf("waiting for connection...\n");

  while(1){
    clientLen = sizeof(clientAddress);
    //recvfrom = get udp datagram from receiver
    //args = socket fd, packet, size of packet, flag, address of recevier
    if (recvfrom(socketfd,&incoming, sizeof(incoming),0,(struct sockaddr*) 
		 &clientAddress,&clientLen) <0){
      printf("no packet received\n");
      continue;
    }
    printf("\nreceived a packet!\n");
    printPacket(incoming);

    //incoming packet is request
    if (incoming.type == 0){
      sendData(socketfd, clientAddress, clientLen, windowSize, incoming);
      break; //for testing purposes
    }
    if (incoming.type == 2){
      //end of transaction
      printf("\nending connection\n");
      break;
    }  
  }
  close(socketfd);
  return 0;
}


void sendData(int socketfd, struct sockaddr_in clientAddress, 
	      socklen_t clientLen,int windowSize,struct packet incoming){
  //struct packet outgoing;
  char* filename;
  filename= incoming.data;
  printf("\nrequested file: %s\n",filename);
  struct fileInfo info = readFile(filename, socketfd,clientAddress,clientLen);
  int fileSize = info.size;
  char* buffer = info.buffer;
  
  //divide file into packets of max size 1000 bytes
  int totalNumPackets = fileSize/PACKET_SIZE; 
  if (fileSize % PACKET_SIZE > 0){
    totalNumPackets++; //because need one more packet for extra data
  }

  //variables to keep track of how much of file sent
  int packetsSent=0;
  int packetsSentTemp = 0; //packets sent but not yet acked
  int minPos = 0;
  int maxPos = PACKET_SIZE * (windowSize-1);
  int sequenceNum = 0;
  int filePos = 0; //track where in file sent so far
  //int size; //size of packet
  char temp[PACKET_SIZE];
  struct package *front;
  //front = (struct package*) malloc(sizeof(struct package) * windowsize);
  front = NULL;

  printf("file size = %d\n",fileSize);
  printf("total number of packets to send = %d\n",totalNumPackets);

  while (1){
    printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    printf("packets to send: %d\n",totalNumPackets);
    printf("packets temp sent: %d\n",packetsSentTemp);
    printf("sent: %d\n",packetsSent);
    printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    
    if (packetsSent == totalNumPackets){ //no more packets to send
      printf("done sending all packets!\n");
      //send fin packet
      struct packet last;
      last.type = 2;
      last.size = 0;
      sendto(socketfd, &last,sizeof(last),0,(struct sockaddr *) 
	       &clientAddress,clientLen);      
      break;
    }

    if (packetsSentTemp < windowSize){ //send packet
      struct packet *outgoing = (struct packet *)malloc(sizeof(struct packet));
      if (filePos < fileSize){ //still stuff left to send in file
	int packSize= PACKET_SIZE;
	if (fileSize - filePos < PACKET_SIZE){ //amount left in file less than default packet size
	  packSize = fileSize - filePos;
	}
	bzero(temp,packSize);
	memcpy(temp, buffer+filePos,packSize);

	outgoing->type = 3;
	outgoing->seqNum = sequenceNum;
	strcpy(outgoing->data,temp);
	
	struct package* pack = (struct package *)malloc(sizeof(struct package));
	pack->p = *outgoing;
	pack->p.size = packSize;
	pack->p.seqNum = sequenceNum;
	free(outgoing);

	if (front == NULL){ //first packet in window
	  printf("first packet evaaa\n");
	  pack->isMinPos = 1;
	  pack->isMaxPos = 0;
	  front = pack;
	}
	else{
	  pack->isMinPos =0;
	  //loop until next == NULL and add packet
	  struct package *point = front;
	  while (point->next != NULL){
	    point = point->next;
	  }
	  point->next = pack; //necessary?
	  if (pack->p.seqNum == maxPos){
	    pack->isMaxPos = 1;
	  }
	}

	//increment values: seqNum, position in file, packets sent (w/o ACKs) for next iteration
	pack->next = NULL;
	filePos+= packSize;
	sequenceNum+=packSize; 
	packetsSentTemp++;
	sendto(socketfd, &pack->p,sizeof(pack->p),0,(struct sockaddr *) //deal with corruption here
	       &clientAddress,clientLen);
	sentSuccessful(pack->p);
	gettimeofday(&(pack->startTime),NULL); //set time for packet just sent
      }
    }

    //printf("window size = %d\n",windowSize);
    fd_set fileSet;
    FD_ZERO(&fileSet);
    FD_SET(socketfd,&fileSet);

    //set up time out time
    struct timeval tOut;
    tOut.tv_sec = TOUT_SEC;
    tOut.tv_usec = TOUT_USEC;
    double tOutD = (tOut.tv_sec * 1000.0) + (tOut.tv_usec/1000.0); //in ms

    struct timeval curr;
    gettimeofday(&curr,NULL);
    double elapsedTime;
    double minTimeLeft = tOutD;

    struct package *e = front;
    struct package *earliest = front;
    double leftD = 0;
    int alreadyRetransmitted = 0;
    while (e != NULL){
      elapsedTime = (curr.tv_sec - e->startTime.tv_sec)*1000.0;
      elapsedTime += (curr.tv_usec - e->startTime.tv_usec)/1000.0;
      double leftD = tOutD - elapsedTime;
      if (leftD < 0){ //packet timed out, resend
	printf("already timed out\n");
	sendto(socketfd,&e->p,sizeof(e->p),0,(struct sockaddr *)
	       &clientAddress,clientLen);
	retransmitSuccessful(e->p); //deal with corruption here!
	//reset timer for earliest
	gettimeofday(&(e->startTime),NULL); //set time for packet just sent
	alreadyRetransmitted = 1;
	break;
      }
      if (leftD < minTimeLeft){
	minTimeLeft = leftD;
	earliest = e; 
      }
      e = e->next;
    }
    printf("Time left: %f ms\n",minTimeLeft);

    if (alreadyRetransmitted == 0){ //didn't already retransmit
      printf("still have time\n");
      struct timeval left;
      double tv = (minTimeLeft/1000)*1000000; 
      //printf("tv: %f\n",tv);
      left.tv_sec = 0;
      left.tv_usec = tv;

      printf("ls :%f\n",(double)left.tv_sec);
      printf("lus : %f\n",(double)left.tv_usec);
      int received = select(socketfd+1,&fileSet,NULL,NULL,&left);
      if (received < 1){
	//retransmit earliest sent packet
	printf("retransmitting earliest sent packet\n");
	sendto(socketfd,&earliest->p,sizeof(earliest->p),0,(struct sockaddr *)
	       &clientAddress,clientLen);
	retransmitSuccessful(earliest->p); //deal with corruption here!
	gettimeofday(&(earliest->startTime),NULL); //set time for packet just sent	
	continue;
      }
      if (recvfrom(socketfd,&incoming,sizeof(incoming),0,(struct sockaddr *) 
		   &clientAddress, &clientLen) >=0){ 
	printf("anything\n");
	if (incoming.type == 1){ //RECEIVE ACK
	  int packetNumber = (incoming.seqNum / PACKET_SIZE);
	  printf("RECEIVED AN ACK for Packet #%d\n",packetNumber);
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
    }

    //NEED TO CHANGE TO START TIME OF EARLIEST SENT PACKET
    /*elapsedTime = (curr.tv_sec - front->startTime.tv_sec)*1000.0;
    elapsedTime += (curr.tv_usec- front->startTime.tv_usec)/1000.0;
    printf("elapsed time: %f ms\n",elapsedTime);
    double leftD = tOutD - elapsedTime;
    printf("time left: %f ms\n",leftD);
    
    if (leftD < 0){ // timed out
      //printf("need to retransmit!\n");
      sendto(socketfd, &front->p,sizeof(front->p),0,(struct sockaddr *) 
	       &clientAddress,clientLen);      
      retransmitSuccessful(front->p);
      continue;
    }
    else{
      printf("stil have time\n");
      struct timeval left;
      double tv = (leftD/1000)*1000000; 
      printf("tv: %f\n",tv);
      left.tv_sec = 0;
      left.tv_usec = tv;

      printf("ls :%f\n",(double)left.tv_sec);
      printf("lus : %f\n",(double)left.tv_usec);
      int received = select(socketfd+1,&fileSet,NULL,NULL,&left);
      if (received < 1){
	//retransmit earliest sent packet
	printf("retransmitting earliest sent packet\n");
	continue;
      }

      if (recvfrom(socketfd,&incoming,sizeof(incoming),0,(struct sockaddr *) 
		   &clientAddress, &clientLen) >=0){ 
	printf("anything\n");
	if (incoming.type == 1){ //RECEIVE ACK
	  int packetNumber = (incoming.seqNum / PACKET_SIZE);
	  printf("RECEIVED AN ACK for Packet #%d\n",packetNumber);
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
    }*/
  }
}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~HELPER FUNCTIONS~~~~~~~~~~~~~~~~~~~~~~~~~~~//
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
  buffer[sourceSize] ='\0';
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

void retransmitSuccessful(struct packet p){
  printf("~~~~~~~~~~~~~~~~~~~~~~\n");
  printf("Successfully Resent Packet #%d!\n",((p.seqNum /PACKET_SIZE)+1));
  printf("Sequence Number: %d\n",p.seqNum);
  printf("Packet size: %d\n", p.size);
  if (p.type == 3){
    printf("Packet type: data\n");
  }
  printf("~~~~~~~~~~~~~~~~~~~~~~~\n");
}
