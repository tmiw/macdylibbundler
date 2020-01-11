#include "DylibBundler.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <utility>

#ifdef __linux
#include <linux/limits.h>
#endif
#ifndef __clang__
#include <sys/types.h>
#endif

#include "Dependency.h"
#include "Settings.h"
#include "Utils.h"

std::vector<Dependency> deps;
std::map<std::string, std::vector<Dependency>> deps_per_file;
std::map<std::string, bool> deps_collected;
std::set<std::string> frameworks;
std::set<std::string> rpaths;
bool qt_plugins_called = false;

void addDependency(const std::string& path, const std::string& dependent_file)
{
    Dependency dependency(path, dependent_file);

    // check if this library was already added to |deps| to avoid duplicates
    bool in_deps = false;
    for (auto& dep : deps) {
        if (dependency.mergeIfSameAs(dep))
            in_deps = true;
    }

    // check if this library was already added to |deps_per_file[dependent_file]| to avoid duplicates
    bool in_deps_per_file = false;
    for (auto& dep : deps_per_file[dependent_file]) {
        if (dependency.mergeIfSameAs(dep))
            in_deps_per_file = true;
    }

    // check if this library is in /usr/lib, /System/Library, or in ignored list
    if (!Settings::isPrefixBundled(dependency.getPrefix()))
        return;

    if (!in_deps && dependency.isFramework())
        frameworks.insert(dependency.getOriginalPath());
    if (!in_deps)
        deps.push_back(dependency);
    if (!in_deps_per_file)
        deps_per_file[dependent_file].push_back(dependency);
}

void collectDependencies(const std::string& dependent_file, std::vector<std::string>& lines)
{
    parseLoadCommands(dependent_file, std::string("LC_LOAD_DYLIB"), std::string("name"), lines);
}

void collectDependenciesForFile(const std::string& file, std::vector<std::string>& lines)
{
    if (deps_collected.find(file) == deps_collected.end())
        collectDependencies(file, lines);
}

void collectDependenciesForFile(const std::string& dependent_file)
{
    std::vector<std::string> lines;
    collectDependenciesForFile(dependent_file, lines);
    collectRpathsForFilename(dependent_file);

    for (const auto& line : lines) {
        if (!Settings::isPrefixBundled(line))
            continue; // skip system/ignored prefixes
       if (isRpath(line))
            collectRpathsForFilename(dependent_file);
        addDependency(line, dependent_file);
    }
    deps_collected[dependent_file] = true;
}

void collectRpaths(const std::string& filename)
{
    std::vector<std::string> lines;
    parseLoadCommands(filename, std::string("LC_RPATH"), std::string("path"), lines);
    for (const auto& line : lines) {
        rpaths.insert(line);
        Settings::addRpathForFile(filename, line);
        if (Settings::verboseOutput())
            std::cout << "  rpath: " << line << std::endl;
    }
}

void collectRpathsForFilename(const std::string& filename)
{
    if (!Settings::fileHasRpath(filename))
        collectRpaths(filename);
}

void collectSubDependencies()
{
    size_t dep_counter = deps.size();
    if (Settings::verboseOutput()) {
        std::cout << "(pre sub) # OF FILES: " << Settings::filesToFixCount() << std::endl;
        std::cout << "(pre sub) # OF DEPS: " << deps.size() << std::endl;
    }

    size_t deps_size = deps.size();
    while (true) {
        deps_size = deps.size();
        for (size_t n=0; n<deps_size; ++n) {
            std::string original_path = deps[n].getOriginalPath();
            if (Settings::verboseOutput())
                std::cout << "  (collect sub deps) original path: " << original_path << std::endl;
            if (isRpath(original_path))
                original_path = searchFilenameInRpaths(original_path);

            std::vector<std::string> lines;
            collectDependenciesForFile(original_path, lines);
            collectRpathsForFilename(original_path);

            for (const auto& line : lines) {
                if (!Settings::isPrefixBundled(line))
                    continue; // skip system/ignored prefixes
                if (isRpath(line))
                    collectRpathsForFilename(searchFilenameInRpaths(line));
                addDependency(line, original_path);
            }
        }
        // if no more dependencies were added on this iteration, stop searching
        if (deps.size() == deps_size)
            break;
    }

    if (Settings::verboseOutput()) {
        std::cout << "(post sub) # OF FILES: " << Settings::filesToFixCount() << std::endl;
        std::cout << "(post sub) # OF DEPS: " << deps.size() << std::endl;
    }
    if (Settings::bundleLibs() && Settings::bundleFrameworks()) {
        if (!qt_plugins_called || (deps.size() != dep_counter))
            bundleQtPlugins();
    }
}

void changeLibPathsOnFile(const std::string& file_to_fix)
{
    if (deps_collected.find(file_to_fix) == deps_collected.end())
        collectDependenciesForFile(file_to_fix);

    std::cout << "* Fixing dependencies on " << file_to_fix << "\n";

    const size_t dep_amount = deps_per_file[file_to_fix].size();
    for (size_t n=0; n<dep_amount; ++n) {
        deps_per_file[file_to_fix][n].fixDependentFiles(file_to_fix);
    }
}

void fixRpathsOnFile(const std::string& original_file, const std::string& file_to_fix)
{
    std::set<std::string> rpaths_to_fix;
    if (Settings::fileHasRpath(original_file))
        rpaths_to_fix = Settings::getRpathsForFile(original_file);

    for (const auto& rpath_to_fix : rpaths_to_fix) {
        std::string command = std::string("install_name_tool -rpath ");
        command += rpath_to_fix + " " + Settings::insideLibPath() + " " + file_to_fix;
        if (systemp(command) != 0) {
            std::cerr << "\n\n/!\\ ERROR: An error occured while trying to fix dependencies of " << file_to_fix << "\n";
            exit(1);
        }
    }
}

void bundleDependencies()
{
    for (const auto& dep : deps) {
        dep.print();
    }
    std::cout << "\n";
    if (Settings::verboseOutput()) {
        for (const auto& rpath : rpaths) {
            std::cout << "rpaths: " << rpath << std::endl;
        }
    }
    // copy & fix up dependencies
    if (Settings::bundleLibs()) {
        createDestDir();
        for (const auto& dep : deps) {
            dep.copyToAppBundle();
            changeLibPathsOnFile(dep.getInstallPath());
            fixRpathsOnFile(dep.getOriginalPath(), dep.getInstallPath());
        }
    }
    // fix up selected files
    const auto files = Settings::filesToFix();
    for (const auto& file : files) {
        changeLibPathsOnFile(file);
        fixRpathsOnFile(file, file);
    }
}

void bundleQtPlugins()
{
    bool qtCoreFound = false;
    bool qtGuiFound = false;
    bool qtNetworkFound = false;
    bool qtSqlFound = false;
    bool qtSvgFound = false;
    bool qtMultimediaFound = false;
    bool qt3dRenderFound = false;
    bool qt3dQuickRenderFound = false;
    bool qtPositioningFound = false;
    bool qtLocationFound = false;
    bool qtTextToSpeechFound = false;
    bool qtWebViewFound = false;
    std::string original_file;

    for (const auto& framework : frameworks) {
        if (framework.find("QtCore") != std::string::npos) {
            qtCoreFound = true;
            original_file = framework;
        }
        if (framework.find("QtGui") != std::string::npos)
            qtGuiFound = true;
        if (framework.find("QtNetwork") != std::string::npos)
            qtNetworkFound = true;
        if (framework.find("QtSql") != std::string::npos)
            qtSqlFound = true;
        if (framework.find("QtSvg") != std::string::npos)
            qtSvgFound = true;
        if (framework.find("QtMultimedia") != std::string::npos)
            qtMultimediaFound = true;
        if (framework.find("Qt3DRender") != std::string::npos)
            qt3dRenderFound = true;
        if (framework.find("Qt3DQuickRender") != std::string::npos)
            qt3dQuickRenderFound = true;
        if (framework.find("QtPositioning") != std::string::npos)
            qtPositioningFound = true;
        if (framework.find("QtLocation") != std::string::npos)
            qtLocationFound = true;
        if (framework.find("TextToSpeech") != std::string::npos)
            qtTextToSpeechFound = true;
        if (framework.find("WebView") != std::string::npos)
            qtWebViewFound = true;
    }

    if (!qtCoreFound)
        return;
    if (!qt_plugins_called)
        createQtConf(Settings::resourcesFolder());
    qt_plugins_called = true;

    const auto fixupPlugin = [original_file](const std::string& plugin) {
        std::string dest = Settings::pluginsFolder();
        std::string framework_root = getFrameworkRoot(original_file);
        std::string prefix = filePrefix(framework_root);
        std::string qt_prefix = filePrefix(prefix.substr(0, prefix.size()-1));
        std::string qt_plugins_prefix = qt_prefix + "plugins/";
        if (fileExists(qt_plugins_prefix + plugin)) {
            mkdir(dest + plugin);
            copyFile(qt_plugins_prefix + plugin, dest);
            std::vector<std::string> files = lsDir(dest + plugin+"/");
            for (const auto& file : files) {
                Settings::addFileToFix(dest + plugin+"/"+file);
                collectDependenciesForFile(dest + plugin+"/"+file);
                changeId(dest + plugin+"/"+file, "@rpath/" + plugin+"/"+file);
            }
        }
    };

    std::string framework_root = getFrameworkRoot(original_file);
    std::string prefix = filePrefix(framework_root);
    std::string qt_prefix = filePrefix(prefix.substr(0, prefix.size()-1));
    std::string qt_plugins_prefix = qt_prefix + "plugins/";

    std::string dest = Settings::pluginsFolder();
    mkdir(dest + "platforms");
    copyFile(qt_plugins_prefix + "platforms/libqcocoa.dylib", dest + "platforms");
    Settings::addFileToFix(dest + "platforms/libqcocoa.dylib");
    collectDependenciesForFile(dest + "platforms/libqcocoa.dylib");

    fixupPlugin("printsupport");
    fixupPlugin("styles");
    fixupPlugin("imageformats");
    fixupPlugin("iconengines");
    if (!qtSvgFound)
        systemp(std::string("rm -f ") + dest + "imageformats/libqsvg.dylib");
    if (qtGuiFound) {
        fixupPlugin("platforminputcontexts");
        fixupPlugin("virtualkeyboard");
    }
    if (qtNetworkFound)
        fixupPlugin("bearer");
    if (qtSqlFound)
        fixupPlugin("sqldrivers");
    if (qtMultimediaFound) {
        fixupPlugin("mediaservice");
        fixupPlugin("audio");
    }
    if (qt3dRenderFound) {
        fixupPlugin("sceneparsers");
        fixupPlugin("geometryloaders");
    }
    if (qt3dQuickRenderFound)
        fixupPlugin("renderplugins");
    if (qtPositioningFound)
        fixupPlugin("position");
    if (qtLocationFound)
        fixupPlugin("geoservices");
    if (qtTextToSpeechFound)
        fixupPlugin("texttospeech");
    if (qtWebViewFound)
        fixupPlugin("webview");

    collectSubDependencies();
}
