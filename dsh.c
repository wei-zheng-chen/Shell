#include "dsh.h"

void seize_tty(pid_t callingprocess_pgid); /* Grab control of the terminal for the calling process pgid.  */
void continue_job(job_t *j); /* resume a stopped job */
void spawn_job(job_t *j, bool fg); /* spawn a new job */


//-----erase when finish--------------------/
void printMyJob(job_t *j);
void printMyJobProcess(process_t*p);
//-------------------------------------------/

job_t* headOfJobCollection; //collection of jobs that are not the built-in commmands

/* Sets the process group id for a given job and process */
int set_child_pgid(job_t *j, process_t *p) {
  
  if (j->pgid < 0) /* first child: use its pid for job pgid */
    j->pgid = p->pid;
  return(setpgid(p->pid,j->pgid));
}

/* Creates the context for a new child by setting the pid, pgid and tcsetpgrp */
void new_child(job_t *j, process_t *p, bool fg) {
  /* establish a new process group, and put the child in
   * foreground if requested
   */

  /* Put the process into the process group and give the process
   * group the terminal, if appropriate.  This has to be done both by
   * the dsh and in the individual child processes because of
   * potential race conditions.  
   * */
  
  p->pid = getpid();

  /* also establish child process group in child to avoid race (if parent has not done it yet). */
  set_child_pgid(j, p);

  if(fg) // if fg is set
		seize_tty(j->pgid); // assign the terminal

  /* Set the handling for job control signals back to the default. */
  signal(SIGTTOU, SIG_DFL);
}

// Error logging
void logError(char* text) {
   //store completed entry in log
   FILE* logfile = fopen("dsh.log", "a");
   fprintf(logfile, "Error: (%s) %s", strerror(errno), text);
   fclose(logfile);
}

// I/O Redirection - Works
void input(process_t*p){
  int fd = open(p->ifile, O_RDONLY);
  if (fd != -1){
    dup2(fd, STDIN_FILENO);
    close(fd);
  } else {
    logError("Input file cannot be opened; cannot read\n");
  }
}

void output(process_t *p){
  int fd = open(p->ofile, O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU);
  if (fd != -1){
    dup2(fd, STDOUT_FILENO);
    close(fd);
  } else {
    logError("Output file cannot be opened; cannot write\n");
  }
}

void redirection(process_t * p){
  if(p->ifile != NULL){
    input(p);
  }
  if(p->ofile != NULL){
    output(p);
  }
}

// for compiliing c programs
void compiler(process_t *p){
  int status =0;
  pid_t pid;

  // The next 3 lines just make it so that the compiled file name is not always devil.
  // char* compileFileName = (char*) malloc(sizeof(char)*(strlen(p->argv[0])-2));
  // memcpy(compileFileName, p->argv[0],(strlen(p->argv[0])-2));
  // compileFileName[(strlen(p->argv[0])-2)] ='\0';

  // printf("this is my filename: %s This is it's size: %lu\n", p->argv[0],strlen(p->argv[0])-2);
  // printf("this is my compilefilename: %s\n ",compileFileName);

  //built up the gcc argument stuff
  char **gccArgs = (char**)malloc(sizeof(char*)*5);
  gccArgs[0] = "gcc";
  gccArgs[1] = "-o";
  gccArgs[2] = "devil"; //compileFileName;
  gccArgs[3] = p->argv[0];
  gccArgs[4] = '\0';

  //do the fork stuff, similar to the fork thingy in spawn_job
  switch (pid = fork()){
    case -1:
      printf("FORK ERRORRRRRRR!!!\n");
      // LOG ERROR???

    case 0:
      execv("/usr/bin/gcc",gccArgs);

    default:
      if (waitpid(pid, &status, 0) < 0){
          printf("My error is from the parent waiting\n");
          perror("waitpid");
          exit(EXIT_FAILURE);
        }
        // now that child has completed, what shall we do?

      // check exit status (which means what?)
      // if(WIFEXITED(status)){
      //     // something with exit status here?
      //     printf("My error code is: %s\n", strerror(errno));
      //     // I really don't understand what this does ^^^^^
      // }
     
  }
  //put the executable files back in to argv[0] AKA replacing the "file.c"
// <<<<<<< HEAD
  sprintf(p->argv[0], "./%s", "devil"); //compileFileName);
// =======

//   // TODO: all executable files should be nameved devil

//   sprintf(p->argv[0], "./%s", compileFileName);
//   printf("this is my new filename: %s\n", p->argv[0]);

  // free(compileFileName);
  free(gccArgs);
}


void makeParentWait(job_t* j, int status, int pid){
  if(pid == waitpid(WAIT_ANY,&status,WUNTRACED) <= 0){
    return;
  }
  //get the process
  process_t* p;
  job_t *current = j;
  while(current != NULL){
    process_t* currentProcess = current -> first_process;
    while( currentProcess != NULL){
      if(currentProcess -> pid == pid){
        p = currentProcess;
      }
      currentProcess = currentProcess->next;
    }
    current = current -> next;
  }

  //check it against the conditions and modifieds

  if(WIFEXITED(status) == true){
    p->completed = true;
   fflush(stdout);
  }

  if(WIFSTOPPED(status)== true){
    printf("process is stopped\n");
    p->stopped = true;
    j->notified = true;
    j->bg = true;
  }

  if(WIFCONTINUED(status)== true){
    p->stopped = false;
  }

  if(WIFSIGNALED(status)==true){
    p->completed = true;
  }else{
    printf("child died\n");
  }

  if(job_is_stopped(j) && isatty(STDIN_FILENO)){
      seize_tty(getpid());
      return;
  }

  //if the job is not full stoped and the tty can't not be taken control,keep waiting
  return makeParentWait(j,status, pid);

}

/* Spawning a process with job control. fg is true if the 
 * newly-created process is to be placed in the foreground. 
 * (This implicitly puts the calling process in the background, 
 * so watch out for tty I/O after doing this.) pgid is -1 to 
 * create a new job, in which case the returned pid is also the 
 * pgid of the new job.  Else pgid specifies an existing job's 
 * pgid: this feature is used to start the second or 
 * subsequent processes in a pipeline.
 * */
void single_process(job_t *j, bool fg){

  pid_t pid;
  process_t *p = j->first_process;

  switch (pid = fork()) {

    case -1: /* fork failure */
        perror("fork error in single_process");
        exit(EXIT_FAILURE);

    case 0: /* child process  */
        p->pid = getpid();      
        new_child(j, p, fg);
        
        // set up the programming environment!
        redirection(p);
        
        // check if argv[0] is a c file and not run with gcc already
        if (strstr(p->argv[0], ".c") != NULL && strstr(p->argv[0], "gcc ") == NULL){
          compiler(p);
        }

        // execute the file
        execvp(p->argv[0], p->argv);
        
        // let's debug why it didn't work...
        // printf("My error code is: %s\n", strerror(errno));

        // once child program completes, this case is done
        exit(EXIT_FAILURE);  /* NOT REACHED */
        break;    /* NOT REACHED */

    default: /* parent */
        /* establish child process group */
        p->pid = pid;
        set_child_pgid(j, p);

        int status = 0;
        if (waitpid(pid, &status, 0) < 0){
          perror("waitpid");
          exit(EXIT_FAILURE);
        }
        
        // check exit status (which means what?)
        // ATTENTION: does this need to be logged?

        // if(WIFEXITED(status)){
        //   // something with exit status here?
        //   printf("My error code is: %s\n", strerror(errno));
        //   // I really don't understand what this does ^^^^^
        // }
  }

  free(j);
  seize_tty(getpid()); // assign the terminal back to dsh
}

void pipeline_process(job_t *j, bool fg){
 
  pid_t pid;
  process_t *p;

  // counts number of pipes needed; there will be numPipes+1 total processes
  int numPipes = -1; 

  for(p = j->first_process; p; p = p->next) {
    numPipes++;
  }

  // now we need to make n pipes
  int pipes[numPipes*2]; // each pipe needs 2 fds
  int i = 0;

  while(i++ < numPipes){
    pipe(pipes + 2*i);
  }

  // now let's loop to fork the children
  int numProcess = 0;

  // loops through each item in the pipeline
  for(p = j->first_process; p; p = p->next){
    numProcess++; // which number process are we on?

    // will be different pipeline situation depending on location
    switch (pid = fork()){

      case -1: // Fork failed
        perror("fork in pipeline");
        exit(EXIT_FAILURE);

      case 0: // Child process
        p->pid = getpid(); // Should this be updated for group ids?
        new_child(j, p, fg);

        // set up pipeline based on location

        // how to add the redirection here? (is there redirection?)

        // First process
        if (p == j->first_process){
          dup2(pipes[1], 1); // write to the pipeline

        // Last process
        } 

        if (p->next == NULL){
          dup2(pipes[numPipes*2 - 2], 0); // read from the pipeline

        // Middle process
        } else {
          dup2(pipes[numPipes*4 - 1], 1); // write to pipeline
          dup2(pipes[numPipes*4 - 4], 0); // read from pipeline
        }

        // close pipelines
        int i = 0;
        int n = 2*numPipes; // number of pipe ends
        while (i++ < n){
          close(pipes[i]);
        }

        // check if argv[0] is a c file and not run with gcc already
        if (strstr(p->argv[0], ".c") != NULL && strstr(p->argv[0], "gcc ") == NULL){
          compiler(p);
        }

        redirection(p);
        // execute the file
        execvp(p->argv[0], p->argv);
        
        exit(EXIT_FAILURE);  /* NOT REACHED */
        break;    /* NOT REACHED */

      default: // Parent process
        
        // establish child process group (how?)
        p->pid = pid;
        set_child_pgid(j, p);

        // want parent to continue the loop to fork again
        // deal with tty here?
        // seize_tty(getpid()); // assign the terminal back to dsh

        // break;

        // parent waits until all child complete
        // not sure where to write the wait?
        // wait(NULL);
    }
    int status= 0;
    int pid = 0;
    if(fg == true){

      makeParentWait(j,status,pid);
      
      
     }

    
  }
  // where should this be located?
  // do we also need to free all of the processes?

  // free(j);
}

void spawn_job(job_t *j, bool fg){
  // Builtin commands are already taken care earlier
  if (j->first_process->next == NULL){
    single_process(j, fg);
  } else {
    pipeline_process(j, fg);
  }
}

/* Sends SIGCONT signal to wake up the blocked job */
void continue_job(job_t *j) {
     if(kill(-j->pgid, SIGCONT) < 0)
          perror("kill(SIGCONT)");
}

void printJobCollection(){
  // int jobCounter = 0;
  char* promptMessage;  
  // char* jobStatus;

  job_t* current;
  current = headOfJobCollection;

  if(current == NULL){
    promptMessage = "There are not currently any jobs";
    printf("%s\n", promptMessage);
    return;
  } // question: is this ^^ ever possible? since the process of calling
    // jobs will put jobs in the collection, right?

  while(current!=NULL){

   
  //   if(current ->notified){
  //     jobStatus = "(Complete)";
  //   } else {
  //     jobStatus = "(Running)";
  //   }

  //   printf("%d: (%ld) %s %s\n",jobCounter,(long)current->pgid, current->commandinfo, jobStatus);

  //   current = current->next;
  //   jobCounter ++;
  }
}

//ATTENTION: NEEDS TO IMPLEMENT - GETTING RID OF COMPLETED JOBS IN 
//           THE JOB BANK (somewhat done)

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 * it immediately.  
 */
bool builtin_cmd(job_t *last_job, int argc, char **argv) {
  
  // Should we "quit"?
  if (!strcmp(argv[0], "quit")) {
    exit(EXIT_SUCCESS);
  
  // Should we print the jobs? 
  } else if (!strcmp("jobs", argv[0])) {

    printJobCollection();
    free(last_job);
    return true;

  // Are we changing directories?
  } else if (!strcmp("cd", argv[0])) {
    
    if(argc <= 1 || chdir(argv[1]) == -1) {
      logError("Improper use of cd\n");
    }

    free(last_job);
    return true;
  
  // Should it run in the background?
  } else if (!strcmp("bg", argv[0])) {
      /* Your code here */
  
  // Should it run in the foreground?
  } else if (!strcmp("fg", argv[0])) {
      /* Your code here */
  }
  
  // We've checked all of the builtin commands
  // This is not a builtin command
  return false;
}

/* Build prompt messaage */
char* promptmsg() {
  char str[50];
  char* promptMessage; 

  // Modified as to include pid 
  snprintf(str,50,"%s%ld%s", "dsh-", (long)getpid(), "$ ");
  promptMessage = str;
	return promptMessage;
}

void addToJobCollection(job_t* lastJob){
  
  if(headOfJobCollection == NULL){
    headOfJobCollection = lastJob;
  
  } else {

    job_t* current;
    current = headOfJobCollection;

    while(current->next != NULL){
      current = current->next;
    }

    current->next = lastJob;
  }
}

//for know what the hell job it contains--------TESTING NOT IMPORTANT:
void printMyJobProcess(process_t * p){
  if(p == NULL){
    return;
  }
  while(p != NULL){
    printf("This is my argc: %d\n This is my pid: %ld\n This is my Complete: %d\n This is my stopped: %d\n This is my status: %d \n This is my ifile: %s\n This is my ofile: %s\n",p->argc, (long)p->pid,p->completed,
                                  p->stopped,p->status, p->ifile, p->ofile);
    // for(int i =0; i < p->argc; i++){
    //   printf("This is argv %d: %s\n",i,p->argv[i] );
    // }

    p = p->next;

  }

}

void printMyJob(job_t* j){
  if(j == NULL){
    return;
  }
  job_t * current;
  current = j;

  while(current!=NULL){
    printf("This is the commandinfo: %s\n This is my pgid: %ld\n This is notified: %d\n This is mystdin: %d\n This is mystdout: %d\n This is mystderr: %d\n This is bg: %d\n",current->commandinfo,(long)current->pgid, current->notified,
                              current->mystdin, current->mystdout, current->mystderr, current->bg);
    if(current->first_process!=NULL){
      printMyJobProcess(current->first_process);
    }else{
      printf("This job's first process is NULL");
    }
    current = current->next;
  }
}
//-------------------------------------------------

int main() {

	init_dsh();
  // ATTENTION: NEED TO CLEAR THE LOG FILE WHEN STARTING SHELL

	DEBUG("Successfully initialized\n");
  headOfJobCollection = NULL;

	while(1) {
        job_t *j = NULL;

    // ATTENTION: doesn't support multiple jobs in one go
    // such as ls ; cat hello.c <-- only does ls

		if(!(j = readcmdline(promptmsg()))) {
			if (feof(stdin)) { /* End of file (ctrl-d) */
				fflush(stdout);

				printf("\n");
				exit(EXIT_SUCCESS);
           		}
			continue; /* NOOP; user entered return or spaces with return */
		}

        /* Only for debugging purposes to show parser output; turn off in the
         * final code */
        // if(PRINT_INFO) print_job(j);

        /* Your code goes here */
        /* You need to loop through jobs list since a command line can contain ;*/
        /* Check for built-in commands */
        /* If not built-in */
            /* If job j runs in foreground */
            /* spawn_job(j,true) */
            /* else */
            /* spawn_job(j,false) */


        while(j !=NULL){
          int argc = j->first_process->argc;

          char** argv = j->first_process->argv;

          // printMyJob(j);

          if(!builtin_cmd(j,argc,argv)){
            // printf("Getting a bloody Job\n");
            addToJobCollection(j);
            spawn_job(j,!(j->bg)); 
          }
          j = j->next;
        }

    }
}
