/*
**                  Viziteu David-Andrei 2E1
** client.cpp
** made by modifying the server exmaple from the beej's guite (link in the resources section below)
*/
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <thread>

#define MAXDATASIZE 1000  // max number of bytes we can get at once
char *PORT;               // the port client will be connecting to
auto sock_flags = MSG_CONFIRM | MSG_NOSIGNAL;
#define handle_err(msg) \
    {                   \
        perror(msg);    \
        exit(1);        \
    }
using namespace std;

int q_time, id, queue_time;
char ans = '.';
char q_text[1000];
bool finish = false;
char nickname[100];
int accepted = 0;
// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

int reach_server(const char *hostname) {
    int sock_fd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;  //indicates that getaddrinfo() should return socket addresses for any address family ipv4/6
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(hostname, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }

    // loop through all the results and connect to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sock_fd);
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "failed to connect. maybe wrong port?\n");
        exit(1);
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
    printf("connecting to the server %s\n", s);

    freeaddrinfo(servinfo);  // all done with this structure
    return sock_fd;
}

void fetch_basics(int fd) {
    cout << "Fetching your id and queue time...\n\n";
    size_t result = recv(fd, &id, 4, sock_flags);
    size_t result2 = recv(fd, &queue_time, 4, sock_flags);

    //if connections closes recv returns something > 0
    if (errno == ECONNRESET || errno == EPIPE) {
        cout << "The server closed connection unexpectedly\n";
        finish = true;
        exit(2);
    }
    if (result <= 0) {
        if (errno == ECONNRESET || errno == EPIPE) {
            cout << "The server closed connection unexpectedly\n";
            finish = true;
            exit(2);
        } else
            handle_err("Unable to fetch id");
    }
    if (result2 <= 0) {
        if (errno == ECONNRESET || errno == EPIPE) {
            cout << "The server closed connection unexpectedly\n";
            finish = true;
            exit(2);
        } else
            handle_err("Unable to fetch questions time");
    }
    cout << "Your id is: " << id << endl;
    cout << "Please note it down\n\n";
}

void fetch_server_data(int fd) {
    size_t result;
    result = recv(fd, &ans, 1, sock_flags);
    result = recv(fd, &q_time, 4, sock_flags);
    if (result <= 0) {
        if (errno == ECONNRESET || errno == EPIPE || errno == EINVAL) {
            cout << "The server closed connection unexpectedly\n";
            finish = true;
            return;
        } else
            handle_err("Unable to read timer from server");
    }

    if (q_time <= 0) {  // we got our score
        cout << "Total score: " << -q_time << endl;
        char ranking[5000];
        recv(fd, ranking, 5000, sock_flags);
        cout << ranking;
        finish = true;
        return;
    }

    result = recv(fd, &q_text, MAXDATASIZE, sock_flags);
    if (result <= 0) {
        if (errno == ECONNRESET || errno == EPIPE || errno == EINVAL) {
            cout << "The server closed connection unexpectedly\n";
            finish = true;
            return;
        } else
            handle_err("Unable to write question to client");
    }

    cout << endl
         << "Time: " << q_time << " seconds" << endl
         << q_text;
    this_thread::sleep_for(chrono::seconds(q_time));  // sleeping
    cout << "Correct answer: " << ans << endl;
}

void send_answers(int fd) {
    char ans, buff[100];
    do {
        cin.getline(buff, 99);
        if (buff[0] == '\n' || buff[0] == '\r' || buff[0] == '\0')
            continue;  // ignores enters
        if (buff[1] == '\0' || (buff[1] == '\n' && buff[2] == '\0'))
            ans = buff[0];
        else {
            cout << "Please enter only one small letter from a-z or a number from 0-9\n";
            continue;
        }

        if (!((ans >= 'a' && ans <= 'z') || (ans >= '0' && ans <= '9'))) {
            cout << "Only small letters from a-z or numbers from 0-9 are allowed\n";
            continue;
        }
        size_t result = send(fd, &ans, 1, sock_flags);
        if (result <= 0) {
            if (errno == ECONNRESET || errno == EPIPE) {
                cout << "The server closed connection unexpectedly\n";
                finish = true;
                return;
            } else
                handle_err("Unable to write question to client");
        }
    } while (!finish);
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);
    cout << endl;
    if (argc != 3) {
        fprintf(stderr, "usage: client hostname port\n");
        exit(1);
    }
    PORT = argv[2];
    int sock_fd;
    do {
        cout << "nickname?\n";
        cin.getline(nickname, 100);

        sock_fd = reach_server(argv[1]);

        if (-1 == send(sock_fd, &nickname, 100, sock_flags)) {
            perror("Unable to send nickname to server ");
            exit(1);
        }
        if (-1 == recv(sock_fd, &accepted, 4, sock_flags)) {
            perror("Unable to recv if nickname is available from server ");
            exit(1);
        }
        if (accepted == 0) {
            cout << "Already taken :(\nAnother ";
            close(sock_fd);
        }
    } while (accepted != 1);

    fetch_basics(sock_fd);

    cout << "\nWelcome! " << nickname << '\n';
    cout << "Your session will begin in " << queue_time << " seconds or less!\n";
    cout << "If you disconnect anytime from now on, you will be disqualified\n\n";
    char dummy_buff[5000];
    size_t result0 = recv(sock_fd, dummy_buff, 5000, sock_flags);
    cout << dummy_buff << endl;
    try {
        thread t(send_answers, sock_fd);
        t.detach();

    } catch (const std::exception &e) {
        std::cerr << "starting thread\n";
        std::cerr << e.what() << '\n';
    }

    while (!finish) {
        fetch_server_data(sock_fd);
    }
    close(sock_fd);
    return 0;
}
/*
        ADITIONAL RESOURCES:
https://beej.us/guide/bgnet/html//index.html

*/
