/* 
 * udpclient.c - A simple UDP client
 * usage: udpclient <host> <port>
 */
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
	  printf("corruption\n");
		return 1;
	}
	else
	{
	  printf("no corruption\n");
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
    printf("sizeeee: %d\n",p_out.size);
    strcpy(p_out.data,filename); //+1
    //memcpy(p_out.data,filename,p_out.size);

    //send request message to server
    printf("CLIENT: Sending request for file: %s\n", p_out.data);
    n = sendto(sockfd, &p_out, sizeof(p_out), 0, (struct sockaddr*) &serveraddr, serverlen); //send to the socket
    if (n < 0){ 
         error("ERROR writing to socket");
	 //add something to fix if error
    }
    printf("CLIENT: request for file sent. Waiting for server\n");
	
    //ACK response packet
    int current_seqNum = 0;
    bzero(&p_out, sizeof(p_out));
    p_out.type = 1;
    p_out.seqNum = current_seqNum;
    p_out.size = 0;
	
    //Packet ACK recv buffer with size 30 (30 is max seqNum we will have)
    struct packet ackRecvPacketsBuffer[30];
    int packetIndex = 0;
    struct packet *currentLookUp = &ackRecvPacketsBuffer[0];
	
    char n_filename[FILENAMESIZE];
    strcpy(n_filename, "tn_");
    strcat(n_filename, filename);
    file = fopen(n_filename, "w+");
    if (file == NULL)
    {
      error("Error opening file for writing");
    }
	
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
			printf("CLIENT: Received FIN packet\n");
			break;
		}
		else //no packet loss or corruption
		{
		  //printf("type: %d\n",p_in.type);
			if(p_in.type == 3) //means this is a data packet
			{
				printf("CLIENT:Received data packet\n");
				printf("Received Packet #%d\n",((p_in.seqNum/PACKET_SIZE)+1));
				printf("(Type: %d, seq: %d, size: %d)\n", p_in.type, p_in.seqNum, p_in.size);
				
				//storing the packet in correct index of buffer
				packetIndex = p_in.seqNum;
				if (packetIndex > 30)
				{
					error("ERROR in seqNum. Greater than max size");	
				}
				//store the current packet we recieved in the recieve buffer
				ackRecvPacketsBuffer[packetIndex] = p_in;
				
				//write packet data to file if in correct order
				//make sure the seqNum we currently on is the same as the seqNum in the buffer index we are looking at
				//if not we have out of order packet
				if(currentLookUp != NULL && current_seqNum == currentLookUp->seqNum )
				{
					fwrite(currentLookUp->data,1,currentLookUp->size,file); //will be conditional
			 	                                    //depends on if in order
					currentLookUp++; //increment the pointer of the buffer to the next element
					current_seqNum++; //incrment our current seqNum we should be expecting to write to file
					
					if(current_seqNum > 30) //reset buffer when full
					{
						currentLookUp = &ackRecvPacketsBuffer[0] ;
						current_seqNum = 0;
						//int i;
						//for (i = 0; i < 30; i++)
						//{
						  //ackRecvPacketsBuffer[i] = NULL;
						  //bzero(ackRecvPacketsBuffer[i],sizeof(struct packet));
						//}
					}
					
				}
				else //out of order packet
				{
					printf("Packet with seq: %d out of order buffered in bufferArray", p_in.seqNum);
					printf("Expected Packet with seq: %d to be written to the file next. Will wait to write to file until correct packet comes in", current_seqNum);
				}
				
				
				//printf("message: %s\n",p_in.data);
				p_out.seqNum = p_in.seqNum + p_in.size;
				//p_out.seqNum = current_seqNum+PACKET_SIZE;
				//current_seqNum+=p_in.size;
				//write data to file
			}
			else //means no data in packet
			{
				printf("CLIENT: Received non-data packet: seq # = %d\n", p_in.seqNum);
	  
                continue;
			}
		}
		
		//sending ACK packet
		n = sendto(sockfd, &p_out, sizeof(p_out),0, (struct sockaddr*) &serveraddr, serverlen); //send to the socket
		if (n < 0)
		{
			error("ERROR sending ACK packet");
		}	
		int packetNumber = (p_in.seqNum/PACKET_SIZE)+1;
		printf("CLIENT: sent ACK Packet #%d (type: %d, seq: %d, size: %d)\n", packetNumber,p_out.type, p_out.seqNum, p_out.size);
		
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
     
    printf("Closing file, client, and socket\n");
    close(sockfd); //close socket
    fclose(file);
    
    return 0;
}
