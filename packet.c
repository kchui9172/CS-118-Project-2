//packet

struct packet{
  int type;  //0 = request, 1 = ACK, 2 = FIN, 3 = data
  char *data;
  int size;
  int seqNum;
};
