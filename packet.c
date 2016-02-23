//packet

#define PACKET_SIZE 1024

struct packet{
  int type;  //0 = request, 1 = ACK, 2 = FIN, 3 = data
  char data[PACKET_SIZE];
  int size;
  int seqNum;
};
