/* 
 * File: sender_main.c
 * Author: 
 *
 * Created on 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include "TCP.h"
#include "List.h"
#include <sys/unistd.h>
#include <sys/fcntl.h>

//linked list to act as buffer
Node *list;

//Number of repeated ACKs, used for fast retransmitt
int repeatedACK;

//the base sequence number of sending window
int base;
//the sequence number of next unsended packet
int nextSeq;

//total number of segment in buffer(list)
int SegNum;
//number of sent but unacked segments in buffer
int SegUnAcked;
//number of segments in buffer not send yet
int SegNotSend;

//save the time that the timer is started
time_t TimerStartAt;
//control if timer is running
int IsTimerOn;

//Congestion windows size;
float CWND;

struct sockaddr_in si_other;
socklen_t s;
int slen;

void diep(char *s) {
    perror(s);
    exit(1);
}

//Send next UnSended segment in buffer.
void SendSegment(){
    TCP_Seg seg;
    if( SeqCompare( nextSeq, SeqAdd( base, WINDOW_SIZE) ) < 0 ){
        seg = GetSeg(list, nextSeq);
        sendto(s, &seg, sizeof(TCP_Seg), 0, (struct sockaddr*) &si_other, sizeof (si_other));
        SetNodeStatus(list, nextSeq, UNACKED);
        SegUnAcked = GetStatusNumber(list, UNACKED);
        SegNotSend = GetStatusNumber(list, NOTSEND);
        if(base == nextSeq){
            TimerStartAt = clock();
            IsTimerOn = 1;
        }
        nextSeq = SeqAdd(nextSeq,1);
    }
}

//Function dealing with received ACK
void ReceiveACK(TCP_Seg seg){
    //if ACK number equals to base number, repeatedACK should add by one
    if(seg.ACK == base)
            repeatedACK++;
    //if a new segment is acked
    if( SeqCompare(seg.ACK,base) > 0 ){
        repeatedACK == 0;
        if(CWND < WINDOW_SIZE/2)
            CWND += 1/CWND;
        while( SeqCompare( base, seg.ACK ) < 0 ){
            SetNodeStatus(list, base, ACKED);
            RemoveNode( &list, base );
            SegUnAcked = GetStatusNumber(list, UNACKED);
            SegNum = GetNumber(list);
            base = SeqAdd(base,1);
        }
        if(SegUnAcked != 0){
            TimerStartAt = clock();
            IsTimerOn = 1;
        }
        else
            IsTimerOn = 0;
    }
}

//Function dealing with timeout
void TimeOut(){
    if(CWND >=2)
        CWND /= 2;
    TCP_Seg seg;
    //If timeout occurs, resend WINDOW_SIZE/10 UNACKED segments from base (if exist)
    for(int i=0; i<WINDOW_SIZE/10; i++ ){
    	if( GetStatus(list, SeqAdd(base,i)) == UNACKED ){
	    	seg = GetSeg(list, SeqAdd(base,i));
	    	sendto(s, &seg, sizeof(TCP_Seg), 0, (struct sockaddr*) &si_other, sizeof (si_other));	
    	}
    }
    TimerStartAt = clock();
    IsTimerOn = 1;
    repeatedACK = 0;
}

//Add a segment to buffer
void AddSegToBuffer(TCP_Seg seg){
    AddNode(&list, seg, NOTSEND);
    SegNotSend = GetStatusNumber(list, NOTSEND);
    SegNum = GetNumber(list);
}

void GetDataFromFile(char* filename, unsigned long long *offset, unsigned long long* filesize, char* data, int* len){
    FILE *fileptr;
    fileptr = fopen(filename, "rb");  // Open the file in binary mode
    if(fileptr == NULL)
        printf("cannot open file \n");
    fseek( fileptr, (long)*offset, SEEK_SET );
    unsigned long long res = *filesize - *offset;
    if(res >= MSS){
        size_t out = fread( data, 1, MSS, fileptr);
        *len = MSS;
    }
    else{
        size_t out = fread( data, 1, res, fileptr);
        *len = res;
    }
    fclose(fileptr);
    *offset += *len;
    return;
}

int IsFileEnd(unsigned long long int offset, unsigned long long int filesize){
    if (offset < filesize)
        return 0;
    return 1;
}

void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer) {

	/* Determine how many bytes to transfer */

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");
    
    //set recvfrom to be unblock, or we will stuck there.
    fcntl(s, F_SETFL, O_NONBLOCK);

    memset((char *) &si_other, 0, sizeof (si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(hostUDPport);
    if (inet_aton(hostname, &si_other.sin_addr) == 0) {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }
    
    TCP_Seg seg;
    
    //Initial and Hankshake process
    srand ( time(NULL) );
    int InitSeq = rand() % MAX_SEQ;
    base = InitSeq;
    nextSeq = InitSeq;
    SegNum = GetNumber(list);
    SegNotSend = GetStatusNumber(list, NOTSEND);
    SegUnAcked = GetStatusNumber(list, UNACKED);
    CWND = 1;
    repeatedACK = 0;
    make_SYN_Seg(&seg, InitSeq, 0);
    AddSegToBuffer(seg);
    SendSegment();
    while( (recvfrom(s, &seg, sizeof(TCP_Seg), 0, (struct sockaddr*) &si_other, &slen)) == -1 ) ;
    if( (seg.ACK == base + 1) && (seg.SYN == 1) ){
        SetNodeStatus(list, base, ACKED);
        RemoveNode( &list, base );
        SegUnAcked = GetStatusNumber(list, UNACKED);
        SegNum = GetNumber(list);
        base = SeqAdd(base,1);
        puts("Successfully connected!");
    }
    else{
        puts("Fail to Handshake!");
    }
    unsigned long long int read_offset = 0; //lzy added ****
    while(1){
        //printf("base:%d nextseq:%d total:%d notsend:%d unacked:%d\n",base,nextSeq,SegNum,SegNotSend,SegUnAcked);
        //If there is no Segment waitting to be ACK and the file has ended
        if( (SegNum == 0) && IsFileEnd(read_offset,bytesToTransfer) ){
        	puts("Ending connecting......");
            make_FIN_Seg(&seg, SeqAdd(base, SegNum), 0);
            AddSegToBuffer(seg);
            SendSegment();
            while( (recvfrom(s, &seg, sizeof(TCP_Seg), 0, (struct sockaddr*) &si_other, &slen)) == -1 ) ;
            if( (seg.ACK == base + 1) && (seg.FIN == 1) ){
                SetNodeStatus(list, base, ACKED);
                RemoveNode( &list, base );
                SegUnAcked = GetStatusNumber(list, UNACKED);
                SegNum = GetNumber(list);
                base = SeqAdd(base,1);
                puts("Finish Sending!");
                break;
            }
            else{
                puts("Error in Finishing!");
                break;
            }
        }
        
        while( (SegNum < WINDOW_SIZE) && !IsFileEnd(read_offset,bytesToTransfer) ){
            char data[MSS];
            int len;
            GetDataFromFile(filename,&read_offset,&bytesToTransfer,data,&len);//should give data[] and len value
            make_Seg(&seg, SeqAdd(base, SegNum), 0, len, data);
            AddSegToBuffer(seg);
        }
        
        while( (GetStatusNumber(list, UNACKED) < CWND) && (SegNotSend != 0) ){
                SendSegment();
        }
        
        while( (recvfrom(s, &seg, sizeof(TCP_Seg), 0, (struct sockaddr*) &si_other, &slen)) != -1 ){
            ReceiveACK(seg);
        }
        
        if( IsTimerOn == 1 ){
            if( (clock() - TimerStartAt) >= CLOCKS_PER_SEC ){
            	printf("base:%d nextseq:%d total:%d notsend:%d unacked:%d CWND:%f\n",base,nextSeq,SegNum,SegNotSend,SegUnAcked, CWND);
            	puts("Timeout.");
                TimeOut();
            }
        }
        
        //if more than three repeatedACK, retransmitt immediately.(By calling timeout() function, which is intended for the same goal)
        if(repeatedACK >= 3)
            TimeOut();
    }
    
    close(s);
    return;

}

/*
 * 
 */
int main(int argc, char** argv) {

    unsigned short int udpPort;
    unsigned long long int numBytes;

   if (argc != 5) {
       fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
       exit(1);
   }
    udpPort = (unsigned short int) atoi(argv[2]);
    numBytes = atoll(argv[4]);



    reliablyTransfer(argv[1], udpPort, argv[3], numBytes);


    return (EXIT_SUCCESS);
}


