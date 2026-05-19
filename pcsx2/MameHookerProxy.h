#pragma once
#include <string>

// MameHookerProxy is only supported on Windows and Linux.
// On other platforms (e.g. Mac) we provide a stub so callers compile without changes.
#if defined(__APPLE__)

class MameHookerProxy
{
public:
  std::string activeGame = "";
  bool active = false;

  static MameHookerProxy& GetInstance() { static MameHookerProxy instance; return instance; }
  void Init() {}
  void CloseGame() {}
  void Gunshot(int /*gunIndex*/) {}
  void SendState(std::string /*key*/, int /*value*/) {}
  void StartGame(std::string /*id*/) {}
};

#else

#include <thread>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#endif

class MameHookerProxy
{
public:
  std::string activeGame = "";
  bool pipeConnected = false;
  bool pipeConnectedGunA = false;
  bool pipeConnectedGunB = false;
  bool pipeConnectedGunC = false;
  bool pipeConnectedGunD = false;
  bool active = false;

  #ifdef _WIN32
  HANDLE hPipe = nullptr;
  HANDLE hPipeGunA = nullptr;
  HANDLE hPipeGunB = nullptr;
  HANDLE hPipeGunC = nullptr;
  HANDLE hPipeGunD = nullptr;
  PROCESS_INFORMATION* processInfo = nullptr;
  #else
  int hPipe = -1;
  int hPipeGunA = -1;
  int hPipeGunB = -1;
  int hPipeGunC = -1;
  int hPipeGunD = -1;
  pid_t childPid = -1;
  #endif

  static MameHookerProxy& GetInstance();
  static std::string getExecutableDirectory();
  void Init();
  void CloseGame();
  void Gunshot(int gunIndex);
  void SendState(std::string key, int value);
  void StartGame(std::string id);

#ifdef _WIN32
  bool writeToFd(HANDLE h, const std::string& msg);
  HANDLE connectGunPipe(const std::string& pipeName);
#else
  bool writeToFd(int fd, const std::string& msg);
  int connectGunPipe(const std::string& pipeName);
#endif
};

#endif // __APPLE__
