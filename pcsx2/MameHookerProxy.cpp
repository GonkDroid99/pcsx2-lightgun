#include "MameHookerProxy.h"
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <cstring>
#include <climits>
#include <cerrno>
#include <cstdio>
#endif


MameHookerProxy& MameHookerProxy::GetInstance()
{
  // TODO: insérer une instruction return ici
  static MameHookerProxy s_instance;
  return s_instance;
}


//Do we actually need this? Not used.
// bool MameHookerProxy::fileExists(const wchar_t* filePath)
// {
//   DWORD fileAttributes = GetFileAttributesW(filePath);
//   return (fileAttributes != INVALID_FILE_ATTRIBUTES &&
//           !(fileAttributes & FILE_ATTRIBUTE_DIRECTORY));
// }


bool MameHookerProxy::writeToFd(int fd, const std::string& msg)
{
#ifdef _WIN32
  HANDLE h = reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd));
  DWORD written;
  return WriteFile(h, msg.c_str(), static_cast<DWORD>(msg.size()), &written, NULL) != 0;
#else
  ssize_t ret = write(fd, msg.c_str(), msg.size());
  return ret == static_cast<ssize_t>(msg.size());
#endif
}

static void closeFd(int& fd)
{
  if (fd < 0) return;
#ifdef _WIN32
  CloseHandle(reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd)));
#else
  close(fd);
#endif
  fd = -1;
}



// Got a processs to launch MameOutputSender but very over the top. Most of it isn't even used.
// void MameHookerProxy::launchProgram(const char* programPath, PROCESS_INFORMATION& processInfo)
// {
//   STARTUPINFOA startupInfo;
//   ZeroMemory(&startupInfo, sizeof(startupInfo));
//   startupInfo.cb = sizeof(startupInfo);

//   if (!CreateProcessA(NULL,                // No module name (use command line)
//                       (LPSTR)programPath,  // Command line
//                       NULL,                // Process handle not inheritable
//                       NULL,                // Thread handle not inheritable
//                       FALSE,               // Set handle inheritance to FALSE
//                       0,                   // No creation flags
//                       NULL,                // Use parent's environment block
//                       NULL,                // Use parent's starting directory
//                       &startupInfo,        // Pointer to STARTUPINFO structure
//                       &processInfo         // Pointer to PROCESS_INFORMATION structure
//                       ))
//   {
//     std::cerr << "Error launching program: " << GetLastError() << std::endl;
//   }
// }

std::string MameHookerProxy::getExecutableDirectory()
{
#ifdef _WIN32
  char buffer[MAX_PATH];
  GetModuleFileNameA(NULL, buffer, MAX_PATH);
  std::string fullPath(buffer);
  size_t found = fullPath.find_last_of("\\/");
  return fullPath.substr(0, found);
#else
  char buffer[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (len < 0) return ".";
  buffer[len] = '\0';
  std::string fullPath(buffer);
  size_t found = fullPath.find_last_of('/');
  return (found != std::string::npos) ? fullPath.substr(0, found) : ".";
#endif
}


// On Windows: connects to \\.\pipe\<name>
// On Linux:   connects to /tmp/CoreFxPipe_<name>  (the path .NET Core uses — note lowercase 'x')  CHANGE PIPE EVENTUALLY AS OLD .NET
int MameHookerProxy::connectGunPipe(const std::string& pipeName)
{

//Set pipe path and open return handle to a int, if fail -1
#ifdef _WIN32
  std::string path = "\\\\.\\pipe\\" + pipeName;
  HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  return (h == INVALID_HANDLE_VALUE) ? -1 : (int)(intptr_t)h;


// same but linux.  Create a Unix domain socket, set to fd return -1 if fail and bail
#else
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
#endif
}



void MameHookerProxy::Init()
{
  pipeConnected = false;
  hPipe = -1;
  hPipeGunA = -1;
  hPipeGunB = -1;
  hPipeGunC = -1;
  hPipeGunD = -1;
#ifdef _WIN32
  processInfo = nullptr;
#else
  childPid = -1;
#endif
}

void MameHookerProxy::CloseGame()
{
  activeGame = "";
  active = false;
  bool needForceClose = true; //?? Why
  // if (processInfo != nullptr)
  // {
    if (pipeConnected && hPipe >= 0)
    {
      writeToFd(hPipe,"CLOSE");
    }

#ifdef _WIN32
  if (processInfo != nullptr)
  {
    DWORD exitCode;
    if (GetExitCodeProcess(processInfo->hProcess, &exitCode) && exitCode == STILL_ACTIVE)
    {
      TerminateProcess(processInfo->hProcess, 0);
      CloseHandle(processInfo->hProcess);
      CloseHandle(processInfo->hThread);
    }
    delete processInfo;
    processInfo = nullptr;
  }
#else
  if (childPid > 0)
  {
    kill(childPid, SIGTERM);
    waitpid(childPid, nullptr, WNOHANG);
    childPid = -1;
  }
#endif

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








  //     std::string message = "CLOSE";
  //     DWORD bytesWritten;
  //     DWORD messageLength = static_cast<DWORD>(message.length());
  //     if (!WriteFile(hPipe, message.c_str(), messageLength, &bytesWritten, NULL))
  //     {
  //       CloseHandle(hPipe);
  //       pipeConnected = false;
  //     }
  //     else
  //     {
  //       needForceClose = false;
  //     }
  //   }

  //   if (needForceClose)
  //   {
  //     DWORD exitCode;
  //     if (GetExitCodeProcess((*processInfo).hProcess, &exitCode) && exitCode == STILL_ACTIVE)
  //     {
  //       // Le programme est toujours en cours d'exécution, tuez-le
  //       if (!TerminateProcess((*processInfo).hProcess, 0))
  //       {

  //       }
  //       CloseHandle((*processInfo).hProcess);
  //       CloseHandle((*processInfo).hThread);
  //     }
  //   }
  // }

  
  // if (hPipeGunA != nullptr)
  // {
  //   CloseHandle(hPipeGunA);
  // }

  // if (hPipeGunB != nullptr)
  // {
  //   CloseHandle(hPipeGunB);
  // }

  // if (hPipeGunC != nullptr)
  // {
  //   CloseHandle(hPipeGunC);
  // }

  // if (hPipeGunD != nullptr)
  // {
  //   CloseHandle(hPipeGunD);
  // }
  
  // if (hPipe != nullptr)
  // {
  //   CloseHandle(hPipe);
  // }

  // if (hPipeSindenGunA != nullptr)
  // {
	// CloseHandle(hPipeSindenGunA);
  // }

  // if (hPipeSindenGunB != nullptr)
  // {
	// CloseHandle(hPipeSindenGunB);
  // }

  // pipeConnected = false;
  // pipeConnectedGunA = false;
  // pipeConnectedGunB = false;
  // pipeConnectedGunC = false;
  // pipeConnectedGunD = false;

  // pipeConnectedSindenGunA = false;
  // pipeConnectedSindenGunB = false;

//}





void MameHookerProxy::Gunshot(int gunIndex)
{
  std::string message = "1";


  // commented out and removed Sinden, It's was added but it doesn't actually do anything. Just added UI elements, maybe output to file. So removing for now. 
  // if (useSindenRecoil)
  // {
	// if (gunIndex == 0)
	// {
	//   if (!pipeConnectedSindenGunA)
	//   {
	// 	if (hPipeSindenGunA != nullptr)
	// 	{
	// 		CloseHandle(hPipeSindenGunA);
	// 	}
	// 	hPipeSindenGunA =
	// 		CreateFileA("\\\\.\\pipe\\RecoilSindenGunA", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	// 	if (hPipeSindenGunA != INVALID_HANDLE_VALUE)
	// 	{
	// 		pipeConnectedSindenGunA = true;
	// 	}
	//   }
	//   if (pipeConnectedSindenGunA)
	//   {
	// 	DWORD bytesWritten;
	// 	DWORD messageLength = static_cast<DWORD>(message.length());
	// 	if (!WriteFile(hPipeSindenGunA, message.c_str(), messageLength, &bytesWritten, NULL))
	// 	{
	// 		CloseHandle(hPipeSindenGunA);
	// 		pipeConnectedSindenGunA = false;
	// 	}
	//   }
	// }
	// if (gunIndex == 1)
	// {
	//   if (!pipeConnectedSindenGunB)
	//   {
	// 	if (hPipeSindenGunB != nullptr)
	// 	{
	// 		CloseHandle(hPipeSindenGunB);
	// 	}
	// 	hPipeSindenGunB = CreateFileA("\\\\.\\pipe\\RecoilSindenGunB", GENERIC_WRITE, 0, NULL,
	// 		OPEN_EXISTING, 0, NULL);
	// 	if (hPipeSindenGunB != INVALID_HANDLE_VALUE)
	// 	{
	// 		pipeConnectedSindenGunB = true;
	// 	}
	//   }
	//   if (pipeConnectedSindenGunB)
	//   {
	// 	DWORD bytesWritten;
	// 	DWORD messageLength = static_cast<DWORD>(message.length());
	// 	if (!WriteFile(hPipeSindenGunB, message.c_str(), messageLength, &bytesWritten, NULL))
	// 	{
	// 		CloseHandle(hPipeSindenGunB);
	// 		pipeConnectedSindenGunB = false;
	// 	}
	//   }
	// }  
  // }



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



  

  //Way over complicated, use fd and a array? 
  // if (gunIndex == 0)
  // {
  //   if (!pipeConnectedGunA)
  //   {
  //     if (hPipeGunA != nullptr)
  //     {
  //       CloseHandle(hPipeGunA);
  //     }
  //     hPipeGunA =
  //         CreateFileA("\\\\.\\pipe\\MameHookerProxyRecoilGunA", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  //     if (hPipeGunA != INVALID_HANDLE_VALUE)
  //     {
  //       pipeConnectedGunA = true;
  //     }
  //   }
  //   if (pipeConnectedGunA)
  //   {
  //     DWORD bytesWritten;
  //     DWORD messageLength = static_cast<DWORD>(message.length());
  //     if (!WriteFile(hPipeGunA, message.c_str(), messageLength, &bytesWritten, NULL))
  //     {
  //       CloseHandle(hPipeGunA);
  //       pipeConnectedGunA = false;
  //     }
  //   }
  // }
  // if (gunIndex == 1)
  // {
  //   if (!pipeConnectedGunB)
  //   {
  //     if (hPipeGunB != nullptr)
  //     {
  //       CloseHandle(hPipeGunB);
  //     }
  //     hPipeGunB = CreateFileA("\\\\.\\pipe\\MameHookerProxyRecoilGunB", GENERIC_WRITE, 0, NULL,
  //                             OPEN_EXISTING, 0, NULL);
  //     if (hPipeGunB != INVALID_HANDLE_VALUE)
  //     {
  //       pipeConnectedGunB = true;
  //     }
  //   }
  //   if (pipeConnectedGunB)
  //   {
  //     DWORD bytesWritten;
  //     DWORD messageLength = static_cast<DWORD>(message.length());
  //     if (!WriteFile(hPipeGunB, message.c_str(), messageLength, &bytesWritten, NULL))
  //     {
  //       CloseHandle(hPipeGunB);
  //       pipeConnectedGunB = false;
  //     }
  //   }
  // }
  // if (gunIndex == 2)
  // {
  //   if (!pipeConnectedGunC)
  //   {
  //     if (hPipeGunC != nullptr)
  //     {
  //       CloseHandle(hPipeGunC);
  //     }
  //     hPipeGunC = CreateFileA("\\\\.\\pipe\\MameHookerProxyRecoilGunC", GENERIC_WRITE, 0, NULL,
  //                             OPEN_EXISTING, 0, NULL);
  //     if (hPipeGunC != INVALID_HANDLE_VALUE)
  //     {
  //       pipeConnectedGunC = true;
  //     }
  //   }
  //   if (pipeConnectedGunC)
  //   {
  //     DWORD bytesWritten;
  //     DWORD messageLength = static_cast<DWORD>(message.length());
  //     if (!WriteFile(hPipeGunC, message.c_str(), messageLength, &bytesWritten, NULL))
  //     {
  //       CloseHandle(hPipeGunC);
  //       pipeConnectedGunC = false;
  //     }
  //   }
  // }
  // if (gunIndex == 3)
  // {
  //   if (!pipeConnectedGunD)
  //   {
  //     if (hPipeGunD != nullptr)
  //     {
  //       CloseHandle(hPipeGunD);
  //     }
  //     hPipeGunD = CreateFileA("\\\\.\\pipe\\MameHookerProxyRecoilGunD", GENERIC_WRITE, 0, NULL,
  //                             OPEN_EXISTING, 0, NULL);
  //     if (hPipeGunD != INVALID_HANDLE_VALUE)
  //     {
  //       pipeConnectedGunD = true;
  //     }
  //   }
  //   if (pipeConnectedGunD)
  //   {
  //     DWORD bytesWritten;
  //     DWORD messageLength = static_cast<DWORD>(message.length());
  //     if (!WriteFile(hPipeGunD, message.c_str(), messageLength, &bytesWritten, NULL))
  //     {
  //       CloseHandle(hPipeGunD);
  //       pipeConnectedGunD = false;
  //     }
  //   }
  // }
}











// use closeFd WriteToFd
// void MameHookerProxy::SendState(std::string key, int value)
// {
//   if (!pipeConnected || !active)
//     return;

//   std::string message = key + ":" + std::to_string(value);
//   DWORD bytesWritten;
//   DWORD messageLength = static_cast<DWORD>(message.length());
//   if (!WriteFile(hPipe, message.c_str(), messageLength, &bytesWritten, NULL))
//   {
//     CloseHandle(hPipe);
//     hPipe = CreateFileA("\\\\.\\pipe\\MameHookerProxyControl", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
//     if (hPipe != INVALID_HANDLE_VALUE)
//     {
//       pipeConnected = true;
//     }
//     else
//     {
//       pipeConnected = false;
//     }
//   }

// }



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



// Rewrite using closeFd with linux support. Why are we using the CreateProcess? Process info? 

void MameHookerProxy::StartGame(std::string id)
{
  if (id == activeGame || id.empty())
    return;
    fprintf(stderr, "[MameHooker] fork result pid: %d\n", (int)childPid);


    //setup variables
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


 //Orignal Windows 

#ifdef _WIN32
  std::string exePath = execDir + "\\MameOutputSender.exe"; //
  std::string args =
    "gamename=\"" + id + "\" "
    "outputs=\"GunRecoil_P1,GunRecoil_P2,TriggerPress_P1,TriggerPress_P2\"";
  std::string cmdLine = exePath + " " + args;

  std::wstring widePath(exePath.begin(), exePath.end());
  DWORD attr = GetFileAttributesW(widePath.c_str());
  bool exists = (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));

  if (exists)
  {
    processInfo = new PROCESS_INFORMATION();
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    if (!CreateProcessA(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, processInfo))
    {
      delete processInfo;
      processInfo = nullptr;
    }
  }


Swap Windows stuff like PROCESS_INFORMATION for childPid

#else
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
#endif

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







// void MameHookerProxy::StartGame(std::string id)
// {
//   if (id == activeGame)
//     return;
//   if (id == "")
//     return;

//   activeGame = id;

//   active = true;

//   if (hPipe != nullptr)
//   {
//     CloseHandle(hPipe);
//   }
//   if (hPipeGunA != nullptr)
//   {
//     CloseHandle(hPipeGunA);
//   }

//   if (hPipeGunB != nullptr)
//   {
//     CloseHandle(hPipeGunB);
//   }

//   if (hPipeGunC != nullptr)
//   {
//     CloseHandle(hPipeGunC);
//   }

//   if (hPipeGunD != nullptr)
//   {
//     CloseHandle(hPipeGunD);
//   }

//   if (hPipeSindenGunA != nullptr)
//   {
// 	CloseHandle(hPipeSindenGunA);
//   }

//   if (hPipeSindenGunB != nullptr)
//   {
// 	CloseHandle(hPipeSindenGunB);
//   }

//   pipeConnected = false;
//   pipeConnectedGunA = false;
//   pipeConnectedGunB = false;
//   pipeConnectedGunC = false;
//   pipeConnectedGunD = false;
//   pipeConnectedSindenGunA = false;
//   pipeConnectedSindenGunB = false;

//   std::string executableDirectory = getExecutableDirectory();
//   std::string programPath = executableDirectory + "\\MameOutputSender.exe";
//   std::string arguments =
//       "gamename=\"" + id +
//       "\" "
//       "outputs=\"GunRecoil_P1,GunRecoil_P2,TriggerPress_P1,TriggerPress_P2\"";

//   std::string commandLine = std::string(programPath) + " " + std::string(arguments);


//   std::wstring wideProgramPath(programPath.begin(), programPath.end());
//   if (fileExists(wideProgramPath.c_str()))
//   {
//     processInfo = new PROCESS_INFORMATION();

//     STARTUPINFOA startupInfo;
//     ZeroMemory(&startupInfo, sizeof(startupInfo));
//     startupInfo.cb = sizeof(startupInfo);
//     if (!CreateProcessA(NULL,                             // No module name (use command line)
//                         (LPSTR)commandLine.c_str(),  // Command line
//                        NULL,                             // Process handle not inheritable
//                        NULL,                             // Thread handle not inheritable
//                        FALSE,                            // Set handle inheritance to FALSE
//                        0,                                // No creation flags
//                        NULL,                             // Use parent's environment block
//                        NULL,                             // Use parent's starting directory
//                        &startupInfo,                     // Pointer to STARTUPINFO structure
//                        processInfo))
//     {
//       delete processInfo;     // Libérer la mémoire en cas d'échec
//       processInfo = nullptr;  // Réinitialiser à nullptr
//     }
//   }
//   if (processInfo != nullptr)
//   {

//      std::thread([=]() {
//       int compteur = 0;
//       //std::this_thread::sleep_for(std::chrono::seconds(1));  // Attendre 5 secondes
//       if (!active)
//         return;
//       while (active && compteur < 50)
//       {
//         compteur++;
//         std::this_thread::sleep_for(std::chrono::milliseconds(100));
//         hPipe = CreateFileA("\\\\.\\pipe\\MameHookerProxyControl", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0,NULL);
//         if (hPipe == INVALID_HANDLE_VALUE)
//         {
//           pipeConnected = false;
          
//         }
//         else
//         {
//           pipeConnected = true;
//           break;
//         }
//       }
//     }).detach();  // Détacher le thread pour éviter de bloquer la sortie du programme
//   }
// }





