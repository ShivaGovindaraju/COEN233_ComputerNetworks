/*
 Ryan Schulz

 Assignment 1 Server
 
 Used this resource for help setting up socket stuff:
 https://www.cs.rutgers.edu/~pxk/417/notes/sockets/index.html
 
 If port 21234 is used, you might have to change it to a local port that is open on your device
 
 Notes/Assumptions: Server utilizes a counter to verify packet sequence integrity. After 2.5 seconds of not receiving a new packet, it resets that packet counter and displays a message. If you run two different transmissions within 2.5 seconds, it will error out due to the packet counter not resetting.
 
 Please let the packet counter reset between testing different error cases.
 
 The options to modify packets and induce errors can be seen on line 74 of the client.c file
 
 I assumed when the client received an error from the server, it would display the respective error and quit. It does not attempt to retransmit the message.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <stdbool.h>

#define SERVICE_PORT 21234    /* hard-coded port number */
#define BUFSIZE 2048

#define PACKETS 5
#define START_ID 0xFFFF // packet start ID
#define END_ID 0xFFFF // packet end ID
#define CLIENT_ID 0x52
#define CLIENT_MAX_ID 0xFF // max size of client id
#define LENGTH_MAX 0xFF //max length
#define DATA 0xFFF1
#define ACK 0xFFF2 //acknowledge
#define REJ 0xFFF3 //reject
#define REJ_SUB1 0xFFF4 // packer sequence messed up
#define REJ_SUB2 0xFFF5 // packet length mismatch
#define REJ_SUB3 0xFFF6 // end of packet not recv
#define REJ_SUB4 0xFFF7 //duplicate packet

//packets received from client
typedef struct data_packet {
    short start_id;
    char client_id;
    short data;
    char seg_num;
    char length;
    char payload[255];
    short end_id;
} data_packet;

//return packets sent to server
typedef struct ret_packet{
    short start_id;
    char client_id;
    short type;
    short rej_sub;
    char seg_num_rec;
    short end_id;
} ret_packet;



int main(int argc, char **argv)
{
    struct sockaddr_in myaddr;    // our address
    struct sockaddr_in remaddr;    // remote address
    socklen_t addrlen = sizeof(remaddr);        // length of addresses
    int recvlen; //# bytes received
    int poll_result; //result from poll
    int fd;  // socket created
    int prev_seg = -1; //initial seg = -1, to compare to first packet seg_num 0.
    int current_seg;
    data_packet data;
    ret_packet response;
    
    // create a UDP socket
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Server: socket error:\n");
        return 0;
    }
    
    // set up polling for counter reset to assume a new client connection
    struct pollfd pfdsock;
    pfdsock.fd = fd;
    pfdsock.events = POLLIN;
    
    
    
    // bind the socket to local ip address and a specific port
    memset((char *)&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    myaddr.sin_port = htons(SERVICE_PORT);
    
    if (bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
        perror("Server: Bind error:\n");
        return 0;
    }
    
    
    
    
    printf("\n*********************************************************************\n");
    printf("\nServer: Waiting on port %d for client connection!\n\n", SERVICE_PORT);
    printf("\n*********************************************************************\n\n");
    
    /*
     no need for time-out prior to first connection
     this just prevents it from printing an unnecessary message
     */
    bool firstloop = true;
    
    
    
    // now loop, receiving data, printing any errors, and sending back packet to client
    for (;;) {
        
        
        /*
            If not the first loop, reset packet counter after 2.5 seconds
            This is assuming a new client session will connect
            packet counter must be reset for error detection
        */
        if(!firstloop){
            poll_result = poll(&pfdsock,1,2500);
            if (poll_result == 0){
                printf("\n*********************************************************************\n");
                printf("\nServer: Connection timed out. Resetting received packet counter.\n");
                printf("\nWaiting on port %d for a new client connection!\n", SERVICE_PORT);
                printf("\n*********************************************************************\n\n");
                prev_seg = -1;
            }
        }
        
        
        /*
            Receive packet from client
         */
        recvlen = recvfrom(fd, &data, sizeof(data_packet), 0, (struct sockaddr *)&remaddr, &addrlen);
        if (recvlen > 0) {
            printf("Server: Received message: \"%s\" (%d bytes)\n", data.payload, recvlen);
        }
        else{
            printf("Server: Receive error!\n");
        }
        
        
        
        /*
            after first loop, allow timer for packet_count reset to happen
         */
        firstloop = false;
        
        
        
        //Initialize common attributes in packet to send to client
        response.start_id = START_ID;
        response.end_id = END_ID;
        response.type = REJ; //Assume REJ initially
        response.client_id = CLIENT_ID;
        response.seg_num_rec = data.seg_num;
        
        
        current_seg = data.seg_num;
        
        
        //Error/Ack decision logic
        if (prev_seg == current_seg){
            //repeat packet
            response.rej_sub = REJ_SUB4;
            printf("Server: ERROR. Duplicate packets. Was expecting seg_num %d but receieved seg_num %d again.\n\n", prev_seg+1, current_seg);
        }
        else if((prev_seg + 1) != current_seg){
            response.rej_sub = REJ_SUB1;
            //out of order, need retransmission
            printf("Server: ERROR. Out of order packets. Was expecting seg_num %d but received seg_num %d.\n\n", prev_seg+1, current_seg);
        }
        else if(data.end_id != (short)END_ID){
            //end id not received properly
            response.rej_sub = REJ_SUB3;
            printf("Server: ERROR. Packet %d has an invalid end of frame ID.\n\n", current_seg);
        }
        else if(data.length != (char)sizeof(data.payload)){
            //error in length from frame length data and payload size
            response.rej_sub = REJ_SUB2;
            printf("Server: ERROR. Packet %d has a length mismatch.\n\n", current_seg);
        }
        else{
            //No error found: Send acknowledgment
            response.type = ACK;
            response.rej_sub = 0;
            printf("Server: ACK. Packet %d acknowledged.\n\n", current_seg);
            
        }
        
        
        
        //save seg_num to compare with next packet for order integrity
        prev_seg = current_seg;
        
        //transmit err/ack packet
        if (sendto(fd, &response, sizeof(ret_packet), 0, (struct sockaddr *)&remaddr, addrlen) < 0){
            perror("Server: Sendto error:\n");
        }
        
    }
    /* server never quits */
}