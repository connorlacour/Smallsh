#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <signal.h>


// DECLARATIONS
//
// Functions:
void exit_command();
void cd_command();
void status_command();
void user_commands();
void run();


//
// Global Variables
//
//
//
// Note to grader: I know it's 'dirty' to maintain a
//     lot of global variables. At the moment, I'm doing
//     my best to wrap my brain around a lot of the
//     fundamentals of C. So, I'm prioritizing functionality,
//     but I'm taking this into consideration as I get a
//     little more comfortable and will do my best to curb
//     this habit as I move forward.
//
//
//
char* args[] = {};
int child_pids[100] = {0};
char user_chdir[256] = "";
char user_command[2048];
pid_t cur_fg_child = 0;
int most_recent_status = 0;

bool run_in_background = false;
bool foreground_only = false;
bool fg_only_message_pending = false;

sigset_t sig_ignore_set;

void foreground_mode() {
    fg_only_message_pending = true;
    // if foreground_only is true, set to false and display message
    if (foreground_only) {
        foreground_only = false;
    }

    // if foreground_only is false, set to true and display message
    else {
        foreground_only = true;
    }
}

int pid(){
    // returns current pid
    int current_pid;
    current_pid = getpid();
    return current_pid;
}

 void update_children_processes(int pid) {
     int i = 0;

     // find next empty slot and populate with the pid passed into the function
     while (child_pids[i] != 0) {
         i++;
     }
     child_pids[i] = pid;
 }

 void background_management() {
     int i = 0;
     int j = 0;
     int childPid;
     int childStatus;

     // iterate the array and check each value for an exit or term status
     // if status found, remove from array by moving all other values down one (ie. i-1)
     while (child_pids[i] != 0) {
         childPid = waitpid(child_pids[i], &childStatus, WNOHANG);

         // ie. has reaped a defunct process
         // remove from array & shift array values by 1
         if (childPid != 0) {
             // printf("reaping child : %d\n", child_pids[i]);
             // fflush(stdout);

            if (WIFEXITED(childStatus)) {
                printf("Child %d exited normally with status %d\n", childPid, WEXITSTATUS(childStatus));
                fflush(stdout);
            }
            else {
                printf("Child %d exited abnormally due to signal %d\n", childPid, WTERMSIG(childStatus));
                fflush(stdout);
            }

             j = i + 1;
             while (child_pids[j] != 0) {
                 child_pids[j - 1] = child_pids[j];
                 j++;
             }
             child_pids[j - 1] = 0;
             i = i - 1;
         }
         i++;
     }


 }

void exit_command() {
    // exit
    exit(0);
}

void status_command() {
    // print most_recent_status
    printf("exit value: %d\n", most_recent_status);
    fflush(stdout);
}

void cd_command(){
    int new_dir;

    // store env HOME variable
    char* home;
    home = (char*)getenv("HOME");

    // if no additional argument, change directory to HOME
    if(strcmp(user_chdir, "") == 0){
        new_dir = chdir(home);
        if(new_dir != 0){
            perror("Error");
        }
    }

    // else, change directory to the path specified
    else{
        new_dir = chdir(user_chdir);
        if(new_dir != 0){
            perror("Error");
        }
    }
}

void run() {
    // initialize local variables
    const char comment_test[2] = "#";
    const char cd_test[4] = "cd ";
    const char exit_test[5] = "exit";
    const char status_test[10] = "status";
    const char expansion_char[3] = "$$";

    // reset counters and global trackers
    int cd_counter = 0;
    int exit_counter = 0;
    int status_counter = 0;
    int expansions_counted = 0;
    int current_pid;
    run_in_background = false;

    memset(user_chdir,0,sizeof(user_chdir));

    // message pending occurs when ctrl+z is entered while another process is
    //     performing some task. This just ensures that we wait until this moment
    //     to alert the user that fg-only mode has been toggled
    if (fg_only_message_pending) {
        if (foreground_only) {
            printf("Entering foreground-only mode (& is now ignored)\n");
            fflush(stdout);
        }
        else {
            printf("Exiting foreground-only mode\n");
            fflush(stdout);
        }
        fg_only_message_pending = false;
    }

    // call background_management to check for any defunct processes
    // before every shell ": " prompt
    background_management();

    // provide user the "prompt"; ie. ": "
    // then scan user input
    printf("\n: ");
    fflush(stdout);
    fgets(user_command,80,stdin);
    int command_len = strlen(user_command);

    // test if blank line
    if(command_len == 1){
        // nothing to do here, pass along to end of run()
        // thus reprompting user on new line for input
    }
    else{
        // if not a blank line: test for if user entered comment
        //    we do a simple iteration comparing first value of both user input
        //    and the comment_test char: "#"
        //    ie. if user's input's first char is "#", we consider that input
        //    comment and do nothing more.
        for(int i=0; i<1; i++) {
            if(user_command[i] == comment_test[i]) {
                // nothing to do here
                // if user has entered a comment, we proceed to the end of run()
                //    and prompt the user for a new command
            }
            else{

                // user has not entered a comment;
                // continue with processing to see if user has entered
                //     a built-in or other command

                char user_temp[2048];
                current_pid = pid();
                char pid_string[30];
                int offset;

                sprintf(pid_string, "%i", current_pid);
                fflush(stdout);
                memset(user_temp,0,sizeof(user_temp));

                // check for $$ variable to be expanded
                // the strategy here (as I saw no better way to do it) is to check
                //    user_command char by char for an occurence of $$. if there is a
                //    successful occurrence of $$, we want to, in that place, strcat
                //    the current pid value.
                //
                // We then have to account for the iterator values being displaced. So,
                //    an offset value is generated for future expansion occurences.
                //
                //    eg. if: user_command == "foo$$ bar$$$",
                //      we expect a result of user_temp == "foo[pid] bar[pid]$"
                for(int c=0; user_command[c]!='\0'; c++) {
                    if(user_command[c] == expansion_char[0]) {
                        if(user_command[c+1] == expansion_char[1]) {
                            expansions_counted++;
                            strcat(user_temp, pid_string);
                            c++; // <- I get it..
                        }
                        else {
                            if(expansions_counted > 0) {
                                offset = c + (expansions_counted*(strlen(pid_string)) - 2);
                                user_temp[offset] = user_command[c];
                            }
                            else {
                                user_temp[c] = user_command[c];
                            }
                        }
                    }
                    else {
                        if(expansions_counted > 0) {
                            offset = c + (expansions_counted*(strlen(pid_string)) - 2);
                            user_temp[offset] = user_command[c];
                        }
                        else {
                            user_temp[c] = user_command[c];
                        }
                    }
                }

                // update our command val and re-evaluate its corresponding
                //    command_len val.
                memset(user_command,0,sizeof(user_command));
                strcpy(user_command, user_temp);
                command_len = strlen(user_command);

                // this is a simple string comparison to see if user_command begins
                //    with "cd "
                //    ie. if cd_counter == 3, we have confirmed the user has at least
                //    entered the command associated with changing directories
                for(int j=0; j < sizeof(cd_test); j++) {
                    if(user_command[j] == cd_test[j]) {
                        cd_counter++;
                    }
                }

                // if cd_counter == 3: call cd(user_chdir)
                if(cd_counter==3){
                    if(command_len==3) {
                        strcpy(user_chdir, "");
                    }
                    else{
                        for(int k=3;k<(command_len-1); k++) {
                            user_chdir[k-3] = user_command[k];
                        }
                    }

                    cd_command();
                }
                else {
                    // if command_len == 5, test for "exit" as user command
                    // a command_len > 5 implies we could have a user_command such as
                    //    "exitexitexit", something I don't believe should qualify as
                    //    an exit command
                    if(command_len == 5){
                        for(int m=0; m < sizeof(exit_test); m++) {
                            if(user_command[m] == exit_test[m]) {
                                exit_counter++;
                            }
                        }
                    }
                    if(exit_counter==4){
                        exit_command();
                    }

                    // check for "status" command
                    for(int n=0; n < command_len; n++) {
                        if(user_command[n] == status_test[n]) {
                            status_counter++;
                        }
                    }
                    if(status_counter==6) {
                        status_command();
                    }

                    // if not foreground only, we respect a command ending in '&'
                    //     if foreground_only == true, we ignore the '&' request
                    if (!foreground_only) {
                        if (user_command[command_len - 2] == '&') {
                            run_in_background = true;
                        }
                    }

                    // if we have not called status, exit, or cd, we aim to treat the
                    //    user_command as a fork()-ing command
                    // that is all done in user_commands()
                    if(status_counter!=6 && exit_counter!=4 && cd_counter!=3){
                        if (strcmp(user_command, "^Z") == 0) {

                        }
                        else {
                            user_commands();
                        }
                    }
                }
            }
        }
    }
}

void user_commands() {
    int childStatus;
    pid_t childPid;

    pid_t spawnpid = -5;
    spawnpid = fork();

    switch(spawnpid) {

    case -1:
        perror("fork unsuccessful!");
        fflush(stdout);
        exit(1);
        break;

    case 0:;

        // if foreground child process, accept ^C SIGINT
        if (!run_in_background) {
            struct sigaction SIGINT_action = {{0}}, SIGTSTP_ignore = {{0}};
            SIGINT_action.sa_handler = SIG_DFL;
            sigaction(SIGINT, &SIGINT_action, NULL);

            SIGTSTP_ignore.sa_handler = SIG_IGN;
            sigaction(SIGTSTP, &SIGTSTP_ignore, NULL);
        }

        // if run_in_background, ignore SIGTSTP but accept SIGINT
        struct sigaction SIGTSTP_ignore = {{0}};
        SIGTSTP_ignore.sa_handler = SIG_IGN;
        sigaction(SIGTSTP, &SIGTSTP_ignore, NULL);

        char* saveptr;
        char* token;
        char* temp;
        char* out_path;
        char* in_path;

        bool next_is_out_path = false;
        bool stdin_changed = false;
        bool stdout_changed = false;
        bool next_is_in_path = false;

        int i = 0;
        int result;

        temp = strdup(user_command);
        token = strtok_r(temp, " ", &saveptr);

        while (token != NULL) {

            if (token[strlen(token) - 1] == '\n') {
                token[strlen(token) - 1] = '\0';
            }

            // if std_in redirected
            if (strcmp(token, "<") == 0) {
                next_is_in_path = true;
                stdin_changed = true;
            }

            // if std_out redirected
            else if (strcmp(token, ">") == 0) {
                next_is_out_path = true;
                stdout_changed = true;
            }
            // if next_is_out_path is true, it was set during the previous token
            //     meaning THIS token is the redirected out_path specified
            else if (next_is_out_path) {
                next_is_out_path = false;
                out_path = token;
            }
            // if next_is_in_path is true, it was set during the previous token
            //     meaning THIS token is the redirected in_path specified
            else if (next_is_in_path) {
                next_is_in_path = false;
                in_path = token;
            }
            else if (strcmp(token, "&") == 0) {
            }
            // if we make it here, the token should be appended to our args array
            //     to be passed into execvp() momentarily
            else {
                args[i] = token;
                i++;
            }
            token = strtok_r(NULL, " ", &saveptr);
        }

        // add NULL pointer to end of args array to comply with execvp() standards
        args[i] = NULL;

        // if process is to be run in background and has either stdin or stdout
        // unspecified, any unspecified should be redirected to /dev/null
        if (run_in_background) {
            if (!stdin_changed) {
                // Open source file
                int sourceFD = open("/dev/null", O_RDONLY);
                // handle error
                if (sourceFD == -1) {
                    perror("source open()");
                    exit(1);
                }

                // redirect stdin
                result = dup2(sourceFD, 0);
                // handle error
                if (result == -1) {
                    perror("source dup2()");
                    exit(2);
                }
            }
            if (!stdout_changed) {
                // open target file
                int targetFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0640);
                // handle error
                if (targetFD == -1) {
                    perror("target open()");
                    exit(1);
                }

                // Redirect stdout to target file
                result = dup2(targetFD, 1);
                // handle error
                if (result == -1) {
                    perror("target dup2()");
                    exit(2);
                }
            }
        }

        if (stdin_changed) {
            // Open source file
            int sourceFD = open(in_path, O_RDONLY);
            // handle error
            if (sourceFD == -1) {
                perror("source open()");
                exit(1);
            }

            // redirect stdin
            result = dup2(sourceFD, 0);
            // handle error
            if (result == -1) {
                perror("source dup2()");
                exit(2);
            }
        }

        if (stdout_changed) {
            // open target file
            int targetFD = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0640);
            // handle error
            if (targetFD == -1) {
                perror("target open()");
                exit(1);
            }

            // Redirect stdout to target file
            result = dup2(targetFD, 1);
            // handle error
            if (result == -1) {
                perror("target dup2()");
                exit(2);
            }
        }

        // finally, call execvp
        execvp(args[0], args);

        // exec returns only on error
        perror("execvp");
        fflush(stdout);
        exit(EXIT_FAILURE);
        break;

    default:

        // if fork process is NOT run in background, we wait for it to complete
        if (!run_in_background) {
            childPid = waitpid(spawnpid, &childStatus, 0);

            // update child return status
            if (WIFEXITED(childStatus)) {
                most_recent_status = WEXITSTATUS(childStatus);
            }
            else {
                most_recent_status = WTERMSIG(childStatus);
                if (most_recent_status != 11) {
                    printf("\nterminated by signal %d\n", most_recent_status);
                    fflush(stdout);
                }
            }
        }

        // if fork process IS to be run in background, we print the pid and move on
        else {
            update_children_processes(spawnpid);
            printf("background process started with pid: %d\n", spawnpid);
            fflush(stdout);
        }
        break;
    }
}

int main()
{
    // initialize SIG_INT ignore and SIGTSTP fg-only mode signal handling
    struct sigaction SIGINT_action = {{0}}, SIGTSTP_action = {{0}};
    SIGINT_action.sa_handler = SIG_IGN;
    SIGTSTP_action.sa_handler = &foreground_mode;
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGINT, &SIGINT_action, NULL);
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    while (1) {
        run();
    }
}
