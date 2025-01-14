// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_FILE_HANDLER_UTIL_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_FILE_HANDLER_UTIL_H_

#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/platform_file.h"
#include "chrome/common/extensions/extension.h"

class Browser;
class FileBrowserHandler;
class GURL;
class Profile;

namespace fileapi {
class FileSystemURL;
}

namespace file_manager {
// TODO(satorux): Rename 'file_handler_util' to 'file_tasks'. crbug.com/267337
namespace file_handler_util {

// Tasks are stored as a vector in order of priorities.
typedef std::vector<const FileBrowserHandler*> FileBrowserHandlerList;

// Specifies the task type for a task id that represents some file action, Drive
// action, or Web Intent action.
extern const char kTaskFile[];
extern const char kTaskDrive[];
extern const char kTaskApp[];

void UpdateFileHandlerUsageStats(Profile* profile, const std::string& task_id);

// Returns true if the task should be used as a fallback. Such tasks are
// Files.app's internal handlers as well as quick office extensions.
bool IsFallbackTask(const FileBrowserHandler* task);

// Update the default file handler for the given sets of suffixes and MIME
// types.
void UpdateDefaultTask(Profile* profile,
                       const std::string& task_id,
                       const std::set<std::string>& suffixes,
                       const std::set<std::string>& mime_types);

// Returns the task ID of the default task for the given |mime_type|/|suffix|
// combination. If it finds a MIME type match, then it prefers that over a
// suffix match. If it a default can't be found, then it returns the empty
// string.
std::string GetDefaultTaskIdFromPrefs(Profile* profile,
                                      const std::string& mime_type,
                                      const std::string& suffix);

// Generates task id for the action specified by the extension. The |task_type|
// must be one of kTaskFile, kTaskDrive or kTaskApp.
std::string MakeTaskID(const std::string& extension_id,
                       const std::string& task_type,
                       const std::string& action_id);

// Extracts action, type and extension id bound to the file task. Either
// |target_extension_id| or |action_id| are allowed to be NULL if caller isn't
// interested in those values.  Returns false on failure to parse.
bool CrackTaskID(const std::string& task_id,
                 std::string* target_extension_id,
                 std::string* task_type,
                 std::string* action_id);

// This generates a list of default tasks (tasks set as default by the user in
// prefs) from the |common_tasks|.
void FindDefaultTasks(Profile* profile,
                      const std::vector<base::FilePath>& files_list,
                      const FileBrowserHandlerList& common_tasks,
                      FileBrowserHandlerList* default_tasks);

// This generates list of tasks common for all files in |file_list|.
bool FindCommonTasks(Profile* profile,
                     const std::vector<GURL>& files_list,
                     FileBrowserHandlerList* common_tasks);

// Finds a task for a file whose URL is |url| and whose path is |path|.
// Returns default task if one is defined (The default task is the task that is
// assigned to file browser task button by default). If default task is not
// found, tries to match the url with one of the builtin tasks.
bool GetTaskForURLAndPath(Profile* profile,
                          const GURL& url,
                          const base::FilePath& path,
                          const FileBrowserHandler** handler);

// Used for returning success or failure from task executions.
typedef base::Callback<void(bool)> FileTaskFinishedCallback;

// Executes file handler task for each element of |file_urls|.
// Returns |false| if the execution cannot be initiated. Otherwise returns
// |true| and then eventually calls |done| when all the files have been handled.
// |done| can be a null callback.
bool ExecuteFileTask(Profile* profile,
                     const GURL& source_url,
                     const std::string& file_browser_id,
                     int32 tab_id,
                     const std::string& extension_id,
                     const std::string& task_type,
                     const std::string& action_id,
                     const std::vector<fileapi::FileSystemURL>& file_urls,
                     const FileTaskFinishedCallback& done);

}  // namespace file_handler_util
}  // namespace file_manager

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_FILE_HANDLER_UTIL_H_
