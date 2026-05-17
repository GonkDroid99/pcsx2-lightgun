#include "MameHookerProxy.h"
#include <linux/limits.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <cstring>
#include <cerrno>
#include <cstdio>


MameHookerProxy& MameHookerProxy::GetInstance()
{
 
  static MameHookerProxy s_instance;
  return s_instance;
}





bool MameHookerProxy::writeToFd(int fd, const std::string& msg)
{
  ssize_t ret = write(fd, msg.c_str(), msg.size());
  return ret == static_cast<ssize_t>(msg.size());
}

static void closeFd(int& fd)
{
  if (fd < 0) return;
  close(fd);
  fd = -1;
}


std::string MameHookerProxy::getExecutableDirectory()
{
  char buffer[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (len < 0) return ".";
  buffer[len] = '\0';
  std::string fullPath(buffer);
  size_t found = fullPath.find_last_of('/');
  return (found != std::string::npos) ? fullPath.substr(0, found) : ".";
}


// On Linux:   connects to /tmp/CoreFxPipe_<name>  (the path .NET Core uses — note lowercase 'x')  CHANGE PIPE EVENTUALLY AS OLD .NET
int MameHookerProxy::connectGunPipe(const std::string& pipeName)
{

// Create a Unix domain socket, set to fd return -1 if fail and bail
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  //Declare adress struct
  struct sockaddr_un addr{};
  // Tell kernal it's a unix domain socket address
  addr.sun_family = AF_UNIX;
  //build socket path
  std::string socketPath = "/tmp/CoreFxPipe_" + pipeName;
  //copy the path into the sun_path buffer and leave space for null terminator
  strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);


  //Attempt connect to the server listening
  if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
  {
    close(fd);
    return -1;
    //conection faiiled

  }
  //connection success return
  return fd;
}



void MameHookerProxy::Init()
{
  pipeConnected = false;
  hPipe = -1;
  hPipeGunA = -1;
  hPipeGunB = -1;
  hPipeGunC = -1;
  hPipeGunD = -1;
  childPid = -1;
}

void MameHookerProxy::CloseGame()
{
  activeGame = "";
  active = false;
    if (pipeConnected && hPipe >= 0)
    {
      writeToFd(hPipe,"CLOSE");
    }

  if (childPid > 0)
  {
    kill(childPid, SIGTERM);
    waitpid(childPid, nullptr, WNOHANG);
    childPid = -1;
  }

  closeFd(hPipeGunA);
  closeFd(hPipeGunB);
  closeFd(hPipeGunC);
  closeFd(hPipeGunD);
  closeFd(hPipe);


  pipeConnected = false;
  pipeConnectedGunA = false;
  pipeConnectedGunB = false;
  pipeConnectedGunC = false;
  pipeConnectedGunD = false;
}





void MameHookerProxy::Gunshot(int gunIndex)
{
  std::string message = "1";




  if (!pipeConnected || !active)
    return;


  const char* pipeNames[] = {
    "MameHookerProxyRecoilGunA",
    "MameHookerProxyRecoilGunB",
    "MameHookerProxyRecoilGunC",
    "MameHookerProxyRecoilGunD",
  };
  int* fds[] = { &hPipeGunA, &hPipeGunB, &hPipeGunC, &hPipeGunD };
  bool* connected[] = { &pipeConnectedGunA, &pipeConnectedGunB, &pipeConnectedGunC, &pipeConnectedGunD };

  if (gunIndex < 0 || gunIndex > 3) return;

  if (!*connected[gunIndex])
  {
    closeFd(*fds[gunIndex]);
    *fds[gunIndex] = connectGunPipe(pipeNames[gunIndex]);
    *connected[gunIndex] = (*fds[gunIndex] >= 0);
  }

  if (*connected[gunIndex])
  {
    if (!writeToFd(*fds[gunIndex], "1"))
    {
      closeFd(*fds[gunIndex]);
      *connected[gunIndex] = false;
    }
  }
}





void MameHookerProxy::SendState(std::string key, int value)
{
  if (!pipeConnected || !active)
    return;

  std::string message = key + ":" + std::to_string(value);
  if (!writeToFd(hPipe, message))
  {
    closeFd(hPipe);
    hPipe = connectGunPipe("MameHookerProxyControl");
    pipeConnected = (hPipe >= 0);
  }
}



// Rewrite using closeFd with linux support. 

void MameHookerProxy::StartGame(std::string id)
{
  if (id == activeGame || id.empty())
    return;
  


  
  activeGame = id;
  active = true;

  closeFd(hPipe);
  closeFd(hPipeGunA);
  closeFd(hPipeGunB);
  closeFd(hPipeGunC);
  closeFd(hPipeGunD);
  pipeConnected = false;
  pipeConnectedGunA = false;
  pipeConnectedGunB = false;
  pipeConnectedGunC = false;
  pipeConnectedGunD = false;

  std::string execDir = getExecutableDirectory();
  fprintf(stderr, "[MameHooker] execDir=%s\n", execDir.c_str());


  std::string exePath = execDir + "/MameOutputSender";




  struct stat st{};
  //Get file information, (convert exePath to C string for stat) Fill Struct st with file details, return if == 0 success, && check if user has execution perms. True if all good
  bool exists = (stat(exePath.c_str(), &st) == 0 && (st.st_mode & S_IXUSR));
  fprintf(stderr, "[MameHooker] exePath=%s exists=%d\n", exePath.c_str(), (int)exists);

  if (exists)
  {

    //Clone process if exists for debug
    pid_t pid = fork();
    fprintf(stderr, "[MameHooker] fork() returned pid=%d\n", (int)pid);
    if (pid == 0)
    {
      // child — redirect stdout/stderr to a log file for debugging

      //Try to open/create log
      int logfd = open("/tmp/MameOutputSender.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);

      //check if it actually opend if fail skip
      if (logfd >= 0)
      {
        //dump to Console and Log then close
        dup2(logfd, STDOUT_FILENO);
        dup2(logfd, STDERR_FILENO);
        close(logfd);
      }

      //setup game name args and Output args.
      std::string gnArg = "gamename=" + id;
      std::string outArg = "outputs=GunRecoil_P1,GunRecoil_P2,TriggerPress_P1,TriggerPress_P2";

      //Execute MameOutputSender
      execl(exePath.c_str(), exePath.c_str(), gnArg.c_str(), outArg.c_str(), nullptr);
      _exit(1);
    }
    //Save the pid so we can kill it later etc
    else if (pid > 0)
    {
      childPid = pid;
      fprintf(stderr, "[MameHooker] child launched, childPid=%d\n", (int)childPid);
    }
  }

  // Attempt to connect to the control pipe on a background thread also printing to a debug log probably disable just for testing
  std::thread([this]() {
    FILE* dbg = fopen("/tmp/duckstation_pipe_debug.log", "w"); // open log
    if (dbg) { fprintf(dbg, "[DS] Background thread started, will try 50x to connect\n"); fflush(dbg); }

    //try 50 times probably over kill
    for (int i = 0; i < 50 && active; ++i)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      std::string socketPath = "/tmp/CoreFxPipe_MameHookerProxyControl"; //set socketpath probably change name
      struct stat st{};
      //check socket path
      bool exists = (stat(socketPath.c_str(), &st) == 0);


      //print debug info
      if (dbg) { fprintf(dbg, "[DS] attempt %d: socket exists=%d\n", i, (int)exists); fflush(dbg); }

      //Try to connect to the pipe with the name MameHookerProxyControl
      int fd = connectGunPipe("MameHookerProxyControl");
      if (fd >= 0)
      {
        hPipe = fd;
        pipeConnected = true;
        if (dbg) { fprintf(dbg, "[DS] Connected on attempt %d!\n", i); fflush(dbg); }
        fclose(dbg);
        return;
      }
      else
      {
        if (dbg) { fprintf(dbg, "[DS] attempt %d failed: errno=%d (%s)\n", i, errno, strerror(errno)); fflush(dbg); }
      }
    }
    if (dbg) { fprintf(dbg, "[DS] All 50 attempts failed, pipeConnected stays false\n"); fclose(dbg); }
  }).detach();

  }

