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

//Definitions given by the problem document.
#define START_ID 0xFFFF
#define END_ID 0xFFFF
#define LENGTH_MAX 0xFF
#define DATA 0xFFF1
#define ACK 0xFFF2
#define REJECT 0xFFF3
#define REJECT_SUB_CODE_1 0xFFF4
#define REJECT_SUB_CODE_2 0xFFF5
#define REJECT_SUB_CODE_3 0xFFF6
#define REJECT_SUB_CODE_4 0xFFF7
//User-made Definitions for hard-coded values.
#define SERVICE_PORT 23456 //hard-coded the port number (picked it randomly, and it was available).
#define BUFFER_LEN 2048
#define CLIENT_ID 0x42 //client ID - set it myself to 0x42 because 42's the answer to life.
#define NUM_PACKETS 5 //Number of packets the client will send the server.

//Data structures for sending data to the server and receiving return ACK/REJECTs.
typedef struct data_pkt {
    short start_id;
    char client_id;
    short data;
    char seg_num;
    char length;
    char payload[LENGTH_MAX];
    short end_id;
} data_pkt;

typedef struct return_pkt {
    short start_id;
    char client_id;
    short type;
    short rej_sub;
    char seg_num;
    short end_id;
} return_pkt;


int main(int argc, char **argv) {
    if (argc < 2) { //Ensuring that Arguments is available for Client to know which test case to run.
        printf("ERROR: Missing Arguments for determining which Client Test to run.\n");
        return -1;
    }
    int test_number = atoi(argv[1]); //setting the test case being run.
    
    struct sockaddr_in client_addr, server_addr; //sock addresses for client and server.
    int sock_fd;                        //fd for socket
    socklen_t addr_len = sizeof(client_addr); //length of a sockaddr_in
    char buffer[BUFFER_LEN];            //buffer for putting messages into (because I couldn't bother counting)
    int recv_len;                       //variable to hold length of received message packet
    return_pkt server_pkt;              //struct to hold return packet from server
    data_pkt client_pkt;                //struct for data packet being sent to server
    int send_attempt;                   //counter for each packet's send attempts
    int poll_ret;                       //return value for poll (used as timer).
    
    //Creating a UDP Socket for the Client
    sock_fd=socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        fprintf(stderr, "Client Error -- Could Not Create Socket.\n");
        return -1;
    }
    
    //Setup the Client Sock Addr
    //Bind it to the Socket and the Selected Port for this communication
    memset((char *)&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    client_addr.sin_port = htons(0);
    if (bind(sock_fd, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
        fprintf(stderr, "Client Error -- Failed to Bind Socket to Local Address and Selected Port.\n");
        return -1;
    }
    //Define the Server Sock Addr that Client needs to connect to for package sending/receiving.
    memset((char *) &server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVICE_PORT);
      
    //Setting up the Polling socket for doing Time-outs
    // -- this was recommended when I was complaining to a friend about doing time-outs
    //    so far, it seems to work much better than the timers and forking
    ///   structures I was playing with before.
    struct pollfd acktimer_sockfd;
    acktimer_sockfd.fd = sock_fd;
    acktimer_sockfd.events = POLLIN; //notes anything coming in on the socket
    
    //Creating an array of data packets for transmission to the server for testing the socket connection
    //The setup for this was described in the assignment document.
    data_pkt dp_arr[NUM_PACKETS];
    for (int i = 0; i < NUM_PACKETS; i++) {
        //The general data-packet fields that are common to all data-packets sent by the client.
        dp_arr[i].start_id = START_ID;
        dp_arr[i].client_id = CLIENT_ID;
        dp_arr[i].data = DATA;
        dp_arr[i].end_id = END_ID;
        //The more specific details to differentiate each packet (seg-no, payload, length).
        dp_arr[i].seg_num = i;
        sprintf(buffer, "Hello from the Client Host. Packet %d of %d.", i + 1, NUM_PACKETS);
        strncpy(dp_arr[i].payload, buffer, LENGTH_MAX); //used buffer to ensure message fit in the payload (even if the message gets cut off at the end, because I'm being lazy with counting out the length of my message for testing purposes).
        dp_arr[i].length = sizeof(dp_arr[i].payload);
    }
    
    //Rather than make separate clients for each test case, I figured I'd just take the base-case
    //(normal operation) and then just use CLI to specify which test case I want the client to run.
    //Thus, by specifying a specific Case, I can force the Server to detect different kinds of errors
    //as described in the problem description.
    //Case 1 - Out of Order Packet Transmission (get a packet early)
    //Case 2 - Mismatch in the Length field of the data packet.
    //Case 3 - The End-of-Packet ID is incorrect.
    //Case 4 - A Duplicated Packet was sent (instead of 0-1-2-3-4 getting 0-1-2-2-4).
    data_pkt tmp;
    int rand_adjust = 0;
    printf("COEN 233 Fall 2020 Assignment 1\nConnecting Client to Server on Port %d...\n", SERVICE_PORT);
    if (test_number == 1) {
        printf("Client Test Case 1: Out-of-Order Packets.\n\n"); //expect 3, get 4
        tmp = dp_arr[3]; //just a simple array-element switch
        dp_arr[3] = dp_arr[4];
        dp_arr[4] = tmp;
    } else if (test_number == 2) {
        printf("Client Test Case 2: Mismatch in Length.\n\n"); //mismatch on 3
        while (rand_adjust == 0)
            rand_adjust = (rand() % 10) - 5; //using a random number adjustment (for fun) to create bad length
        dp_arr[3].length = dp_arr[3].length + rand_adjust;
    } else if (test_number == 3) {
        printf("Client Test Case 3: Incorrect End of Packet ID.\n\n"); //bad eop on 3
        dp_arr[3].end_id = 0x1234; //random value that isn't the specified END_ID value defined in the doc.
    } else if (test_number == 4) {
        printf("Client Test Case 4: Duplicate Packets.\n\n"); //expect 3, get 2
        dp_arr[3] = dp_arr[2]; //made packet 3 a copy of packet 2.
    } else {
        printf("Client is Sending Data Normally, Not Testing Error Handling.\n\n");
    }
    
    //Loop through the packet-array and start sending data packets until we've run out.
    for (int pack_num = 0; pack_num < NUM_PACKETS; pack_num++) {
        client_pkt = dp_arr[pack_num]; //specify which packet in the array we're sending
        send_attempt = 1; //initialize the attempt number (we try thrice).
        //Send the packe to the server via the set-up socket connections.
        printf("Client is sending Packet %d to Server. Attempt %d\n", pack_num, send_attempt);      
        if (sendto(sock_fd, &client_pkt, sizeof(data_pkt), 0, (struct sockaddr *)&server_addr, addr_len) < 0) {
            fprintf(stderr, "Client Experienced Error in Sending Packet %d to Server.\n", pack_num);
            return -1;
        }
        
        while(send_attempt <= 3) {
            poll_ret = poll(&acktimer_sockfd, 1, 3000); //The timer waits for three seconds to get an ACK
            if(poll_ret == 0) { //Poll Timer ran out and we didn't get the ACK.
                send_attempt++; //Increment Send Attempt counter.
                //Try sending the packet again.
                if (send_attempt <= 3) {
                    printf("No Response from Server to Client. Attempt %d. Retransmitting...\n", send_attempt);
                    if (sendto(sock_fd, &client_pkt, sizeof(data_pkt), 0, (struct sockaddr *)&server_addr, addr_len) < 0) {
                        fprintf(stderr, "Client Experienced Error in Sending Packet %d to Server.\n", pack_num);
                        return -1;
                    }
                }
            } else if (poll_ret == -1) { //Something's gone wrong with the poll - abort!
                fprintf(stderr, "Client Experienced Error in Polling.\n");
                return -1;
            } else { //Got something before time-out, need to read it in from the socket.
                //Reading in the return packet from the socket.
                recv_len = recvfrom(sock_fd, &server_pkt, sizeof(return_pkt), 0, (struct sockaddr *)&server_addr, &addr_len);
                if(recv_len == -1) { //bad packet received. abort due to error in connection.
                    fprintf(stderr, "Client Experienced Error in Receiving server_pkt from Server.\n");
                    return -1;
                }
                //Now, we need to check what kind of packet we got...
                if (server_pkt.type == (short )ACK) {
                    //Return Packet was an ACK. Yay!
                    //We can just skip to the next iteration of the for loop.
                    printf("Received ACK for Packet %d from Server.\n", pack_num);
                    break;
                } else if (server_pkt.type == (short )REJECT) {
                    //Return Packet was a REJECT! Need to determine which Error Code was thrown.
                    //Since we got a REJECT, we can end the client's operations here.
                    if (server_pkt.rej_sub == (short )REJECT_SUB_CODE_1) {
                        printf("Error: REJECT Sub-Code 1. Out-of-Order Packets. Expected %d, Got %d.\n", pack_num, server_pkt.seg_num);
                        return -1;
                    } else if (server_pkt.rej_sub == (short )REJECT_SUB_CODE_2) {
                        printf("Error: REJECT Sub-Code 2. Length Mis-Match in Packet %d.\n", pack_num);
                        return -1;
                    } else if (server_pkt.rej_sub == (short )REJECT_SUB_CODE_3) {
                        printf("Error: REJECT Sub-Code 3. Invalid End-of-Packet ID on Packet %d.\n", pack_num);
                        return -1;
                    } else if (server_pkt.rej_sub == (short )REJECT_SUB_CODE_4) {
                        printf("Error: REJECT Sub-Code 4. Duplicate Packets. Expected %d, Got Duplicate %d.\n", pack_num, server_pkt.seg_num);
                        return -1;
                    }   
                } else {
                    //I don't expect this to ever be relevant, but just on the safe side...
                    fprintf(stderr, "Client Error -- Received neither ACK or REJECT Packet.\n");
                    return -1;
                }
            }
        }
        
        //If we've somehow timed out after three attempts at transmission,
        //Then we need to quit sending packets and exit.
        if (send_attempt > 3) {
            fprintf(stderr, "Time-Out Error: Client Attmpted to Send Packet %d thrice.\n\nServer Does Not Respond.\n\nExiting...\n", pack_num);
            return -1;
        }
    }
    
    close(sock_fd); //need to close the socket now that we're done with it (bad practice if we left it).
    //Wrap things up with a message for the user, and exit with grace.
    printf("\nClient Received All Packets With No Errors.\n\nExiting...\n");
    return 0;
}