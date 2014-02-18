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

  if(fg){ // if fg is set
    if(job_is_stopped(j) && isatty(STDIN_FILENO)){  //this if-statement was not part of the original new child
      seize_tty(j->pgid); // assign the terminal
      /* Set the handling for job control signals back to the default. */
      signal(SIGTTOU, SIG_DFL);
    }
  }
}

// Error logging
void logError(char* text) {
   //store completed entry in log
   FILE* logfile = fopen("dsh.log", "a");
   fprintf(logfile, "Error: (%s) %s\n", strerror(errno), text);
   fclose(logfile);
}

// I/O Redirection - Works
void input(process_t*p){
  int fd = open(p->ifile, O_RDONLY);
  if (fd != -1){
    if (dup2(fd, STDIN_FILENO) < 0){
      logError("error occured in dup2 when inputting");
    }
  } else {
    logError("input file cannot be opened; cannot read");
  }
  close(fd);
}

void output(process_t *p){
  int fd = open(p->ofile, O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU);
  if (fd != -1){
    if (dup2(fd, STDOUT_FILENO) < 0){
      logError("error occured in dup2 when outputting");
    }
  } else {
    logError("output file cannot be opened; cannot write");
  }
  close(fd);
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

  // The next 3 lines just make it so that the compiled file name is not always devil:
  // char* compileFileName = (char*) malloc(sizeof(char)*(strlen(p->argv[0])-2));
  // memcpy(compileFileName, p->argv[0],(strlen(p->argv[0])-2));
  // compileFileName[(strlen(p->argv[0])-2)] ='\0';

  // build the arguments required for gcc
  char **gccArgs = (char**) malloc(sizeof(char*) * 5);
  gccArgs[0] = "gcc";
  gccArgs[1] = "-o";
  gccArgs[2] = "devil"; // compileFileName;
  gccArgs[3] = p->argv[0];
  gccArgs[4] = '\0';

  switch (pid = fork()){
    case -1: // error
      printf("FORK ERRORRRRRRR!!!\n");
      // LOG ERROR???

    case 0: // child
      execv("/usr/bin/gcc", gccArgs);

    default: // parent
      if (waitpid(pid, &status, 0) < 0){
        printf("My error is from the parent waiting\n");
        perror("waitpid");
        exit(EXIT_FAILURE);
      }
  }
  // put the executable files back in to argv[0] AKA replacing the "file.c"
  sprintf(p->argv[0], "./%s", "devil");
  
  // if we had wanted the executable file to have the same name as the c file:
  //   sprintf(p->argv[0], "./%s", compileFileName);
  //   printf("this is my new filename: %s\n", p->argv[0]);
  //   free(compileFileName);
  free(gccArgs);
}


void makeParentWait(job_t* j, int status, pid_t pid){
  // requires a valid pid
  if(pid <= 0){
    return;
  }

  // get the process
  process_t* p;
  int innerWhileBreak = 0;
  job_t *current = j;

  while(current != NULL){
    process_t* currentProcess = current->first_process;
    // go through all of the processes in this job to search for pid
    while (currentProcess != NULL){
      if (currentProcess->pid == pid){
        p = currentProcess;
        innerWhileBreak = 1;
        break;
      }
      currentProcess = currentProcess->next;
    }
    // break if the pid was located
    if(innerWhileBreak == 1){
      break;
    }
    current = current->next;
  }



printf("this is my job--------------------------------------------------\n");
printMyJob(j);
printf("this is the process before it get check the signals ------------\n");
printMyJobProcess( p);
printf("---------------------------------------------------------------\n");

  // printf("hiiiiiiiiii\n");

  //check it against the conditions and modifieds

  //check if the process exit and said the process are all complete - everything is normal
  if(WIFEXITED(status) == true){
    printf("process is completed Successfully\n");
    printf("this is the status: %d\n",status );

    p->completed = true;

    p->status = 0;
    fflush(stdout);
  }

  //check if its stopped by a signal or something
  if(WIFSTOPPED(status)== true){
    printf("process is stopped, this is the signal that killed it: %d\n", WSTOPSIG(status));
    printf("this is the status: %d\n",status );
    p->stopped = true;
    current->notified = true;
    current->bg = true;
  }

  // check if the signal told the process to continue again
  // child resume if SIGCOUT is signaled
  if(WIFCONTINUED(status)== true){
    p->stopped = false;
  }

  // Check if the child's process is terminated by the terminal
  if(WIFSIGNALED(status)==true){
    p->completed = true;
    printf( "this is the number of signal that cause this process to terminate: %d\n", WTERMSIG(status));
    printf("this is the status: %d\n",status );

    if(WCOREDUMP(status) == true){
      printf("child is taking a core dump\n");
    }
  }


  // return tty to the parent
  if(job_is_stopped(j) && isatty(STDIN_FILENO)){
    // printf(" you im seizing this bitch\n");
    seize_tty(getpid());
    return;
  }
  //if the job is not full stoped and the tty can't not be taken control, keep waiting

  //having the code below first so it does return a segfault - figuring out why that is
  //possiblitly - > missing a base case.....

  // return makeParentWait(j,status, pid);

  return; // makeParentWait(j,status, pid);
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

// sets up the pipe environment for processes when spawning
void setUpPipe(job_t* j, process_t* p, int* prev, int* next,int read, int write){

  // it is the first process
  if(p != j->first_process){    // when p is not the first process / cause if its the first process, then default should take care of it
    close(prev[write]);         // this also means that p is in the ahead of the first process, next to it, we close the previous write port
    dup2(prev[read], read);     // duplicated the fd of the previous read of the pipe to the current read pipe
    close(prev[read]);          // close the read pipe
  
  // it is the middle process
  } else if(p->next != NULL){   // handles the case where p has a next pipe that its needs to feed into
    close(next[read]);          // we close the next read pipe, it does not need to be mod
    dup2(next[write],write);    //duplicate the next write and use it as the write for this processes ( aka feeding output to the next pipe's output)
    close(next[write]);         //close the write
  
  // it is the last process
  } else {
    dup2(write, next[write]);   //duplicate the write to the next[write] as output if its the end of the process
    close(next[read]);
    close(next[write]);
  }
}

void spawn_job(job_t *j, bool fg){
 /* Builtin commands are already taken care earlier */
  pid_t pid;
  process_t *p;
  int prev[2]; // Pipeline for before this current process
  
  if(pipe(prev) == -1){
    perror("prev pipe failed");
    exit(EXIT_FAILURE);
  }

  for(p = j->first_process; p; p = p->next) {

    int next[2]; // pipeline for after this current process
    int read = 0;
    int write = 1;

    if(pipe(next) == -1){
      perror("next pipe failed");
      exit(EXIT_FAILURE);
    }
    
    switch (pid = fork()) {

      case -1: /* fork failure */
        perror("fork");
        exit(EXIT_FAILURE);

      case 0: /* child process  */
        p->pid = getpid();   
        set_child_pgid(j, p);  

        setUpPipe(j, p, prev, next, read, write);  
        new_child(j, p, fg);


        if (strstr(p->argv[0], ".c") != NULL && strstr(p->argv[0], "gcc ") == NULL){
          compiler(p);
        }

        redirection(p);

        if (execvp(p->argv[0], p->argv) == -1){
          logError("execvp failed");
        }

        perror("New child should have done an exec");
        exit(EXIT_FAILURE);  /* NOT REACHED */
        break;    /* NOT REACHED */

      default: /* parent */
        /* establish child process group */
        close(next[write]);

        if(p->next == NULL){
          close(next[read]);
        }

        p->pid = pid;
        set_child_pgid(j, p);


        prev[write] = next[write];
        prev[read] = next[read];
    }
         
    if(fg){
      pid_t pid;
      pid = waitpid(WAIT_ANY, &(p->status), WUNTRACED);

      makeParentWait(j, p->status, pid);
    }
  }
}

/* Sends SIGCONT signal to wake up the blocked job */
void continue_job(job_t *j) {
  if(kill(-j->pgid, SIGCONT) < 0)
    perror("kill(SIGCONT)");
  // Should we add an error message to STDERR?
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

   
    // if(current ->notified){
    //   jobStatus = "(Complete)";
    // } else {
    //   jobStatus = "(Running)";
    // }

    // printf("%d: (%ld) %s %s\n",jobCounter,(long)current->pgid, current->commandinfo, jobStatus);

    current = current->next;
    // jobCounter ++;
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
    return true;

  // Should we print the jobs? 
  } else if (!strcmp("jobs", argv[0])) {
    printJobCollection();
    return true;

  // Are we changing directories?
  } else if (!strcmp("cd", argv[0])) {
    if(argc <= 1 || chdir(argv[1]) == -1) {
      logError("improper use of cd");
    }
    return true;
  
  // Should it run in the background?
  } else if (!strcmp("bg", argv[0])) {
    
    /* Your code here */
    return true;

  // Should it run in the foreground?
  } else if (!strcmp("fg", argv[0])) {
    // moves a background command to foreground
    // if there's a parameter, that's the job that should be moved
    // otherwise, grabs the most recently suspended job

    // assume no parameters (needs to )
    job_t *j = last_job;

    // need to overwrite with actual job information
    if (argc > 1){
      int pgid = atoi(argv[1]); // get the pid from the line
      // find the appropriate job in the job list
      job_t *temp = headOfJobCollection;
      while((temp != NULL) && (temp->pgid != pgid)){
        temp = temp->next;
      }
      // that pgid number does not exist
      if(temp == NULL){
        logError("job did not exist");
        return true;
      }
      j = temp;
    }

    j->bg = false;

    if (job_is_stopped(j)){
      continue_job(j);
      
      if(isatty(STDIN_FILENO)) {
        seize_tty(j->pgid);
      }
    }

    return true;
  }
  
  // We've checked all of the builtin commands; this is not a builtin command
  return false;
}

/* Build prompt messaage */
char* promptmsg() {
  char str[50];
  char* promptMessage; 

  // Modified as to include pid 
  snprintf(str, 50, "%s%ld%s", "dsh-", (long)getpid(), "$ ");
  promptMessage = str;
	return promptMessage;
}

// Adds the latest job to the collection of non-builtin jobs
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
    current = current->next;
    printf("printing last job in JobCollection\n");
    printMyJob(current);
    current->next = NULL;
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
    printf("This is the commandinfo: %s\n  This is my pgid: %ld\n  This is notified: %d\n  This is mystdin: %d\n  This is mystdout: %d\n  This is mystderr: %d\n  This is bg: %d\n", current->commandinfo,(long)current->pgid, current->notified, current->mystdin, current->mystdout, current->mystderr, current->bg);
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
  remove("dsh.log"); // clear log file when starting the shell
	DEBUG("Successfully initialized\n");
  headOfJobCollection = NULL;

	while(1) {
    job_t *j = NULL;

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
    //if(PRINT_INFO) print_job(j);

    // Loop through the jobs listed in the command line
    while(j != NULL){
      int argc = j->first_process->argc;
      char** argv = j->first_process->argv;
      // printf("--------------What is my Curent job------------------------\n");
      // printMyJob(j);
      // printf("-----------------------------------------------------------\n");
      if(!builtin_cmd(j, argc, argv)){
        // headOfJobCollection = NULL;
        addToJobCollection(j);
        spawn_job(j,!(j->bg)); 
      }
      j = j->next;
      // printf("--------------What is my after job-------------------------\n");
      // printMyJob(j);
      // printf("-----------------------------------------------------------\n");
      // j = j->next;
    }
    // printf("done spawning all processes in job, reading from cmdline again\n");
  }
}
