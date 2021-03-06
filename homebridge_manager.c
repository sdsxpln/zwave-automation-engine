#include "homebridge_manager.h"
#include "config.h"
#include "logger.h"
#include "event.h"
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>

DECLARE_LOGGER(HomebridgeManager)

int homebridge_start();
void homebridge_stopped_handler(int sig);

pid_t   homebridge_pid;
int     homebridge_fd = -1;

int homebridge_manager_init()
{
    if(global_config.homebridge_enable)
    {
        LOG_ADVANCED(HomebridgeManager, "Initializing Homebridge manager...");
        
        homebridge_fd = homebridge_start();
        return homebridge_fd;
    }
    else
    {
        LOG_ADVANCED(HomebridgeManager, "Homebridge support disabled");
    }

    return -1;
}

int homebridge_start()
{
    int pipefd[2];
    if(0 != pipe2(pipefd, O_NONBLOCK))
    {
        printf("Pipe error\n");
        LOG_ERROR(HomebridgeManager, "Failed to create process pipe");
        return -1;
    }

    homebridge_pid = fork();

    if(0 != homebridge_pid)
    {
        // parent
        struct sigaction sa;
        sa.sa_handler = &homebridge_stopped_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
        if (sigaction(SIGCHLD, &sa, 0) == -1) 
        {
            perror(0);
            exit(1);
        }

        // Close write part of the pipe
        LOG_DEBUG(HomebridgeManager, "Homebridge read fd: %d", pipefd[0]);
        close(pipefd[1]);
        return pipefd[0];
    }
    else
    {
        char* args[] = { NULL, NULL, NULL, NULL };
        char plugin_arg[256] = {0};
        snprintf(plugin_arg, 255, "-P %s", global_config.homebridge_plugin_path);
        args[0] = "homebridge";
        args[1] = "-P";
        args[2] = global_config.homebridge_plugin_path;

        /* Create a new SID for the child process */  
        pid_t sid = setsid();  
        if (sid < 0)    
        {  
            exit(EXIT_FAILURE);  
        }  
    
        // Close read part of the pipe
        close(pipefd[0]);
        
        // Duplicate stdout and stderr to pipe
        dup2 (pipefd[1], STDOUT_FILENO);  
        dup2 (pipefd[1], STDERR_FILENO);  
      
        //close(STDOUT_FILENO);
        //close(STDERR_FILENO);

        /*resettign File Creation Mask */  
        umask(027); 

        execvp("homebridge", args);
        perror("execvp");
        exit(1);
    }
}

void homebridge_stopped_handler(int sig)
{
  int saved_errno = errno;
  int status = 0;
  pid_t pid = waitpid(homebridge_pid, &status, WNOHANG);

  if(WIFEXITED(status))
  {
      // The following 2 lines cause hangs if signal is received from within malloc() call
      LOG_DEBUG(HomebridgeManager, "Homebridge stopped");
      event_unregister_fd(homebridge_fd);
  }

  errno = saved_errno;
}

void homebridge_manager_stop()
{
    event_unregister_fd(homebridge_fd);
    kill(homebridge_pid, SIGINT);
}

void homebridge_on_event(int homebridge_fd, void* context)
{
    char buf[512] = {0};
    int nread = 0;

    nread = read(homebridge_fd, buf, 511);
    
    // Remove '\r\n'...
    if(nread > 1)
    {
        buf[nread-1] = 0;
        LOG_INFO(HomebridgeManager, buf);
    }
}
