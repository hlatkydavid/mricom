/*
 * socketcomm.h
 *
 */

#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#define BUFS 256
#define MRIBGPORT 8080
#define MRIBGHOST "r155806"
#define MRIBGLOG "mribg.log"

#define MSG_ACCEPT "ACCEPTED"
#define MSG_REJECT "REJECTED"

int make_msg(char *msg, int argc, char **argv);
int parse_msg(char *msg, char **argv);

