#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "./comm.h"
#include "./db.h"


/*
 * Use the variables in this struct to synchronize your main thread with client
 * threads. Note that all client threads must have terminated before you clean
 * up the database.
 */
static int accepting_clients = 1; 

typedef struct server_control {
    pthread_mutex_t server_mutex;
    pthread_cond_t server_cond;
    int num_client_threads;
} server_control_t;

/*
 * Controls when the clients in the client thread list should be stopped and
 * let go.
 */
typedef struct client_control {
    pthread_mutex_t go_mutex;
    pthread_cond_t go;
    int stopped;
} client_control_t;

/*
 * The encapsulation of a client thread, i.e., the thread that handles
 * commands from clients.
 */
typedef struct client {
    pthread_t thread;
    FILE *cxstr;  // File stream for input and output

    // For client list
    struct client *prev;
    struct client *next;
} client_t;

/*
 * The encapsulation of a thread that handles signals sent to the server.
 * When SIGINT is sent to the server all client threads should be destroyed.
 */
typedef struct sig_handler {
    sigset_t set;
    pthread_t thread;
} sig_handler_t;

client_t *thread_list_head;
pthread_mutex_t thread_list_mutex = PTHREAD_MUTEX_INITIALIZER;

void *run_client(void *arg);
void *monitor_signal(void *arg);
void thread_cleanup(void *arg);
client_control_t client_control = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0};  
server_control_t server_control = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0};  
// Called by client threads to wait until progress is permitted
void client_control_wait() {
    pthread_mutex_lock(&client_control.go_mutex);
    while(client_control.stopped == 1){
        pthread_cond_wait(&client_control.go, &client_control.go_mutex);
    }
     pthread_mutex_unlock(&client_control.go_mutex);
    // TODO: Block the calling thread until the main thread calls
    // client_control_release(). See the client_control_t struct.
}

// Called by main thread to stop client threads
void client_control_stop() {
    pthread_mutex_lock(&client_control.go_mutex); 
    client_control.stopped = 1;
    pthread_cond_signal(&client_control.go); 
    pthread_mutex_unlock(&client_control.go_mutex); 
    // TODO: Ensure that the next time client threads call client_control_wait()
    // at the top of the event loop in run_client, they will block.
}

// Called by main thread to resume client threads
void client_control_release() {
    pthread_mutex_lock(&client_control.go_mutex); 
    client_control.stopped = 0;
    pthread_cond_broadcast(&client_control.go); 
    pthread_mutex_unlock(&client_control.go_mutex); 
    // TODO: Allow clients that are blocked within client_control_wait()
    // to continue. See the client_control_t struct.
}

// Called by listener (in comm.c) to create a new client thread
void client_constructor(FILE *cxstr) {
    // You should create a new client_t struct here and initialize ALL
    // of its fields. Remember that these initializations should be
    // error-checked.
    //
    // TODO:
    // Step 1: Allocate memory for a new client and set its connection stream
    // to the input argument.
    // Step 2: Create the new client thread running the run_client routine.
    // Step 3: Detach the new client thread 
    pthread_t new_thread; 
    malloc(sizeof(cxstr)); 
    run_client(cxstr); 
    pthread_create(&new_thread, 0, run_client, cxstr);
    pthread_detach(new_thread); 
}

void client_destructor(client_t *client) {
    // TODO: Free and close all resources associated with a client.
    // Whatever was malloc'd in client_constructor should
    // be freed here!
    comm_shutdown(client->cxstr);
    close(client->thread); 
    free(client); 
}

// Code executed by a client thread
void *run_client(void *arg) {
    // TODO:
    // Step 1: Make sure that the server is still accepting clients. This will 
    //         will make sense when handling EOF for the server. 
    // Step 2: Add client to the client list and push thread_cleanup to remove
    //       it if the thread is canceled.
    // Step 3: Loop comm_serve (in comm.c) to receive commands and output
    //       responses. Execute commands using interpret_command (in db.c)   
    // Step 4: When the client is done sending commands, exit the thread
    //       cleanly.
    //
    // You will need to modify this when implementing functionality for stop and go!
    int accepting_clients = 1; 
    client_t *client = (client_t*) arg; 
    //int client_threads = 0; 
    pthread_mutex_lock(&thread_list_mutex); 
    if(accepting_clients == 0){
        client_destructor(client); 
        return NULL;        
    }
        pthread_cleanup_push(thread_cleanup, client);
        client = arg; 
        if(thread_list_head == NULL){
            client->prev = NULL; 
            client->next = NULL; 
            client->cxstr = (FILE*) arg; 
            thread_list_head = client; 
        }
        else{
             client->prev = thread_list_head; 
             client->next = thread_list_head->next; 
             client->cxstr = (FILE*) arg;
        }
        pthread_mutex_lock(&server_control.server_mutex);
        server_control.num_client_threads++; 
        pthread_mutex_unlock(&server_control.server_mutex);
        pthread_mutex_unlock(&thread_list_mutex); 
        char buffer1[1024];
        char buffer2[1024];
        memset(buffer1, 0, 1024);
        memset(buffer2, 0, 1024);
        
        while(comm_serve(client->cxstr, buffer1, buffer2) != -1){
            client_control_wait(); 
            interpret_command(buffer1, buffer2, 1024);
        }
        pthread_cleanup_pop(1);
    return NULL;
}

void delete_all() {
    //client_t *thread_list_head;
    client_t *current_client = thread_list_head;
    while(current_client != NULL){
        pthread_cancel(current_client->thread); 
        current_client = thread_list_head->next;         
    }
    // TODO: Cancel every thread in the client thread list with the
}

// Cleanup routine for client threads, called on cancels and exit.
void thread_cleanup(void *arg) {
    client_t *current_client = thread_list_head;
    while(current_client != NULL){
        pthread_mutex_lock(&thread_list_mutex);
        client_destructor(thread_list_head);
        current_client = thread_list_head->next;   
        pthread_mutex_unlock(&thread_list_mutex);        
    }
    // TODO: Remove the client object from thread list and call
    // client_destructor. This function must be thread safe! The client must
    // be in the list before this routine is ever run.
}

// Code executed by the signal handler thread. For the purpose of this
// assignment, there are two reasonable ways to implement this.
// The one you choose will depend on logic in sig_handler_constructor.
// 'man 7 signal' and 'man sigwait' are both helpful for making this
// decision. One way or another, all of the server's client threads
// should terminate on SIGINT. The server (this includes the listener
// thread) should not, however, terminate on SIGINT!
void *monitor_signal(void *arg) {
    sigset_t * signalset = (sigset_t *) arg; 
    int sig; 
    while(1){
        sigwait(signalset, &sig); 
        if(sig == SIGINT){
            fprintf(stderr, "canceling client"); 
            pthread_mutex_lock(&thread_list_mutex); 
            delete_all();
            pthread_mutex_unlock(&thread_list_mutex); 
        }
    }
    // TODO: Wait for a SIGINT to be sent to the server process and cancel
    // all client threads when one arrives.
    return 0;
}

sig_handler_t *sig_handler_constructor() {
    sig_handler_t *sig_handler;
    sig_handler = malloc(sizeof(sig_handler_t)); 
    sigemptyset(&sig_handler->set); 
    sigaddset(&sig_handler->set, SIGINT); 
    sigaddset(&sig_handler->set, SIGPIPE); 
    pthread_sigmask(SIG_BLOCK, &sig_handler->set, 0); 
    pthread_create(&sig_handler->thread, 0, monitor_signal, &sig_handler->set); 
    // TODO: Create a thread to handle SIGINT. The thread that this function
    // creates should be the ONLY thread that ever responds to SIGINT.
    return sig_handler;
}

void sig_handler_destructor(sig_handler_t *sighandler) {
    pthread_cancel(sighandler->thread); 
    pthread_join(sighandler->thread, PTHREAD_CANCELED);
    free(sighandler); 
    // TODO: Free any resources allocated in sig_handler_constructor.
    // Cancel and join with the signal handler's thread. 
}


// The arguments to the server should be the port number.
int main(int argc, char *argv[]) {
    // TODO:
    // Step 1: Set up the signal handler for handling SIGINT. 
    // Step 2: block SIGPIPE so that the server does not abort when a client disocnnects
    // Step 3: Start a listener thread for clients (see start_listener in
    //       comm.c).
    sigset_t set; 
    sigemptyset(&set); 
    sigaddset(&set, SIGPIPE); 
    pthread_sigmask(SIG_BLOCK, &set, 0); 
    pthread_t tid = start_listener(atoi(argv[1]), client_constructor);
    // Step 4: Loop for command line input and handle accordingly until EOF.
    // Step 5: Destroy the signal handler, delete all clients, cleanup the
    //       database, cancel and join with the listener thread
    //
    // You should ensure that the thread list is empty before cleaning up the
    // database and canceling the listener thread. Think carefully about what
    // happens in a call to delete_all() and ensure that there is no way for a
    // thread to add itself to the thread list after the server's final
    // delete_all().
    //find function in db.c to clean up database
    //signal handler 
    size_t sz; 
    char oldbuffer[1024];
    memset(oldbuffer, 0, 1024);
    // char newbuffer[1024];
    // memset(newbuffer, 0, 1024);
    sig_handler_t *sig_handler = sig_handler_constructor(); 

    // void eliminate_space(char *oldchar[100], char *newbuffer[1024]){
    //         int index = 0; 
    //         while(oldchar[index] != '\0'){
    //             if(oldchar[index] != ' '){
    //                 newbuffer[index] = oldchar[index]; 
    //             }
    //             index++; 
    //         }
    //  }

    while((sz = read(STDIN_FILENO, &oldbuffer, 1024)) > 0){
        oldbuffer[sz - 1] = '\0';
        // eliminate_space(&oldbuffer[0], &newbuffer);
        if(strcmp(&oldbuffer[0], "g") == 0){
            client_control_release(); 
        }
        if(strcmp(&oldbuffer[0], "s") == 0){
            client_control_stop();
        }
        if(strcmp(&oldbuffer[0], "p") == 0){
            db_print(&oldbuffer[0]);
        }
    }
    sig_handler_destructor(sig_handler);
    pthread_mutex_lock(&thread_list_mutex); 
    accepting_clients = 0; 
    pthread_mutex_unlock(&thread_list_mutex); 
    pthread_mutex_lock(&server_control.server_mutex);
    while(server_control.num_client_threads > 0){
        pthread_cond_wait(&server_control.server_cond, &server_control.server_mutex); 
        server_control.num_client_threads--; 
    } 
    pthread_mutex_unlock(&server_control.server_mutex);
    delete_all(); 
    pthread_cancel(tid); 
    pthread_join(tid, NULL);
    db_cleanup(); 
    pthread_exit(NULL); 
    return 0;
}
