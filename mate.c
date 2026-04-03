#define MATE_IMPLEMENTATION
#include "mate.h"

int main() {
  StartBuild();
  {
    Executable executable = CreateExecutable((ExecutableOptions){
      .output = "main",
      .flags = "-Wall -O3"
      // .flags = "-Wall -g3 -fsanitize=address,undefined"
    });

    AddFile(executable, "./src/main.c");
    AddFile(executable, "./src/http.c");
    AddFile(executable, "./src/async_io.c");

    AddFile(executable, "./vendor/libaco/aco.c");
    AddFile(executable, "./vendor/libaco/acosw.S");

    AddLibraryPaths(executable, "./vendor/cJSON/");
    AddIncludePaths(executable, "./vendor/base/");
    AddIncludePaths(executable, "./vendor/cJSON/");
    AddIncludePaths(executable, "./vendor/libaco/");

    LinkSystemLibraries(executable, "cjson", "uring");

    InstallExecutable(executable);
    RunCommand(executable.outputPath);
    CreateCompileCommands(executable);
  }
  EndBuild();
}
