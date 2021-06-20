/*
 Ryan Schulz

 Assignment 1 Client
 
 Used this resource for help setting up socket stuff:
 https://www.cs.rutgers.edu/~pxk/417/notes/sockets/index.html
 
 If port 21234 is used, you might have to change it to a local port that is open on your device
 
 Notes/Assumptions: Server utilizes a counter to verify packet sequence integrity. After 2.5 seconds of not receiving a new packet, it resets that packet counter and displays a message. If you run two different transmissions within 2.5 seconds, it will error out due to the packet counter not resetting.
 
 Please let the packet counter reset between testing different error cases.
 
 The options to modify packets and induce errors can be seen on line 74
 
 I assumed when the client received an error from the server, it would display the respective error and quit. It does not attempt to retransmit the message.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <poll.h>

#define PACKETS 5 //num of packets to send
#define START_ID 0xFFFF // packet start ID
#define END_ID 0xFFFF // packet end ID
#define CLIENT_ID 0x52 //'R' client ID
#define CLIENT_MAX_ID 0xFF // max size of client id
#define LENGTH_MAX 0xFF //max length
#define DATA 0xFFF1 //data
#define ACK 0xFFF2 //acknowledge
#define REJ 0xFFF3 //reject
#define REJ_SUB1 0xFFF4 // packer sequence messed up
#define REJ_SUB2 0xFFF5 // packet length mismatch
#define REJ_SUB3 0xFFF6 // end of packet not recv
#define REJ_SUB4 0xFFF7 //duplicate packet


#define SERVICE_PORT 21234    /* hard-coded port number */
#define BUFLEN 2048

//packets sent to server
typedef struct data_packet {
    short start_id;
    char client_id;
    short data;
    char seg_num;
    char length;
    char payload[255];
    short end_id;
} data_packet;

//repsonse packets received from server
typedef struct ret_packet{
    short start_id;
    char client_id;
    short type;
    short rej_sub;
    char seg_num_rec;
    short end_id;
} ret_packet;

/*prototype*/
int inet_aton(const char *cp, struct in_addr *inp);
int close(int fd);



int main(void)
{
    int demo_transmission_case = 0;
    /*
     OPTION 0: Normal Transmission
     -Order goes 0, 1, 2, 3, 4
     
     OPTION 1: Out of order packet is sent (CASE 1)
     -Order goes 0, 1, 2, 4, 3
     
     OPTION 2: Length mismatch (CASE 2)
     -The 3rd packet's length field is set to 0x03; actual length is 0xFF
     
     OPTION 3: EOF is wrong (CASE 3)
     - End of frame identifier for 3rd packet is set to 0xF00F; actual is 0xFFFF
     
     OPTION 4: Duplicate packet (CASE 4)
     -Order goes 0, 1, 1, 3, 4
     */
    
    struct sockaddr_in myaddr, remaddr;
    int fd, i, send_attempt = 1, poll_result, slen=sizeof(remaddr);
    char buf[BUFLEN];    /* message buffer */
    int recvlen;        /* # bytes in acknowledgement message */
    char *server = "127.0.0.1";    /* change this to use a different server */
    
    /*
     create a socket
     */
    
    if ((fd=socket(AF_INET, SOCK_DGRAM, 0))==-1)
        printf("Client: socket created\n");
    
    /*
     bind it to all local addresses and selected port number
     */
    
    memset((char *)&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    myaddr.sin_port = htons(0);
    
    if (bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
        perror("Client: bind failed");
        return 0;
    }
    
    /*
      define remaddr, the address to whom we want to send messages
      the host address is expressed as a numeric IP address
      will convert that to a binary format via inet_aton
    */
    memset((char *) &remaddr, 0, sizeof(remaddr));
    remaddr.sin_family = AF_INET;
    remaddr.sin_port = htons(SERVICE_PORT);
    if (inet_aton(server, &remaddr.sin_addr)==0) {
        fprintf(stderr, "Client: inet_aton() failed\n");
        exit(1);
    }
    
    
    /*
     setup pollfd for ack_timer
    */
    struct pollfd pfdsock;
    pfdsock.fd = fd;
    pfdsock.events = POLLIN;
    
    
    /*
     Error-less packet setup
     */
    data_packet data_array[PACKETS];
    for (i=0; i<PACKETS; i++){
        data_array[i].client_id=CLIENT_ID;
        data_array[i].start_id = START_ID;
        data_array[i].end_id = END_ID;
        data_array[i].data = DATA;
        data_array[i].seg_num = i;
        sprintf(buf, "Client: This is packet #%d", i);
        strncpy(data_array[i].payload, buf, 255);
        data_array[i].length = sizeof(data_array[i].payload);
    }
    
    
    
    
    /*
     Induces errors for demo
    */
    data_packet temp;
    printf("\n*************************************************************\n\n");
    switch(demo_transmission_case){
        case 1: //CASE 1: Out of order
            temp = data_array[3];
            data_array[3] = data_array[4];
            data_array[4] = temp;
            printf("Client: Out of order packet transmission selected\n");
            break;
        case 2: //CASE 2: Length missmatch
            data_array[2].length = 0x03;
            printf("Client: Length mismatch transmission selected\n");
            break;
        case 3: //CASE 3: EOF wrong
            data_array[2].end_id = 0xF00F;
            printf("Client: Wrong EOF transmission selected\n");
            break;
        case 4: //CASE 4: Duplicate Packet
            data_array[2] = data_array[1];
            printf("Client: Duplicate packet transmission selected:\n");
            break;
        default:
            printf("Client: Normal transmission selected\n");
    }
    printf("\n*************************************************************\n\n");
    
    
    /*
     create receive and return variables
    */
    ret_packet response;
    data_packet data;
    i = 0;
    
    /*
     This loop sends the messages and awaits for feedback from server
    */
    while (i < PACKETS) {
        data = data_array[i];
        printf("Client: Sending packet %d to %s port %d. Attempt 1 out of 3\n", i, server, SERVICE_PORT);
        
        if (sendto(fd, &data, sizeof(data_packet), 0, (struct sockaddr *)&remaddr, slen)==-1) {
            perror("Client: Sendto error:\n");
            exit(1);
        }
        
        send_attempt = 1;
        //ack_timer implementation
        while(send_attempt < 3){
            //poll for 3 seconds, and resend up to 2 times
            poll_result = poll(&pfdsock,1,3000);
            
            if(poll_result == 0) {
                //timeout, must resend
                send_attempt++;
                printf("Client: No response. Retransmitting. Attempt %d out of 3\n", send_attempt);
                if (sendto(fd, &data, sizeof(data_packet), 0, (struct sockaddr *)&remaddr, slen)==-1) {
                    perror("Client: Sendto error:\n");
                    exit(1);
                }
                
                
            }
            else if(poll_result == -1) {
                //error
                perror("Client: Poll error:\n");
                return poll_result;
                
            }
            else {
                /* now receive an response from the server */
                recvlen = recvfrom(fd, &response, sizeof(ret_packet), 0, (struct sockaddr *)&remaddr, &slen);
                if(recvlen == -1){
                    perror("Client: Receive error:\n");
                    exit(1);
                }
                
                //checks to make sure the start of the packet is correct -just for debugging purposes
                if (response.start_id != (short)START_ID){
                    printf("Client: Invalid response from server. Was expecting 0xFFFF for start_id, but received %x\n", response.start_id);
                    return -1;
                }
                
                
                if(response.type == (short)ACK){ //ACK received!
                    printf("Server: ACK. Packet %d acknowledged.\n\n", i);
                    break;
                }
                else if(response.type == (short)REJ){ //Error received! determine what error occured
                    if(response.rej_sub == (short)REJ_SUB1){
                        printf("Server: ERROR. Out of order packets. Was expecting seg_num %d but received seg_num %d.\n\n", i, response.seg_num_rec);
                        return -1;
                    }
                    else if(response.rej_sub == (short)REJ_SUB2){
                        printf("Server: ERROR. Packet %d has a length mismatch.\n\n", i);
                        return -1;
                    }
                    else if(response.rej_sub == (short)REJ_SUB3){
                        printf("Server: ERROR. Packet %d has an invalid end of frame ID.\n\n", i);
                        return -1;
                    }
                    else if(response.rej_sub == (short)REJ_SUB4){
                        printf("Server: ERROR. Duplicate packets. Was expecting seg_num %d but receieved seg_num %d again.\n\n", i, response.seg_num_rec);
                        return -1;
                    }
                    
                }
                else{ //ACK nor REJ received. Corrupt packet? debugging purposes.
                    printf("Client: Unknown response packet received.\n");
                    return -1;
                }
            }
        }
        
        //
        if(send_attempt >= 3){ //no need to check for REJ packet as REJ packets would have returned
            break; //time out error, break out of transmission loop
        }
        else //send was ack'd. move to next packet
            i++;

    }
    
    close(fd);
    if(send_attempt < 3){ //while loop broken due to all packets being sent
        printf("Client: All finished with no errors\n\n");
        return 0;
    }
    else{ //while loop broken due to time-out error
        printf("Client: No response. Time-out error.\n\nClient: Packet %d was not acknowledged after 3 transmission attempts.\nServer does not respond.\nExiting. . . \n\n",i);
        return -1;
    }
}