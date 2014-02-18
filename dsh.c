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
   fprintf(logfile, "Error: (%s) %s\n", strerror(errno), text);
   fclose(logfile);
}

// I/O Redirection - Works
void input(process_t*p){
  int fd = open(p->ifile, O_RDONLY);
  if (fd != -1){
    if (dup2(fd, STDIN_FILENO) < 0){
      logError("input dup2 failed");
    }
    close(fd);
  } else {
    logError("input file cannot be opened; cannot read");
  }
}

void output(process_t *p){
  int fd = open(p->ofile, O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU);
  if (fd != -1){
    if (dup2(fd, STDOUT_FILENO) < 0){
      logError("output dup2 failed");
    }
    close(fd);
  } else {
    logError("output file cannot be opened; cannot write");
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

  // If we wanted to compile each file so that the exec file is called
  // its own name (rather than always being called "devil"), use these lines:
  //    char* compileFileName = (char*) malloc(sizeof(char)*(strlen(p->argv[0])-2));
  //    memcpy(compileFileName, p->argv[0],(strlen(p->argv[0])-2));
  //    compileFileName[(strlen(p->argv[0])-2)] ='\0';

  // Create the arguments required for running gcc
  char **gccArgs = (char**)malloc(sizeof(char*)*5);
  gccArgs[0] = "gcc";
  gccArgs[1] = "-o";
  gccArgs[2] = "devil"; //compileFileName;
  gccArgs[3] = p->argv[0];
  gccArgs[4] = '\0';

  // fork to create the devil.exec file
  switch (pid = fork()){
    case -1:  // fork failure
        perror("fork error in single_process");
        exit(EXIT_FAILURE);

    case 0:   // child process
      execv("/usr/bin/gcc", gccArgs);

    default: // parent process
      if (waitpid(pid, &status, 0) < 0){
          printf("My error is from the parent waiting\n");
          perror("waitpid");
          exit(EXIT_FAILURE);
        }     
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


void checkStatus(job_t* j, process_t* p, int status){

// printf("this is my job--------------------------------------------------\n");
// printMyJob(j);
// printf("this is the process before it get check the signals ------------\n");
// printMyJobProcess( p);
// printf("---------------------------------------------------------------\n");

  // printf("hiiiiiiiiii\n");
  //check it against the conditions and modifieds

  //check if the process exit and said the process are all complete - everything is normal
  if(WIFEXITED(status) == true){
    printf("process is completed Successfully\n");
    printf("this is the status: %d\n",status );
    p->completed = true;
    // p->status = status;
   fflush(stdout);
  }else

  //check if its stopped by a signal or something
  if(WIFSTOPPED(status)== true){
    printf("process is stopped, this is the signal that killed it: %d\n", WSTOPSIG(status));
    printf("this is the status: %d\n",status );
    p->stopped = true;
    j->notified = true;
    j->bg = true;
  }else

  //check if the signal told the process to continue again
  //child resume if SIGCOUT is signaled
  if(WIFCONTINUED(status)== true){
    p->stopped = false;
  }else

// Check if the child's process is terminated by the terminal
  if(WIFSIGNALED(status)==true){
    p->completed = true;
    printf( "this is the number of signal that cause this process to terminate: %d\n", WTERMSIG(status));
    printf("this is the status: %d\n",status );

    if(WCOREDUMP(status) == true){
      printf("child is taking a core dump\n");
    }
  }

}


process_t* findCurrentProcess(job_t* j , pid_t pid){
  int innerWhileBreak = 0;
  job_t *current = j;
  process_t * p;

  while(current != NULL){
    process_t* currentProcess = current -> first_process;
    while( currentProcess != NULL){
      if(currentProcess -> pid == pid){
      p = currentProcess;
                    // printf("this is the process that i am looking at\n");
                    // printMyJobProcess( p);
      innerWhileBreak = 1;
      break;
      }
      currentProcess = currentProcess->next;
    }
    if(innerWhileBreak == 1){
        break;
    }
    current = current -> next;
  }

  return p;

}

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

void pipeline(job_t * j, bool fg){
// Builtin commands are already taken care earlier
  // if (j->first_process->next == NULL){
  //   printf("single process\n");
  //   single_process(j, fg);
  // } else {
  //   printf("pipeline bitch!\n");
  //   pipeline_process(j, fg);
  // }

  pid_t pid;
  process_t *p;
  
  int pipeFd[2], input;
  input = pipeFd[0];
  // output = STDOUT_FILENO;
  if(strcmp("cat",j->first_process->argv[0])==0){
    single_process(j, fg);
  }


  for(p = j->first_process; p; p = p->next) {
    if(pipe(pipeFd) == -1){
      perror("your pipe is shit");
    }
   

    /* YOUR CODE HERE? */
    /* Builtin commands are already taken care earlier */
    
    switch (pid = fork()) {

          case -1: /* fork failure */
            perror("fork");
            exit(EXIT_FAILURE);

          case 0: /* child process  */
            p->pid = getpid();   
          set_child_pgid(j, p);
        if(fg){ // if fg is set
          if( job_is_stopped(j) && isatty(STDIN_FILENO)){
              seize_tty(j->pgid); // assign the terminal


             /* Set the handling for job control signals back to the default. */
             signal(SIGTTOU, SIG_DFL);
          }
       }
        
            // printf("im after redirection\n"); 

            if(p == j->first_process && p->next == NULL){
              close(pipeFd[0]);
              close(pipeFd[1]);
            }

            if( p!= j->first_process || (p==j->first_process && p -> next != NULL)  ){
              if( p == j->first_process){
                close(pipeFd[0]);
                dup2(pipeFd[1], STDOUT_FILENO);
              }else if ( p->next != NULL){
                dup2(input, STDIN_FILENO);
                close(pipeFd[0]);
                dup2(pipeFd[1],1);
              }else{
                dup2(input,0);
                close(pipeFd[0]);
              }


            }

            redirection(p);



            if (strstr(p->argv[0], ".c") != NULL && strstr(p->argv[0], "gcc ") == NULL){
               compiler(p);
            }
            // printf("im after compiler\n"); 
                // printf("im in the child of the for %s\n",p->argv[0]);



            if( execvp(p->argv[0], p->argv) == -1){
              perror("execvp failed");
            }
            // printf("this is after exect\n");
            
      /* YOUR CODE HERE?  Child-side code for new process. */
            perror("New child should have done an exec");
            exit(EXIT_FAILURE);  /* NOT REACHED */
            break;    /* NOT REACHED */

          default: /* parent */

            printf("i am in the parent case\n");

            printf( "this is the pid that i am setting my progress to %d\n", pid);
            p->pid = pid;
            set_child_pgid(j, p);
            input = pipeFd[0];
            close(pipeFd[1]);


            if(fg){
              int cpid;
              int status;

              while((cpid = waitpid(WAIT_ANY, &status, WUNTRACED))>0){
                printf("im waiting motherfucker!!!!\n ");
                p = findCurrentProcess(j,cpid);
                checkStatus(j, p, status);
              }

            }




          }

    printf(" i am at the end of the for loops\n");

  } // for loops end bracket

           //  if( fg){

           //  for(p = j->first_process; p; p = p->next) {
           //    int status;
           //    if( waitpid(p->pid, &status, WUNTRACED)>0){
           //      checkStatus(j, p,status);


           //    }


           //  }


           // }



           if(job_is_stopped(j) && isatty(STDIN_FILENO)){
            seize_tty(getpid());
           }




}

void spawn_job(job_t *j, bool fg){
  // Builtin commands are already taken care earlier
  if (j->first_process->next == NULL){
    single_process(j, fg);
   } else {
    pipeline(j, fg);
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
    
    return true;
  // Should we print the jobs? 
  } else if (!strcmp("jobs", argv[0])) {

    printJobCollection();
    // free(last_job);
    return true;

  // Are we changing directories?
  } else if (!strcmp("cd", argv[0])) {
    
    if(argc <= 1 || chdir(argv[1]) == -1) {
      logError("Improper use of cd\n");
    }

    //free(last_job); // or do we need to mark it as status = completed

    return true;
  
  // Should it run in the background?
  } else if (!strcmp("bg", argv[0])) {
      /* Your code here */
  
    return true;
  // Should it run in the foreground?
  } else if (!strcmp("fg", argv[0])) {
      /* Your code here */
    printf( "hi im in fg");

    return true;
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
  snprintf(str, 50, "%s%ld%s", "dsh-", (long)getpid(), "$ ");
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
    //if(PRINT_INFO) print_job(j);

    // Loop through the jobs listed in the command line

    addToJobCollection(j);

    while(j != NULL){

      int argc = j->first_process->argc;
      char** argv = j->first_process->argv;

          if(!builtin_cmd(j,argc,argv)){
            printf("Getting a bloody Job\n");
            spawn_job(j,!(j->bg)); 
          }

      // printf("--------------What is my after job-------------------------\n");
      // printMyJob(j);
      // printf("-------------------------------------------------------------\n");

      j = j->next;
    }
  }
}
