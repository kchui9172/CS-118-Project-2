#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h> //for socket
#include <netinet/in.h> //for sockaddr_in
#include <string.h> //for memset
#include <sys/types.h>  //for bind
#include <arpa/inet.h>
#include "packet.c"

//sample call: sender <portNum> CWnd Pl Pc
void printPacket(struct packet toPrint);
void sendData(int socketfd, struct sockaddr_in clientAddress, 
	      socklen_t clientLen,int windowSize,struct packet incoming);
struct fileInfo readFile(char* fName,int socketfd, struct sockaddr_in 
			 clientAddress, socklen_t clientLen);
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
    printf("Received a packet!\n");
    //here print packet
    printPacket(incoming);

    //Incoming packet is request
    if (incoming.type == 0){
      sendData(socketfd, clientAddress, clientLen, windowSize, incoming);
    }
    //if (incoming.type == 2){
      //end of transaction
    //}  
  }
  close(socketfd);
  return 0;
}

void printPacket(struct packet toPrint){
  printf("Packet Seq Num: %d\n",toPrint.seqNum);
  char* packType;
  if (toPrint.type == 0){
    packType = "request";
  }
  else if (toPrint.type == 1){
    packType = "ACK";
  }
  else if (toPrint.type == 2){
    packType = "FIN";
  }
  else{
    packType = "data";
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
    //set outgoing packet: FIN and seqNum = 0 to show that error
    outgoing.type = 2;
    outgoing.size = 0;
    outgoing.seqNum = 0;
    if (sendto(socketfd,&outgoing,sizeof(outgoing),0,(struct sockaddr *) 
	&clientAddress,clientLen)<0){
      printf("Error with sending FIN to receiver\n");
    }
  }
  
  //find size of file
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
  f.size = sourceSize;
  f.buffer = buffer;
  return f;
}

void sendData(int socketfd, struct sockaddr_in clientAddress, 
	      socklen_t clientLen,int windowSize,struct packet incoming){
  struct packet outgoing;
  char* fileName = incoming.data;
  printf("Requested file:%s\n",fileName);
  char* fName = incoming.data;
  struct fileInfo info = readFile(fName, socketfd,clientAddress,clientLen);
  int fileSize = info.size;
  char* buffer = info.buffer;
  
  //divide file into packets of max size 1000 bytes
  int numPackets = fileSize/1024; 
  if (fileSize % 1024 > 0){
    numPackets++; //because need one more packet for extra data
  }

  //send packet to receiver saying how many packets will be sent
  printf("Sending number of packets that will be sent\n");
  outgoing.type = 0;
  outgoing.size = numPackets;
  sendto(socketfd,&outgoing,sizeof(outgoing),0,(struct sockaddr *) 
	     &clientAddress, clientLen);
      
  //start sending packets
  //need way of keeping track of smallest/largest position of packets sent
  //need way to correspond timer to each packet sent 
  //need to know if window at end and all sent
  int packetsSent = 0;
  int tempPos = 0;
  int seqNumber = 0 ;
  struct packet out;

  while (packetsSent < numPackets){
    //send windowSize number of packets
    int i;
    int* sentStatus;
    int* ackReceived;
    sentStatus = malloc(sizeof(int)*windowSize);
    ackReceived = malloc(sizeof(int) * windowSize);
    int minPos = 0;
    int maxPos = windowSize - 1;

    for (i = 0; i < windowSize; i++){
      char message[1024];
      int messageSize = 1024;
      if (fileSize - tempPos <1024){
	messageSize = fileSize - tempPos;
      }
      bzero(message,1024);
      memcpy(message,buffer+tempPos, messageSize);
      out.data = (char *)message;
      out.size = sizeof(message);
      out.type = 3;
      out.seqNum = seqNumber;
      sentStatus[i] = sendto(socketfd,&out, sizeof(out),0,(struct sockaddr *)
			     &clientAddress,clientLen);
      ackReceived[i] = 0; //default = 0, meaning no ack yet
      seqNumber += 1024;
      tempPos += 1024;
      //print out what is sending- DATA packet of size x ...
    }

    //here can check value of sent to see if lost or corrupted
    /*for(i = 0; i < windowSize; i++){
      if (sentStatus[i] == 0){
	//lost
      }
      else if (setnStatus[i] = 1){
	//corrupted
      }
      else{
	//packet sent
      }
    }*/
    
    int packetsSentTemp = windowSize;
    int acksReceived = 0;
    while (acksReceived < packetsSentTemp && acksReceived < windowSize){
      //check for acks
      fd_set inSet;
      int received;
      struct timeval timeout;
      struct packet incoming; 

      //need timer for each packet
      timeout.tv_sec = 5;//change values here
      timeout.tv_usec = 10;

      FD_ZERO(&inSet);
      FD_SET(socketfd, &inSet);
      received = select(socketfd+1,&inSet, NULL,NULL,&timeout);
      if (received < 1){
	printf("Timed out when waiting for ACK\n");
	//resend
      }
      if (recvfrom(socketfd,&incoming, sizeof(incoming),0,(struct sockaddr*) 
		   &clientAddress,&clientLen) <0){
	printf("issue with receiving packet\n");
      }
      //ACK for smallest in window
      if (incoming.seqNum == minPos * 1024){
	//send another packet
	//if no packet to send then{
	//packetsSentTemp--;
	//acksReceived++;
	//continue;
	//}
       //shift window (make sure not at end of data to send)
       packetsSent++;
       continue;
      }

      else{
	// ack for some packet within window
	packetsSentTemp--;
	acksReceived++;
	packetsSent++;
	continue;
      }
    }
  }
  free(buffer);
  //need to free all dynamically allocated variables
}


//To do:
  //divide up file into 1k packets and add header-seq number, dest/send port
  //print message of sending - DATA packet, sequence number, corrupted/not
  //timer value
  //seq number and window size give in unit of bytes -max seq #= 30



  //Pseudocode
  /*
    int packetsSent = 0;
    while (packetsSent < numPackets){
      //send windowSize number of packets
      int packetsSentTemp = windowSize;
      int acksReceived = 0;
      while (acksReceived < packetsSentTemp && acksReceived <windowSize){
         check for acks
	 if ack for smallest in window
	    send another packet 
	       if not other packet to send
	         packetsSentTemp--;
		 acksReceived++;
		 continue
	    shift window (make sure not at end of data to send)
	    packetsSent++;
	    continue
	 if ack wasn't for smallest window
	    packetsSentTemp--;
	    acksReceived++;
	    packetsSent++;
	    continue;
	 if timeout
	    resend packet
      }
    }
   */
