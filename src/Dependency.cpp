#include "Dependency.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>

#include <sys/param.h>
#ifndef __clang__
#include <sys/types.h>
#endif

#include "Settings.h"
#include "Utils.h"

Dependency::Dependency(std::string path, const std::string& dependent_file) : is_framework(false)
{
    char original_file_buffer[PATH_MAX];
    std::string original_file;
    std::string warning_msg;

    rtrim_in_place(path);

    if (Settings::verboseOutput()) {
        std::cout<< "** Dependency ctor **" << std::endl;
        if (path != dependent_file)
            std::cout << "  dependent file:  " << dependent_file << std::endl;
        std::cout << "  dependency path: " << path << std::endl;
    }

    if (isRpath(path)) {
        original_file = searchFilenameInRpaths(path, dependent_file);
    }
    else if (realpath(path.c_str(), original_file_buffer)) {
        original_file = original_file_buffer;
        if (Settings::verboseOutput())
            std::cout << "  original_file:   " << original_file << std::endl;
    }
    else {
        warning_msg = "\n/!\\ WARNING: Cannot resolve path '" + path + "'\n";
        original_file = path;
    }

    // check if given path is a symlink
    if (original_file != path)
        addSymlink(path);

    prefix = filePrefix(original_file);
    filename = stripPrefix(original_file);

    if (!prefix.empty() && prefix[prefix.size()-1] != '/')
        prefix += "/";

    // check if this dependency is in /usr/lib, /System/Library, or in ignored list
    if (!Settings::isPrefixBundled(prefix))
        return;

    if (original_file.find(".framework") != std::string::npos) {
        is_framework = true;
        std::string framework_root = getFrameworkRoot(original_file);
        std::string framework_path = getFrameworkPath(original_file);
        std::string framework_name = stripPrefix(framework_root);
        prefix = filePrefix(framework_root);
        filename = framework_name + "/" + framework_path;
        if (Settings::verboseOutput()) {
            std::cout << "  framework root: " << framework_root << std::endl;
            std::cout << "  framework path: " << framework_path << std::endl;
            std::cout << "  framework name: " << framework_name << std::endl;
        }
    }

    // check if the lib is in a known location
    if (prefix.empty() || !fileExists(prefix+filename)) {
        std::vector<std::string> search_paths = Settings::searchPaths();
        // the paths contains at least /usr/lib so if it is empty we have not initialized it
        if (search_paths.empty())
            initSearchPaths();

        // check if file is contained in one of the paths
        for (const auto& search_path : search_paths) {
            if (fileExists(search_path+filename)) {
                warning_msg += "FOUND " + filename + " in " + search_path + "\n";
                prefix = search_path;
                Settings::missingPrefixes(true);
                break;
            }
        }
    }

    if (!Settings::quietOutput())
        std::cout << warning_msg;

    // if the location is still unknown, ask the user for search path
    if (!Settings::isPrefixIgnored(prefix) && (prefix.empty() || !fileExists(prefix+filename))) {
        if (!Settings::quietOutput())
            std::cerr << "\n/!\\ WARNING: Dependency " << filename << " of " << dependent_file << " not found\n";
        if (Settings::verboseOutput())
            std::cout << "     path: " << (prefix+filename) << std::endl;
        Settings::missingPrefixes(true);
        Settings::addSearchPath(getUserInputDirForFile(filename, dependent_file));
    }

    new_name = filename;
}

std::string Dependency::getInstallPath() const
{
    return Settings::destFolder() + new_name;
}

std::string Dependency::getInnerPath() const
{
    return Settings::insideLibPath() + new_name;
}

void Dependency::print() const
{
    std::cout << "\n* " << filename << " from " << prefix << std::endl;
    for (const auto& symlink : symlinks) {
        std::cout << "    symlink --> " << symlink << std::endl;
    }
}

void Dependency::addSymlink(const std::string& s)
{
    if (std::find(symlinks.begin(), symlinks.end(), s) == symlinks.end())
        symlinks.push_back(s);
}

bool Dependency::mergeIfSameAs(Dependency& dep2)
{
    if (dep2.getOriginalFileName() == filename) {
        for (const auto& symlink : symlinks) {
            dep2.addSymlink(symlink);
        }
        return true;
    }
    return false;
}

void Dependency::copyToAppBundle() const
{
    std::string original_path = getOriginalPath();
    std::string dest_path = getInstallPath();

    if (is_framework) {
        original_path = getFrameworkRoot(original_path);
        dest_path = Settings::destFolder() + stripPrefix(original_path);
    }

    if (Settings::verboseOutput()) {
        std::string inner_path = getInnerPath();
        std::cout << "  - original path: " << original_path << std::endl;
        std::cout << "  - inner path:    " << inner_path << std::endl;
        std::cout << "  - dest_path:     " << dest_path << std::endl;
        std::cout << "  - install path:  " << getInstallPath() << std::endl;
    }

    copyFile(original_path, dest_path);

    if (is_framework) {
        std::string headers_path = dest_path + std::string("/Headers");
        char buffer[PATH_MAX];
        if (realpath(rtrim(headers_path).c_str(), buffer))
            headers_path = buffer;
        deleteFile(headers_path, true);
        deleteFile(dest_path + "/*.prl");
    }

    changeId(getInstallPath(), "@rpath/"+new_name);
}

void Dependency::fixDependentFiles(const std::string& file) const
{
    changeInstallName(file, getOriginalPath(), getInnerPath());
    for (const auto& symlink : symlinks) {
        changeInstallName(file, symlink, getInnerPath());
    }

    if (Settings::missingPrefixes()) {
        changeInstallName(file, filename, getInnerPath());
        for (const auto& symlink : symlinks) {
            changeInstallName(file, symlink, getInnerPath());
        }
    }
}
