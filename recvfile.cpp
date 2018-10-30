#include <iostream>
#include <thread>

#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>

#include "helpers.h"

using namespace std;

int socket_fd;
struct sockaddr_in server_addr, client_addr;

void send_ack() {
    char frame[MAX_FRAME_SIZE];
    char data[MAX_DATA_SIZE];
    char ack[ACK_SIZE];
    size_t frame_size;
    size_t data_size;
    socklen_t client_addr_size;
    
    unsigned int recv_seq_num;
    bool frame_error;
    bool eot;

    while (true) {
        frame_size = recvfrom(socket_fd, (char *)frame, MAX_FRAME_SIZE, 
                MSG_WAITALL, (struct sockaddr *) &client_addr, 
                &client_addr_size);
        frame_error = read_frame(&recv_seq_num, data, &data_size, &eot, frame);

        create_ack(recv_seq_num, ack, frame_error);
        sendto(socket_fd, ack, ACK_SIZE, MSG_CONFIRM, 
                (const struct sockaddr *) &client_addr, client_addr_size);
    }
}

int main(int argc, char * argv[]) {
    unsigned int port;
    unsigned int window_size;
    ssize_t max_buffer_size;
    char *fname;

    if (argc == 5) {
        fname = argv[1];
        window_size = (size_t) atoi(argv[2]) * (size_t) 1024;
        max_buffer_size = (size_t) atoi(argv[3]) * (size_t) 1024;
        port = atoi(argv[4]);
    } else {
        cerr << "usage: ./recvfile <filename> <window_size> <buffer_size> <port>" << endl;
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr)); 
    memset(&client_addr, 0, sizeof(client_addr)); 
      
    /* Fill server address data structure */
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; 
    server_addr.sin_port = htons(port);

    /* Create socket file descriptor */ 
    if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        cerr << "socket creation failed" << endl;
        return 1;
    }

    /* Bind socket to server address */
    if (bind(socket_fd, (const struct sockaddr *)&server_addr, 
            sizeof(server_addr)) < 0) { 
        cerr << "socket binding failed" << endl;
        return 1;
    }

    FILE *file = fopen(fname, "wb");
    char frame[MAX_FRAME_SIZE];
    char data[MAX_DATA_SIZE];
    char ack[ACK_SIZE];
    size_t frame_size;
    size_t data_size;
    ssize_t lfr, laf;
    socklen_t client_addr_size;
    
    char *buffer;
    size_t buffer_size;
    bool eot;
    bool frame_error;
    unsigned int recv_seq_num;

    bool recv_done = false;
    unsigned int frame_num = 0;
    while (!recv_done) {
        buffer = new char[max_buffer_size];
        buffer_size = max_buffer_size;
    
        unsigned int recv_seq_count = max_buffer_size / MAX_DATA_SIZE;
        bool window_recv_mask[window_size];
        for (int i = 0; i < window_size; i++) {
            window_recv_mask[i] = false;
        }

        lfr = -1;
        laf = lfr + window_size;
        
        while (true) {
            frame_size = recvfrom(socket_fd, (char *)frame, MAX_FRAME_SIZE, 
                    MSG_WAITALL, (struct sockaddr *) &client_addr, 
                    &client_addr_size);
            frame_error = read_frame(&recv_seq_num, data, &data_size, &eot, frame);

            create_ack(recv_seq_num, ack, frame_error);
            sendto(socket_fd, ack, ACK_SIZE, MSG_CONFIRM, 
                    (const struct sockaddr *) &client_addr, client_addr_size);

            if (recv_seq_num <= laf) {
                if (!frame_error) {
                    size_t buffer_shift = recv_seq_num * MAX_DATA_SIZE;

                    if (recv_seq_num == lfr + 1) {
                        memcpy(buffer + buffer_shift, data, data_size);

                        unsigned int shift = 1;
                        for (unsigned int i = 1; i < window_size; i++) {
                            if (!window_recv_mask[i]) break;
                            shift += 1;
                        }
                        for (unsigned int i = 0; i < window_size - shift; i++) {
                            window_recv_mask[i] = window_recv_mask[i + shift];
                        }
                        for (unsigned int i = window_size - shift; i < window_size; i++) {
                            window_recv_mask[i] = false;
                        }
                        lfr += shift;
                        laf = lfr + window_size;
                    } else if (recv_seq_num > lfr + 1) {
                        if (!window_recv_mask[recv_seq_num - (lfr + 1)]) {
                            memcpy(buffer + buffer_shift, data, data_size);
                            window_recv_mask[recv_seq_num - (lfr + 1)] = true;
                        }
                    }

                    if (eot) {
                        buffer_size = buffer_shift + data_size;
                        recv_seq_count = recv_seq_num + 1;
                        recv_done = true;
                        cout << "[" << frame_num << " RECV FRAME EOT " << recv_seq_num << "] " << data_size << " bytes" << endl;
                        cout << "[" << frame_num << " SENT ACK EOT " << recv_seq_num << "]" << endl;
                    } else {
                        cout << "[" << frame_num << " RECV FRAME " << recv_seq_num << "] " << data_size << " bytes" << endl;
                        cout << "[" << frame_num << " SENT ACK " << recv_seq_num << "]" << endl;
                    }
                } else {
                    cout << "[" << frame_num << " ERR FRAME " << recv_seq_num << "] " << data_size << " bytes" << endl;
                    cout << "[" << frame_num << " SENT NAK " << recv_seq_num << "]" << endl;
                }
            } else {
                cout << "[" << frame_num << " IGN FRAME " << recv_seq_num << "] " << data_size << " bytes" << endl;

            }

            if (lfr == recv_seq_count - 1) {
                break;
            }
        }

        frame_num += 1;
        fwrite(buffer, 1, buffer_size, file);
        memset(buffer, 0, buffer_size);
    }

    fclose(file);

    /* Start thread to keep sending ack to sender for 4 seconds */
    cout << "[SENDING REQUESTED ACK FOR 4 SECONDS...]" << endl;
    thread ack_thread(send_ack);
    sleep_for(4000);
    ack_thread.detach();

    return 0;
}