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

#define NUM_PACKETS 5 //num of packets to send
#define START_ID 0xFFFF // packet start ID
#define END_ID 0xFFFF // packet end ID
#define CLIENT_MAX_ID 0xFF // max size of client id
#define LENGTH_MAX 0xFF //max length
#define DATA 0xFFF1 //data
#define ACK 0xFFF2 //acknowledge
#define REJ 0xFFF3 //reject
#define REJ_SUB_CODE_1 0xFFF4 // packer sequence messed up
#define REJ_SUB_CODE_2 0xFFF5 // packet length mismatch
#define REJ_SUB_CODE_3 0xFFF6 // end of packet not recv
#define REJ_SUB_CODE_4 0xFFF7 //duplicate packet

#define SERVICE_PORT 21234    /* hard-coded port number */
#define BUFFER_LEN 2048
#define CLIENT_ID 0x42 //client ID - set it myself.

//packets sent to server
typedef struct data_packet {
    short start_id;
    char client_id;
    short data;
    char seg_num;
    char length;
    char payload[LENGTH_MAX];
    short end_id;
} data_packet;

//repsonse packets received from server
typedef struct ret_packet{
    short start_id;
    char client_id;
    short type;
    short rej_sub;
    char seg_num;
    short end_id;
} ret_packet;

/*prototype*/
//int inet_aton(const char *cp, struct in_addr *inp);
//int close(int fd);



int main(int argc, char **argv) {
    if (argc < 2) {
        printf("ERROR: Missing Arguments for determining which Client Test to run.\n");
        return -1;
    }
    int test_number = atoi(argv[1]);
    
    struct sockaddr_in client_addr, server_addr;
    int sock_fd;
    int addr_len = sizeof(client_addr);
    char buffer[BUFFER_LEN];    /* message buffer */
    int recvlen;        /* # bytes in acknowledgement message */
    //char *server_name = "127.0.0.1";    /* change this to use a different server */
    ret_packet server_pkt;
    data_packet client_pkt;
    int send_attempt;
    int poll_ret;
    int pack_num = 0;
    
    /*
     create a socket
     */
    sock_fd=socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0){
        fprintf(stderr, "Client Error -- Could Not Create Socket.\n");
        return -1;
    }
    
    /*
     bind it to all local addresses and selected port number
     */
    memset((char *)&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    client_addr.sin_port = htons(0);
    
    if (bind(sock_fd, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
        fprintf(stderr, "Client Error -- Failed to Bind Socket to Local Addresses and Selected Port.\n");
        return -1;
    }
    
    /*
      define server_addr, the address to whom we want to send messages
      the host address is expressed as a numeric IP address
      will convert that to a binary format via inet_aton
    */
    memset((char *) &server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVICE_PORT);
    /*if (inet_aton(server_name, &server_addr.sin_addr)==0) {
        fprintf(stderr, "Client Error -- Failed to Convert IP address to binary format\n");
        return -1;
    }*/
      
    /*
     setup pollfd for ack_timer
    */
    struct pollfd acktimer_sockfd;
    acktimer_sockfd.fd = sock_fd;
    acktimer_sockfd.events = POLLIN;
    
    /*
     Error-less packet setup
     */
    data_packet dp_arr[NUM_PACKETS];
    for (int i = 0; i < NUM_PACKETS; i++){
        dp_arr[i].start_id = START_ID;
        dp_arr[i].client_id = CLIENT_ID;
        dp_arr[i].data = DATA;
        dp_arr[i].end_id = END_ID;
        
        dp_arr[i].seg_num = i;
        //printf("\n%d\n", sizeof("Hello from the Client Host. Packet 1 of 5."));
        sprintf(buffer, "Hello from the Client Host. Packet %d of %d.", i + 1, NUM_PACKETS);
        strncpy(dp_arr[i].payload, buffer, LENGTH_MAX);
        dp_arr[i].length = sizeof(dp_arr[i].payload);
    }
    
    /*
     Induces errors for demo
    */
    data_packet temp;
    //printf("\n*************************************************************\n\n");
    /*switch(test_number){
        case 1: //CASE 1: Out of order
            temp = dp_arr[3];
            dp_arr[3] = dp_arr[4];
            dp_arr[4] = temp;
            printf("Client: Out of order packet transmission selected\n");
            break;
        case 2: //CASE 2: Length missmatch
            dp_arr[2].length = 0x03;
            printf("Client: Length mismatch transmission selected\n");
            break;
        case 3: //CASE 3: EOF wrong
            dp_arr[2].end_id = 0xF00F;
            printf("Client: Wrong EOF transmission selected\n");
            break;
        case 4: //CASE 4: Duplicate Packet
            dp_arr[2] = dp_arr[1];
            printf("Client: Duplicate packet transmission selected:\n");
            break;
        default:
            printf("Client: Normal transmission selected\n");
    }*/
    //printf("\n*************************************************************\n\n");
    int rand_adjust = 0;
    if (test_number == 1) {
        printf("Client Test Case 1: Out-of-Order Packets.\n\n"); //expect 3, get 4
        temp = dp_arr[3];
        dp_arr[3] = dp_arr[4];
        dp_arr[4] = temp;
    } else if (test_number == 2) {
        printf("Client Test Case 2: Mismatch in Length.\n\n"); //mismatch on 3
        while (rand_adjust == 0)
            rand_adjust = (rand() % 10) - 5;
        dp_arr[3].length = dp_arr[3].length + rand_adjust;
    } else if (test_number == 3) {
        printf("Client Test Case 3: Incorrect End of Packet ID.\n\n"); //bad eop on 3
        dp_arr[3].end_id = 0x1234;
    } else if (test_number == 4) {
        printf("Client Test Case 4: Duplicate Packets.\n\n"); //expect 3, get 2
        dp_arr[3] = dp_arr[2];
    } else {
        printf("Client is Sending Data Normally, Not Testing Error Handling.\n\n");
    }
    
    /*
     create receive and return variables
    */
    /*ret_packet server_pkt;
    data_packet data;
    int send_attempt;
    int poll_ret;
    int pack_num = 0;*/
    /*
     This loop sends the messages and awaits for feedback from server
    */
    while (pack_num < NUM_PACKETS) {
        client_pkt = dp_arr[pack_num];
        send_attempt = 1;
        printf("Client is sending packet %d to Server. Attempt %d\n", pack_num, send_attempt);

        /*if (sendto(sock_fd, &client_pkt, sizeof(data_packet), 0, (struct sockaddr *)&server_addr, addr_len)==-1) {
            perror("Client: Sendto error:\n");
            exit(1);
        }*/       
        if (sendto(sock_fd, &client_pkt, sizeof(data_packet), 0, (struct sockaddr *)&server_addr, addr_len) < 0) {
            fprintf(stderr, "Client Experienced Error in Sending Packet %d to Server.\n", pack_num);
            return -1;
        }
        
        //send_attempt = 1;
        //ack_timer implementation
        while(send_attempt < 3){
            //poll for 3 seconds, and resend up to 2 times
            poll_ret = poll(&acktimer_sockfd, 1, 3000);
            
            if(poll_ret == 0) {
                //timeout, must resend
                send_attempt++;
                /*printf("Client: No server_pkt. Retransmitting. Attempt %d out of 3\n", send_attempt);
                if (sendto(sock_fd, &client_pkt, sizeof(data_packet), 0, (struct sockaddr *)&server_addr, addr_len)==-1) {
                    perror("Client: Sendto error:\n");
                    exit(1);
                }*/
                printf("No Response from Server to Client. Attempt %d. Retransmitting...\n", send_attempt);
                if (sendto(sock_fd, &client_pkt, sizeof(data_packet), 0, (struct sockaddr *)&server_addr, addr_len) < 0) {
                    fprintf(stderr, "Client Experienced Error in Sending Packet %d to Server.\n", pack_num);
                    return -1;
                }
            } else if(poll_ret == -1) {
                //error
                /*perror("Client: Poll error:\n");
                return poll_ret;*/
                fprintf(stderr, "Client Experienced Error in Polling.\n");
                return -1;
            }
            else {
                /* now receive an server_pkt from the server */
                recvlen = recvfrom(sock_fd, &server_pkt, sizeof(ret_packet), 0, (struct sockaddr *)&server_addr, &addr_len);
                if(recvlen == -1){
                    /*perror("Client: Receive error:\n");
                    exit(1);*/
                    fprintf(stderr, "Client Experienced Error in Receiving server_pkt from Server.\n");
                    return -1;
                }
                
                //checks to make sure the start of the packet is correct -just for debugging purposes
                /*if (server_pkt.start_id != (short)START_ID){
                    printf("Client: Invalid server_pkt from server. Was expecting 0xFFFF for start_id, but received %x\n", server_pkt.start_id);
                    return -1;
                }*/
                
                if(server_pkt.type == (short)ACK){ //ACK received!
                    //printf("Server: ACK. Packet %d acknowledged.\n\n", pack_num);
                    printf("Received ACK for Packet %d from Server.\n", pack_num);
                    break;
                }
                else if(server_pkt.type == (short)REJ){ //Error received! determine what error occured
                    if(server_pkt.rej_sub == (short)REJ_SUB_CODE_1){
                        //printf("Server: ERROR. Out of order packets. Was expecting seg_num %d but received seg_num %d.\n\n", pack_num, server_pkt.seg_num);
                        printf("Error: REJECT Sub-Code 1. Out-of-Order Packets. Expected %d, Got %d.\n", pack_num, server_pkt.seg_num);
                        return -1;
                    } else if(server_pkt.rej_sub == (short)REJ_SUB_CODE_2){
                        //printf("Server: ERROR. Packet %d has a length mismatch.\n\n", pack_num);
                        printf("Error: REJECT Sub-Code 2. Length Mis-Match in Packet %d.\n", pack_num);
                        return -1;
                    } else if(server_pkt.rej_sub == (short)REJ_SUB_CODE_3){
                        //printf("Server: ERROR. Packet %d has an invalid end of frame ID.\n\n", pack_num);
                        printf("Error: REJECT Sub-Code 3. Invalid End-of-Packet ID on Packet %d.\n", pack_num);
                        return -1;
                    } else if(server_pkt.rej_sub == (short)REJ_SUB_CODE_4){
                        //printf("Server: ERROR. Duplicate packets. Was expecting seg_num %d but receieved seg_num %d again.\n\n", pack_num, server_pkt.seg_num);
                        printf("Error: REJECT Sub-Code 4. Duplicate Packets. Expected %d, Got Duplicate %d.\n", pack_num, server_pkt.seg_num);
                        return -1;
                    }   
                } else{ //ACK nor REJ received. Corrupt packet? debugging purposes.
                    //printf("Client: Unknown server_pkt packet received.\n");
                    fprintf(stderr, "Client Error -- Received neither ACK or REJECT Packet.\n");
                    return -1;
                }
            }
        }
        
        //
        if(send_attempt >= 3){ //no need to check for REJ packet as REJ packets would have returned
            break; //time out error, break out of transmission loop
        }
        //else //send was ack'd. move to next packet
        //    pack_num++;
        pack_num++;
    }
    
    close(sock_fd);
    
    if(send_attempt < 3){ //while loop broken due to all packets being sent
        printf("Client Received All Packets With No Errors. Exiting...\n");
        return 0;
    } else{ //while loop broken due to time-out error
        //printf("Client: No server_pkt. Time-out error.\n\nClient: Packet %d was not acknowledged after 3 transmission attempts.\nServer does not respond.\nExiting. . . \n\n",pack_num);
        fprintf(stderr, "Time-Out Error: Client Attempted to Send Packet %d thrice.\n\nServer Does Not Respond.\nExiting...\n", pack_num);
        return -1;
    }
}