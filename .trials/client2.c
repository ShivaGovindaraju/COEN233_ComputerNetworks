#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <math.h>
#include <poll.h>

#define SERVER "1337" //Server port
#define CLIENTID 0x45   //My ID
#define MAXPAY 0xff     //255
#define MAXBUFLEN 1276  //5 packets
#define STARTID 0xffff  //The following are as defined in the assignment
#define ENDID 0xffff
#define DATA 0xfff1
#define ACK 0xfff2
#define REJECT 0xfff3
#define REJSUB1 0xfff4  //out of sequence
#define REJSUB2 0xfff5  //length mismatch
#define REJSUB3 0xfff6  //missing end of packet
#define REJSUB4 0xfff7  //duplicat packet

//Define packet structures (one of each due to different size requirements)
typedef struct datapack {
    int numseg;
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

//create buffer structure
typedef struct databuf {
    void *data;
    int next;
    size_t size;
}databuf;


//Function definitions
databuf *new_buffer();
datapack * fragment(char message[]);
void serialize_Data(datapack send, databuf *output);
void serialize_short(short x, databuf *b);
void serialize_char(char x, databuf *b);
int deserialize(ackpack *ack,rejpack *rej, char buffer[]);

//Takes in an ip address (localhost for client on same computer) and string to send.
int main()
{
    //variable initializations
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes, res;
    struct sockaddr_in their_addr;
    databuf *b = new_buffer();
    int count = 0;
    char messbuf[MAXBUFLEN];
    unsigned char buf1[MAXBUFLEN];
    struct pollfd fd;
    int count1 = 0;


    //Get the input message and IP address
    printf("Enter your message:\n");
    fgets(messbuf,MAXBUFLEN,stdin);

    //fragment the input message
    datapack *send = fragment(messbuf);

    //Setup hints for network
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    //Make sure your and server networks are the same and you have the correct address. Since this is on one machine,
    //localhost is used in place of receiving an argument
    if ((rv = getaddrinfo("localhost", SERVER, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    //Create a socket with the required parameters.
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("Socket failed\n");
            continue;
        }
        break;
    }

    //Setup polling
    fd.fd = sockfd;
    fd.events = POLLIN;

    //error check the socket was created properly
    if (p == NULL) {
        fprintf(stderr, "Failed to create socket\n");
        return 2;
    }


    //LOOP START
    while(count < send->numseg) {
        //Serialize the first fragment
        serialize_Data(*send,b);
        //send the first fragment to the server
        numbytes = sendto(sockfd, b->data, b->size, 0, p->ai_addr, p->ai_addrlen);
        if (numbytes == -1)
        {
            perror("Sendto error\n");
            exit(1);
        }

        //wait for ACK/REJ from server (This is ack_timer
        while(count1 < 2){
            //Poll for 3 seconds then resend up to twice before erroring out.
            res = poll(&fd,1,3000);
            if(res == 0) {
                //timeout
                //resend + increment count
                numbytes = sendto(sockfd, b->data, b->size, 0, p->ai_addr, p->ai_addrlen);
                if (numbytes == -1)
                {
                    perror("Sendto error\n");
                    exit(1);
                }
                count1++;

            }
            else if(res == -1) {
                //error
                perror("Poll error\n");
                return res;

            } else {
                //Receive the ACK/REJ
                socklen_t len = sizeof(their_addr);
                int numbytes = recvfrom(sockfd,buf1,MAXBUFLEN-1, 0,&their_addr, &len);
                if(numbytes == -1){
                    perror("ACK/REJ reception error\n");
                    exit(1);
                }
                ackpack *ack = malloc(sizeof(ackpack));
                rejpack *rej = malloc(sizeof(rejpack));

                //Deserialize the received packet (this handles error checking as well)
                deserialize(ack,rej,buf1);
                free(ack);
                free(rej);
                break;
            };
        }



        //next packet to send
        if(count < send->numseg-1){
            send = send->next;
        }
        b->next = 0;
        count1 = 0;
        count++;
        printf("Seg %d sent and acknowledged.\n",count);
        //LOOP END

    }






    //free memory from server info after sending and close the socket and turn off client
    printf("Transmission Complete, no errors received.\n");
    return 0;
}


//databuffer initilization. Returns a buffer the size needed for datapack
databuf *new_buffer(){
    databuf *b = malloc(sizeof(datapack)*2);

    b->data = malloc(sizeof(datapack));
    b->size = sizeof(datapack);
    b->next = 0;

    return b;
};

//Fragments message into maximum size. Takes in the message buffer and returns a pointer to a packet (linked list if multiple)
datapack *fragment(char message[]) {
    int count = 0,flag1 = 0,i = 0;
    int nsegs = ceil(((float) strlen(message))/((float) MAXPAY)); //find number of fragments to create based on message length
    printf("nsegs = %d\n",nsegs);
    datapack *sendpack = malloc(sizeof(datapack)*nsegs);        //create array with as many packets as needed

    //loop for as many segments as you need
    while(count < nsegs)
    {
        //initialize all the constant data for the packet
        sendpack[count].startid = STARTID;
        sendpack[count].clientid = CLIENTID;
        sendpack[count].data = DATA;
        sendpack[count].segnum = count +1;
        sendpack[count].numseg = nsegs;
        sendpack[count].len = MAXPAY;

        //Insert payload into packet one byte at a time. If message is less than max length, the rest will be set to null characters.
        //This is to ensure uniformity throughout the packets, but length is adjusted and the server will not loop through empty data.
        i = 0;
        while(i < MAXPAY){
            sendpack[count].payload[i] = message[255*count+i];
            if(message[255*count+i] == '\0' && flag1 != 1){
                sendpack[count].len = i; //Set length to i to error check server side. All extra will be null
                flag1 = 1;
            }
            i++;
        }

        //Insert endid
        sendpack[count].endid = ENDID;

        //If creating more than one packet, string them together as a linked list
        if(count > 0)
        {
            sendpack[count-1].next = &sendpack[count];
        }

        //Increment count and reset the flag if necessary.
        count++;
        flag1 = 0;
    }

    return sendpack;
}

//Serialize each fragment. Takes in a packet and a pointer to storage. Serializes the data and places it in the storage
void serialize_Data(datapack send, databuf *output) {
    serialize_short(send.startid,output);
    serialize_char(send.clientid,output);
    serialize_short(send.data,output);
    serialize_char(send.segnum,output);
    serialize_char(send.len,output);
    int i;
    for(i = 0; i < MAXPAY; i++)
    {
        serialize_char(send.payload[i],output);
    }
    serialize_short(send.endid,output);
}

//Serialize by data type. Takes one piece of data of a given and a buffer.
//Places the data in the next spot and increments the location according to the size
void serialize_short(short x, databuf *b) {
    //reserve(b, sizeof(short));
    memcpy(((char *)b->data) + b->next, &x, sizeof(short));
    b->next += sizeof(short);
};

void serialize_char(char x, databuf *b) {
    //reserve(b,sizeof(char));
    memcpy(((char *)b->data)+ b->next,&x,sizeof(char));
    b->next += sizeof(char);
};

//Deserialize ACK/REJ packs. Takes in a pointer to storage for an ack or rej and a buffer to be deserialized.
//Returns a value based on the message inside the packet.
int deserialize(ackpack *ack,rejpack *rej, char buffer[]){


    //Since this is client side, only checks necessary bits. If they are ACK, it'll return 1.
    // Otherwise it will exit according to the rejection code.
    if(((u_char) buffer[3] == 0xf2 && (u_char) buffer[4] == 0xff)
       || ((u_char) buffer[3] == 0xff && (u_char) buffer[4] == 0xf2)){
        ack->ack = ACK;
        printf("ACK received\n");
        return 1;

    }else if(((u_char) buffer[3] == 0xf3 && (u_char) buffer[4] == 0xff)
            || ((u_char) buffer[3] == 0xff && (u_char) buffer[4] == 0xf3)){

        rej->reject = REJECT;
        rej->subc = buffer[5] + buffer[6]+1;
        if(rej->subc == 1){
            perror("Error: Packet StartID incorrect\n");
            exit(1);
        } else if(rej->subc == 2){
            perror("Error: Packet client field incorrect\n");
            exit(2);
        } else if(rej->subc == 3){
            perror("Error: Packet DATA field incorrect\n");
            exit(3);
        } else if(rej->subc == REJSUB1){
            perror("Error: Packet Segment Out of Order\n");
            exit(REJSUB1);
        } else if(rej->subc == REJSUB2){
            perror("Error: Payload does not match length\n");
            exit(REJSUB2);
        } else if(rej->subc == REJSUB3){
            perror("Error: Missing End of Packet ID\n");
            exit(REJSUB3);
        } else if(rej->subc == REJSUB4){
            perror("Error: Duplicate Packet sent\n");
            exit(REJSUB4);
        }

    } else{
        return 2;
    }

};