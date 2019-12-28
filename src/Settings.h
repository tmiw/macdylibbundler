#ifndef _settings_
#define _settings_

#include <string>
#include <vector>

namespace Settings {

bool isPrefixBundled(std::string prefix);
bool isPrefixIgnored(std::string prefix);
void ignorePrefix(std::string prefix);

bool canOverwriteFiles();
void canOverwriteFiles(bool permission);

bool canOverwriteDir();
void canOverwriteDir(bool permission);

bool canCreateDir();
void canCreateDir(bool permission);

bool bundleLibs();
void bundleLibs(bool on);

bool bundleFrameworks();
void bundleFrameworks(bool status);

std::string appBundle();
void appBundle(std::string path);

std::string destFolder();
void destFolder(std::string path);

void addFileToFix(std::string path);
std::string fileToFix(int n);
std::vector<std::string> filesToFix();
size_t filesToFixCount();

std::string insideLibPath();
void insideLibPath(std::string p);

void addSearchPath(std::string path);
int searchPathCount();
std::string searchPath(int n);

bool quietOutput();
void quietOutput(bool status);

bool verboseOutput();
void verboseOutput(bool status);

} // namespace Settings

#endif
