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

//Definitions given in the problem document.
#define START_ID 0xFFFF
#define END_ID 0xFFFF
#define ACC_PER 0xFFF8
#define NOT_PAID 0xFFF9
#define NOT_EXIST 0xFFFA
#define ACC_OK 0xFFFB
//User-made definitions for hard-coded values.
#define SERVICE_PORT 23456 //hard-coded the port number (picked it randomly, and it was available).

//Data structure for sending and receiving data with the Client.
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


//This is a helper function used for searching the Verification Database.
//It takes in an array of unsigned longs detailing verified subscriber numbers on the database,
//an integer for the length of the database, and an unsigned long for the subscriber number
//that needs to be searched for.
//This function returns an int for the index in the databse correlating to the subscriber number searched for.
//If the subscriber number cannot be found in the database array, this function returns -1.
int linear_search (unsigned long *subn_arr, int arr_len, unsigned long sub_num) {
    int ret_index = -1;
    for (int i = 0; i < arr_len; i++) {
        if (subn_arr[i] == sub_num) {
            ret_index = i;
            break;
        }
    }
    return ret_index;
}

int main() {
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
    struct sockaddr_in server_addr, client_addr; //sock addresses for client and server.
    int sock_fd;                                 //fd for socket
    socklen_t addr_len = sizeof(client_addr);    //length of a sockaddr_in
    int recv_len;                                //variable to hold length of received message packet
    data_pkt client_pkt;                         //struct to hold data packet being sent to server
    data_pkt server_pkt;                         //struct for return packet from server
    int valid_ind = -1;                          //Index of a Subscriber Number on the Verified Database
    
    //Creating a UDP Socket for the Client
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        fprintf(stderr, "Server Error -- Could Not Create Socket.\n");
        return -1;
    }
    
    //Setup the Server Sock Addr
    //Bind it to the Socket and the Selected Port for this communication
    memset((char *)&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(SERVICE_PORT);
    if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Sever Error -- Failed to Bind Socket to Local Address and Selected Port.\n");
        return -1;
    }
    
    //Setting up the Polling socket for doing Time-outs
    // --- see comments in Assignment 1, server.c for reasoning.
    struct pollfd newclientpoll_sockfd;
    newclientpoll_sockfd.fd = sock_fd;
    newclientpoll_sockfd.events = POLLIN; //notes anything coming in on the socket.
    
    
    printf("COEN 233 Fall 2020 Assignment 2\nInitializing Server.\nWaiting for Client on Port %d.\n---\n\n", SERVICE_PORT);
    
    
    //The Server runs on an endless loop, waiting to take in data continuously.
    while(1) {        
        //We wait on the socket to get a data packet from the Client
        recv_len = recvfrom(sock_fd, &client_pkt, sizeof(data_pkt), 0, (struct sockaddr *)&client_addr, &addr_len);
        if (recv_len > 0) { //make sure tha packet was received properly.
            printf("Message Received from Client!\nAccess Permission Requested by Subscriber #: %lu\n", client_pkt.sub_num);
        } else {
            printf("Server Error -- Error in Package Reception from Client!\n");
        }
        
        //Data packes sent back to the user have several commonalities, regardless of response type.
        server_pkt.start_id = START_ID;
        server_pkt.end_id = END_ID;
        server_pkt.client_id = client_pkt.client_id;
        server_pkt.seg_num = client_pkt.seg_num;
        server_pkt.tech = client_pkt.tech; //This will get changed later if there's a Tech Mis-Match.
        server_pkt.sub_num = client_pkt.sub_num;
        server_pkt.length = sizeof(client_pkt.tech) + sizeof(client_pkt.sub_num);
        
        //First, search the database for the client's subscriber number, and verify it.
        valid_ind = linear_search(sub_num, db_len, client_pkt.sub_num);
        //Now, run through verification checks
        if (valid_ind < 0) { //The subscriber number couldn't be found on the database.
            printf("NOT EXIST: Subscriber %lu Does Not Exist in the Verification Database.\nAccess Denied.\n\n", client_pkt.sub_num);
            server_pkt.type = NOT_EXIST;
        } else if (client_pkt.tech != tech[valid_ind]) { //The subscriber number asked for the wrong Technology
            printf("NOT EXIST: Subscriber %lu Requested Access to Incorrect Technology.\nRequested %dG, but is Authorized for %dG.\nAccess Denied.\n\n", client_pkt.sub_num, (int )client_pkt.tech, (int )tech[valid_ind]);
            server_pkt.type = NOT_EXIST;
            server_pkt.tech = (char )0;
        } else if (paid[valid_ind] == 0) { //The subscriber number has not paid.
            printf("NOT PAID: Subscriber %lu has Not Paid for Access.\nAccess Denied.\n\n", client_pkt.sub_num);
            server_pkt.type = NOT_PAID;
        } else { //No issues found in database or client-packet. Give Access Permission to Client.
            printf("ACCESS OKAY: Subscriber %lu has been Verified against the Database.\nAccess Granted.\n\n", client_pkt.sub_num);
            server_pkt.type = ACC_OK;
        }
        
        //Finally, we send the packet to the Client with the access-request response.
        if (sendto(sock_fd, &server_pkt, sizeof(data_pkt), 0, (struct sockaddr *)&client_addr, addr_len) < 0) {
            fprintf(stderr, "\nServer Error -- Failed to Send Packet to Client.\n\n");
        }
    } //No exit for the Server - it will always wait for Clients. Force-kill Server via CLI (ctrl-C).
}