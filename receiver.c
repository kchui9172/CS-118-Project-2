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
    double corrupt = atof(argv[4]);
    double loss = atof(argv[5]);

    if (portno < 0)
    {		
    	error("Error: Port number must be positive");
    }	
    if(corrupt > 1 || corrupt < 0 || loss < 0 || loss > 1)
    {
    	error("Error: Invalid probabilities for corruption and loss");
    }	

    printf("hello\n");
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
    printf("its me\n");
    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(portno);
    serverlen = sizeof(serveraddr);
    printf("i was wondering\n");

    //packet is being requested
    bzero(&p_out, sizeof(p_out));
    p_out.type = 0;
    p_out.seqNum = 0;
    p_out.size = strlen(filename);
    printf("if after all this time\n");
    strcpy(p_out.data,filename)+1;
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
	
	char n_filename[FILENAMESIZE];
	strcpy(n_filename, "n_");
	strcat(n_filename, filename);
	file = fopen(n_filename, "w+");
	if (file == NULL)
	{
		error("Error opening file for writing");
	}
	
	//right now assume no corruption or loss
	while(1)
	{
		n = recvfrom(sockfd, &p_in, sizeof(p_in), 0, (struct sockaddr*) &serveraddr, &serverlen);
		if (n < 0)
		{
			error("ERROR in recvFrom");
		}			
		if(p_in.type == 2) //means final packet to acknowledge a close
		{
			printf("CLIENT: Received FIN packet");
			break;
		}
		else //no packet loss or corruption
		{
		  printf("type: %d\n",p_in.type);
			if(p_in.type == 3) //means this is a data packet
			{
				printf("CLIENT:Received data packet\n");
				printf("Received Packet (type: %d, seq: %d, size: %d)\n", p_in.type, p_in.seqNum, p_in.size);
				//printf("message: %s\n",p_in.data);
				p_out.seqNum = current_seqNum;
			}
			else //means no data in packet
			{
				printf("CLIENT: Received non-data packet: seq # = %d\n", p_in.seqNum);
                		continue;
			}
		}
		
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
