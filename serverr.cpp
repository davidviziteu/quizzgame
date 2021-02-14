/*                  QuizzGame
**                  Viziteu David-Andrei 2E1
**                  viziteu.david@gmail.com
** serverr.cpp
** made by modifying the server exmaple from the beej's guite (link in the resources section below)
*/

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "./xml_handler/rapidxml.hpp"
using namespace std;

#define xml_file_path "./questions.xml"
char *PORT;
#define max_pending_connections 1000
#define handle_err(msg)                \
    {                                  \
        printf("errno = %d\n", errno); \
        perror(msg);                   \
        exit(1);                       \
    }
//
//
//
//
//
unsigned session_size = 2;           // clients
unsigned q_pause_time = 3;           // seconds
unsigned questions_time = 3;         // seconds
unsigned questions_per_session = 3;  // questions
unsigned queue_time = 10;            // seconds
auto sock_flags = MSG_CONFIRM | MSG_NOSIGNAL | MSG_DONTWAIT;
struct client {
    char ip[INET6_ADDRSTRLEN];  // client's ip address in human readable form
    int fd, id;                 // socket file descriptor, client id
    string answers;             // client's answers
    unsigned int score = 0;     // current/final score
    bool is_connected = true;   // flag
    char nickname[100];
    string *correct_answers;
};
vector<vector<client>> sessions;
vector<pair<string, char>> questions;  // pair of question and answer
bool allow_new_clients, last_session;  // some sort of flags
int total_sessions, current_client_id = 1;
mutex stdout_mutex;              // for threads
vector<int> completed_sessions;  // for exporting functions
int sock_fd;                     // listening socket
//
//
//
//
void *get_in_addr(struct sockaddr *sa);  // get sockaddr, IPv4 or IPv6:
void accept_loop();
int server_init();
void begin_session(int ses_id);
void print_help();
void load_questions(const char *__XML);
void fetch_answer(struct client &cli, int th_id);
void send_question(struct client &cli, const char *text, const int time, const char ans);
void set_nonblock(int fd);
void empty_sock(struct client &cli);
void mark_disconnected(struct client &cli);
int send_client_basics(struct client &cli);
void admin_interface();
void thread_print(const char *th_id, const char *msg);
int get_int(const char *s, const bool ok_zero = false);
int handle_nickname(struct client &cli, vector<client> &current_session);
void add_question(const char *__XML, pair<string, char> &new_q);
//
//
//
int main(int argc, char *argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <PORT>\n";
        exit(1);
    }
    signal(SIGPIPE, SIG_IGN);
    PORT = argv[1];
    sock_fd = server_init();
    load_questions(xml_file_path);
    printf("Ready to accept new connections!\n");
    print_help();
    try {
        thread admin_control(admin_interface);
        admin_control.join();
    } catch (const exception &e) {
        cerr << "thread admin_control\n";
        cerr << e.what() << '\n';
    }
    return 0;
}

void admin_interface() {
    // controller
    bool sessions_running = false;
    bool exported = false;
    char *arg1, *arg2, *arg3, *arg4, *arg_foolproof;
    arg1 = (char *)malloc(100 * sizeof(char));
    arg2 = (char *)malloc(100 * sizeof(char));
    arg3 = (char *)malloc(100 * sizeof(char));
    arg4 = (char *)malloc(100 * sizeof(char));
    arg_foolproof = (char *)malloc(100 * sizeof(char));
    if (arg1 == NULL || arg2 == NULL || arg3 == NULL || arg4 == NULL || arg_foolproof == NULL)
        handle_err("Cannot malloc for admin interface");
    do {
        char console_in[1000];
        cin.getline(console_in, 999);
        cout << "  ";
        //
        // cod din tema 1 (tema mea 1, nu tema altui student, sper ca nu apare ca fiind "plagiat")
        arg1[0] = arg2[0] = arg3[0] = arg4[0] = arg_foolproof[0] = '\0';
        int args_given = sscanf(console_in, "%s %s %s %s %s\n", arg1, arg2, arg3, arg4, arg_foolproof);

        //errs
        if (args_given > 4)
            thread_print("admin interf", "incorrect command\n");

        //exit, help
        if (args_given == 1) {
            if (strcmp("exit", arg1) == 0) {
                if (completed_sessions.size() == total_sessions) {
                    if (!exported && total_sessions != 0) {
                        thread_print("admin interf", "you didnt export the results. type again 'exit' to if you really want to exit\n");
                        exported = true;
                        continue;
                    }
                    thread_print("admin interf", "okay, bye\n");
                    return;
                } else {
                    thread_print("admin interf", "there are still running sessions, I'll wait for them to finish.\n");
                    thread_print("admin interf", "to force the exit, use signals (ctrl + c)\n");
                    // thread_print("admin interf", "do you really want to exit? y/n\n");
                }
            } else if (strcmp("help", arg1) == 0) {
                print_help();
            } else {
                thread_print("admin interf", "incorrect command\n");
            }
        }

        // start / last ses / export
        if (args_given == 2) {
            if (strcmp("start", arg1) == 0 && strcmp("sessions", arg2) == 0) {
                //-------------- start -----------
                if (last_session && total_sessions > 0) {
                    thread_print("admin interf",
                                 "You you can start new sessions only by restarting the app\n");
                    continue;
                }
                thread_print("admin interf", "starting sessions\n");
                try {
                    thread clients_handler(accept_loop);
                    clients_handler.detach();
                    sessions_running = true;
                } catch (const exception &e) {
                    cerr << e.what() << '\n';
                }

            } else if (strcmp("last", arg1) == 0 && strcmp("session", arg2) == 0) {
                //-------------- last -----------
                thread_print("admin interf", "ok, not accepting new clients\n");
                last_session = true;
                // close(sock_fd);
            } else if (strcmp("export", arg1) == 0 && strcmp("results", arg2) == 0) {
                //-------------- exporter -----------
                if (last_session == true && total_sessions == completed_sessions.size())
                    try {
                        if (completed_sessions.size() == 0) {
                            stdout_mutex.lock();
                            cout << "There are no results to be exported\n";
                            stdout_mutex.unlock();
                            continue;
                        }
                        //https://stackoverflow.com/questions/997946/how-to-get-current-time-and-date-in-c
                        auto end = chrono::system_clock::now();
                        time_t now = chrono::system_clock::to_time_t(end);
                        string exp_file_name("Results_");
                        exp_file_name += ctime(&now);  // so that every file name is unique
                        exp_file_name += ".txt";
                        // exp_file_name.replace(exp_file_name.begin() + 7, exp_file_name.end() - 4, ':', '-');
                        fstream exp_file;
                        exp_file.open(exp_file_name, fstream::out);

                        exp_file << "Participants are ordered by their score in each session" << endl;
                        for (auto idx : completed_sessions) {
                            exp_file << "\nResults for session #" << idx << endl;
                            exp_file << "Correct answers: ";
                            auto &c = sessions.at(idx)[0];
                            exp_file << c.correct_answers->data() << endl;
                            for (auto cli : sessions.at(idx)) {
                                exp_file << "\tClient: " << cli.nickname << " id: " << cli.id << "  ip: " << cli.ip;
                                if (cli.is_connected == false)
                                    exp_file << " (left during the session)";
                                exp_file << endl;
                                exp_file << "\t  "
                                         << "Score: " << cli.score << endl;
                                exp_file << "\t  "
                                         << "Answers: " << cli.answers << endl;
                            }
                        }
                        // exp_file << "\n---------------------------\nQuestions:\n";
                        // for (auto itm : questions)
                        //     exp_file << itm.first << " Correct answer: " << itm.second << " Time (seconds): " << questions_time << "\n\n";
                        stdout_mutex.lock();
                        cout << "ok, results exported \n";
                        exported = true;
                        stdout_mutex.unlock();
                    } catch (const exception &e) {
                        cerr << "err exporting results to file";
                        cerr << e.what() << '\n';
                    }
                else {
                    thread_print("results exporter",
                                 "please use command 'last session' and wait for all sessions to finish before exporting\n");
                }

            } else {
                thread_print("admin interf", "incorrect command\n");
            }
        }

        //print results
        if (args_given == 3) {
            if (strcmp("print", arg1) == 0 && strcmp("results", arg2) == 0) {
                if (strcmp("all", arg3) == 0)
                    try {
                        if (completed_sessions.size() == 0) {
                            stdout_mutex.lock();
                            cout << "There are no results to be printed\n";
                            stdout_mutex.unlock();
                            continue;
                        }
                        stdout_mutex.lock();
                        cout << "\nParticipants are ordered by their score";
                        for (auto idx : completed_sessions) {
                            cout << "\nResults for session #" << idx;
                            cout << "\nCorrect answers: ";
                            auto &c = sessions.at(idx)[0];
                            cout << c.correct_answers->data() << endl;
                            for (auto &cli : sessions.at(idx)) {
                                cout << "\tClient id: " << cli.id << "  ip: " << cli.ip;
                                if (cli.is_connected == false)
                                    cout << " (left during the session)";
                                cout << endl;
                                cout << "\t  "
                                     << "Score: " << cli.score << endl;
                                cout << "\t  "
                                     << "Answers: " << cli.answers << endl;
                                // cout << ' ' << ;
                                // cout << endl;
                            }
                        }
                        stdout_mutex.unlock();
                    } catch (const exception &e) {
                        cerr << "stdout mutex err for commnad print results all";
                        cerr << e.what() << '\n';
                    }
                else {
                    bool printed = false;
                    int __res = get_int(arg3, true);
                    if (__res == -1) {
                        thread_print("admin interf", "syntax: print results <session number / all>\n");
                        continue;
                    }
                    for (auto ses : completed_sessions) {
                        if (__res == ses) {
                            try {
                                stdout_mutex.lock();
                                cout << "\nParticipants are ordered by their score";
                                cout << "\nResults for session #" << ses << endl;
                                for (auto cli : sessions.at(ses)) {
                                    cout << "\tClient id: " << cli.id << "  ip: " << cli.ip;
                                    if (cli.is_connected == false)
                                        cout << " (left during the session)";
                                    cout << endl;
                                    cout << "\t  "
                                         << "Score: " << cli.score << endl;
                                    cout << "\t  "
                                         << "Answers: " << cli.answers << endl;
                                }
                                stdout_mutex.unlock();
                                printed = true;

                            } catch (const exception &e) {
                                cerr << "printing results for session #" << ses << endl;
                                cerr << e.what() << '\n';
                            }
                        }
                    }
                    if (!printed)
                        thread_print("printing certain session",
                                     "session did not finish or does not exits\n");
                }

            } else {
                thread_print("admin interf", "incorrect command\n");
            }
        }

        //questions time + session size + session q time
        if (args_given == 4) {
            if (strcmp("set", arg1) == 0 && strcmp("queue", arg2) == 0 && strcmp("time", arg3) == 0) {
                if (sessions_running) {
                    cout << "You can't change queue time while sessions are running\n";
                    // well, you actually can but this effect will (probably) be applied for the next "waiting rooms"
                    continue;
                }
                int __res = get_int(arg4);
                if (__res == -1) {
                    thread_print("admin interf", "syntax: set queue time <positive integer>\n");
                    continue;
                }
                queue_time = __res;
                thread_print("admin interf", "ok, queue time changed\n");
                continue;
            }
            if (strcmp("set", arg1) == 0 && strcmp("session", arg2) == 0 && strcmp("size", arg3) == 0) {
                if (sessions_running) {
                    cout << "You can't change question time while sessions are running\n";
                    // well, you actually can but this effect will (probably) be applied for the next "waiting rooms"
                    continue;
                }
                int __res = get_int(arg4);
                if (__res == -1) {
                    thread_print("admin interf", "syntax: set session size <positive integer>\n");
                    continue;
                }
                session_size = __res;
                thread_print("admin interf", "ok, session size changed\n");
                continue;
            }
            if (strcmp("set", arg1) == 0 && strcmp("questions", arg2) == 0 && strcmp("time", arg3) == 0) {
                if (sessions_running) {
                    cout << "You can't change questions time while sessions are running\n";
                    // well, you actually can but this effect will (probably) be applied for the next "waiting rooms"
                    continue;
                }
                int __res = get_int(arg4);
                if (__res == -1) {
                    thread_print("admin interf", "syntax: set questions time <positive integer>\n");
                    continue;
                }
                questions_time = __res;
                thread_print("admin interf", "ok, question time changed\n");
                continue;
            }
            if (strcmp("set", arg1) == 0 && strcmp("pause", arg2) == 0 && strcmp("time", arg3) == 0) {
                if (sessions_running) {
                    cout << "You can't change pause time while sessions are running\n";
                    // well, you actually can but this effect will (probably) be applied for the next "waiting rooms"
                    continue;
                }
                int __res = get_int(arg4);
                if (__res == -1) {
                    thread_print("admin interf", "syntax: set pause time <positive integer>\n");
                    continue;
                }
                q_pause_time = __res;
                thread_print("admin interf", "ok, pause time changed\n");
                continue;
            }
            if (strcmp("set", arg1) == 0 && strcmp("questions", arg2) == 0 && strcmp("number", arg3) == 0) {
                if (sessions_running) {
                    cout << "You can't change questions number while sessions are running\n";
                    // well, you actually can but this effect will (probably) be applied for the next "waiting rooms"
                    continue;
                }
                int __res = get_int(arg4);
                if (__res > questions.size()) {
                    thread_print("admin interf", "there are not so many questions loaded\n");
                    continue;
                }
                if (__res == -1) {
                    thread_print("admin interf", "syntax: set questions number <positive integer>\n");
                    continue;
                }
                questions_per_session = __res;
                thread_print("admin interf", "ok, questions number changed\n");
                continue;
            }
            thread_print("admin interf", "incorrect command\n");
        }
    } while (1);
    free(arg1), free(arg2), free(arg3), free(arg4), free(arg_foolproof);
}

void print_help() {
    stdout_mutex.lock();
    cout << endl;
    cout << "waiting for clients on port " << PORT << endl
         << endl;
    cout << "Questions loaded: " << questions.size() << endl;
    cout << "Session size is " << session_size << " clients" << endl;
    cout << "Pause in between questions " << q_pause_time << " seconds" << endl;
    cout << questions_per_session << " questions per session" << endl;
    cout << "Queue time is " << queue_time << " seconds" << endl;
    cout << "Questions time is " << questions_time << " seconds" << endl
         << endl;
    cout << "Available commands:\n";
    cout << " - set session size <number>\n";
    cout << " - set questions time <integer number - seconds>\n";
    cout << " - set questions number <integer number>\n";
    cout << " - set pause time <integer number - seconds>\n";
    cout << " - set queue time <integer number - seconds>\n";
    cout << " - print results <finished session number / all>\n";
    cout << " - export results\n";
    cout << " - start sessions\n";
    cout << " - last session\n";
    cout << " - help\n";
    cout << " - exit\n";
    stdout_mutex.unlock();
}

int server_init() {
    int sock_fd, __res;  // listen on sock_fd; __res is for error checking and such
    struct addrinfo hints, *servinfo, *p;
    int yes = 1;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;  //indicates that getaddrinfo() should return socket addresses for any address family ipv4/6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;  // use my IP
    //and node is NULL, then the returned socket addresses will be suitable for bind(2)ing a socket that will accept(2) connections.
    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }

    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }

        if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt SO_REUSEADDR");
            exit(1);
        }

        if (bind(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sock_fd);
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo);  // all done with this structure

    if (p == NULL) {
        fprintf(stderr, "failed to bind to given port. maybe port is already in use?\n");
        exit(1);
    }

    if (listen(sock_fd, max_pending_connections) == -1)
        handle_err("listen");
    allow_new_clients = true;
    last_session = false;
    return sock_fd;
}

void accept_loop() {
    do {
        int __res, yes = 1;                  // for err checking, yes is yes
        struct sockaddr_storage their_addr;  // connector's address information
        socklen_t sin_size;
        char s[INET6_ADDRSTRLEN];
        client new_client;
        vector<client> clients_buff;

        //epoll
        int epoll_fd = epoll_create1(0);
        if (epoll_fd < 0) handle_err("epoll");
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = sock_fd;
        __res = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &ev);
        if (__res < 0) handle_err("fatal error - epoll_ctl: listen_sock");

        //timer - https://stackoverflow.com/questions/997946/how-to-get-current-time-and-date-in-c
        auto start = chrono::system_clock::now();
        auto end = start;
        bool clock_update_flag = true;
        do {  // main accept() loop
            sin_size = sizeof their_addr;

            auto readyfds = epoll_wait(epoll_fd, &ev, 1, queue_time * 1000);
            if (readyfds < 0) handle_err("epoll wait");
            if (readyfds > 0) {
                new_client.fd = accept(sock_fd, (struct sockaddr *)&their_addr, &sin_size);
                if (new_client.fd == -1) {
                    perror("accept");
                    continue;
                }

                if (setsockopt(new_client.fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof yes) == -1)
                    handle_err("setsockopt - accept main loop");
                // do stuff with the client & his data
                set_nonblock(new_client.fd);
                //check for existing nickname;
                __res = handle_nickname(new_client, clients_buff);
                if (__res == 0)
                    continue;
                inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
                printf("server: got connection from %s. id assigned: %d\n", s, current_client_id);
                strcpy(new_client.ip, s);
                new_client.id = current_client_id;

                if (1 == (__res = send_client_basics(new_client))) {
                    current_client_id++;
                    clients_buff.emplace_back(new_client);

                    if (clock_update_flag)
                        start = chrono::system_clock::now(), clock_update_flag = false;
                    // the above if: to start counting time from the first client that joins a new session
                }

                if (clients_buff.size() == session_size)
                    allow_new_clients = false;
            }
            if (readyfds == 0 && clients_buff.size() != 0) {
                allow_new_clients = false;
            }
            end = chrono::system_clock::now();
            chrono::duration<double> elapsed_seconds = end - start;

            if (elapsed_seconds.count() + 1 > queue_time && clients_buff.size() != 0)
                allow_new_clients = false;

        } while (allow_new_clients && !last_session);
        if (last_session && clients_buff.size() == 0)
            break;
        allow_new_clients = true;  //gets ready for the next session
        //start new thread
        try {
            sessions.emplace_back(clients_buff);
            thread new_ses(&begin_session, total_sessions);
            new_ses.detach();
            clients_buff.clear();
        } catch (const exception &e) {
            cerr << "launch new session / sessions.emplace_back(clients buff) / clients buff clean\n";
            cerr << e.what() << '\n';
        }
        total_sessions++;
        start = chrono::system_clock::now();
        clock_update_flag = true;
    } while (!last_session);
    close(sock_fd);
}

int get_int(const char *s, const bool ok_zero) {
    // returns atoi(s) if s *is* a strictly positive
    // return -1 otherwise
    // returns 0 if "ok zero" is set to true and atoi(s) == 0
    // returns -1 if "ok zero" is NOT set to true and atoi(s) == 0
    int i;
    for (i = 0; s[i] != '\0'; ++i) {
        if (strchr("0123456789", s[i]) == NULL)
            if (s[i] == '\0')
                continue;
            else
                return -1;
    }
    i = 0;
    while (s[i] == '0')  // if s == "0000000000000000000000000000001"
        ++i;
    if (strlen(s + i) > 4)  // avoids too large numbers such as "313213513132135131351313213"
        return -1;
    auto ret_valute = atoi(s);
    if (ok_zero) {  // returns 0 if "ok zero" is set to true and atoi(s) == 0
        if (ret_valute < 0)
            return -1;
    } else if (ret_valute <= 0)  // returns -1 if "ok zero" is NOT set to true and atoi(s) == 0
        return -1;
    return ret_valute;
}

void begin_session(int ses_id) {
    // mersenne twister (:
    auto seed = chrono::high_resolution_clock::now().time_since_epoch().count();
    mt19937_64 mt_rand(seed);

    string *local_answers;
    vector<pair<string, char>> questions_local;  // pair of question and answer for specific session
    try {
        set<int> loaded_indexes;
        local_answers = new string();  // for clients pointer. to remeber the correct arr of answers / sessuion
        // could have been implemented a bit more efficiently tho
        auto upper_bound = questions.size();
        for (short i = 0; i < questions_per_session; ++i) {
            int idx = mt_rand() % upper_bound;
            if (loaded_indexes.find(idx) == loaded_indexes.end()) {
                loaded_indexes.emplace(idx);
                questions_local.emplace_back(questions.at(idx));
            }
        }
        for (auto &q : questions_local)
            local_answers->push_back(q.second);
    } catch (const std::exception &e) {
        cerr << e.what() << '\n';
        cerr << " begin_session(), at the beginning; questions / session \n";
    }

    stdout_mutex.lock();
    cout << "session " << ses_id << " has started\n";
    stdout_mutex.unlock();
    int active_clients = 0;

    string duelling("Duelling now:\n");

    for (auto &cli : sessions.at(ses_id)) {
        cli.correct_answers = local_answers;
        duelling += cli.nickname;
        duelling += "\n";
    }
    duelling += "\n";

    for (auto &cli : sessions.at(ses_id))
        send(cli.fd, duelling.data(), 5000, sock_flags);
    //i dont really care now for errors, it checks later anyways

    for (auto &q : questions_local) {
        int active_clients = 0;
        for (auto &cli : sessions.at(ses_id))  //sending question to every client in the current session
            if (cli.is_connected) {
                empty_sock(cli);  // ignore other things client might have sent
                send_question(cli, q.first.c_str(), questions_time, q.second);
                active_clients++;
            }
        if (active_clients == 0)
            break;

        this_thread::sleep_for(chrono::seconds(questions_time));  // sleeping

        for (auto &cli : sessions.at(ses_id))  // reading answers
            if (cli.is_connected) {
                fetch_answer(cli, ses_id);
            }
        this_thread::sleep_for(chrono::seconds(q_pause_time));  // sleeping
    }
    try {
        //compute score
        for (auto &cli : sessions.at(ses_id))
            for (unsigned i = 0; i < cli.answers.size(); ++i) {
                if (cli.answers[i] == questions_local[i].second)
                    cli.score++;
            }
        for (auto &cli : sessions.at(ses_id))
            if (cli.is_connected) {
                // informs client about his score
                int dummy = -cli.score;
                send(cli.fd, "F", 1, sock_flags);
                send(cli.fd, &dummy, 4, sock_flags);
            }

        auto &ses_arr = sessions.at(ses_id);
        sort(ses_arr.begin(), ses_arr.end(), [](struct client &a, struct client &b) {
            return a.score > b.score;
        });

        //telling clients their rankning
        string ranking("Ranking:\n");
        for (auto &cli : ses_arr) {
            ranking += cli.nickname;
            ranking += " with ";
            char ch_score[5];
            sprintf(ch_score, "%d", cli.score);
            ranking += ch_score;
            ranking += " points\n";
        }

        for (auto &cli : ses_arr) {
            send(cli.fd, ranking.data(), ranking.length() + 1, sock_flags);
            close(cli.fd);
        }

        completed_sessions.emplace_back(ses_id);
        stdout_mutex.lock();
        cout << "session " << ses_id << " has ended. computed participants' score. results can now be printed\n";
        stdout_mutex.unlock();
    } catch (const exception &e) {
        cerr << "last part of begin_session, thread #" << ses_id << endl;
        cerr << e.what() << "\n\n";
    }
}

void thread_print(const char *th_id, const char *msg) {
    try {
        stdout_mutex.lock();
        cout << msg;
        stdout_mutex.unlock();
    } catch (const exception &e) {
        cerr << "err printing - thread #" << th_id;
        cerr << e.what() << '\n';
    }
}

void *get_in_addr(struct sockaddr *sa) {
    // get sockaddr, IPv4 or IPv6:
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

inline void load_questions(const char *__XML) {
    //https://gist.github.com/JSchaenzle/2726944
    using namespace rapidxml;
    try {
        cout << "Loading questions..." << endl;
        xml_document<> doc;
        xml_node<> *root_node;
        ifstream theFile(__XML);
        if (!theFile.is_open()) {
            cerr << "Unable to open .xml questions file: " << __XML << endl;
            exit(1);
        }
        vector<char> buffer((istreambuf_iterator<char>(theFile)), istreambuf_iterator<char>());
        buffer.push_back('\0');
        doc.parse<0>(&buffer[0]);
        root_node = doc.first_node("questions");
        string q_buff;
        q_buff.reserve(1000);
        char q_resp;
        for (xml_node<> *entry = root_node->first_node("entry"); entry; entry = entry->next_sibling()) {
            //for each question
            q_buff = entry->first_attribute("text")->value();
            q_buff += string("\n");
            q_resp = entry->first_attribute("ans")->value()[0];
            for (xml_node<> *answer = entry->first_node("ans"); answer; answer = answer->next_sibling()) {
                // Interate over the answers
                q_buff += string(answer->first_attribute("letter")->value());
                q_buff += string(") ");
                q_buff += string(answer->value());
                q_buff += string("\n");
            }
            q_buff += string("\n");
            questions.emplace_back(make_pair(q_buff, q_resp));
            q_buff.clear();
        }
        cout << "Questions loaded!" << endl
             << endl;
    } catch (const exception &e) {
        cerr << "xml loader\n";
        cerr << e.what() << '\n';
        exit(1);
    }

    // questions.emplace_back(make_pair(string("haha1. time: 60 seconds. possible answers: \n a) ala \n b) bala \n c) porto \n d) cala\n"), 'a'));
    // questions.emplace_back(make_pair(string("haha2. time: 60 seconds. possible answers: \n a) ala \n b) bala \n c) porto \n d) cala\n"), 'a'));
    // questions.emplace_back(make_pair(string("haha3. time: 60 seconds. possible answers: \n a) ala \n b) bala \n c) porto \n d) cala\n"), 'a'));
    // questions.emplace_back(make_pair(string("haha4. time: 60 seconds. possible answers: \n a) ala \n b) bala \n c) porto \n d) cala\n"), 'a'));
    // questions.emplace_back(make_pair(string("haha5. time: 60 seconds. possible answers: \n a) ala \n b) bala \n c) porto \n d) cala\n"), 'a'));
    // questions.emplace_back(make_pair(string("haha6. time: 60 seconds. possible answers: \n a) ala \n b) bala \n c) porto \n d) cala\n"), 'a'));
    // questions.emplace_back(make_pair(string("haha7. time: 60 seconds. possible answers: \n a) ala \n b) bala \n c) porto \n d) cala\n"), 'a'));
    // questions.emplace_back(make_pair(string("haha8. time: 60 seconds. possible answers: \n a) ala \n b) bala \n c) porto \n d) cala\n"), 'a'));
}

void send_question(struct client &cli, const char *text, const int time, const char ans) {
    int fd = cli.fd;
    int n = strlen(text) + 1;
    int t = time;
    size_t result;
    result = send(fd, &ans, 1, sock_flags);
    if (result < 0) {
        if (errno == ECONNRESET || errno == EPIPE) {
            mark_disconnected(cli);
            return;
        } else
            handle_err("cannot write ans to client");
    }
    result = send(fd, &t, 4, sock_flags);
    if (result < 0) {
        if (errno == ECONNRESET || errno == EPIPE) {
            mark_disconnected(cli);
            return;
        } else
            handle_err("cannot write timer to client");
    }
    result = send(fd, text, n, sock_flags);
    if (result < 0) {
        if (errno == ECONNRESET || errno == EPIPE) {
            mark_disconnected(cli);
            return;
        } else
            handle_err("cannot write question to client");
    }
}

void fetch_answer(struct client &cli, int th_id) {
    int __res;
    char ans;
    __res = recv(cli.fd, &ans, 1, sock_flags);
    if (__res <= 0) {
        if (errno == ECONNRESET || errno == EPIPE || errno == EBADF) {
            mark_disconnected(cli);
            return;
        } else if (errno == EWOULDBLOCK) {
            ans = '/';  //timeout
        } else {
            cerr << " Unable to read answer from client id = " << cli.id << endl;
            perror(" Most probably he disconnected: ");
            mark_disconnected(cli);
        }
    }
    // added for "demo client"
    if (ans != '/')  // checks if the answer was marked as timeout first, if not, then
        if (!(('0' <= ans && ans <= '9') || ('a' <= (ans) && (ans) <= 'z') || ('A' <= (ans) && (ans) <= 'Z')))
            ans = '?';  // treat unknown characters
    try {
        cli.answers.push_back(ans);
    } catch (const exception &e) {
        cerr << "client" << cli.id << " push back answer, session #" << th_id << endl;
        cerr << e.what() << '\n';
    }
}

inline void empty_sock(struct client &cli) {
    int __res;
    char ans[100];
    __res = recv(cli.fd, &ans, 100, sock_flags);
    if (__res <= 0)
        if (errno == ECONNRESET || errno == EPIPE) {
            cli.is_connected = false;
            close(cli.fd);
            return;
        }
}

inline void mark_disconnected(struct client &cli) {
    cli.is_connected = false;
    close(cli.fd);
}

int send_client_basics(struct client &cli) {
    size_t result = send(cli.fd, &cli.id, 4, sock_flags);
    size_t result2 = send(cli.fd, &queue_time, 4, sock_flags);
    if (result != 4) {
        perror("cannot send id to client.. skipping him");
        return -1;
    }
    if (result2 != 4) {
        perror("cannot send quete time to client.. skipping him");
        return -1;
    }
    return 1;
}

inline void set_nonblock(int fd) {
    int res;
    if (-1 == (res = fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0) | O_NONBLOCK)))
        handle_err("fnctl");
}

int handle_nickname(struct client &new_cli, vector<client> &current_session) {
    auto nick_flags = sock_flags & ~MSG_DONTWAIT;
    int __res = 0;
    char new_nick[100];
    if (-1 == (__res = recv(new_cli.fd, new_nick, 100, nick_flags))) {
        close(new_cli.fd);
        perror("cannot recv nickname");
        return 0;
    }
    int res = 0;
    for (auto &cli : current_session)
        if (strcmp(cli.nickname, new_nick) == 0) {
            cout << "nickname taken\n";
            send(new_cli.fd, &res, 4, nick_flags);
            close(new_cli.fd);
            return 0;
        }
    res = 1;
    send(new_cli.fd, &res, 4, nick_flags);
    strcpy(new_cli.nickname, new_nick);
    return 1;
}

void add_question(const char *__XML, const string &q_text, const char q_ans, vector<string> &q_choices) {
    // using namespace rapidxml;

    // xml_document<> doc;
    // xml_node<> *root_node;
    // ifstream theFile(__XML);
    // if (!theFile.is_open()) {
    //     cerr << "Unable to open .xml questions file: " << __XML << endl;
    //     exit(1);
    // }
    // vector<char> buffer((istreambuf_iterator<char>(theFile)), istreambuf_iterator<char>());
    // buffer.push_back('\0');
    // doc.parse<0>(&buffer[0]);

    // xml_node<> *node = doc.allocate_node(node_element, "a", "Google");
    // doc.append_node(node);
    // xml_attribute<> *attr = doc.allocate_attribute("href", "google.com");
    // node->append_attribute(attr);
}
/*
        ADITIONAL RESOURCES:
https://beej.us/guide/bgnet/html//index.html
http://www.cplusplus.com/reference/thread/thread/
https://profs.info.uaic.ro/~andrei.panu/
https://stackoverflow.com/questions/6374264/is-cout-synchronized-thread-safe

peste astea am dat (intamplator) cand faceam temele de la seminar si am retinut informatii din ele:
https://stackoverflow.com/questions/108183/how-to-prevent-sigpipes-or-handle-them-properly
https://stackoverflow.com/questions/2593236/how-to-know-if-the-client-has-terminated-in-sockets

*/
// todo:
// -client propose questions module