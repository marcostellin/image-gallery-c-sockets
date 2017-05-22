
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <stdint.h>

#include <sys/un.h>          
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include "server.h"

#define MAXCONN 50
#define MAXSTRLEN 101

int s_tcp_fd;
int s_udp_fd;
int tcp_port;
char *gw_ip;
int gw_port;
node *head = NULL;

int index_s = 0;
int index_t = 0;

char *serialize_cmd(cmd_add m){

    char *buffer;
    buffer = malloc(sizeof(m));
    memset(buffer, 0, sizeof(m));
    memcpy(buffer, &m, sizeof(m));
    
    return buffer;

}

static void handler(int signum)
{   
    message m;
    m.type = 1;
    m.port = tcp_port;
    
    char *buffer;
    buffer = malloc(sizeof(m));
    memset(buffer, 0, sizeof(m));
    memcpy(buffer, &m, sizeof(m));

    
    struct sockaddr_in gw_addr;
    gw_addr.sin_family = AF_INET;
    gw_addr.sin_port = htons(gw_port);
        
    if (!inet_aton(gw_ip, &gw_addr.sin_addr)){
        perror("Gateway IP not valid");
        exit(1);
    }

    if (sendto(s_udp_fd, buffer, sizeof(m), 0, (struct sockaddr*)&gw_addr, sizeof(gw_addr)) < 0){
        perror("Failed UDP connection with gateway");
        exit(1);
    }
    
    if (close(s_udp_fd) < 0)
        perror("UDP socket not closed");
    
    if (close(s_tcp_fd) < 0)
        perror("TCP socket not closed");
        
    exit(0);
}

void server_tcp_setup(){
    
    s_tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    if (s_tcp_fd == -1) {
        perror("STREAM socket error");
        exit(1);
    }
    
    struct sockaddr_in server_tcp_addr;
    server_tcp_addr.sin_family = AF_INET;
    server_tcp_addr.sin_port = htons(tcp_port);
    server_tcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(s_tcp_fd, (struct sockaddr *)&server_tcp_addr, sizeof(server_tcp_addr)) < 0){
        perror("Binding error");
        exit(1);
    }  
    
}

void server_udp_setup(){
    
    s_udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_udp_fd == -1) {
        perror("DGRAM socket error");
        exit(1);
    }
    
}

void server_to_gw(){
    
    message m;
    m.type = 0;
    m.port = tcp_port;

    printf("%d\n", m.port);
    
    char *buffer;
    buffer = malloc(sizeof(m));
    memset(buffer, 0, sizeof(m));
    memcpy(buffer, &m, sizeof(m));

    
    struct sockaddr_in gw_addr;
    gw_addr.sin_family = AF_INET;
    gw_addr.sin_port = htons(gw_port);
        
    if (!inet_aton(gw_ip, &gw_addr.sin_addr)){
        perror("Gateway IP not valid");
        exit(1);
    }

    if (sendto(s_udp_fd, buffer, sizeof(m), 0, (struct sockaddr*)&gw_addr, sizeof(gw_addr)) < 0){
        perror("Failed UDP connection with gateway");
        exit(1);
    }

}

int accept_connection(){

    if (listen(s_tcp_fd, MAXCONN) < 0){
            perror("Listen problem");
            exit(1);
    }

    int new_tcp_fd;
    new_tcp_fd = accept(s_tcp_fd, NULL, NULL);
    if ( new_tcp_fd < 0){
        perror("Accept problem");
        exit(1);
    }

    return new_tcp_fd;
}

void * serve_client (void * socket){

        int * new_tcp_fd = (int *)socket;

        printf("Client is being served on socket %d\n", *new_tcp_fd);
        
        while (1){

            //printf("Waiting for command...\n");
            char *buffer = malloc(sizeof(cmd_add));

            if (recv(*new_tcp_fd, buffer, sizeof(cmd_add), 0) <= 0){
                break;
            }
            
            cmd_add cmd;
            memcpy(&cmd, buffer, sizeof(cmd));

            //printf("%d\n", cmd.code );

            /* Add photo command */

            if (cmd.code == 10){

                int photo_size = cmd.size;
                char * photo_name = cmd.name;

                char p_array[photo_size];
                int nbytes = 0;
                int cur_index = 0;
                FILE *image;
                image = fopen(cmd.name, "w");

                while(cur_index < photo_size){
                    nbytes = recv(*new_tcp_fd, p_array, photo_size,0);
                    cur_index = cur_index + nbytes;
                    fwrite(p_array, 1, nbytes, image);
                    printf("%d\n", nbytes);
                }
            
                fclose(image);

                node *new_image = malloc(sizeof(node));
                strncpy(new_image->name, cmd.name,100);
                strncpy(new_image->keywords, "\0", 100);
                new_image->identifier = clock() * getpid();

                insert(new_image);
                printlist();

                cmd_add resp;

                resp.type = 1;
                resp.id = new_image->identifier;

                //printf("%d\n", cmd.id );

                char * response = serialize_cmd(resp);

                if (send(*new_tcp_fd, response, sizeof(cmd_add), 0) < 0){
                    perror("Add photo response problem");
                    break;
                }

            } else if (cmd.code == 11){

                //printf("IM HERE\n");

                node * photo_info = search_by_id(cmd.id);

                if (photo_info == NULL){
                    cmd_add resp;
                    resp.type = 2;
                    char * response = serialize_cmd(resp);
                    if (send(*new_tcp_fd, response, sizeof(cmd_add), 0) < 0){
                        perror("Keyword send response error");
                        break;
                    }

                } else {

                    strncpy(photo_info->keywords + strlen(photo_info->keywords), cmd.keyword, MAX_KEYWORD_LEN);
                    printlist();

                    cmd_add resp;
                    resp.type = 1;
                    char * response = serialize_cmd(resp);
                    if (send(*new_tcp_fd, response, sizeof(cmd_add), 0) < 0){
                        perror("Keyword send response error");
                        break;
                    }
                }

            } else if (cmd.code == 12) {

                int status = remove_node(cmd.id);
                printlist();

                if (status == 0){
                    cmd_add resp;
                    resp.type = 2;
                    char * response = serialize_cmd(resp);
                    if (send(*new_tcp_fd, response, sizeof(cmd_add), 0) < 0){
                        break;
                    }
                } else if (status == 1){
                    cmd_add resp;
                    resp.type = 1;
                    char * response = serialize_cmd(resp);
                    if (send(*new_tcp_fd, response, sizeof(cmd_add), 0) < 0){
                        perror("Photo delete response error");
                        break;
                    }

                }

            } else if (cmd.code == 15){

                node * photo_info = search_by_id(cmd.id);

                if (photo_info == NULL){
                    cmd_add resp;
                    resp.type = 2;
                    char * response = serialize_cmd(resp);
                    if (send(*new_tcp_fd, response, sizeof(cmd_add), 0) < 0){
                        perror("Download picture response error");
                        break;
                    }                   
                } else {

                    FILE *picture;
                    picture = fopen(photo_info->name, "r");
                    if (picture == NULL){
                        perror("File not found");
                        exit(1);
                    }

                    int size;
                    fseek(picture, 0, SEEK_END);
                    size = ftell(picture);
                    fseek(picture, 0, SEEK_SET);


                    cmd_add resp;
                    resp.code = 15;
                    resp.type = 1;
                    resp.size = size;

                    char * buffer = serialize_cmd(resp);

                    if (send(*new_tcp_fd, buffer, sizeof(cmd_add), 0) < 0){
                        perror("Download picture response failed");
                        break;
                    }

                    /* Send image */
                    char send_buffer[size];
                    while(!feof(picture)) {
                        int read = fread(send_buffer, 1, sizeof(send_buffer), picture);
                        if (read > 0){
                            int sent = send(*new_tcp_fd, send_buffer, sizeof(send_buffer),0);
                            printf("SENT: %d\n", sent);
                        }

                        bzero(send_buffer, sizeof(send_buffer));
                    }

                    fclose(picture);


                }

            } else if (cmd.code == 13){

                node* cur_node = head;
                int num_keywords = 0;
                int size = 100;
                int index = 0;

                uint32_t* ids = malloc(size*sizeof(uint32_t));

                while ((cur_node = search_by_keyword(cur_node, cmd.keyword)) != NULL){

                    num_keywords++;

                    if (index < size){ 
                        ids[index++] = cur_node->identifier;

                    } else {
                        uint32_t* new_ids = malloc(index*2*sizeof(uint32_t));
                        size = 2*size;

                        for (int i = 0; i < index; i++){
                            new_ids[i] = ids[i];
                        }

                        free(ids);
                        ids = new_ids;
                        ids[index++] = cur_node->identifier;
                    }

                    cur_node = cur_node->next;
                }


                if (num_keywords == 0){

                    cmd_add resp;
                    resp.code = 15;
                    resp.type = 2;

                    char * buffer = serialize_cmd(resp);

                    if (send(*new_tcp_fd, buffer, sizeof(cmd_add), 0) < 0){
                        perror("Search keyword response failed");
                        break;
                    }

                } else {

                    cmd_add resp;
                    resp.code = 15;
                    resp.type = 1;
                    resp.size = num_keywords;

                    char * buffer = serialize_cmd(resp);

                    if (send(*new_tcp_fd, buffer, sizeof(cmd_add), 0) < 0){
                        perror("Search keyword response failed");
                        break;
                    }

                    int i;
                    for (i = 0; i < num_keywords; i++){
                        resp.id = ids[i];
                        //printf("identifier: %u\n", resp.id);
                        buffer = serialize_cmd(resp);
                        if (send(*new_tcp_fd, buffer, sizeof(cmd_add), 0) < 0){
                            perror("Search keyword response failed");
                            break;
                        }

                    }

                    free(ids);

                    if (i < num_keywords){
                        break;
                    }

                }

            } else if (cmd.code == 14){

                node * photo_info = search_by_id(cmd.id);

                if (photo_info == NULL){
                    cmd_add resp;
                    resp.type = 2;
                    char * response = serialize_cmd(resp);
                    if (send(*new_tcp_fd, response, sizeof(cmd_add), 0) < 0){
                        perror("Photo name response error");
                        break;
                    }

                } else {

                    cmd_add resp;
                    resp.type = 1;
                    strncpy(resp.name, photo_info->name, MAX_NAME_LEN);

                    char * response = serialize_cmd(resp);
                    if (send(*new_tcp_fd, response, sizeof(cmd_add), 0) < 0){
                        perror("Keyword send response error");
                        break;
                    }
                }


            }

        }

        //printf("IM HERE2\n");
        if (close(*new_tcp_fd) < 0)
            perror("TCP socket not closed");

        *new_tcp_fd = -1;

        pthread_exit(NULL);
}

void insert(node* new_node){
    
    if (head == NULL){
        head = new_node;
        head->next = NULL;
   
    } else {
        new_node->next = head;
        head = new_node;
    }
    
}

node* search_by_id(uint32_t id){

    if (head == NULL)
        return NULL;

    if (head->identifier == id){
        return head;
    }

    node* cur_node = head;

    while (cur_node->next != NULL){

        if (cur_node->identifier == id){

            return cur_node;
        }

        cur_node = cur_node->next;
    }

    if (cur_node->identifier == id){
        return cur_node;
    }

    return NULL;
}

node* search_by_keyword(node* start, char* keyword){

    if (start == NULL)
        return NULL;

    if (strstr(start->keywords, keyword) != NULL){
        return start;
    }

    node* cur_node = start;

    while (cur_node->next != NULL){

        if (strstr(start->keywords, keyword) != NULL){

            return cur_node;
        }

        cur_node = cur_node->next;
    }

    if (strstr(cur_node->keywords, keyword) != NULL){
        return cur_node;
    }

    return NULL;
}


int remove_node(uint32_t id){

    if (head == NULL)
        return 0;

    if (head->identifier == id){

        node *new_head = head->next;
        free(head);
        head = new_head;
        return 1;
    }

    node* cur_node = head;
    
    while (cur_node->next != NULL){

        if (cur_node->next->identifier == id){

            node *temp = cur_node->next->next;
            free(cur_node->next);
            cur_node->next = temp;
            return 1;
        }

        cur_node = cur_node->next;
    }    

    return 0;
}


void printlist(){

    node * cur_node = head;

    if (cur_node ==  NULL){
        return;
    }

    while (cur_node->next != NULL){
        printf("(Name : %s , keywords: %s, identifier: %u)\n", cur_node->name, cur_node->keywords, cur_node->identifier );
        cur_node = cur_node->next;
    }

    printf("(Name : %s , keywords: %s, identifier: %u)\n", cur_node->name, cur_node->keywords, cur_node->identifier );
}



int main(int argc, char *argv[]){
    
    if (argc < 3){
        printf("[IP gateway] [Port Gateway]\n");
        exit(1);
    }
    
    tcp_port = 3000 + getpid();
    
    gw_ip = argv[1];
    gw_port = atoi(argv[2]);
    
    /* SIGINT handling */
    struct sigaction sa;
    sa.sa_handler = handler;
    if (sigaction(SIGINT, &sa, NULL) < 0){
        perror("Error in handling SIGINT");
        exit(1);
    }
    
    /* Creates socket to send datagrams to gw  */
    
    server_udp_setup();
    
    /* Creates socket of type stream for connections with clients */
    
    server_tcp_setup();
    
    /* Send sock_stream address to gw */
    
    server_to_gw();

    pthread_t threads[MAXCONN];
    int sockets[MAXCONN];
    memset(&sockets, -1, MAXCONN*sizeof(int));
    
    while(1){

        while (sockets[index_s] != -1){
            index_s++;
        }

        sockets[index_s] = accept_connection();

        //printf("%d, %d\n", index_s, sockets[index_s]);

        int error;
        error = pthread_create(&threads[index_t++], NULL, serve_client, &sockets[index_s]);
        if(error != 0){
            perror("Thread creation error");
            exit(-1);
        }

        index_s++;

        if (index_t >= MAXCONN)
            index_t = 0;
    
    }

    exit(0);
    
}
