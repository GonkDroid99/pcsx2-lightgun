#include "MameHookerProxy.h"
#include <string>
#include <windows.h>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>


MameHookerProxy& MameHookerProxy::GetInstance()
{
  
  static MameHookerProxy s_instance;
  return s_instance;
}



bool MameHookerProxy::writeToFd(int fd, const std::string& msg)
{
  HANDLE h = reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd));
  DWORD written;
  return WriteFile(h, msg.c_str(), static_cast<DWORD>(msg.size()), &written, NULL) != 0;
}

static void closeFd(int& fd)
{
  if (fd < 0) return;
  CloseHandle(reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd)));
  fd = -1;
}


std::string MameHookerProxy::getExecutableDirectory()
{
  char buffer[MAX_PATH];
  GetModuleFileNameA(NULL, buffer, MAX_PATH);
  std::string fullPath(buffer);
  size_t found = fullPath.find_last_of("\\/");
  return fullPath.substr(0, found);
}


// On Windows: connects to \\.\pipe\<name>

int MameHookerProxy::connectGunPipe(const std::string& pipeName)
{

//Set pipe path and open return handle to a int, if fail -1
  std::string path = "\\\\.\\pipe\\" + pipeName;
  HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  return (h == INVALID_HANDLE_VALUE) ? -1 : (int)(intptr_t)h;
}



void MameHookerProxy::Init()
{
  pipeConnected = false;
  hPipe = -1;
  hPipeGunA = -1;
  hPipeGunB = -1;
  hPipeGunC = -1;
  hPipeGunD = -1;
  processInfo = nullptr;
}

void MameHookerProxy::CloseGame()
{
  activeGame = "";
  active = false;
  bool needForceClose = true; 
    if (pipeConnected && hPipe >= 0)
    {
      writeToFd(hPipe,"CLOSE");
    }

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





void MameHookerProxy::StartGame(std::string id)
{
  if (id == activeGame || id.empty())
    return;
    


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

  // Attempt to connect to the control pipe on a background thread 
  std::thread([this]() {
    

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
        
        return;
      }
    }
    
  }).detach();

  }
