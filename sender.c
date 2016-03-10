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
	      socklen_t clientLen,int windowSize,struct packet incoming, 
	      double lossprob, double corrprob);
void printPacket(struct packet toprint);
struct fileInfo readFile(char* fname,int socketfd, struct sockaddr_in 
			  clientaddress, socklen_t clientlen);
void sendFIN(int socketfd, struct sockaddr_in clientaddress, socklen_t clientlen);
void sentSuccessful(struct packet p, int timesRepeated, int maxPackets);
void retransmitSuccessful(struct packet p, int timesRepeated, int maxPackets);
int corruptOrLossSimulator(double probability);

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
    fprintf(stderr,"Incorrect values for lossprob and corrprob: must be btwn 0-1\n");
    exit(1);
  }

  if (windowSize < 1){
    fprintf(stderr,"Window size must be at least 1\n");
    exit(1);
  }

  if (windowSize/PACKET_SIZE <= 0){
    fprintf(stderr,"Window size too small, can't send any packets\n");
    exit(1);
  }

  //variables
  int socketfd, portnum, newsocketfd;
  struct sockaddr_in address,clientAddress;
  socklen_t clientLen;
  struct packet incoming;

  //SOCK_DGRAM = UDP
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
  printf("Waiting for connection...\n");

  while(1){
    clientLen = sizeof(clientAddress);
    //recvfrom = get udp datagram from receiver
    //args = socket fd, packet, size of packet, flag, address of recevier
    if (recvfrom(socketfd,&incoming, sizeof(incoming),0,(struct sockaddr*) 
		 &clientAddress,&clientLen) <0){
      printf("no packet received\n");
      continue;
    }
    printf("\nReceived a packet!\n");
    printPacket(incoming);

    //incoming packet is request
    if (incoming.type == 0){
      sendData(socketfd, clientAddress, clientLen, windowSize, incoming, lossprob, corrprob);
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


void sendData(int socketfd, struct sockaddr_in clientAddress, 
	      socklen_t clientLen,int windowSize,struct packet incoming, 
	      double lossprob, double corrprob){
  char* filename;
  filename= incoming.data;
  printf("\nRequested file: %s\n",filename);
  struct fileInfo info = readFile(filename, socketfd,clientAddress,clientLen);
  int fileSize = info.size;
  char* buffer = info.buffer;
  
  //divide file into packets of max size 1024 bytes
  int totalNumPackets = fileSize/PACKET_SIZE; 
  if (fileSize % PACKET_SIZE > 0){
    totalNumPackets++; //because need one more packet for extra data
  }

  //variables to keep track of how much of file sent
  int packetsSent=0;
  int packetsSentTemp = 0; //packets sent but not yet acked
  int minPos = 0;
  int maxPos = (windowSize/PACKET_SIZE) * PACKET_SIZE; //equal to seqnum of next packet outside window
  int sequenceNum = 0;
  int timesRepeated = 0; //number of times reset sequenceNum = 0
  int maxPackets = SEQNUM_LIM/PACKET_SIZE;
  int filePos = 0; //track where in file sent so far
  char temp[PACKET_SIZE];
  struct package *front;
  front = NULL;

  while (1){    
    if (packetsSent == totalNumPackets){ //no more packets to send
      printf("Done sending all packets!\n");
      //send fin packet
      struct packet last;
      last.type = 2;
      last.size = 0;
      sendto(socketfd, &last,sizeof(last),0,(struct sockaddr *) 
	       &clientAddress,clientLen);      
      break;
    }

    //windowSize in bytes- to find number of packets can send, divide windowSize by packet size
    int packetWindowSize = windowSize / PACKET_SIZE;

    if (packetsSentTemp < packetWindowSize){ //send packet
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
	pack->acked = 0;
	pack->p.timesRepeated = timesRepeated;
	free(outgoing);

	if (front == NULL){ //first packet in window
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
	  point->next = pack; 
	  if (pack->p.seqNum == (maxPos - PACKET_SIZE)){
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
	sentSuccessful(pack->p, timesRepeated, maxPackets);
	gettimeofday(&(pack->startTime),NULL); //set time for packet just sent
	//if hit sequence number limit, return to 0
	if (sequenceNum >= SEQNUM_LIM){
	  sequenceNum = 0;
	  timesRepeated += 1;
	}

      }
    }

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
      if (e->acked != 1){ //if packet hasn't been acked yet then check time
	elapsedTime = (curr.tv_sec - e->startTime.tv_sec)*1000.0;
	elapsedTime += (curr.tv_usec - e->startTime.tv_usec)/1000.0;
	double leftD = tOutD - elapsedTime;
	if (leftD < 0){ //packet timed out, resend
	  sendto(socketfd,&e->p,sizeof(e->p),0,(struct sockaddr *)
		 &clientAddress,clientLen);
	  retransmitSuccessful(e->p,timesRepeated,maxPackets); 
	  //reset timer for earliest
	  gettimeofday(&(e->startTime),NULL); //set time for packet just sent
	  alreadyRetransmitted = 1;
	  break;
	}
	if (leftD < minTimeLeft){
	  minTimeLeft = leftD;
	  earliest = e; 
	}
      }
      e = e->next;
    }
    printf("\nMin Time Left Until Next Time Out: %f ms\n",minTimeLeft);

    if (alreadyRetransmitted == 0){ //didn't already retransmit
      struct timeval left;
      double tv = (minTimeLeft/1000)*1000000; 
      left.tv_sec = 0;
      left.tv_usec = tv;

      int received = select(socketfd+1,&fileSet,NULL,NULL,&left);
      if (received < 1){
	//retransmit earliest sent packet
	sendto(socketfd,&earliest->p,sizeof(earliest->p),0,(struct sockaddr *)
	       &clientAddress,clientLen);
	retransmitSuccessful(earliest->p,timesRepeated,maxPackets); 
	gettimeofday(&(earliest->startTime),NULL); //set time for packet just sent	
	continue;
      }
    
      if (recvfrom(socketfd,&incoming,sizeof(incoming),0,(struct sockaddr *) 
		   &clientAddress, &clientLen) >=0){ 
	if (incoming.type == 1){ //RECEIVE ACK
	  int packetNumber = (incoming.seqNum / PACKET_SIZE) + (timesRepeated*maxPackets);

	  if (corruptOrLossSimulator(lossprob)){//packet lost
	    printf("PACKET #%d LOST\n", packetNumber);
	    continue;
	  }
	  if (corruptOrLossSimulator(corrprob)){ //packet corrupted, don't ACK
	    printf("Packet #%d Corrupted!\n", packetNumber);
	    continue;
	  }

	  if (incoming.seqNum % PACKET_SIZE != 0){ //last packet size different
	    packetNumber+=1;
	  }
	  printf("\nRECEIVED AN ACK for Packet with SeqNum #%d\n",incoming.seqNum);


	  if (incoming.timesRepeated < 0){
	      incoming.timesRepeated = 0;
	    }
	  //NEED TO CHANGE THIS STATEMENT IN CASE LAST PACKET
	  if (front->p.seqNum == incoming.seqNum - PACKET_SIZE && front->p.timesRepeated == incoming.timesRepeated){ //front always min pos

	      packetsSent++;
	      packetsSentTemp--; //decrement so can shift window

	      minPos = front->p.seqNum + PACKET_SIZE; //shift window
	      maxPos = maxPos + PACKET_SIZE; 

	      if (front->next != NULL){ //if have following packets 
		struct package *point = front;
		front->next->isMinPos = 1;
		front = front->next;
		free(point); //remove acked package 

		//check that later packets not already acked
		struct package *pA = front;
		int nextSeqNum = 0;
		while (pA != NULL){
		  if (pA->acked == 1){
		    //remove from list
		    packetsSentTemp--;
		    minPos = front->p.seqNum + PACKET_SIZE; 
		    maxPos = maxPos + PACKET_SIZE; 		
		    struct package *tempHolder = pA;
		    nextSeqNum = pA->p.seqNum + PACKET_SIZE;
		    if (pA->next != NULL){
		      pA->next->isMinPos = 1;
		    }
		    pA = pA->next;
		    free(tempHolder);
		  }
		  else{
		    break;
		  }
		}
		front = pA;
	      }
	      else{
		front = NULL;
	      }
	  }
	  else{
	    //not the first packet in list
	    struct package *point = front;
	    int count = 1;
	    //find matching packet and ACK
	    if (incoming.timesRepeated < 0){
	      incoming.timesRepeated = 0;
	    }
	    while (point != NULL){
	      if (point->p.seqNum == incoming.seqNum -PACKET_SIZE){ //found match
		if(point->p.timesRepeated == incoming.timesRepeated){
		  packetsSent++;
		  point->acked = 1;
		  break;
		}
	      }

	      //last packet seqNum
	      int maxSN = fileSize;
	      int te = SEQNUM_LIM * timesRepeated;
	      if (incoming.seqNum == maxSN -te){
		  //end of file so acknowledge
		struct package *last = front;
		while (last->next != NULL){
		  last = last->next;
		}
		last->acked = 1;
		packetsSent++;
		break;
	      }
	      point = point->next;
	      count +=1;
	    }
 
	  }
	}

      } 
    }
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

void sentSuccessful(struct packet p, int timesRepeated,int maxPackets){
  //printf("~~~~~~~~~~~~~~~~~~~~~~\n");
  int packNum = ((p.seqNum/PACKET_SIZE) + 1) + (timesRepeated*maxPackets);
  printf("\nSuccessfully sent Packet SeqNum #%d!\n",p.seqNum);
  printf("Packet size: %d\n", p.size);
  if (p.type == 3){
    printf("Packet type: data\n");
  }
  //printf("~~~~~~~~~~~~~~~~~~~~~~~\n");
}

void retransmitSuccessful(struct packet p, int timesRepeated, int maxPackets){
  //printf("~~~~~~~~~~~~~~~~~~~~~~\n");
 int packNum = ((p.seqNum/PACKET_SIZE) + 1) + (timesRepeated*maxPackets);
  printf("\nSuccessfully Resent Packet SeqNum #%d!\n",p.seqNum);
  printf("Packet size: %d\n", p.size);
  if (p.type == 3){
    printf("Packet type: data\n");
  }
  //printf("~~~~~~~~~~~~~~~~~~~~~~~\n");
}

int corruptOrLossSimulator(double probability){
  double corrupt_or_loss = rand() / (double) RAND_MAX;
  if (corrupt_or_loss <= probability){
    return 1; //corruption or loss occured
  }
  else{
    return 0;
  }
}
