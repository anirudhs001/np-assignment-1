
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <stdio.h> 
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/resource.h> // Required to increase limit on size of msg queue

#define and &&
#define or ||

#define INFY __INT_MAX__
#define MAX_MSG_LENGTH 200
#define MAX_NAME_LENGTH 20
#define SERVER_MSGQ_PATH "chat.c"
#define MAX_NUM_USERS 10 
#define MAX_NUM_GROUPS 10
#define MAX_GROUP_SIZE 10
#define MAX_CHUNK_SIZE 1024 

#define SERV_PORT 8080 // TODO: don't use this
#define LISTEN_Q 10

#define USER_LIST_REQ_M_TYPE 30
#define USER_LIST_RESP_M_TYPE 31
#define ADD_REQ_M_TYPE 32
#define STATUS_REQ_M_TYPE 33
#define BASIC_RESP_M_TYPE 34
#define CREATE_GROUP_REQ_M_TYPE 35
#define GROUP_LIST_RESP_M_TYPE 36
#define BROADCAST_M_TYPE 40

#define RED   		"\033[0;31m"
#define GREEN 		"\033[0;32m"
#define YELLOW 		"\033[0;33m"
#define BLUE 		"\033[0;34m"
#define PURPLE 		"\033[0;35m"
#define CYAN 		"\033[0;36m"
#define INVERT		"\033[0;7m"
#define RESET  		"\e[0m" 
#define BOLD		"\e[1m"
#define ITALICS		"\e[3m"
#define UNDERLINE	"\e[4m"

typedef struct
{
    long m_type;                // all messages sent by this user will have this message type
                                // mtype must be unique for each user
    int user_id;                // unique user_id for each user (idk use if reqd)
    char name[MAX_NAME_LENGTH]; // username string stored in struct itself
    int status;                 // online or not
    // anything else we might need later
} USER;

typedef struct
{
    USER users[MAX_NUM_USERS];
    int user_count;
} USER_DICTIONARY;

typedef struct
{
    long m_type;
    int num_members;
    char name[MAX_NAME_LENGTH];
    USER users[MAX_GROUP_SIZE];
} GROUP;

typedef struct 
{
    int group_count;
    GROUP groups[MAX_NUM_GROUPS];
} GROUP_DICTIONARY;

void deep_copy_user_dict(USER_DICTIONARY *src, USER_DICTIONARY *dest)
{
    for (int i = 0; i < src->user_count; i++)
    {
        dest->users[i].m_type = src->users[i].m_type;
        dest->users[i].status = src->users[i].status;
        dest->users[i].user_id = src->users[i].user_id;
        strncpy(dest->users[i].name, src->users[i].name, MAX_NAME_LENGTH);
    }
    dest->user_count = src->user_count;
}

void deep_copy_user(USER *src, USER *dest)
{
    dest->m_type = src->m_type;
    dest->status = src->status;
    dest->user_id = src->user_id;
    strncpy(dest->name, src->name, MAX_NAME_LENGTH);
}

void deep_copy_group(GROUP* src, GROUP* dest) {
    strncpy(dest->name, src->name, sizeof(src->name)); 
    dest->num_members = src->num_members;
    dest->m_type = src->m_type;
    // copy all users
    for (int i = 0; i < src->num_members; i++) {
        deep_copy_user(&(src->users[i]), &(dest->users[i]));
    }
}

void deep_copy_group_dict(GROUP_DICTIONARY* src, GROUP_DICTIONARY* dest) {

    dest->group_count = src->group_count;
    for (int i = 0; i < src->group_count; i++)
    {
        deep_copy_group(&(src->groups[i]), &(dest->groups[i]));
    }
}


#define OK 0
#define ERR 1
typedef struct
{
    long m_type;
    struct {
        char m_data[MAX_MSG_LENGTH]; // message text
        USER user;                   // sender's data in struct itself
        time_t time;                 // time when mesg was sent
    } data;
} CHAT_MESSAGE;

typedef struct
{
    long m_type;
    struct {
        USER_DICTIONARY user_dict;
        // GROUP_DICTIONARY group_dict;
        int user_idx;
    } data;
} USER_LIST_RESPONSE;

typedef struct
{
    long m_type;
    struct
    {
        GROUP_DICTIONARY group_dict;
        int group_idx;
    } data;
} GROUP_LIST_RESPONSE;

typedef struct
{
    long m_type;
} USER_LIST_REQUEST;

typedef struct
{
    long m_type;
    USER user;
} ADD_USER_REQUEST;

typedef struct
{
    long m_type;
    GROUP group;
} CREATE_GROUP_REQUEST;

typedef struct
{
    long m_type;
    struct {
        USER user;
        int new_status;
    } data;
} CHANGE_STATUS_REQUEST;

typedef struct
{
    long m_type;
    int result;
} BASIC_RESPONSE;


void do_nothing(int sig);
void recv_from_msgqueue(int conn_fd, int msg_qid, long targ_m_type, USER user);
void display_user(USER *user);
void handle_client(int conn_fd, int msg_qid);
void handle_msg_queue(int msg_qid);

int main(int argc, char* argv[])
{
    if (argc != 2) {
        printf("[%9d] Usage : %s <PORT>\n", getpid(), argv[0]);
        return -1;
    }
    // // increasing max space allotable to msg queue
    // struct rlimit rlim;
    // rlim.rlim_cur = RLIM_INFINITY;
    // rlim.rlim_max = RLIM_INFINITY;

    // if (setrlimit(RLIMIT_MSGQUEUE, &rlim) == -1) {
    //     perror("setrlimit");
    //     return 1;
    // }

    // Message Queue setup
    int msg_qid;
    key_t key;
    printf("[%9d] Creating Message Queue.\n", getpid());
    if ((key = ftok(SERVER_MSGQ_PATH, 'A')) == -1)
    {
        printf("[%9d] Could not create key. File might not exist.\n", getpid());
        exit(1);
    }
    if ((msg_qid = msgget(key, IPC_CREAT | 0666)) == -1)
    {
        printf("[%9d] Could not create message queue.\n", getpid());
        if (errno == EEXIST)
        {
            printf("[%9d] Message Queue already exists. Proceeding.\n", getpid());
        }
        else
            exit(1);
    }
    printf("[%9d] Message Queue created.\n", getpid());
    // new process to handle requests on message queue
    if (fork() == 0)
    {
        handle_msg_queue(msg_qid);
        return 0;
    }

    // Socket setup
    printf("[%9d] Creating TCP Socket.\n", getpid());
    int listen_fd, conn_fd;
    pid_t child_pid;
    socklen_t cli_len;
    struct sockaddr_in cli_addr, serv_addr;
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    // serv_addr.sin_port = htons(SERV_PORT);
    serv_addr.sin_port = htons(atoi(argv[1]));

    if ((listen_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        printf("Socket creation failed. Exiting.\n");
        return 1;
    }
    bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    listen(listen_fd, LISTEN_Q);

    // Connection handling: new child for each new connection
    for (;;)
    {
        cli_len = sizeof(cli_addr);
        conn_fd = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
        printf("[%9d] Connected to client!\n", getpid());
        if (fork() == 0)
        {
            handle_client(conn_fd, msg_qid);
        }
        close(conn_fd);
    }
    return 0;
}

void display_user(USER *user)
{
    printf("[%9d] Name    : %s\n", getpid(), user->name);
    printf("[%9d] m_type  : %ld\n", getpid(), user->m_type);
    printf("[%9d] user_id : %d\n", getpid(), user->user_id);
}

void handle_client(int conn_fd, int msg_qid)
{

    USER user;
    USER_DICTIONARY user_dict;
    GROUP_DICTIONARY group_dict;
    ADD_USER_REQUEST add_req;
    CREATE_GROUP_REQUEST grp_req;
    USER_LIST_RESPONSE user_list_resp;
    GROUP_LIST_RESPONSE grp_list_resp;
    USER_LIST_REQUEST list_req;
    CHANGE_STATUS_REQUEST stat_req;
    int err;
    user_list_resp.m_type = USER_LIST_RESP_M_TYPE;
    grp_list_resp.m_type = GROUP_LIST_RESP_M_TYPE;
    list_req.m_type = USER_LIST_REQ_M_TYPE;
    add_req.m_type = ADD_REQ_M_TYPE;
    grp_req.m_type = CREATE_GROUP_REQ_M_TYPE;
    stat_req.m_type = STATUS_REQ_M_TYPE;

    signal(SIGUSR1, do_nothing);
    // initialise user
    send(conn_fd, "Enter Username:", sizeof("Enter Username:"), 0);
    recv(conn_fd, user.name, sizeof(user.name), 0);
    user.name[strcspn(user.name, "\r\n")] = '\0';
    printf("[%9d] Received username:%s\n", getpid(), user.name);
    user.user_id = getpid();
    user.m_type = INFY; // placeholder m_type

    // request to add in user dictionary
    deep_copy_user(&user, &add_req.user);
    printf("[%9d] Sending request to main proc to create user.\n", getpid());
    err = msgsnd(msg_qid, &add_req, sizeof(add_req.user), 0);
    printf("[%9d] msgsnd returned %d. errno=%d\n", getpid(), err, errno);
    printf("[%9d] Sent request to main proc to create user.\n", getpid());

    // msgrcv with no flag is blocking call. wait for response.
    // response will also contain the m_type for this user
    msgrcv(msg_qid, &user_list_resp, sizeof(user_list_resp.data), user_list_resp.m_type, 0);
    deep_copy_user_dict(&user_list_resp.data.user_dict, &user_dict);
    int user_idx = user_list_resp.data.user_idx;
    // read m_type of current user from user dictionary
    user.m_type = user_dict.users[user_idx].m_type;
    printf("[%9d] Received response from main proc. User alloted m_type : %ld\n", getpid(), user.m_type);
    // send(conn_fd, &user.m_type, sizeof(user.m_type), 0); // NO NEED TO SEND M_TYPE
    // printf("[%9d] Sent user m_type : (%ld) for user : %s\n", getpid(), user.m_type, user.name);

    // request to change status of new user
    stat_req.data.new_status = 1;
    deep_copy_user(&user, &stat_req.data.user);
    msgsnd(msg_qid, &stat_req, sizeof(stat_req.data), 0);

    // setup done. read and send messages/user list
    printf("[%9d] User created.\n", getpid());
    CHAT_MESSAGE chat_msg;
    chat_msg.data.user = user;

    bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
    sprintf(chat_msg.data.m_data, "User created succesfully. Type help to read the help menu\n");
    send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
    display_user(&user);
    long targ_m_type = -1;
    long effective_m_type = -1;
    for (;;)
    {

        bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
        time(&chat_msg.data.time);
        struct tm msg_time;
        localtime_r(&chat_msg.data.time, &msg_time);
        sprintf(chat_msg.data.m_data, "(%2d:%2d) [%10s] >> ", msg_time.tm_hour, msg_time.tm_min, user.name);
        send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);

        bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
        recv(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
        chat_msg.data.m_data[strcspn(chat_msg.data.m_data, "\r\n")] = '\0';
        if (strncmp(chat_msg.data.m_data, "list", 4) == 0 and targ_m_type <= 0)
        { // user requested a list of users

            // get updated user's dict from main server via msg queue
            msgsnd(msg_qid, &list_req, 0, 0);
            msgrcv(msg_qid, &user_list_resp, sizeof(user_list_resp.data), user_list_resp.m_type, 0);
            deep_copy_user_dict(&user_list_resp.data.user_dict, &user_dict);
            msgrcv(msg_qid, &grp_list_resp, sizeof(grp_list_resp.data), grp_list_resp.m_type, 0);
            deep_copy_group_dict(&grp_list_resp.data.group_dict, &group_dict);

            // send to client
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "index. m_type [ status]   name\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            for (int i = 0; i < user_dict.user_count; i++)
            {
                bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                sprintf(chat_msg.data.m_data, "%5d. %6ld [ %7s ] %s \n", i, user_dict.users[i].m_type, user_dict.users[i].status == 1 ? "online" : "offline", user_dict.users[i].name);
                send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            }

            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "index. m_type Group      [members]\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            for (int i = 0; i < group_dict.group_count; i++)
            {
                bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                sprintf(chat_msg.data.m_data, "%5d. %6ld %10s [", i, group_dict.groups[i].m_type, group_dict.groups[i].name);
                send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
                for (int j = 0; j < group_dict.groups[i].num_members; j++) {
                    bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                    sprintf(chat_msg.data.m_data, "%s", group_dict.groups[i].users[j].name);
                    send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
                    if (j < group_dict.groups[i].num_members - 1) {
                        // TODO: print usernames as well
                        send(conn_fd, ", ", 3, 0);
                    }
                }
                send(conn_fd, "]\n", 3, 0);
            }

            // send(conn_fd, &user_dict, sizeof(user_dict), 0);
            continue;
        }
        else if (strncmp(chat_msg.data.m_data, "help", 4) == 0 and targ_m_type <= 0) {
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "###############################################\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "#                   Help Menu                 #\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "###############################################\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "#                             help : print this thing\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "#                             list : display the list of users\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "#                                    Use list to refresh the list of groups and users periodically. user lists are not updated automatically\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "#                  select <m_type> : enter the chat room of user with given m_type\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "#                   group <m_type> : enter the group chat room with given m_type\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "#                             exit : exit the current chat room\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "#                        broadcast : send msgs to all users\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "# create <group_name> <m_type> ... : create group of users with name <group_name> followed by list of m_types of members.\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "#                                    To change the group name or add/remove members, call create again with new name and new list of members\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "###############################################\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            continue;;
        }
        else if (strncmp(chat_msg.data.m_data, "exit", 4) == 0 and targ_m_type <= 0)
        {
            // client terminated connection
            // update status
            stat_req.data.new_status = 0;
            deep_copy_user(&user, &stat_req.data.user);
            // update status 
            msgsnd(msg_qid, &stat_req, sizeof(stat_req.data), 0);
            printf("[%9d] Connection Terminated. Process stopped\n", getpid());
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "Exiting. Goodbye!\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            return;
        }
        else if (strncmp(chat_msg.data.m_data, "select", 6) == 0 and targ_m_type <= 0) // user selects someone to talk to.
        {
            // client will only listen to msgs of person it is talking with.
            targ_m_type = atoi(&chat_msg.data.m_data[7]);
            printf("[%9d] Received target m_type = %ld from client\n", getpid(), targ_m_type);

            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "###############################################\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "# Entering chat room of user with m_type : %3ld#\n", targ_m_type);
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "###############################################\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);

            // also tell the other end that this user has left the chat
            struct tm msg_time;
            localtime_r(&chat_msg.data.time, &msg_time); // time when msg was sent by sender
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "\n(%2d:%2d) [%10s] <<" GREEN "<user has joined the chat>" RESET, msg_time.tm_hour, msg_time.tm_min, chat_msg.data.user.name);
            msgsnd(msg_qid, &chat_msg, sizeof(chat_msg.data), 0);

            // read msgs from queue asynchronously in separate thread
            pid_t read_proc_pid;
            if ((read_proc_pid = fork()) == 0)
            {
                recv_from_msgqueue(conn_fd, msg_qid, user.m_type, user);
                return;
            }
            // read via tcp and put on msg queue
            while (1)
            {
                bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                time(&chat_msg.data.time);
                struct tm msg_time;
                localtime_r(&chat_msg.data.time, &msg_time);
                sprintf(chat_msg.data.m_data, "(%2d:%2d) [%10s] >> ", msg_time.tm_hour, msg_time.tm_min, user.name);
                send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);

                bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                int bytes_read = recv(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
                chat_msg.data.m_data[strcspn(chat_msg.data.m_data, "\r\n")] = '\0';
                chat_msg.m_type = targ_m_type;
                if (bytes_read >= 0)
                {
                    if (strncmp(chat_msg.data.m_data, "exit", 4) == 0)
                    {
                        kill(read_proc_pid, SIGTERM);
                        printf("[%9d] User exited chat\n", getpid());
                        bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                        sprintf(chat_msg.data.m_data, "###############################################\n");
                        send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
                        bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                        sprintf(chat_msg.data.m_data, "#               Exiting chat room             #\n");
                        send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
                        bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                        sprintf(chat_msg.data.m_data, "###############################################\n");
                        send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);


                        // also tell the other end that this user has left the chat
                        struct tm msg_time;
                        localtime_r(&chat_msg.data.time, &msg_time); // time when msg was sent by sender
                        bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                        sprintf(chat_msg.data.m_data, "\n(%2d:%2d) [%10s] <<" RED "<user has left the chat>" RESET, msg_time.tm_hour, msg_time.tm_min, chat_msg.data.user.name);
                        msgsnd(msg_qid, &chat_msg, sizeof(chat_msg.data), 0);
                        targ_m_type = -1;
                        break;
                    }

                    printf("[%9d] Got message from client \"%s\".\n", getpid(), chat_msg.data.m_data);
                    msgsnd(msg_qid, &chat_msg, sizeof(chat_msg.data), 0);
                }
            }
            continue;
        }
        else if (strncmp(chat_msg.data.m_data, "group", 5) == 0 and targ_m_type <= 0) // user selects someone to talk to.
        {
            // client will only listen to msgs of people in group.
            targ_m_type = atoi(&chat_msg.data.m_data[6]);
            GROUP group;
            // find current from group_dict using group's m_type;
            for (int i = 0; i < group_dict.group_count; i++) {
                if (group_dict.groups[i].m_type == targ_m_type) {
                    deep_copy_group(&group_dict.groups[i], &group);
                    break;
                }
            }
            // first check if user is allowed to enter group
            int access_allowed = 0;
            for (int i = 0; i < group.num_members; i++) {
                if (group.users[i].m_type == user.m_type) {
                    access_allowed = 1;
                    break;
                }
            } 
            if (access_allowed == 0) {
                bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                sprintf(chat_msg.data.m_data, "You aren't a member of this group!\n");
                send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
                targ_m_type = -1;
                continue;
            }

            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "###############################################\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "#    Entering Group Chat with m_type : %3ld   #\n", targ_m_type);
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "###############################################\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);

            // also tell the other end that this user has joined the chat
            struct tm msg_time;
            localtime_r(&chat_msg.data.time, &msg_time); // time when msg was sent by sender
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "\n(%2d:%2d) [%10s] << <user has joined the chat>", msg_time.tm_hour, msg_time.tm_min, chat_msg.data.user.name);
            for (int i = 0; i < group.num_members; i++)
            {
                if (group.users[i].m_type != user.m_type)
                {
                    // targ_m_type = user_dict.users[i].m_type;
                    chat_msg.m_type = group.users[i].m_type;
                    msgsnd(msg_qid, &chat_msg, sizeof(chat_msg.data), 0);
                }
                        }
            // read msgs from queue asynchronously in separate thread
            pid_t read_proc_pid;
            if ((read_proc_pid = fork()) == 0)
            {
                recv_from_msgqueue(conn_fd, msg_qid, user.m_type, user);
                return;
            }
            // read via tcp and put on msg queue
            while (1)
            {
                bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                time(&chat_msg.data.time);
                struct tm msg_time;
                localtime_r(&chat_msg.data.time, &msg_time);
                sprintf(chat_msg.data.m_data, "(%2d:%2d) [%10s] >> ", msg_time.tm_hour, msg_time.tm_min, user.name);
                send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);

                bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                int bytes_read = recv(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
                chat_msg.data.m_data[strcspn(chat_msg.data.m_data, "\r\n")] = '\0';
                chat_msg.m_type = targ_m_type;
                if (bytes_read >= 0)
                {
                    if (strncmp(chat_msg.data.m_data, "exit", 4) == 0)
                    {
                        kill(read_proc_pid, SIGTERM);
                        printf("[%9d] User exited chat\n", getpid());
                        bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                        sprintf(chat_msg.data.m_data, "###############################################\n");
                        send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
                        bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                        sprintf(chat_msg.data.m_data, "#              Exiting Group chat             #\n");
                        send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
                        bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                        sprintf(chat_msg.data.m_data, "###############################################\n");
                        send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);

                        // also tell the other end that this user has left the chat
                        struct tm msg_time;
                        localtime_r(&chat_msg.data.time, &msg_time); // time when msg was sent by sender
                        bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                        sprintf(chat_msg.data.m_data, "\n(%2d:%2d) [%10s] << <user has left the chat>", msg_time.tm_hour, msg_time.tm_min, chat_msg.data.user.name);
                        for (int i = 0; i < group.num_members; i++)
                        {
                            if (group.users[i].m_type != user.m_type)
                            {
                                // targ_m_type = user_dict.users[i].m_type;
                                chat_msg.m_type = group.users[i].m_type;
                                msgsnd(msg_qid, &chat_msg, sizeof(chat_msg.data), 0);
                            }
                        }
                        targ_m_type = -1;
                        break;
                    }

                    printf("[%9d] Got message from client \"%s\".\n", getpid(), chat_msg.data.m_data);
                    // send msgs to all members in group
                    // send to all users
                    for (int i = 0; i < group.num_members; i++)
                    {
                        if (group.users[i].m_type != user.m_type)
                        {
                            // targ_m_type = user_dict.users[i].m_type;
                            chat_msg.m_type = group.users[i].m_type;
                            msgsnd(msg_qid, &chat_msg, sizeof(chat_msg.data), 0);
                        }
                    }
                }
            }
            continue;
        }
        else if (strncmp(chat_msg.data.m_data, "broadcast", 6) == 0 and targ_m_type <= 0) // user started a broadcast
        {
            // client doesn't listen to anyone while it is broadcasting.
            targ_m_type = BROADCAST_M_TYPE;
            printf("[%9d] User started broadcast\n", getpid());

            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "###############################################\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "#              Starting Broadcast             #\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "###############################################\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);

            // read via tcp and put on msg queue
            while (1)
            {
                bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                time(&chat_msg.data.time);
                struct tm msg_time;
                localtime_r(&chat_msg.data.time, &msg_time);
                sprintf(chat_msg.data.m_data, "(%2d:%2d) [%10s] >> ", msg_time.tm_hour, msg_time.tm_min, user.name);
                send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);

                int bytes_read = recv(conn_fd, &chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
                chat_msg.data.m_data[strcspn(chat_msg.data.m_data, "\r\n")] = '\0';
                // chat_msg.m_type = targ_m_type;
                if (bytes_read >= 0)
                {
                    if (strncmp(chat_msg.data.m_data, "exit", 4) == 0)
                    { // close chat
                        printf("[%9d] User stopped broadcast\n", getpid());
                        bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                        sprintf(chat_msg.data.m_data, "###############################################\n");
                        send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
                        bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                        sprintf(chat_msg.data.m_data, "#               Stopping Broadcast            #\n");
                        send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
                        bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
                        sprintf(chat_msg.data.m_data, "###############################################\n");
                        send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
                        targ_m_type = -1;
                        break;
                    }
                    printf("[%9d] Got message from user \"%s\".\n", getpid(), chat_msg.data.m_data);
                    // send to all users
                    for (int i = 0; i < user_dict.user_count; i++)
                    {
                        if (user_dict.users[i].m_type != user.m_type)
                        {
                            // targ_m_type = user_dict.users[i].m_type;
                            chat_msg.m_type = user_dict.users[i].m_type;
                            msgsnd(msg_qid, &chat_msg, sizeof(chat_msg.data), 0);
                        }
                    }
                }
            }
            continue;
        }
        else if (strncmp(chat_msg.data.m_data, "create", 5) == 0 and targ_m_type <= 0)
        {
            // TODO
            char* start = chat_msg.data.m_data + 7; // pointer to starting index of group name
            printf("[%9d] User Initialised Creation of group\n", getpid());

            // 1. parse input
            // 1.1. group name
            GROUP grp;
            char* token = strtok(start, " ");
            strncpy(grp.name, token, strcspn(start, " "));
            printf("[%9d] name : %s ,members : ", getpid(), grp.name);
            // 1.2. mtypes
            token = strtok(NULL, " ");
            while (token != NULL)
            {
                // printf(" %s\n", token);
                // grp.m_types[grp.num_members] = atoi(token);
                targ_m_type = atoi(token);
                printf("%ld ", targ_m_type);
                // find the user with this m_type in user_dict
                for (int i = 0; i < user_dict.user_count; i++) {
                    if (user_dict.users[i].m_type == targ_m_type) {
                        deep_copy_user(&(user_dict.users[i]), &(grp.users[grp.num_members]));
                        break;
                    }
                }
                grp.num_members++;
                token = strtok(NULL, chat_msg.data.m_data);
            }
            printf("\n");

            // 2. send request on msg queue for grp creation
            
            deep_copy_group(&grp, &grp_req.group);
            printf("[%9d] Sending request to main proc to create group.\n", getpid());
            err = msgsnd(msg_qid, &grp_req, sizeof(grp_req.group), 0);
            printf("[%9d] msgsnd returned %d. errno=%d\n", getpid(), err, errno);
            printf("[%9d] Sent request to main proc to create group.\n", getpid());

            // msgrcv with no flag is blocking call. wait for response.
            // response will also contain the m_type for this group
            // msgrcv(msg_qid, &user_list_resp, sizeof(user_list_resp.data), user_list_resp.m_type, 0);
            // deep_copy_user_dict(&user_list_resp.data.user_dict, &user_dict);
            msgrcv(msg_qid, &grp_list_resp, sizeof(grp_list_resp.data), grp_list_resp.m_type, 0);
            printf("[%9d] Received response from main proc. Group alloted m_type : %ld\n", getpid(), grp_list_resp.data.group_dict.groups[grp_list_resp.data.group_idx].m_type);
            deep_copy_group_dict(&grp_list_resp.data.group_dict, &group_dict);
            int group_idx = grp_list_resp.data.group_idx;
            // read m_type of current user from user dictionary
            grp.m_type = group_dict.groups[group_idx].m_type;
            printf("[%9d] Received response from main proc. Group alloted m_type : %ld\n", getpid(), grp.m_type);
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "Group Created\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
            // send(conn_fd, &user.m_type, sizeof(user.m_type), 0); // NO NEED TO SEND M_TYPE
            // printf("[%9d] Sent user m_type : (%ld) for user : %s\n", getpid(), user.m_type, user.name);

            targ_m_type = -1;
            continue;
        }
        else {

            printf("[%9d] \n", getpid());
            bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
            sprintf(chat_msg.data.m_data, "Select a person to talk to first!\n");
            send(conn_fd, chat_msg.data.m_data, sizeof(chat_msg.data.m_data), 0);
        }
    }
}

void recv_from_msgqueue(int conn_fd, int msg_qid, long targ_m_type, USER user)
{
    // reads msgqueue and send via tcp
    CHAT_MESSAGE chat_msg;
    chat_msg.m_type = targ_m_type;
    char buf[MAX_MSG_LENGTH*2];
    while (msgrcv(msg_qid, &chat_msg, sizeof(chat_msg.data), chat_msg.m_type, 0) >= 0)
    {
        if (chat_msg.data.user.m_type == user.m_type)
            continue; // if current user has sent a msg to themself, the targ_m_type and user's m_type will be same. ignore such msgs
        struct tm msg_time;
        time_t time_now;
        time(&time_now);
        localtime_r(&chat_msg.data.time, &msg_time); // time when msg was sent by sender
        bzero(buf, sizeof(buf));
        sprintf(buf, "\n(%2d:%2d) [%10s] << %s", msg_time.tm_hour, msg_time.tm_min, chat_msg.data.user.name, chat_msg.data.m_data);
        send(conn_fd, buf, sizeof(buf), 0);

        localtime_r(&time_now, &msg_time);           // current time
        bzero(buf, sizeof(buf));
        sprintf(buf, "\n(%2d:%2d) [%10s] >> ", msg_time.tm_hour, msg_time.tm_min, user.name);
        send(conn_fd, buf, sizeof(buf), 0);
        bzero(chat_msg.data.m_data, sizeof(chat_msg.data.m_data));
    }
}
void do_nothing(int sig) {}
void handle_msg_queue(int msg_qid)
{

    // INIT
    USER_DICTIONARY user_dict;
    user_dict.user_count = 0;
    GROUP_DICTIONARY group_dict;
    group_dict.group_count = 0;

    // CHAT_MESSAGE cht_msg;
    USER_LIST_REQUEST list_req;
    USER_LIST_RESPONSE user_list_resp;
    GROUP_LIST_RESPONSE grp_list_resp;
    ADD_USER_REQUEST add_req;
    CREATE_GROUP_REQUEST grp_req;
    CHANGE_STATUS_REQUEST stat_req;
    CHAT_MESSAGE chat_msg;
    BASIC_RESPONSE bas_resp;

    list_req.m_type = USER_LIST_REQ_M_TYPE;
    user_list_resp.m_type = USER_LIST_RESP_M_TYPE;
    grp_list_resp.m_type = GROUP_LIST_RESP_M_TYPE;
    add_req.m_type = ADD_REQ_M_TYPE;
    grp_req.m_type = CREATE_GROUP_REQ_M_TYPE;
    stat_req.m_type = STATUS_REQ_M_TYPE;
    bas_resp.m_type = BASIC_RESP_M_TYPE;

    while (1)
    {

        // request for user and group list
        if (msgrcv(msg_qid, &list_req, 0, list_req.m_type, IPC_NOWAIT) >= 0)
        {
            deep_copy_user_dict(&user_dict, &user_list_resp.data.user_dict);
            deep_copy_group_dict(&group_dict, &grp_list_resp.data.group_dict);
            user_list_resp.data.user_idx = -1;
            grp_list_resp.data.group_idx = -1;
            msgsnd(msg_qid, &user_list_resp, sizeof(user_list_resp.data), 0);
            msgsnd(msg_qid, &grp_list_resp, sizeof(grp_list_resp.data), 0);
            printf("[%9d] Sent user and group dictionary on msg queue\n", getpid());
        }
        // request to add new user
        if (msgrcv(msg_qid, &add_req, sizeof(add_req.user), add_req.m_type, IPC_NOWAIT) >= 0)
        {
            int user_exists = 0;
            int user_idx = -1;
            // check if user already exists using user's name
            for (int i = 0; i < user_dict.user_count; i++)
            {
                if (strcmp(user_dict.users[i].name, add_req.user.name) == 0)
                {
                    user_exists = 1;
                    add_req.user.m_type = user_dict.users[i].m_type;
                    user_dict.users[i].user_id = add_req.user.user_id;
                    user_idx = i;
                    break;
                }
            }
            // create new user and assign new mtype if user doesn't exist
            if (user_exists == 0)
            {
                printf("[%9d] user does not exist. creating new entry for user : %s in dictionary\n", getpid(), add_req.user.name);
                user_idx = user_dict.user_count;
                deep_copy_user(&add_req.user, &user_dict.users[user_idx]);
                user_dict.users[user_idx].m_type = 100 + user_dict.user_count;
                user_dict.user_count++;
            }

            deep_copy_user_dict(&user_dict, &user_list_resp.data.user_dict);
            user_list_resp.data.user_idx = user_idx;
            if (msgsnd(msg_qid, &user_list_resp, sizeof(user_list_resp.data), 0) < 0) {
                printf("[%9d] ", getpid());
                fflush(stdout);
                perror("msgsnd");
            }
            printf("[%9d] Sent user and group dictionary on msg queue\n", getpid());
            
        }
        // request to change status
        if (msgrcv(msg_qid, &stat_req, sizeof(stat_req.data), stat_req.m_type, IPC_NOWAIT) >= 0)
        {
            // find the entry in user dict with same name as user
            for (int i = 0; i < user_dict.user_count; i++)
            {
                if (strcmp(user_dict.users[i].name, stat_req.data.user.name) == 0) // not comparing with m_type because m_type will be different if a user re-logins
                {
                    user_dict.users[i].status = stat_req.data.new_status;
                    break;
                }
            }
            printf("[%9d] Changed status of user to [%s]\n", getpid(), stat_req.data.new_status == 1 ? "online" : "offline");
        }
        // request to create group
        if (msgrcv(msg_qid, &grp_req, sizeof(grp_req.group), grp_req.m_type, IPC_NOWAIT) >= 0) {
            int grp_exists = 0;
            int grp_idx = -1;
            // check if group already exists using group's name
            for (int i = 0; i < group_dict.group_count; i++)
            {
                if (strcmp(group_dict.groups[i].name, grp_req.group.name) == 0)
                {
                    grp_exists = 1;
                    grp_req.group.m_type = group_dict.groups[i].m_type;
                    grp_idx = i;
                    break;
                }
            }
            // create new group and assign new mtype if group doesn't exist
            if (grp_exists == 0)
            {
                grp_idx = group_dict.group_count;
                deep_copy_group(&grp_req.group, &(group_dict.groups[grp_idx]));
                group_dict.groups[grp_idx].m_type = 100 + MAX_NUM_USERS + group_dict.group_count;
                group_dict.group_count++;
                printf("[%9d] Group did not exist. Created a new entry for group : %s in dictionary at index : %d \n", getpid(), grp_req.group.name, grp_idx);
            }

            printf("[%9d] Sending latest user and group dictionaries on msg queue\n", getpid());
            deep_copy_group_dict(&group_dict, &grp_list_resp.data.group_dict);
            grp_list_resp.data.group_idx = grp_idx;
            msgsnd(msg_qid, &grp_list_resp, sizeof(grp_list_resp.data), 0);
            printf("[%9d] Group alloted m_type : %ld. Sent user and group dictionary on msg queue\n", getpid(), grp_list_resp.data.group_dict.groups[grp_idx].m_type);
        }
        fflush(stdout);
    }
}
