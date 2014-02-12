#include "dsh.h"

void seize_tty(pid_t callingprocess_pgid); /* Grab control of the terminal for the calling process pgid.  */
void continue_job(job_t *j); /* resume a stopped job */
void spawn_job(job_t *j, bool fg); /* spawn a new job */

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

// I/O Redirection - Needs to be tested
void input(process_t*p){
  int fd = open(p->ifile, O_RDONLY);
  if (fd != -1){
    dup2(fd, STDIN_FILENO);
    close(fd);
  } else {
    printf("Cant open shit for inputting\n"); // oh what pleasant notes we have
  }
}

void output(process_t *p){
  int fd = open(p->ofile, O_CREAT | O_TRUNC | O_WRONLY, O_WRONLY);
  if (fd != -1){
    dup2(fd, STDOUT_FILENO);
    close(fd);
  } else {
    printf("Cant open shit for writing\n");
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

void spawn_job(job_t *j, bool fg) {

	pid_t pid;
	process_t *p;

	for(p = j->first_process; p; p = p->next) {

	  /* YOUR CODE HERE? */
	  /* Builtin commands are already taken care earlier */
	  
    // Three categories of jobs: single, jobs with I/O redirection, and pipelines
    // if single, then job->mystdout set to outfile (then fork, parent waits)
    // if pipeline, do ^^ but pay attention to stdout and stdin (before fork), 
    //    using dup() and dup2()
    // if redirection, use dup() and dup2() to copy over stdin and stdout
    // use syscalls such as create(), open(), read(), write() to preform desired action

	  switch (pid = fork()) {

      case -1: /* fork failure */
        perror("fork");
        exit(EXIT_FAILURE);

      case 0: /* child process  */
        p->pid = getpid();	    
        new_child(j, p, fg);

        execv(p->argv[0], p->argv[1]);
        
        // We've established that the builtin commands are taken care of
        // so here we need to check if that file exists 
        //    if it does --> exec that file
        //    if it doesn't --> log that it doesn't work

        // To preform that job, take in process_t for compile and I/O reading


	    /* YOUR CODE HERE?  Child-side code for new process. */
        perror("New child should have done an exec");
        exit(EXIT_FAILURE);  /* NOT REACHED */
        break;    /* NOT REACHED */

      default: /* parent */
        /* establish child process group */
        p->pid = pid;
        set_child_pgid(j, p);

        // check what type of job this is!
        

        input(p);
        output(p);
        redirection(p);

        /* YOUR CODE HERE?  Parent-side code for new process.  */
    }

    /* YOUR CODE HERE?  Parent-side code for new job.*/
	  seize_tty(getpid()); // assign the terminal back to dsh
	}
}

/* Sends SIGCONT signal to wake up the blocked job */
void continue_job(job_t *j) {
     if(kill(-j->pgid, SIGCONT) < 0)
          perror("kill(SIGCONT)");
}

void printJobCollection(){
  int jobCounter = 0;
  char* promptMessage;  
  char* jobStatus;

  job_t* current;
  current = headOfJobCollection;

  if(current == NULL){
    promptMessage = "there are currently no bloody jobs";
    printf("%s\n", promptMessage);
    return;
  }

  while(current!=NULL){

    if(current ->notified){
      jobStatus = "(Complete)";
    } else {
      jobStatus = "(Running)";
    }

    printf("%d: (%ld) %s %s\n",jobCounter,(long)current->pgid, current->commandinfo, jobStatus);

    current = current->next;
    jobCounter ++;
  }
}

  //ATTENTION: NEEDS TO IMPLEMENT - GETTING RID OF COMPLETED JOBS IN 
  //            THE JOB BANK. 


/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 * it immediately.  
 */
bool builtin_cmd(job_t *last_job, int argc, char **argv) {
  // Check whether the command then execute it immediately
  
  // Should we "quit"?
  if (!strcmp(argv[0], "quit")) {
            exit(EXIT_SUCCESS);
  
  // Should we print the jobs? 
  } else if (!strcmp("jobs", argv[0])) {
      printJobCollection();
      return true;

  // Are we changing directories?
  } else if (!strcmp("cd", argv[0])) {
      if(argc <=1 || chdir(argv[1])==-1){
        printf("cant do this bub\n");  // needs to be put in logger later
      }
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
  
  } else{

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
  printf("This is my argc: %d\n This is my pid: %ld\n This is my Complete: %d\n This is my stopped: %d\n This is my status: %d \n This is my ifile: %s\n This is my ofile: %s\n",p->argc, (long)p->pid,p->completed,
                                  p->stopped,p->status, p->ifile, p->ofile);
  for(int i =0; i < p->argc; i++){
    printf("%d: %s\n",i,p->argv[i] );
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

          printMyJob(j);

          if(!builtin_cmd(j,argc,argv)){
            printf("Getting a bloody Job\n");
            addToJobCollection(j);
            spawn_job(j,!(j->bg)); 
          }
          j = j->next;
        }

    }
}
