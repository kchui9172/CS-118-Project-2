//packet
#include <time.h>
#define PACKET_SIZE 10 //should by 1024
#define TOUT_SEC 1
#define TOUT_USEC 0

struct packet{
  int type;  //0 = request, 1 = ACK, 2 = FIN, 3 = data
  char data[PACKET_SIZE];
  int size;
  int seqNum;
};


struct package{
  struct packet p; //packet being sent
  int acked; // 0 if false, 1 if true
  int isMinPos;
  int isMaxPos;
  struct timeval startTime;
  struct package* next;

  
};
