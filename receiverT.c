#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include "packet.c"

#define BUFSIZE 1024
#define FILENAMESIZE 512

/* 
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(0);
}

//simulates if a packet is corrupt or lost based off its probability
int corrupt_loss_simulation(double probability)
{
	double corrupt_or_loss = rand() / (double) RAND_MAX;
	if (corrupt_or_loss < probability)
	{
	  //printf("corruption\n");
		return 1;
	}
	else
	{
	  //printf("no corruption\n");
		return 0;
	}
}

int main(int argc, char **argv) {
    int sockfd, portno, n;
    int serverlen;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    char *hostname;
    char *filename;
    struct packet p_in;
    struct packet p_out;
    char buf[BUFSIZE];
    FILE *file;

    /* check command line arguments */
    if (argc != 6) {
       error("Invalid arguments");
       exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    filename = argv[3];
    double prob_corrupt = atof(argv[4]);
    double prob_loss = atof(argv[5]);

    if (portno < 0)
    {		
    	error("Error: Port number must be positive");
    }	
    if(prob_corrupt > 1 || prob_corrupt < 0 || prob_loss < 0 || prob_loss > 1)
    {
    	error("Error: Invalid probabilities for corruption and loss");
    }	

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        error("ERROR opening socket");
    }

    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(hostname);
    if (server == NULL) 
    {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(1);
    }

    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(portno);
    serverlen = sizeof(serveraddr);

    //packet is being requested
    bzero(&p_out, sizeof(p_out));
    p_out.type = 0;
    p_out.seqNum = 0;
    p_out.size = strlen(filename);
    printf("size: %d\n",p_out.size);
    strcpy(p_out.data,filename); //+1

    //send request message to server
    printf("CLIENT: Sending request for file: %s\n", p_out.data);
    n = sendto(sockfd, &p_out, sizeof(p_out), 0, (struct sockaddr*) &serveraddr, serverlen); //send to the socket
    if (n < 0){ 
         error("ERROR writing to socket");
	 exit(1);
    }
    printf("CLIENT: Request for file sent. Waiting for server\n");
    printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
	
    //ACK response packet
    int current_seqNum = 0;
    bzero(&p_out, sizeof(p_out));
    p_out.type = 1;
    p_out.seqNum = current_seqNum;
    p_out.size = 0;

	
    char n_filename[FILENAMESIZE];
    strcpy(n_filename, "tn_");
    strcat(n_filename, filename);
    file = fopen(n_filename, "w+");
    if (file == NULL)
    {
      error("Error opening file for writing");
    }
	
    int timesRepeated = 0;
    int maxPackets = SEQNUM_LIM/PACKET_SIZE;
    struct packet * ackRecvPacketsBuffer = malloc((maxPackets+1) * sizeof(struct packet));
    printf("Maxpackets: %d", maxPackets);
    int packetIndex = 0;
    int current_index = 1;
    int remainder = 0;
    int check = 0;
    int wroteToFile = 0;

    while(1)
    {
      n = recvfrom(sockfd, &p_in, sizeof(p_in), 0, (struct sockaddr*) &serveraddr, &serverlen);
      if (n < 0)
      {
	error("ERROR in recvFrom");
      }	
      //now check for packet loss or corruption
      else
      {
	if (corrupt_loss_simulation(prob_corrupt)) //means corrupted packet
	{
	  printf("CLIENT: Packet Corrupted: seqNum: %d\n", p_in.seqNum);
	  continue;
	}
	else //check for loss
        {
	  if(corrupt_loss_simulation(prob_loss)) //means packet loss
	  {
	    printf("CLIENT: Packet Lost: seqNum: %d\n", p_in.seqNum);
	    continue;
	  }
	}
      }

      if(p_in.type == 2) //means final packet to acknowledge a close
      {
	printf("\nCLIENT: Received FIN packet\n");
	break;
      }

      //else //no packet loss or corruption
      //{
      if(p_in.type == 3) //means this is a data packet
      {
	  printf("\nCLIENT: Received data packet\n");
	  int packetNum = ((p_in.seqNum/PACKET_SIZE)+1) + (timesRepeated *maxPackets);
	  printf("Received Packet #%d\n",packetNum);
	  printf("(Type: %d, seq: %d, size: %d)\n", p_in.type, p_in.seqNum, p_in.size);
				
	  //getting the packetNumber and setting as the index
	  packetIndex = ((p_in.seqNum/PACKET_SIZE)+1) % maxPackets;

	  if (packetIndex == 0) //if multiple of maxPackets, need to know which one
	    {
	      packetIndex = maxPackets;
	    }
	  printf("currentIndex: %d\n", current_index);
	  printf("PacketIndex: %d\n", packetIndex);
	  printf("Current seqnum :%d\n",current_seqNum);
	  //printf("timesRepeated: %d\n", timesRepeated);
	
	  ackRecvPacketsBuffer[packetIndex].seqNum = p_in.seqNum;
	  ackRecvPacketsBuffer[packetIndex].size = p_in.size;
	  strcpy(ackRecvPacketsBuffer[packetIndex].data, p_in.data);
				
	  //fwrite(ackRecvPacketsBuffer[packetIndex].data,1,ackRecvPacketsBuffer[packetIndex].size,file)
	  //printf("sizePacket: %d\n", p_in.size);
	  //printf("sizePacketArray: %d\n", ackRecvPacketsBuffer[packetIndex].size);
				
	  //write packet data to file if in correct order
	  if(current_seqNum == ackRecvPacketsBuffer[current_index].seqNum)
	  {
	      //printf("sizeCurrentArray: %d\n", ackRecvPacketsBuffer[current_index].size);
	      fwrite(ackRecvPacketsBuffer[current_index].data,1,ackRecvPacketsBuffer[current_index].size,file);
	      wroteToFile++;
	      printf("Wrote to file %d\n", wroteToFile);
	      current_index++;
								
	      int size = ackRecvPacketsBuffer[packetIndex].size;
	      if (size != PACKET_SIZE){
		printf("last packet to be sent\n");
	      }
	      current_seqNum = current_seqNum + PACKET_SIZE;
	      //current_seqNum = current_seqNum + size;
	      remainder = SEQNUM_LIM - current_seqNum;	

	      if( remainder < PACKET_SIZE || current_index > maxPackets) //reset buffer when full
		{
		  printf("Resetting buffer because it is full\n");
		  check = 1;
		  current_index = 1;
		  current_seqNum = 0;
		  int i;
		  for (i = 1; i < maxPackets+1; i++)
		    {
		      ackRecvPacketsBuffer[i].seqNum = -1;
		      ackRecvPacketsBuffer[i].size = -1;
		      memset(ackRecvPacketsBuffer[i].data, 0, strlen(ackRecvPacketsBuffer[i].data));
		    }
		}
	  }

	  else //out of order packet
	  {
	    printf("\nPacket with seq: %d out of order buffered in bufferArray", p_in.seqNum);
	    printf("\nExpected Packet with seq: %d to be written to the file next. Will wait to write to file until correct packet comes in", current_seqNum);
	  }
      }

      else //means no data in packet
      {
	printf("CLIENT: Received non-data packet: seq # = %d\n", p_in.seqNum);
	continue;
      }
      //}
		
      //sending ACK packet
      p_out.seqNum = p_in.seqNum + p_in.size;
      n = sendto(sockfd, &p_out, sizeof(p_out),0, (struct sockaddr*) &serveraddr, serverlen); //send to the socket
      if (n < 0)
	{
	  error("ERROR sending ACK packet");
	}      
      int packetNumber = ((p_in.seqNum/PACKET_SIZE)+1) + (timesRepeated *maxPackets);
      printf("\nCLIENT: sent ACK Packet #%d (type: %d, seq: %d, size: %d)\n", packetNumber,p_out.type, p_out.seqNum, p_out.size);
		
    }

    printf("curr seq num: %d\n", current_seqNum);
    int lastIndex = ((current_seqNum/PACKET_SIZE)+1) % maxPackets;
    while(ackRecvPacketsBuffer[lastIndex].seqNum != 0){
      printf("printing packet #%d\n", lastIndex);
      fwrite(ackRecvPacketsBuffer[lastIndex].data,1,ackRecvPacketsBuffer[lastIndex].size,file); 
      lastIndex++;
    }
    /*int last = current_seqNum + PACKET_SIZE;
    int lastIndex = ((current_seqNum/PACKET_SIZE)+1) % maxPackets;
    printf("list seqnum and packet index: %d, %d\n",last,lastIndex);
    printf("sizeeee: %d\n",ackRecvPacketsBuffer[lastIndex+1].size);
    //packetIndex = ((p_in.seqNum/PACKET_SIZE)+1) % maxPackets;
    if (ackRecvPacketsBuffer[lastIndex+1].size % PACKET_SIZE != 0){
      printf("weird size packet\n");
      fwrite(ackRecvPacketsBuffer[lastIndex+1].data,1,ackRecvPacketsBuffer[lastIndex+1].size,file);
      
      }*/

    int i;
    for (i = 1; i < maxPackets+1; i++){
      printf("%d: seqNum: %d\n",i,ackRecvPacketsBuffer[i].seqNum);
      printf("size: %d\n",ackRecvPacketsBuffer[i].size);
      //memset(ackRecvPacketsBuffer[i].data, 0, strlen(ackRecvPacketsBuffer[i].data));
    }
	
    //Send FIN packet and close client/socket
    bzero((char *) &p_out, sizeof(p_out));
    p_out.type = 2;
    p_out.seqNum = current_seqNum;
    p_out.size = 0;
    n = sendto(sockfd, &p_out, sizeof(p_out),0, (struct sockaddr*) &serveraddr, serverlen); //send to the socket
    if (n < 0)
	{
		error("ERROR sending FIN packet");
	}		
     
    printf("\nClosing file, client, and socket\n");
    close(sockfd); //close socket
    fclose(file);
    
    return 0;
}
