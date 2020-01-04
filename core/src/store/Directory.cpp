// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "store/Directory.h"

#include <boost/filesystem.hpp>

#include "utils/Exception.h"
#include "utils/Log.h"

namespace milvus {
namespace store {

Directory::Directory(const std::string& dir_path) : dir_path_(dir_path) {
    if (!boost::filesystem::is_directory(dir_path)) {
        auto ret = boost::filesystem::create_directory(dir_path);
        if (!ret) {
            // TODO(zhiru): hard to catch exception
            std::string err_msg = "Failed to create directory: " + dir_path;
            ENGINE_LOG_ERROR << err_msg;
            throw Exception(SERVER_CANNOT_CREATE_FOLDER, err_msg);
        }
    }
}

void
Directory::ListAll(std::vector<std::string>& file_paths) {
    if (boost::filesystem::is_directory(dir_path_)) {
        for (auto& it : boost::filesystem::directory_iterator(dir_path_)) {
            file_paths.emplace_back(it.path().c_str());
        }
    }
}

bool
Directory::DeleteFile(const std::string& file_path) {
    return boost::filesystem::remove(file_path);
}

const std::string&
Directory::GetDirPath() const {
    return dir_path_;
}

}  // namespace store
}  // namespace milvus
