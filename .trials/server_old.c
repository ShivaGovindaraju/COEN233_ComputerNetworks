#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>
#include <math.h>
#include <arpa/inet.h>

#define NUM_PACKETS 5
#define START_ID 0xFFFF // packet start ID
#define END_ID 0xFFFF // packet end ID
//#define CLIENT_ID 0x52
#define CLIENT_MAX_ID 0xFF // max size of client id
#define LENGTH_MAX 0xFF //max length
#define DATA 0xFFF1
#define ACK 0xFFF2 //acknowledge
#define REJ 0xFFF3 //reject
#define REJ_SUB_CODE_1 0xFFF4 // packer sequence messed up
#define REJ_SUB_CODE_2 0xFFF5 // packet length mismatch
#define REJ_SUB_CODE_3 0xFFF6 // end of packet not recv
#define REJ_SUB_CODE_4 0xFFF7 //duplicate packet

#define SERVICE_PORT 21234    /* hard-coded port number */
#define BUFFER_LEN 2048

//packets received from client
typedef struct data_packet {
    short start_id;
    char client_id;
    short data;
    char seg_num;
    char length;
    char payload[LENGTH_MAX];
    short end_id;
} data_packet;

//return packets sent to server
typedef struct ret_packet{
    short start_id;
    char client_id;
    short type;
    short rej_sub;
    char seg_num;
    short end_id;
} ret_packet;


int main(int argc, char **argv)
{
    //struct sockaddr_in server_addr;    // our address
    //struct sockaddr_in client_addr;    // remote address
    struct sockaddr_in server_addr, client_addr;
    socklen_t addrlen = sizeof(client_addr);        // length of addresses
    int recvlen; //# bytes received
    int poll_ret; //result from poll
    int sock_fd;  // socket created
    //int prev_seg = -1; //initial seg = -1, to compare to first packet seg_num 0.
    int exp_pkt = 0;
    int curr_pkt;
    data_packet client_pkt;
    ret_packet server_pkt;
    
    // create a UDP socket
    /*if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Server: socket error:\n");
        return 0;
    }*/
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        fprintf(stderr, "Server Error -- Could Not Create Socket.\n");
        return -1;
    }
    
    // set up polling for counter reset to assume a new client connection
    struct pollfd newclientpoll_sockfd;
    newclientpoll_sockfd.fd = sock_fd;
    newclientpoll_sockfd.events = POLLIN;
    
    // bind the socket to local ip address and a specific port
    memset((char *)&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(SERVICE_PORT);
    
    if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        /*perror("Server: Bind error:\n");
        return 0;*/
        fprintf(stderr, "Sever Error -- Failed to Bind Socket to Local Addresses and Selected Port.\n");
        return -1;
    }
    
    /*printf("\n*********************************************************************\n");
    printf("\nServer: Waiting on port %d for client connection!\n\n", SERVICE_PORT);
    printf("\n*********************************************************************\n\n");
    */
    printf("Initializing Server.\nWaiting for Client on Port %d.\n---\n\n", SERVICE_PORT);
    
    /*
     no need for time-out prior to first connection
     this just prevents it from printing an unnecessary message
     */
    //bool firstloop = true;
    int new_server = 1;
    
    // now loop, receiving data, printing any errors, and sending back packet to client
    //for (;;) {
    while(1) {    
        
        /*
            If not the first loop, reset packet counter after 2.5 seconds
            This is assuming a new client session will connect
            packet counter must be reset for error detection
        */
        /*if(!firstloop){
            poll_ret = poll(&newclientpoll_sockfd,1,2500);
            if (poll_ret == 0){
                printf("\n*********************************************************************\n");
                printf("\nServer: Connection timed out. Resetting received packet counter.\n");
                printf("\nWaiting on port %d for a new client connection!\n", SERVICE_PORT);
                printf("\n*********************************************************************\n\n");
                prev_seg = -1;
            }
        }*/
        if (!new_server) {
            poll_ret = poll(&newclientpoll_sockfd, 1, 2500);
            if (poll_ret==0) {
                printf("---\nServer-Client Connection Timed Out. Preparing for New Client...\nWaiting for Client on Port %d.\n---\n\n", SERVICE_PORT);
                //prev_seg = -1;
                exp_pkt = 0;
            }
        }
        
        /*
            Receive packet from client
         */
        recvlen = recvfrom(sock_fd, &client_pkt, sizeof(data_packet), 0, (struct sockaddr *)&client_addr, &addrlen);
        if (recvlen > 0) {
            printf("Message Received from Client!\nMessage: \"%s\"\n", client_pkt.payload);
        } else{
            printf("Server Error -- Error in Package Reception from Client!\n");
        }
        
        /*
            after first loop, allow timer for packet_count reset to happen
         */
        //firstloop = false;
        new_server = 0;
        
        
        //Initialize common attributes in packet to send to client
        server_pkt.start_id = START_ID;
        server_pkt.end_id = END_ID;
        server_pkt.type = REJ; //Assume REJ initially
        server_pkt.client_id = client_pkt.client_id;//CLIENT_ID;
        server_pkt.seg_num = client_pkt.seg_num;
        
        curr_pkt = client_pkt.seg_num;
        
        //Error/Ack decision logic
        /*if (prev_seg == curr_pkt){
            //repeat packet
            server_pkt.rej_sub = REJ_SUB_CODE_4;
            printf("Server: ERROR. Duplicate packets. Was expecting seg_num %d but receieved seg_num %d again.\n\n", prev_seg+1, curr_pkt);
        }
        else if((prev_seg + 1) != curr_pkt){
            server_pkt.rej_sub = REJ_SUB_CODE_1;
            //out of order, need retransmission
            printf("Server: ERROR. Out of order packets. Was expecting seg_num %d but received seg_num %d.\n\n", prev_seg+1, curr_pkt);
        }
        else if(client_pkt.end_id != (short)END_ID){
            //end id not received properly
            server_pkt.rej_sub = REJ_SUB_CODE_3;
            printf("Server: ERROR. Packet %d has an invalid end of frame ID.\n\n", curr_pkt);
        }
        else if(client_pkt.length != (char)sizeof(client_pkt.payload)){
            //error in length from frame length data and payload size
            server_pkt.rej_sub = REJ_SUB_CODE_2;
            printf("Server: ERROR. Packet %d has a length mismatch.\n\n", curr_pkt);
        }
        else{
            //No error found: Send acknowledgment
            server_pkt.type = ACK;
            server_pkt.rej_sub = 0;
            printf("Server: ACK. Packet %d acknowledged.\n\n", curr_pkt);
            
        }*/
        if (curr_pkt > exp_pkt) {
            printf("ERROR: REJECT Sub-Code 1. Out-of-Order Packets. Expected %d, Got %d.\n", exp_pkt, curr_pkt);
            server_pkt.rej_sub = REJ_SUB_CODE_1;
        } else if ((char)sizeof(client_pkt.payload) != client_pkt.length) {
            printf("ERROR: REJECT Sub-Code 2. Length Mis-Match in Packet %d.\n", curr_pkt);
            server_pkt.rej_sub = REJ_SUB_CODE_2;
        } else if (client_pkt.end_id != (short)END_ID) {
            printf("ERROR: REJECT Sub-Code 3. Invalid End-of-Packet ID on Packet %d.\n", curr_pkt);
            server_pkt.rej_sub = REJ_SUB_CODE_3;
        } else if (curr_pkt < exp_pkt) {
            printf("ERROR: REJECT Sub-Code 4. Duplicate Packets. Expected %d, Got Duplicate %d.\n", exp_pkt, curr_pkt);
            server_pkt.rej_sub = REJ_SUB_CODE_4;
        } else {
            printf("Acknowledged Packet %d. Sending ACK to Client...\n\n", curr_pkt);
            server_pkt.type = ACK;
            server_pkt.rej_sub = 0x0000;
            exp_pkt++;
        }
         
        //save seg_num to compare with next packet for order integrity
        //prev_seg = curr_pkt;
        //exp_pkt++;
        
        //transmit err/ack packet
        if (sendto(sock_fd, &server_pkt, sizeof(ret_packet), 0, (struct sockaddr *)&client_addr, addrlen) < 0){
            //perror("Server: Sendto error:\n");
            fprintf(stderr, "\nServer Error -- Failed to Send Packet to Client.\n\n");
        }
        
    }
    /* server never quits */
}