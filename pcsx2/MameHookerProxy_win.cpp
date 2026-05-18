#include "MameHookerProxy.h"
#include <string>
#include <windows.h>
#include <cstdio>
#include <thread>
#include <chrono>


MameHookerProxy& MameHookerProxy::GetInstance()
{
  static MameHookerProxy s_instance;
  return s_instance;
}


bool MameHookerProxy::writeToFd(HANDLE h, const std::string& msg)
{
  DWORD written;
  return WriteFile(h, msg.c_str(), static_cast<DWORD>(msg.size()), &written, NULL) != 0;
}

static void closeHandle(HANDLE& h)
{
  if (h == NULL || h == INVALID_HANDLE_VALUE) return;
  CloseHandle(h);
  h = NULL;
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

HANDLE MameHookerProxy::connectGunPipe(const std::string& pipeName)
{
  //Set pipe path and open return handle, if fail return NULL
  std::string path = "\\\\.\\pipe\\" + pipeName;
  HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  return (h == INVALID_HANDLE_VALUE) ? NULL : h;
}


void MameHookerProxy::Init()
{
  pipeConnected = false;
  hPipe = NULL;
  hPipeGunA = NULL;
  hPipeGunB = NULL;
  hPipeGunC = NULL;
  hPipeGunD = NULL;
  processInfo = nullptr;
}

void MameHookerProxy::CloseGame()
{
  activeGame = "";
  active = false;
  bool needForceClose = true;
    if (pipeConnected && hPipe != NULL)
    {
      writeToFd(hPipe, "CLOSE");
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

  closeHandle(hPipeGunA);
  closeHandle(hPipeGunB);
  closeHandle(hPipeGunC);
  closeHandle(hPipeGunD);
  closeHandle(hPipe);

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
  HANDLE* handles[] = { &hPipeGunA, &hPipeGunB, &hPipeGunC, &hPipeGunD };
  bool* connected[] = { &pipeConnectedGunA, &pipeConnectedGunB, &pipeConnectedGunC, &pipeConnectedGunD };

  if (gunIndex < 0 || gunIndex > 3) return;

  if (!*connected[gunIndex])
  {
    closeHandle(*handles[gunIndex]);
    *handles[gunIndex] = connectGunPipe(pipeNames[gunIndex]);
    *connected[gunIndex] = (*handles[gunIndex] != NULL);
  }

  if (*connected[gunIndex])
  {
    if (!writeToFd(*handles[gunIndex], "1"))
    {
      closeHandle(*handles[gunIndex]);
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
    closeHandle(hPipe);
    hPipe = connectGunPipe("MameHookerProxyControl");
    pipeConnected = (hPipe != NULL);
  }
}


void MameHookerProxy::StartGame(std::string id)
{
  if (id == activeGame || id.empty())
    return;

  //setup variables
  activeGame = id;
  active = true;

  closeHandle(hPipe);
  closeHandle(hPipeGunA);
  closeHandle(hPipeGunB);
  closeHandle(hPipeGunC);
  closeHandle(hPipeGunD);
  pipeConnected = false;
  pipeConnectedGunA = false;
  pipeConnectedGunB = false;
  pipeConnectedGunC = false;
  pipeConnectedGunD = false;

  std::string execDir = getExecutableDirectory();
  fprintf(stderr, "[MameHooker] execDir=%s\n", execDir.c_str());

  //Original Windows

  std::string exePath = execDir + "\\MameOutputSender.exe";
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

      //Try to connect to the pipe with the name MameHookerProxyControl
      HANDLE h = connectGunPipe("MameHookerProxyControl");
      if (h != NULL)
      {
        hPipe = h;
        pipeConnected = true;
        return;
      }
    }

  }).detach();
}
