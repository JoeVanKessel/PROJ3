#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_SIZE_OF_CMDLINE 2048
#define MAX_NUMBER_OF_ARGS 512

int getArguments(char **, int, int *, int);
void runShell();
void catchSIGINT(int);
void catchSIGTSTP(int);
void catchSIGINTBACK(int);
int runBackgroundProcess(char*, char*, char**, int, int);
int redirectIO(char*, char*, char*, int, int);
int forgroundOnlyMode = 0;

int main(){
  runShell();
}

/*
 FUNCTION: Runs the entire shell. called by main.
*/

void runShell(){

  char *parsedCmds[MAX_NUMBER_OF_ARGS];
  int numCmds;
  int childExitMethod = -5;
  int status;
  int fileDescriptor;
  int result;
  char *inputFile;
  char *outputFile;
  int childPIDS[2048];
  int PIDindex = 0;
  int aPID;
  int i;
  char firstCommand[100];
  char checkComment;
  char doubleDollar[2] = "$$";

  while(1)
  {
    //set up SIGINT so it is ignored
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = SIG_IGN;
    sigaction(SIGINT, &SIGINT_action, NULL);
    //Catch SIGSTP and send to handler function
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = catchSIGTSTP;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    pid_t spawnpid = -5;
    int isRedirection = 0;
    int background = 0;
    int outputFileCheck = 0;
    int inputFileCheck = 0;

    //Get a list of arguments from the user
    //numCmds is the total number of arguments
    numCmds = getArguments(parsedCmds, childExitMethod, childPIDS, PIDindex);

    //check for file redirection is list of arguments
    for (i = 0; i < numCmds; i++)
    {
      if (strcmp(parsedCmds[i], "<") == 0) //check for file for input
      {
        isRedirection = 1;
        inputFile = parsedCmds[i+1];
        inputFileCheck = 1;
      }
      else if (strcmp(parsedCmds[i], ">") == 0) //check for file for output
      {
        isRedirection = 1;
        outputFile = parsedCmds[i+1];
        outputFileCheck = 1;
      }
    }

    if (numCmds != 0 ) //if there are any
    {
      strcpy(firstCommand, parsedCmds[0]); //used to find # for comments

      if (strstr(parsedCmds[numCmds-1], doubleDollar) != NULL)
      {
        char * token = strtok(parsedCmds[numCmds-1], doubleDollar);
        if (token != NULL){ sprintf(parsedCmds[numCmds-1] , "%s%d", token, getpid()); }
        else{ sprintf(parsedCmds[numCmds-1] , "%d", getpid()); }
      }

      if(strcmp(parsedCmds[numCmds-1], "&") == 0 && forgroundOnlyMode == 0) //run background if not in forground only mode
      {
          parsedCmds[numCmds-1] = '\0';
          background = 1;
          if(isRedirection == 0 ){
            //input and output files set to dev/null
            childPIDS[PIDindex] = runBackgroundProcess("/dev/null", "/dev/null", parsedCmds, inputFileCheck, outputFileCheck);
            PIDindex++;
          }
      }

      else if (isRedirection == 1) //Check for file io redirection
      {
        childExitMethod = redirectIO(inputFile, outputFile, parsedCmds[0], inputFileCheck, outputFileCheck);
      }

      else if (strcmp(parsedCmds[0], "exit") == 0) //BUilt in exit command quits shell
      {
        printf("Exiting Shell...\n");
        exit(0);
      }

      else if (strcmp(parsedCmds[0], "cd") == 0) //check for cd command
      {
        if (numCmds == 1){chdir(getenv("HOME"));} //change to home directory listed in env variable if no dir is specified
        else{ chdir(parsedCmds[1]); } //otherwise change to the directory specified
      }

      else if (strcmp(parsedCmds[0], "status") == 0) //check for status command
      {
        if (WIFEXITED(childExitMethod) != 0){printf("exit value %d\n",   WEXITSTATUS(childExitMethod));} //print exit number if process exited
        else if (WIFSIGNALED(childExitMethod) != 0){printf("terminated by signal %d\n", WTERMSIG(childExitMethod));} //print signal number if process was terminated by signal
      }

      else if (firstCommand[0] == '#') {} //if command line starts with # do nothing as this is a comment

      else //If we make it to here only normal forground processes will be run
      {
        if(strcmp(parsedCmds[numCmds-1], "&") == 0){ //in forground only mode &'s can make it to this point '
          parsedCmds[numCmds-1] = '\0';//remove the & from the command because they will not be processed
        }
        //set SIGINT to stop the child process when in forground
        struct sigaction SIGINT_action = {0};
        SIGINT_action.sa_handler = catchSIGINT;
        sigfillset(&SIGINT_action.sa_mask);
        sigaction(SIGINT, &SIGINT_action, NULL);

        spawnpid = fork(); //fork off child
        switch (spawnpid)
        {
          case -1:
            perror("Error Creating Child Process! UHH OH");
            exit(1);
          break;
            case 0:
              if (execvp(*parsedCmds, parsedCmds) < 0){perror("Error executing");exit(1);} //execute command
            default:
              waitpid(spawnpid, &childExitMethod, 0); //Parent waits until child has processed
        }
      }
    }
  }
}


/*NOTE:
 although the input and output files for background processes will always be dev/null in the current state of this shell,
 I was originally going to do background-processes with file redirection, until I realised it wasnt nessasary for the grading script.
 I decided to just leave the function as is though and pass in dev/null when it is called.
*/
/*
 FUNCTION: Runs processes in the background.
 PARAMS: input file name, output file name, command name, and 2 flags to check if there is input redirection output redirection or both.
 RETURNS: the child process PID
*/

int runBackgroundProcess(char* inputFile, char* outputFile, char** parsedCmds, int inputFileCheck, int outputFileCheck){

  int spawnpid = -5;
  int childExitMethod = -5;
  int result, fileDescriptor1, fileDescriptor2;

  spawnpid = fork(); //create background process
  switch (spawnpid)
  {
    case -1:
      perror("Error Creating Child Process! UHH OH");
      exit(1);
    break;
      case 0:

        if (inputFileCheck == 1)
        {
          fileDescriptor1 = open(inputFile, O_RDONLY);
          if (fileDescriptor1 == -1){ perror("Could not open input file"); printf("%s\n", inputFile); exit(1);}
          result = dup2(fileDescriptor1, 0);
          if (result == -1) { perror("source dup2()"); exit(2); }
          fcntl(fileDescriptor1, F_SETFD, FD_CLOEXEC);
        }
        if(outputFileCheck == 1)
        {
          fileDescriptor2 = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
          if (fileDescriptor2 == -1){ perror("Could not open outputFile file"); exit(1);}
          result = dup2(fileDescriptor2, 1);
          if (result == -1) { perror("source dup2()"); exit(2); }
          fcntl(fileDescriptor2, F_SETFD, FD_CLOEXEC);
        }
        if (execvp(*parsedCmds, parsedCmds) < 0){perror("Error executing");exit(1);}
      default:
        printf("background pid is %d\n",spawnpid); //print child pid
        waitpid(spawnpid, &childExitMethod, WNOHANG);// parent does not wait for child
  }
  return spawnpid; //Process ID of child background process
}

/*
 FUNCTION: first checks child processes for completion then gets command line input and parses it into seperate commands.
 PARAMS: array of strings holding each command, Exit method of child, array of PIDs of child processes,  and the number of PIDs in that array.
 RETURNS: the number of commands in the command array.
*/
int getArguments(char **parsedCmds, int childExitMethod, int *childPIDS, int PIDindex){

  char *rawInputText = NULL;
  int numCharInInput = -5;
  size_t bufferSize = 0;
  int i;
  int k;
  int aPID;

  for (i = 0; i < PIDindex; i++)
  {
    //printf("%d ",childPIDS[i]);
    aPID = waitpid(childPIDS[i], &childExitMethod, WNOHANG); //check all PIDs in array to see if they have completed
    if (aPID > 0) //aPID has completed
    {
      printf("background pid %d is done: ", aPID);
      //tell user how it terminated/exited
      if (WIFEXITED(childExitMethod) != 0){printf("exit value %d\n",   WEXITSTATUS(childExitMethod));}
      else if (WIFSIGNALED(childExitMethod) != 0){printf("terminated by signal %d\n", WTERMSIG(childExitMethod));}
      else
      {
        for (k = i-1; k < PIDindex-1; k++){
          childPIDS[k] = childPIDS[k+1];
        }
      }
    }
  }

  printf(":"); //prompt
  numCharInInput = getline(&rawInputText, &bufferSize, stdin); //get user commands

  rawInputText[strcspn(rawInputText, "\n")] = '\0';
  const char delim[2] = " ";
  int j = 1;
  char *token = strtok(rawInputText, delim); //parse by first space
  parsedCmds[0] = token; //store parsed command into array
  while(token != NULL) //parse through every other space store commands in said array
  {
    token = strtok(NULL, delim);
    parsedCmds[j] = token;
    j++;
  }
  return(j-1); //return the number of commands
}

//SIGINT handler for forground child processed terminates the child forground process and displays terminating sig num
void catchSIGINT(int signo){
   char* termDisp;
   termDisp = malloc(sizeof(char)*23);
   sprintf(termDisp, "terminated by signal %d\n", signo);
   write(STDOUT_FILENO, termDisp, 23);
   fflush(stdout);
   free(termDisp);
}

//SIGTSTP signal handler puts shell into or out of forground-only mode and displays message
void catchSIGTSTP(int signo){
  if (forgroundOnlyMode == 0)
  {
    forgroundOnlyMode = 1;
    char * forMess = "Entering forground-only mode (& is now ignored)\n";
    write(STDOUT_FILENO, forMess, 48);
    fflush(stdout);
  }
  else if (forgroundOnlyMode == 1){
    forgroundOnlyMode = 0;
    char * forOffMess = "Exiting forground-only mode\n";
    write(STDOUT_FILENO, forOffMess, 28);
    fflush(stdout);
  }
}

/*
 FUNCTION: Runs processes that have Fileio redirection.
 PARAMS: input file name, output file name, command name, and 2 flags to check if there is input redirection output redirection or both.
 RETURNS: the exit method of the child
*/
int redirectIO(char * inputFile, char * outputFile, char *parsedCmd, int inputFileCheck, int outputFileCheck){

  int spawnpid = -5;
  int childExitMethod = -5;
  int result, fileDescriptor1, fileDescriptor2;

  spawnpid = fork();
  switch (spawnpid)
  {
    case -1:
      perror("Error Creating Child Process! UHH OH");
      exit(1);
    break;
      case 0:
        if (inputFileCheck == 1) //check for input file
        {
          fileDescriptor1 = open(inputFile, O_RDONLY); //open the file
          if (fileDescriptor1 == -1){ perror("Could not open input file"); exit(1);}
          result = dup2(fileDescriptor1, 0); //set to stdin
          if (result == -1) { perror("source dup2()"); exit(2); }
          fcntl(fileDescriptor1, F_SETFD, FD_CLOEXEC); //close files when exec is called
        }
        if(outputFileCheck == 1) //check for output file
        {
          fileDescriptor2 = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644); //open and create if it doesnt exist
          if (fileDescriptor2 == -1){ perror("Could not open outputFile file"); exit(1);}
          result = dup2(fileDescriptor2, 1); //set to stdout
          if (result == -1) { perror("source dup2()"); exit(2); }
          fcntl(fileDescriptor2, F_SETFD, FD_CLOEXEC); //close file when exec is called
        }
        if (execlp(parsedCmd, parsedCmd, NULL) < 0){perror("Error executing");exit(1);} //execute command
      default:
        waitpid(spawnpid, &childExitMethod, 0); //parent waits for process to complete
  }
  return childExitMethod;
}
