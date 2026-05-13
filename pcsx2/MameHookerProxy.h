#pragma once
#include <string>
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

//  //bool useSindenRecoil = false;
//  // bool pipeConnectedSindenGunA = false;
//  // bool pipeConnectedSindenGunB = false;

//   #ifdef _WIN32
//   HANDLE hPipeSindenGunA = nullptr;
//   HANDLE hPipeSindenGunB = nullptr;
  
//   #else
//   //int hPipeSindenGunA = -1;
//   //int hPipeSindenGunB = -1;
  

  static MameHookerProxy& GetInstance();
  //static bool fileExists(const wchar_t* filePath);
  // static void launchProgram(const char* programPath, PROCESS_INFORMATION& processInfo);
  static std::string getExecutableDirectory();
  void Init();
  void CloseGame();
  void Gunshot(int gunIndex);
  void SendState(std::string key, int value);
  void StartGame(std::string id);
  bool writeToFd(int fd, const std::string& msg);
  int connectGunPipe(const std::string& pipeName);

};

// #else

// class MameHookerProxy
// {
// public:
//   std::string activeGame = "";
//   bool active = false;
//   bool useSindenRecoil = false;

//   static MameHookerProxy& GetInstance() { static MameHookerProxy instance; return instance; }
//   void Init() {}
//   void CloseGame() {}
//   void Gunshot(int /*gunIndex*/) {}
//   void SendState(std::string /*key*/, int /*value*/) {}
//   void StartGame(std::string /*id*/) {}
// };


