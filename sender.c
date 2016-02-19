#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h> //for socket
#include <netinet/in.h> //for sockaddr_in
#include <string.h> //for memset
#include <sys/types.h>  //for bind
#include <arpa/inet.h>

char* dumpRequest(int sockfd);

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

  int socketfd, portNum, newSocketfd;
  struct sockaddr_in address,clientAddress;
  socklen_t clientLen;
  char inPack[1028];//shoudl be set to max packet size

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
  listen(socketfd,5);
  printf("Waiting for connection...\n");

  while(1){
    clientLen = sizeof(clientAddress);
    if (recvfrom(socketfd,inPack, sizeof(inPack),0,(struct sockaddr*) &clientAddress,&clientLen) <0){
      printf("no packet received\n");
      continue;
    }
    printf("received!\n");
    //here print packet

    /*if (!fork()){
    close(socketfd);
    char* requestedFile;
    requestedFile = dumpRequest(newSocketfd);
    close(newSocketfd);
    exit(0);
  }
  close(newSocketfd);
  }*/
  return 0;
  }
  //after receiving message from receiver of what file wanted
  //divide up file into 1k packets and add header-seq number, dest/send port
  //print message of sending - DATA packet, sequence number, corrupted/not
  //timer value

  //seq number and window size give in unit of bytes -max seq #= 30
}

char *dumpRequest(int sockfd){
  char buffer[1028];
  bzero(buffer,1028);
  int n;
  n = read(sockfd,buffer,1027);
  if (n<0){
    error("Error reading from socket\n");
  }
  printf("Requested File:\n%s\n", buffer);
}
