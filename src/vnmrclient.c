/*
 * vnmrclient.c
 * ------------
 * Client program on console host, called from user_go with stimulus setup
 * parameters as input, and sending them to TCP server mribg
 *
 * 
 *
 *
 *
 */


#include "common.h"
#include "socketcomm.h"

#define VERBOSE 1

int main(int argc, char **argv) {

    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[BUFS];
    char msg[BUFS];
    int ret;

    // input check
    ret = make_msg(msg, argc, argv);

    portno = MRIBGPORT; // 8080

    /* Create a socket point */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
       perror("ERROR opening socket");
       exit(1);
    }

    if (server == NULL) {
       fprintf(stderr,"ERROR, no such host\n");
       exit(0);
    }

    server = gethostbyname(MRIBGHOST);
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
            (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);

    /* Now connect to the server */
    ret = connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if(ret < 0){
       perror("ERROR connecting");
       exit(1);
    }

    /* Now ask for a message from the user, this message
       * will be read by server
    */
     

    /* Send message to the server */

    memset(buffer, 0, BUFS);
    snprintf(buffer, sizeof(buffer), "%s",msg);
    n = write(sockfd, buffer, strlen(buffer));

    if (n < 0) {
       perror("ERROR writing to socket");
       exit(1);
    }

    /* Now read server response */
    n = read(sockfd, buffer, BUFS-1);

    if (n < 0) {
       perror("ERROR reading from socket");
       exit(1);
    }
    if(strncmp(buffer, MSG_ACCEPT, strlen(MSG_ACCEPT)) == 0){
        if(VERBOSE > 0){
            printf("%s\n",buffer);
        }
        return 0;
    } else if(strncmp(buffer, MSG_REJECT, strlen(MSG_REJECT)) == 0){

        fprintf(stderr, "vnmrclient: message was not processed by server\n");
        return 1;

    } else{

        return -1;
    }

}

