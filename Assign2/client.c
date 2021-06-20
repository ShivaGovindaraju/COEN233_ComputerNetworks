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
#define ACC_PER 0xFFF8
#define NOT_PAID 0xFFF9
#define NOT_EXIST 0xFFFA
#define ACC_OK 0xFFFB
//User-made Definitions for hard-coded values.
#define SERVICE_PORT 23456 //hard-coded the port number (picked it randomly, and it was available).
#define CLIENT_ID 0x42 //client ID - set it myself to 0x42 because 42's the answer to life.

//Data structure for sending and receiving data with the Server.
typedef struct data_pkt {
    short start_id;
    char client_id;
    short type;
    char seg_num;
    char length;
    char tech;
    unsigned long sub_num;
    short end_id;
} data_pkt;


int main(int argc, char **argv) {
    //The following is used to determine the number of verified subscribers in the database.
    char line_buf[18] = {0}; //a buffer to capture string-outputs.
    int db_len;      // This value will contain the length of the database (ie. how many lines).
    FILE *file_len = popen("wc -l Verification_Database.txt", "r");
    if (!file_len) {
        fprintf(stderr, "Client Error -- Could not open file-length.");
        return -1;
    }
    fscanf(file_len, "%s", line_buf); //capturing the output of "wc -l Verification_Database.txt"
    db_len = atoi(line_buf); //actually turning that into an integer we can use.
    pclose(file_len); //Closing the file-pipe used to determine the number of lines in database file.
    
    //We will not initialize the arrays and variables needed to load the table from the database file.
    unsigned long sub_num[db_len]; //an array of all subscriber numbers in the database
    char tech[db_len]; //an array with all technology values registered to the subscribers
    char paid[db_len]; //an array telling us whether the subscribers have paid or not (1 = paid, 0 = not paid).
    //The following are just variables for getting the data out.
    size_t len = 0;
    ssize_t read;
    char * line = NULL;
    char * token = NULL;
    int i = 0, j, k;
    char *end_ptr;
    
    FILE *input_dbfile = fopen("Verification_Database.txt", "r");
    if (!input_dbfile) {
        fprintf(stderr, "Client Error -- Could not open Database file.");
        return -1;
    } //by now, we've opened the database file for reading.
    //Now, we must parse the data base line-by-line to fill the table.
    while (read = getline(&line, &len, input_dbfile) != -1) {
        token = strtok(line," "); //this grabs the subscriber-number and puts it into "token"
        j = 0;
        for (k = 0; k < strlen(token); k++) {
            if (token[k] >= '0' && token[k] <= '9') {
                line_buf[j++] = token[k];
            }
        }
        line_buf[j] ='\0'; //using line_buf, we parse out any non-numeric characters in the subscriber numer.
        sub_num[i] = strtoul(line_buf, &end_ptr, 10); //case the substriber number to an unsigned long (4bytes)
        tech[i] = (char )atoi(strtok(NULL, " ")); //grab the technology value and cast to char
        paid[i] = (char )atoi(strtok(NULL, " ")); //grab the paid-boolean and cast to char
        i++;
    }
    fclose(input_dbfile); //done with the data-base file. We can close it now.
    
    
    //Initializing values for completing socket programming communications
    //Most of the following is just lifted from Assignment 1.
    struct sockaddr_in client_addr, server_addr; //sock addresses for client and server.
    int sock_fd;                                 //fd for socket
    socklen_t addr_len = sizeof(client_addr);    //length of a sockaddr_in
    int recv_len;                                //variable to hold length of received message packet
    data_pkt server_pkt;                         //struct to hold return packet from server
    data_pkt client_pkt;                         //struct for data packet being sent to server
    int send_attempt;                            //counter for each packet's send attempts
    int poll_ret;                                //return value for poll (used as timer).
    
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
    // -- See reasoning in Assignment 1, client.c's comments.
    struct pollfd acktimer_sockfd;
    acktimer_sockfd.fd = sock_fd;
    acktimer_sockfd.events = POLLIN; //notes anything coming in on the socket
    
    
    printf("COEN 233 Fall 2020 Assignment 2\nConnecting Client to Server on Port %d...\n\n", SERVICE_PORT);
    
    /*
    Test 1 : 4085546805 - Access Granted
    Test 2 : 4086668821 - Not Paid
    Test 3 : 4086808821 - Not Exist, Tech Mis-Match
    Test 4 : 4086674674 - Access Granted
    Test 5 : 4084400332 - Not Exist
    */
    //Here, we fill out an array of data-packets containing the Test Cases we want to send the Server
    data_pkt dp_arr[db_len + 1];
    for (i = 0; i < db_len; i++) {
        //The general stuff common to all data packets going to the Server
        dp_arr[i].start_id = START_ID;
        dp_arr[i].client_id = CLIENT_ID;
        dp_arr[i].type = ACC_PER;
        dp_arr[i].end_id = END_ID;
        //The more specific details to differentiate each packet (seg-num, tech, subscriber num, length).
        dp_arr[i].seg_num = i;
        dp_arr[i].tech = tech[i];
        dp_arr[i].sub_num = sub_num[i];
        dp_arr[i].length = sizeof(dp_arr[i].tech) + sizeof(dp_arr[i].sub_num);
    }
    //Adding the Modifications for Testing Cases 3 and 5.
    //Test Case 3
    dp_arr[2].tech = 0x06; //There is no 6G network, so this shouldn't pass.
    dp_arr[2].length = sizeof(dp_arr[2].tech) + sizeof(dp_arr[2].sub_num);
    //Test Case 5
    dp_arr[db_len] = dp_arr[db_len - 1]; //just copy the previous packet, but give a bad subscriber number.
    dp_arr[db_len].sub_num = strtoul("4084400332", &end_ptr, 10);
    dp_arr[db_len].length = sizeof(dp_arr[db_len].tech) + sizeof(dp_arr[db_len].sub_num);
    
    
    //Start Sending Packets for Verification.
    for (int pack_num = 0; pack_num < (db_len + 1); pack_num++) {
        client_pkt = dp_arr[pack_num]; //specify which packet in the array we're sending
        send_attempt = 1; //initialize the attempt number (we try thrice).
        
        //Send the packet to the server via the set-up socket connections.
        printf("Client is sending Packet %d (sub#: %lu) to Server. Attempt %d\n", pack_num, client_pkt.sub_num, send_attempt);      
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
                recv_len = recvfrom(sock_fd, &server_pkt, sizeof(data_pkt), 0, (struct sockaddr *)&server_addr, &addr_len);
                if(recv_len == -1) { //bad packet received. abort due to error in connection.
                    fprintf(stderr, "Client Experienced Error in Receiving server_pkt from Server.\n");
                    return -1;
                }
                //Now, we need to check what kind of packet we got...
                if (server_pkt.type == (short )ACC_OK) {
                    //Return Packet was an ACK. Yay!
                    //We can just skip to the next iteration of the for loop.
                    printf("Received ACCESS OKAY for Packet %d (sub#: %lu) from Server.\nSubscriber May Access the Network.\n\n", pack_num, server_pkt.sub_num);
                    break;
                } else if (server_pkt.type == (short )NOT_PAID) {
                    printf("Error: Received NOT_PAID for Packet %d (sub#: %lu) from Server.\nSubscriber Has Not Paid for Access.\n\n", pack_num, server_pkt.sub_num);
                    break;
                } else if (server_pkt.type == (short )NOT_EXIST && server_pkt.tech == (char )0) {
                    printf("Error: Received NOT_EXIST for Packet %d (sub#: %lu) from Server.\nSubscriber Exists in the Database, but requests Incorrect Technology.\n\n", pack_num, server_pkt.sub_num);
                    break;
                } else if (server_pkt.type == (short)NOT_EXIST) {
                    printf("Error: Received NOT_EXIST for Packet %d (sub#: %lu) from Server.\nSubscriber Does Not Exist in the Database\n\n", pack_num, server_pkt.sub_num);
                    break;
                } else {
                    //I don't expect this to ever be relevant, but just on the safe side...
                    fprintf(stderr, "Client Error -- Received neither ACK or REJECT Packet.\n\n");
                    return -1;
                }
            }
        }
        
        //If we've somehow timed out after three attempts at transmission,
        //Then we need to quit sending packets and exit.
        if (send_attempt > 3) {
            fprintf(stderr, "Time-Out Error: Client Attmpted to Send Packet %d (sub#: %lu) thrice.\n\nServer Does Not Respond.\n\nExiting...\n", pack_num, client_pkt.sub_num);
            return -1;
        }
    }
    
    close(sock_fd); //need to close the socket now that we're done with it (bad practice if we left it).
    //Wrap things up with a message for the user, and exit with grace.
    printf("Client Received All Packets With No Errors.\n\nExiting...\n");
    return 0;
}