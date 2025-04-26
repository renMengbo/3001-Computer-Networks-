#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Selective Repeat protocol.  Adapted from J.F.Kurose
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.2  

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for RS), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications: 
   - removed bidirectional GBN code and other code not used by prac. 
   - fixed C style to adhere to current programming style
   - added RSSelective Repeat protocol implementation
**********************************************************************/

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet */
#define SEQSPACE (WINDOWSIZE*2)      /* the min sequence space for SR must be at least windowsize * 2 */
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver  
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your 
   original checksum.  This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for ( i=0; i<20; i++ ) 
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (false);
  else
    return (true);
}


/********* Sender (A) variables and functions ************/

static struct pkt buffer[WINDOWSIZE];  /* array for storing packets waiting for ACK */
static bool acked[WINDOWSIZE];         /* array to track which packets have been ACKed */
static int windowfirst, windowlast;    /* array indexes of the first/last packet awaiting ACK */
static int windowcount;                /* the number of packets currently awaiting an ACK */
static int A_nextseqnum;               /* the next sequence number to be used by the sender */

/* called from layer 5 (application layer), passed the message to be sent to other side */
 /* A_output 应用层（5）发往传输层（4），调用了tolayer3发往网络层（3）*/
 void A_output(struct msg message)
{
  struct pkt sendpkt;
  int i;

  /* if not blocked waiting on ACK */
  if ( windowcount < WINDOWSIZE) {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for ( i=0; i<20 ; i++ ) 
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt); 

    /* put packet in window buffer */
    windowlast = (windowlast + 1) % WINDOWSIZE; 
    buffer[windowlast] = sendpkt;
    acked[windowlast] = false;  /* Mark packet as unacked*/
    windowcount++;

    /* send out packet */
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3 (A, sendpkt);

    /* start timer if first packet in window */
    if (windowcount == 1)
      starttimer(A,RTT);

    /* get next sequence number, wrap back to 0 */
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;  
  }
  /* if blocked,  window is full */
  else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}

/* called from layer 3, when a packet arrives for layer 4 
   In this practical this will always be an ACK as B never sends data.
*/
/* A_input 网络层（3）发往传输层（4） */



void A_input(struct pkt packet)
{
  int i;

  /* if received ACK is not corrupted */ 
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n",packet.acknum);
    total_ACKs_received++;

    /* mark the corresponding packet as ACKed */ 
    for (i = 0; i < WINDOWSIZE; i++) {
      int current_index = (windowfirst + i) % WINDOWSIZE;
      if (buffer[current_index].seqnum == packet.acknum) {
        acked[current_index] = true;
        break;
      }
    }

    /* slide window forward if the first packet in the window is ACKed */
    while (windowcount > 0 && acked[windowfirst]) {
      acked[windowfirst] = false; /* 重置标记*/
      windowfirst = (windowfirst + 1) % WINDOWSIZE;
      windowcount--;
      if (TRACE > 1)
        printf("Window slid forward. New window count: %d\n", windowcount);
    }

    /* start timer again if there are still more unacked packets in window */
    stoptimer(A);
    if (windowcount > 0)
      starttimer(A, RTT);
  }
  else 
    if (TRACE > 0)
      printf ("----A: corrupted ACK is received, do nothing!\n");
}

void A_timerinterrupt(void)
{
  int i;

  if (TRACE > 0)
    printf("----A: time out, resend unacked packets!\n");

  for(i = 0; i < windowcount; i++) {
    int current_index = (windowfirst + i) % WINDOWSIZE;
    if (!acked[current_index]) {
      if (TRACE > 0)
        printf ("---A: resending packet %d\n", buffer[current_index].seqnum);

      tolayer3(A, buffer[current_index]);
      packets_resent++;
    }
  }

  starttimer(A, RTT);
}



void A_init(void)
{
  int i ;
  /* initialise A's window, buffer and sequence number */
  A_nextseqnum = 0;  /* A starts with seq num 0, do not change this */
  windowfirst = 0;
  windowlast = -1;   
  windowcount = 0;
  /*int i ;  */
  for (i = 0; i < WINDOWSIZE; i++) {
    acked[i] = false;
  }
}

/********* Receiver (B)  variables and procedures ************/

static int expectedseqnum; /* the sequence number expected next by the receiver */
static int B_nextseqnum;   /* the sequence number for the next packets sent by B */
static struct pkt buffer[WINDOWSIZE];  /* array for storing out-of-order packets */
static bool received[WINDOWSIZE];      /* array to track which packets have been received */

/* 从网络层（3）收取数据到传输层（4） */
void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i;
  int index;

  /* if not corrupted */
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n",packet.seqnum);
    packets_received++;

    /* store the packet if it's within the window */
    /*int index; */
    index = (packet.seqnum - expectedseqnum + SEQSPACE) % SEQSPACE;
    if (index < WINDOWSIZE) {
      buffer[index] = packet;
      received[index] = true;
    }

    /* deliver in-order packets to the application */
    while (received[0]) {
      tolayer5(B, buffer[0].payload); /*由传输层（4）交付给应用层（5）*/

      /* shift the window forward */
      for (i = 0; i < WINDOWSIZE - 1; i++) {
        buffer[i] = buffer[i + 1];
        received[i] = received[i + 1];
      }
      received[WINDOWSIZE - 1] = false;

      expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
    }

    /* send an ACK for the received packet */
    sendpkt.acknum = packet.seqnum; 
    sendpkt.seqnum = NOTINUSE;
    for (i = 0; i < 20; i++)
      sendpkt.payload[i] = '0';/* 填充无效数据 */
    sendpkt.checksum = ComputeChecksum(sendpkt);
    tolayer3(B, sendpkt);/* 由传输层（4）回复给链路层（3）*/
  }
  else {
    if (TRACE > 0) 
      printf("----B: packet corrupted, do nothing!\n");
  }
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  expectedseqnum = 0;
  B_nextseqnum = 1;
  int i;
  for (i = 0; i < WINDOWSIZE; i++) {
    received[i] = false;
  }
}
/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)  
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}

