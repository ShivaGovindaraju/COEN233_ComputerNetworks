//Written to build an IPv4 Server

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

//Port to connect to
#define PORT "1337"
#define MAXPAY 0xff     //255
#define MAXBUFLEN 1276  //5 packets
#define STARTID 0xffff  //The following are as defined in the assignment
#define ENDID 0xffff
#define DATA 0xfff1
#define ACK 0xfff2
#define REJECT 0xfff3
#define REJSUB1 0xfff4
#define REJSUB2 0xfff5
#define REJSUB3 0xfff6
#define REJSUB4 0xfff7

//Define packet structures (one of each due to different size requirements
typedef struct datapack {
    unsigned short startid;
    unsigned char clientid;
    unsigned short data;
    unsigned char segnum;
    unsigned char len;
    unsigned char payload[MAXPAY];
    unsigned short endid;
    struct datapack *next;
}datapack;

typedef struct ackpack {
    unsigned short startid;
    unsigned char clientid;
    unsigned short ack;
    unsigned char segnum;
    unsigned short endid;
}ackpack;

typedef struct rejpack {
    unsigned short startid;
    unsigned char clientid;
    unsigned short reject;
    unsigned short subc;
    unsigned char segnum;
    unsigned short endid;
}rejpack;

//Buffer structure
typedef struct databuf {
    void *data;
    int next;
    size_t size;
}databuf;

//Function Definitions
void *get_addr(struct sockaddr *sa);
int deserialize_data(datapack *data, char buffer[]);
databuf *new_ackbuf();
databuf *new_rejbuf();
int ack(char client, char segnum, int sockfd, struct sockaddr_in theiraddr);
int rej(char client,char segnum, char sub, int sockfd, struct sockaddr_in theiraddr);
void serialize_ack(ackpack pack, databuf *b);
void serialize_rej(rejpack pack, databuf *b);
void serialize_short(short x, databuf *b);
void serialize_char(char x, databuf *b);

int main(void)
{
    //Variable declarations
    int sockfd;
    struct addrinfo hints, *servinfo, *p; //Each is a separate struct. Hints is constant for network. Servinfo is address of server. *p is for buffer
    int ex1; //extra variable for error check
    int numbytes;
    struct sockaddr_in clientaddr;
    struct pollfd fd;
    unsigned char buf[MAXBUFLEN], buf1[MAXBUFLEN];
    socklen_t addr_len;
    char s[INET_ADDRSTRLEN];
    datapack *out = malloc(sizeof(datapack));
    int count = 0, lastsegnum = 0, i = 0, bufcount = 0;
    int clientid, check, res;

    //Create info about the server
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; //IPv4
    hints.ai_socktype = SOCK_DGRAM; //UDP
    hints.ai_flags = AI_PASSIVE; //current computer IP

    //Error Checking to ensure network setup is correct and server matches.
    if((ex1 = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "Getaddrinfo: %s\n", gai_strerror(ex1));
        return 1;
    }

    //Create the socket on the server side
    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("Socket Error\n");
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("Socket failed to bind\n");
            continue;
        }

        break;
    }

    //Error checking
    if (p == NULL)
    {
        fprintf(stderr, "p failed to create socket from servinfo.\n");
        return 2;
    }
    //Setup polling
    fd.fd = sockfd;
    fd.events = POLLIN;

    //release memory for server info (only needed for error checking)
    freeaddrinfo(servinfo);

    //LOOP START
    while(1) {
        //Start 'listening'
        printf("Listener waiting to receive...\n");


        //Poll for 5 minutes. If no contact in that time break the loop and turn off the server.
        res = poll(&fd,1,300000);
        addr_len = sizeof(clientaddr);
        if(res == 0){
            //timeout
            //break loop to close socket and free ip
            break;

        } else if(res == -1){
            //error
            perror("Poll Error");
            exit(1);
        } else {
            //Receive something and error check it exists
            numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1, 0, (struct sockaddr *)&clientaddr, &addr_len);
            if(numbytes == -1)
            {
                perror("Recvfrom error\n");
                exit(1);
            }
        }




        //Print who sent the data and its address family (here 127.0.0.1 because it is the localhost machine)
        printf("Received packet from %s\n",
               inet_ntop(clientaddr.sin_family, get_addr((struct sockaddr *)&clientaddr), s, sizeof(s)));
        printf("Address family %d\n", clientaddr.sin_family);


        //Deserialize and send rej packet if any errors detected
        check = deserialize_data(out,buf);
        if(check == 1){
            printf("Packet Error in Field: %d\n",check);
            rej(out->clientid,out->segnum,check,sockfd,clientaddr);
            break;
        } else if(check == REJSUB2){
            printf("Packet Error in Field: %d\n",check);
            rej(out->clientid,out->segnum,check,sockfd,clientaddr);
            break;
        }else if(check == REJSUB3){
            printf("Packet Error in Field: %d\n",check);
            rej(out->clientid,out->segnum,check,sockfd,clientaddr);
            break;
        }

        //If receiving the first packet,
        if(count == 0){
            clientid = out->clientid;
        }

        //Check error in segment number for duplicate first then for out of order.
        if(out->segnum == lastsegnum){
            printf("Duplicate Packet\n");
            rej(out->clientid,out->segnum,REJSUB4,sockfd,clientaddr);
            break;
        }else if(out->segnum != lastsegnum+1){
            printf("Packet Received out of order.\nLast %d\nCurrent %d\n",lastsegnum,out->segnum);
            rej(out->clientid,out->segnum,REJSUB1,sockfd,clientaddr);
            break;
        }

        //Check to ensure  receiving from proper Client
        if(out->clientid != clientid){
            printf("Client ID mismatch\n");
            rej(out->clientid,out->segnum,2,sockfd,clientaddr);
            break;
        }


        //Check packet values by eye except for . Printed in hex where appropriate.
        printf("start: %x\n",out->startid);
        printf("client: %x\n",out->clientid);
        printf("Segnum: %d\n",out->segnum);
        printf("len: %d\n",out->len);
        printf("end: %x\n",out->endid);

        //If no errors are thrown, ACK is sent.
        ack(out->clientid,out->segnum,sockfd,clientaddr);

        //build message as it comes
        while(i < out->len){
            buf1[i + bufcount] = out->payload[i];
            i++;
        }

        //Print the buffer after the last packet is received
        if(out->len < MAXPAY){
            printf("Message length %d\n", strlen(buf1));
            printf("Message: %s\n",buf1);
            break;
        }

        //loop resets
        bufcount += i;
        i = 0;
        count++;
        memset(out->payload,0,sizeof(out->payload));
        lastsegnum = out->segnum;

        //LOOP END
    }


    //close the socket, free memory
    free(out);
    close(sockfd);
    return 0;

}

//gets an address to use
void *get_addr(struct sockaddr *sa) {
    //returns address of the socket
    return &(((struct sockaddr_in*)sa)->sin_addr);
}

//Gets data out of the packet and error checks to ensure packet follows proper structure.
int deserialize_data(datapack *pack, char buffer[]) {
    int end = ENDID;
    int i = 0;
    int ex1;

    //Checks that the first two bytes are ff. Due to adding overflows in chars, adding both would result in 0xfffe
    if((u_char) buffer[0] == 0xff && (u_char) buffer[1] == 0xff){
        pack->startid = STARTID;
    }
    else{
        pack->startid = buffer[0] + buffer[1];
        if(pack->startid != STARTID){
            return 1;
        }
    }
    //Clientid kept for later checks. Single packet are not able to be checked.
    pack->clientid = buffer[2];

    //Checks for correct DATA field in the packet
    if(((u_char) buffer[3] == 0xf1 && (u_char) buffer[4] == 0xff)
            || ((u_char) buffer[3] == 0xff && (u_char) buffer[4] == 0xff1)){
        pack->data = DATA;
    }
    else{
        pack->data = buffer[4] + buffer[3];
        if(pack->data != DATA){
            return 3;
        }
    }

    //Checks segment number. To ensure proper scope, must error check order outside of this function
    pack->segnum = buffer[5];

    //Checks the length of the data.
    pack->len = buffer[6];

    //Get the payload
    while(i<MAXPAY){
        pack->payload[i] = buffer[7+i];
        i++;
    };

    //Check payload length
    ex1 = pack->len;
    if(pack->payload[ex1] != '\0'){
        return REJSUB2;
    }


    //Check the ENNDID
    if((u_char) buffer[8+MAXPAY] == 0xff && (u_char) buffer[9+MAXPAY] == 0xff){
        pack->endid = ENDID;
    }
    else{
        pack->endid = buffer[8+MAXPAY] + buffer[9+MAXPAY];
        if(pack->endid != end){
            return REJSUB3;
        }
    }

    printf("Completed deserialize\n");
    return 0;

};

//Packet buffer initilizations. Returns appropriate size buffer depending on packet type used
databuf *new_ackbuf(){
    databuf *b = malloc(sizeof(ackpack)*2);

    b->data = malloc(sizeof(ackpack));
    b->size = sizeof(ackpack);
    b->next = 0;

    return b;
};

databuf *new_rejbuf(){
    databuf *b = malloc(sizeof(rejpack)*2);

    b->data = malloc(sizeof(rejpack));
    b->size = sizeof(rejpack);
    b->next = 0;
}

//Creates and send the ACK packet for the server
int ack(char client, char segnum, int sockfd, struct sockaddr_in theiraddr){

    ackpack *pack = malloc(sizeof(ackpack));
    databuf *packbuff = new_ackbuf();
    int numbytes;
    int addr_len = sizeof(theiraddr);

    pack->startid = STARTID;
    pack->clientid = client;
    pack->ack = ACK;
    pack->segnum = segnum;
    pack->endid = ENDID;
    serialize_ack(*pack, packbuff);


    numbytes = sendto(sockfd, packbuff->data, packbuff->size, 0, (struct sockaddr *)&theiraddr, addr_len);
    if(numbytes == -1)
    {
        perror("Ack Failed\n");
        exit(1);
    } else{
        printf("Ack Sent\n");
    }
    free(pack);
    free(packbuff);

};

//Creates and sends the REJ packet for the server, including the subcode
int rej(char client,char segnum, char sub, int sockfd, struct sockaddr_in theiraddr) {

    rejpack *pack = malloc(sizeof(rejpack));
    databuf *packbuff = new_rejbuf();
    int numbytes;
    int addr_len = sizeof(theiraddr);

    pack->startid = STARTID;
    pack->clientid = client;
    pack->reject = REJECT;
    pack->subc = sub;
    pack->segnum = segnum;
    pack->endid = ENDID;

    serialize_rej(*pack,packbuff);
    numbytes = sendto(sockfd, packbuff->data, packbuff->size, 0, (struct sockaddr *)&theiraddr, addr_len);
    if(numbytes == -1)
    {
        perror("Rej Failed\n");
        exit(1);
    } else{
        printf("Rej Sent\n");
    }
    free(pack);
    free(packbuff);
};

//Serialize according to packet size
void serialize_ack(ackpack pack, databuf *b){
    serialize_short(pack.startid,b);
    serialize_char(pack.clientid,b);
    serialize_short(pack.ack,b);
    serialize_char(pack.segnum,b);
    serialize_short(pack.endid,b);
};

void serialize_rej(rejpack pack, databuf *b){
    serialize_short(pack.startid,b);
    serialize_char(pack.clientid,b);
    serialize_short(pack.reject,b);
    serialize_short(pack.subc,b);
    serialize_char(pack.segnum,b);
    serialize_short(pack.endid,b);

};

//Serialize by data type. Takes one piece of data of a given and a buffer.
//Places the data in the next spot and increments the location according to the size
void serialize_short(short x, databuf *b){
    memcpy(((char *)b->data) + b->next, &x, sizeof(short));
    b->next += sizeof(short);
};

void serialize_char(char x, databuf *b){
    memcpy(((char *)b->data)+ b->next,&x,sizeof(char));
    b->next += sizeof(char);
};