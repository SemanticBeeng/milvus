// Copyright (C) 2019-2020 Zilliz. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations under the License.

#include "db/engine/ExecutionEngine.h"
#include "utils/BlockingQueue.h"
#include "utils/CommonUtil.h"
#include "utils/Error.h"
#include "utils/LogUtil.h"
#include "utils/SignalUtil.h"
#include "utils/StringHelpFunctions.h"
#include "utils/TimeRecorder.h"
#include "utils/ValidationUtil.h"
#include "utils/ThreadPool.h"

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <boost/filesystem.hpp>
#include <thread>
#include <src/utils/Exception.h>

#include <fiu-local.h>
#include <fiu-control.h>

namespace {

static const char* LOG_FILE_PATH = "./milvus/conf/log_config.conf";

void
CopyStatus(milvus::Status& st1, milvus::Status& st2) {
    st1 = st2;
}

}  // namespace

TEST(UtilTest, EXCEPTION_TEST) {
    std::string err_msg = "failed";
    milvus::server::ServerException ex(milvus::SERVER_UNEXPECTED_ERROR, err_msg);
    ASSERT_EQ(ex.error_code(), milvus::SERVER_UNEXPECTED_ERROR);
    std::string msg = ex.what();
    ASSERT_EQ(msg, err_msg);

    milvus::Exception ex1(milvus::SERVER_UNEXPECTED_ERROR, err_msg);
    ASSERT_EQ(ex1.code(), milvus::SERVER_UNEXPECTED_ERROR);
    msg = ex1.what();
    ASSERT_EQ(msg, err_msg);

    std::string empty_err_msg;
    milvus::Exception empty_ex(milvus::SERVER_UNEXPECTED_ERROR, empty_err_msg);
    ASSERT_EQ(empty_ex.code(), milvus::SERVER_UNEXPECTED_ERROR);
    msg = empty_ex.what();
    ASSERT_NE(msg, empty_err_msg);
}

TEST(UtilTest, SIGNAL_TEST) {
    milvus::server::SignalUtil::PrintStacktrace();
}

TEST(UtilTest, COMMON_TEST) {
    uint64_t total_mem = 0, free_mem = 0;
    milvus::server::CommonUtil::GetSystemMemInfo(total_mem, free_mem);
    ASSERT_GT(total_mem, 0);
    ASSERT_GT(free_mem, 0);

    int64_t thread_cnt = 0;
    milvus::server::CommonUtil::GetSystemAvailableThreads(thread_cnt);
    ASSERT_GT(thread_cnt, 0);

    fiu_init(0);
    fiu_enable("CommonUtil.GetSystemAvailableThreads.zero_thread", 1, NULL, 0);
    milvus::server::CommonUtil::GetSystemAvailableThreads(thread_cnt);
    ASSERT_GT(thread_cnt, 0);
    fiu_disable("CommonUtil.GetSystemAvailableThreads.zero_thread");

    std::string empty_path = "";
    std::string path1 = "/tmp/milvus_test/";
    std::string path2 = path1 + "common_test_12345/";
    std::string path3 = path2 + "abcdef";
    milvus::Status status = milvus::server::CommonUtil::CreateDirectory(path3);
    ASSERT_TRUE(status.ok());

    status = milvus::server::CommonUtil::CreateDirectory(empty_path);
    ASSERT_TRUE(status.ok());

    // test again
    status = milvus::server::CommonUtil::CreateDirectory(path3);
    ASSERT_TRUE(status.ok());

    ASSERT_TRUE(milvus::server::CommonUtil::IsDirectoryExist(path3));

    status = milvus::server::CommonUtil::DeleteDirectory(empty_path);
    ASSERT_TRUE(status.ok());

    status = milvus::server::CommonUtil::DeleteDirectory(path1);
    ASSERT_TRUE(status.ok());
    // test again
    status = milvus::server::CommonUtil::DeleteDirectory(path1);
    ASSERT_TRUE(status.ok());

    ASSERT_FALSE(milvus::server::CommonUtil::IsDirectoryExist(path1));
    ASSERT_FALSE(milvus::server::CommonUtil::IsFileExist(path1));

    std::string exe_path = milvus::server::CommonUtil::GetExePath();
    ASSERT_FALSE(exe_path.empty());

    fiu_enable("CommonUtil.GetExePath.readlink_fail", 1, NULL, 0);
    exe_path = milvus::server::CommonUtil::GetExePath();
    ASSERT_FALSE(!exe_path.empty());
    fiu_disable("CommonUtil.GetExePath.readlink_fail");

    fiu_enable("CommonUtil.GetExePath.exe_path_error", 1, NULL, 0);
    exe_path = milvus::server::CommonUtil::GetExePath();
    ASSERT_FALSE(exe_path.empty());
    fiu_disable("CommonUtil.GetExePath.exe_path_error");

    fiu_enable("CommonUtil.CreateDirectory.create_parent_fail", 1, NULL, 0);
    status = milvus::server::CommonUtil::CreateDirectory(path3);
    ASSERT_FALSE(status.ok());
    fiu_disable("CommonUtil.CreateDirectory.create_parent_fail");

    fiu_enable("CommonUtil.CreateDirectory.create_dir_fail", 1, NULL, 0);
    status = milvus::server::CommonUtil::CreateDirectory(path3);
    ASSERT_FALSE(status.ok());
    fiu_disable("CommonUtil.CreateDirectory.create_dir_fail");

    time_t tt;
    time(&tt);
    tm time_struct;
    memset(&time_struct, 0, sizeof(tm));
    milvus::server::CommonUtil::ConvertTime(tt, time_struct);
    ASSERT_GE(time_struct.tm_year, 0);
    ASSERT_GE(time_struct.tm_mon, 0);
    ASSERT_GE(time_struct.tm_mday, 0);
    milvus::server::CommonUtil::ConvertTime(time_struct, tt);
    ASSERT_GT(tt, 0);

    bool res = milvus::server::CommonUtil::TimeStrToTime("2019-03-23", tt, time_struct);
    ASSERT_EQ(time_struct.tm_year, 119);
    ASSERT_EQ(time_struct.tm_mon, 2);
    ASSERT_EQ(time_struct.tm_mday, 23);
    ASSERT_GT(tt, 0);
    ASSERT_TRUE(res);
}

TEST(UtilTest, STRINGFUNCTIONS_TEST) {
    std::string str = " test str";
    milvus::server::StringHelpFunctions::TrimStringBlank(str);
    ASSERT_EQ(str, "test str");

    str = "\"test str\"";
    milvus::server::StringHelpFunctions::TrimStringQuote(str, "\"");
    ASSERT_EQ(str, "test str");

    str = "a,b,c";
    std::vector<std::string> result;
    milvus::server::StringHelpFunctions::SplitStringByDelimeter(str, ",", result);
    ASSERT_EQ(result.size(), 3UL);

    std::string merge_str;
    milvus::server::StringHelpFunctions::MergeStringWithDelimeter(result, ",", merge_str);
    ASSERT_EQ(merge_str, "a,b,c");
    result.clear();
    milvus::server::StringHelpFunctions::MergeStringWithDelimeter(result, ",", merge_str);
    ASSERT_TRUE(merge_str.empty());

    auto status = milvus::server::StringHelpFunctions::SplitStringByQuote(str, ",", "\"", result);
    ASSERT_TRUE(status.ok());
    ASSERT_EQ(result.size(), 3UL);

    result.clear();
    status = milvus::server::StringHelpFunctions::SplitStringByQuote(str, ",", "", result);
    ASSERT_TRUE(status.ok());
    ASSERT_EQ(result.size(), 3UL);

    str = "55,\"aa,gg,yy\",b";
    result.clear();
    status = milvus::server::StringHelpFunctions::SplitStringByQuote(str, ",", "\"", result);
    ASSERT_TRUE(status.ok());
    ASSERT_EQ(result.size(), 3UL);

    fiu_init(0);
    fiu_enable("StringHelpFunctions.SplitStringByQuote.invalid_index", 1, NULL, 0);
    result.clear();
    status = milvus::server::StringHelpFunctions::SplitStringByQuote(str, ",", "\"", result);
    ASSERT_FALSE(status.ok());
    fiu_disable("StringHelpFunctions.SplitStringByQuote.invalid_index");

    fiu_enable("StringHelpFunctions.SplitStringByQuote.index_gt_last", 1, NULL, 0);
    result.clear();
    status = milvus::server::StringHelpFunctions::SplitStringByQuote(str, ",", "\"", result);
    ASSERT_TRUE(status.ok());
    fiu_disable("StringHelpFunctions.SplitStringByQuote.index_gt_last");

    fiu_enable("StringHelpFunctions.SplitStringByQuote.invalid_index2", 1, NULL, 0);
    result.clear();
    status = milvus::server::StringHelpFunctions::SplitStringByQuote(str, ",", "\"", result);
    ASSERT_FALSE(status.ok());
    fiu_disable("StringHelpFunctions.SplitStringByQuote.invalid_index2");

    fiu_enable("StringHelpFunctions.SplitStringByQuote.last_is_end", 1, NULL, 0);
    result.clear();
    status = milvus::server::StringHelpFunctions::SplitStringByQuote(str, ",", "\"", result);
    ASSERT_TRUE(status.ok());
    fiu_disable("StringHelpFunctions.SplitStringByQuote.last_is_end2");

    ASSERT_TRUE(milvus::server::StringHelpFunctions::IsRegexMatch("abc", "abc"));
    ASSERT_TRUE(milvus::server::StringHelpFunctions::IsRegexMatch("a8c", "a\\d."));
    ASSERT_FALSE(milvus::server::StringHelpFunctions::IsRegexMatch("abc", "a\\dc"));
}

TEST(UtilTest, BLOCKINGQUEUE_TEST) {
    milvus::server::BlockingQueue<std::string> bq;

    static const size_t count = 10;
    bq.SetCapacity(count);

    for (size_t i = 1; i <= count; i++) {
        std::string id = "No." + std::to_string(i);
        bq.Put(id);
    }

    ASSERT_EQ(bq.Size(), count);
    ASSERT_FALSE(bq.Empty());

    std::string str = bq.Front();
    ASSERT_EQ(str, "No.1");

    str = bq.Back();
    ASSERT_EQ(str, "No." + std::to_string(count));

    for (size_t i = 1; i <= count; i++) {
        std::string id = "No." + std::to_string(i);
        str = bq.Take();
        ASSERT_EQ(id, str);
    }

    ASSERT_EQ(bq.Size(), 0);
}

TEST(UtilTest, LOG_TEST) {
    auto status = milvus::server::InitLog(LOG_FILE_PATH);
    ASSERT_TRUE(status.ok());

    EXPECT_FALSE(el::Loggers::hasFlag(el::LoggingFlag::NewLineForContainer));
    EXPECT_FALSE(el::Loggers::hasFlag(el::LoggingFlag::LogDetailedCrashReason));

    std::string fname = milvus::server::CommonUtil::GetFileName(LOG_FILE_PATH);
    ASSERT_EQ(fname, "log_config.conf");
}

TEST(UtilTest, TIMERECORDER_TEST) {
    for (int64_t log_level = 0; log_level <= 6; log_level++) {
        if (log_level == 5) {
            continue;  // skip fatal
        }
        milvus::TimeRecorder rc("time", log_level);
        rc.RecordSection("end");
    }
}

TEST(UtilTest, TIMERECOREDRAUTO_TEST) {
    milvus::TimeRecorderAuto rc("time");
    rc.RecordSection("end");
}

TEST(UtilTest, STATUS_TEST) {
    auto status = milvus::Status::OK();
    std::string str = status.ToString();
    ASSERT_FALSE(str.empty());

    status = milvus::Status(milvus::DB_SUCCESS, "success");
    ASSERT_EQ(status.code(), milvus::DB_SUCCESS);
    str = status.ToString();
    ASSERT_FALSE(str.empty());

    status = milvus::Status(milvus::DB_ERROR, "mistake");
    ASSERT_EQ(status.code(), milvus::DB_ERROR);
    str = status.ToString();
    ASSERT_FALSE(str.empty());

    status = milvus::Status(milvus::DB_NOT_FOUND, "mistake");
    ASSERT_EQ(status.code(), milvus::DB_NOT_FOUND);
    str = status.ToString();
    ASSERT_FALSE(str.empty());

    status = milvus::Status(milvus::DB_ALREADY_EXIST, "mistake");
    ASSERT_EQ(status.code(), milvus::DB_ALREADY_EXIST);
    str = status.ToString();
    ASSERT_FALSE(str.empty());

    status = milvus::Status(milvus::DB_INVALID_PATH, "mistake");
    ASSERT_EQ(status.code(), milvus::DB_INVALID_PATH);
    str = status.ToString();
    ASSERT_FALSE(str.empty());

    status = milvus::Status(milvus::DB_META_TRANSACTION_FAILED, "mistake");
    ASSERT_EQ(status.code(), milvus::DB_META_TRANSACTION_FAILED);
    str = status.ToString();
    ASSERT_FALSE(str.empty());

    auto status_copy = milvus::Status::OK();
    CopyStatus(status_copy, status);
    ASSERT_EQ(status.code(), milvus::DB_META_TRANSACTION_FAILED);

    auto status_ref(status);
    ASSERT_EQ(status_ref.code(), status.code());
    ASSERT_EQ(status_ref.ToString(), status.ToString());

    auto status_move = std::move(status);
    ASSERT_EQ(status_move.code(), status_ref.code());
    ASSERT_EQ(status_move.ToString(), status_ref.ToString());
}

TEST(ValidationUtilTest, VALIDATE_TABLENAME_TEST) {
    std::string table_name = "Normal123_";
    auto status = milvus::server::ValidationUtil::ValidateTableName(table_name);
    ASSERT_TRUE(status.ok());

    table_name = "12sds";
    status = milvus::server::ValidationUtil::ValidateTableName(table_name);
    ASSERT_EQ(status.code(), milvus::SERVER_INVALID_TABLE_NAME);

    table_name = "";
    status = milvus::server::ValidationUtil::ValidateTableName(table_name);
    ASSERT_EQ(status.code(), milvus::SERVER_INVALID_TABLE_NAME);

    table_name = "_asdasd";
    status = milvus::server::ValidationUtil::ValidateTableName(table_name);
    ASSERT_EQ(status.code(), milvus::SERVER_SUCCESS);

    table_name = "!@#!@";
    status = milvus::server::ValidationUtil::ValidateTableName(table_name);
    ASSERT_EQ(status.code(), milvus::SERVER_INVALID_TABLE_NAME);

    table_name = "_!@#!@";
    status = milvus::server::ValidationUtil::ValidateTableName(table_name);
    ASSERT_EQ(status.code(), milvus::SERVER_INVALID_TABLE_NAME);

    table_name = "中文";
    status = milvus::server::ValidationUtil::ValidateTableName(table_name);
    ASSERT_EQ(status.code(), milvus::SERVER_INVALID_TABLE_NAME);

    table_name = std::string(10000, 'a');
    status = milvus::server::ValidationUtil::ValidateTableName(table_name);
    ASSERT_EQ(status.code(), milvus::SERVER_INVALID_TABLE_NAME);

    table_name = "";
    status = milvus::server::ValidationUtil::ValidatePartitionName(table_name);
    ASSERT_EQ(status.code(), milvus::SERVER_INVALID_TABLE_NAME);
}

TEST(ValidationUtilTest, VALIDATE_DIMENSION_TEST) {
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateTableDimension(-1).code(),
              milvus::SERVER_INVALID_VECTOR_DIMENSION);
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateTableDimension(0).code(),
              milvus::SERVER_INVALID_VECTOR_DIMENSION);
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateTableDimension(32769).code(),
              milvus::SERVER_INVALID_VECTOR_DIMENSION);
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateTableDimension(32768).code(), milvus::SERVER_SUCCESS);
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateTableDimension(1).code(), milvus::SERVER_SUCCESS);
}

TEST(ValidationUtilTest, VALIDATE_INDEX_TEST) {
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateTableIndexType((int)milvus::engine::EngineType::INVALID).code(),
              milvus::SERVER_INVALID_INDEX_TYPE);
    for (int i = 1; i <= (int)milvus::engine::EngineType::MAX_VALUE; i++) {
#ifndef CUSTOMIZATION
        if (i == (int)milvus::engine::EngineType::FAISS_IVFSQ8H) {
            ASSERT_NE(milvus::server::ValidationUtil::ValidateTableIndexType(i).code(), milvus::SERVER_SUCCESS);
            continue;
        }
#endif
        ASSERT_EQ(milvus::server::ValidationUtil::ValidateTableIndexType(i).code(), milvus::SERVER_SUCCESS);
    }

    ASSERT_EQ(
        milvus::server::ValidationUtil::ValidateTableIndexType((int)milvus::engine::EngineType::MAX_VALUE + 1).code(),
        milvus::SERVER_INVALID_INDEX_TYPE);

    ASSERT_EQ(milvus::server::ValidationUtil::ValidateTableIndexNlist(0).code(), milvus::SERVER_INVALID_INDEX_NLIST);
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateTableIndexNlist(100).code(), milvus::SERVER_SUCCESS);

    ASSERT_EQ(milvus::server::ValidationUtil::ValidateTableIndexFileSize(0).code(),
              milvus::SERVER_INVALID_INDEX_FILE_SIZE);
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateTableIndexFileSize(100).code(), milvus::SERVER_SUCCESS);

    ASSERT_EQ(milvus::server::ValidationUtil::ValidateTableIndexMetricType(0).code(),
              milvus::SERVER_INVALID_INDEX_METRIC_TYPE);
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateTableIndexMetricType(1).code(), milvus::SERVER_SUCCESS);
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateTableIndexMetricType(2).code(), milvus::SERVER_SUCCESS);
}

TEST(ValidationUtilTest, VALIDATE_TOPK_TEST) {
    milvus::engine::meta::TableSchema schema;
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateSearchTopk(10, schema).code(), milvus::SERVER_SUCCESS);
    ASSERT_NE(milvus::server::ValidationUtil::ValidateSearchTopk(65536, schema).code(), milvus::SERVER_SUCCESS);
    ASSERT_NE(milvus::server::ValidationUtil::ValidateSearchTopk(0, schema).code(), milvus::SERVER_SUCCESS);
}

TEST(ValidationUtilTest, VALIDATE_NPROBE_TEST) {
    milvus::engine::meta::TableSchema schema;
    schema.nlist_ = 100;
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateSearchNprobe(10, schema).code(), milvus::SERVER_SUCCESS);
    ASSERT_NE(milvus::server::ValidationUtil::ValidateSearchNprobe(0, schema).code(), milvus::SERVER_SUCCESS);
    ASSERT_NE(milvus::server::ValidationUtil::ValidateSearchNprobe(101, schema).code(), milvus::SERVER_SUCCESS);
}

TEST(ValidationUtilTest, VALIDATE_PARTITION_TAGS) {
    std::vector<std::string> partition_tags = {"abc"};
    ASSERT_EQ(milvus::server::ValidationUtil::ValidatePartitionTags(partition_tags).code(), milvus::SERVER_SUCCESS);
    partition_tags.push_back("");
    ASSERT_NE(milvus::server::ValidationUtil::ValidatePartitionTags(partition_tags).code(), milvus::SERVER_SUCCESS);
}

#ifdef MILVUS_GPU_VERSION
TEST(ValidationUtilTest, VALIDATE_GPU_TEST) {
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateGpuIndex(0).code(), milvus::SERVER_SUCCESS);
    ASSERT_NE(milvus::server::ValidationUtil::ValidateGpuIndex(100).code(), milvus::SERVER_SUCCESS);

    fiu_init(0);
    fiu_enable("ValidationUtil.ValidateGpuIndex.get_device_count_fail", 1, NULL, 0);
    ASSERT_NE(milvus::server::ValidationUtil::ValidateGpuIndex(0).code(), milvus::SERVER_SUCCESS);
    fiu_disable("ValidationUtil.ValidateGpuIndex.get_device_count_fail");

    size_t memory = 0;
    ASSERT_EQ(milvus::server::ValidationUtil::GetGpuMemory(0, memory).code(), milvus::SERVER_SUCCESS);
    ASSERT_NE(milvus::server::ValidationUtil::GetGpuMemory(100, memory).code(), milvus::SERVER_SUCCESS);
}

#endif

TEST(ValidationUtilTest, VALIDATE_IPADDRESS_TEST) {
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateIpAddress("127.0.0.1").code(), milvus::SERVER_SUCCESS);
    ASSERT_NE(milvus::server::ValidationUtil::ValidateIpAddress("not ip").code(), milvus::SERVER_SUCCESS);

    fiu_init(0);
    fiu_enable("ValidationUtil.ValidateIpAddress.error_ip_result", 1, NULL, 0);
    ASSERT_NE(milvus::server::ValidationUtil::ValidateIpAddress("not ip").code(), milvus::SERVER_SUCCESS);
    fiu_disable("ValidationUtil.ValidateIpAddress.error_ip_result");
}

TEST(ValidationUtilTest, VALIDATE_NUMBER_TEST) {
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateStringIsNumber("1234").code(), milvus::SERVER_SUCCESS);
    ASSERT_NE(milvus::server::ValidationUtil::ValidateStringIsNumber("not number").code(), milvus::SERVER_SUCCESS);

    fiu_init(0);
    fiu_enable("ValidationUtil.ValidateStringIsNumber.throw_exception", 1, NULL, 0);
    ASSERT_NE(milvus::server::ValidationUtil::ValidateStringIsNumber("122").code(), milvus::SERVER_SUCCESS);
    fiu_disable("ValidationUtil.ValidateStringIsNumber.throw_exception");
}

TEST(ValidationUtilTest, VALIDATE_BOOL_TEST) {
    std::string str = "true";
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateStringIsBool(str).code(), milvus::SERVER_SUCCESS);
    str = "not bool";
    ASSERT_NE(milvus::server::ValidationUtil::ValidateStringIsBool(str).code(), milvus::SERVER_SUCCESS);
}

TEST(ValidationUtilTest, VALIDATE_DOUBLE_TEST) {
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateStringIsFloat("2.5").code(), milvus::SERVER_SUCCESS);
    ASSERT_NE(milvus::server::ValidationUtil::ValidateStringIsFloat("not double").code(), milvus::SERVER_SUCCESS);
}

TEST(ValidationUtilTest, VALIDATE_DBURI_TEST) {
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateDbURI("sqlite://:@:/").code(), milvus::SERVER_SUCCESS);
    ASSERT_NE(milvus::server::ValidationUtil::ValidateDbURI("xxx://:@:/").code(), milvus::SERVER_SUCCESS);
    ASSERT_NE(milvus::server::ValidationUtil::ValidateDbURI("not uri").code(), milvus::SERVER_SUCCESS);
    ASSERT_EQ(milvus::server::ValidationUtil::ValidateDbURI("mysql://root:123456@127.0.0.1:3303/milvus").code(),
              milvus::SERVER_SUCCESS);
    ASSERT_NE(milvus::server::ValidationUtil::ValidateDbURI("mysql://root:123456@127.0.0.1:port/milvus").code(),
              milvus::SERVER_SUCCESS);
}

TEST(UtilTest, ROLLOUTHANDLER_TEST) {
    std::string dir1 = "/tmp/milvus_test";
    std::string dir2 = "/tmp/milvus_test/log_test";
    std::string filename[6] = {"log_global.log", "log_debug.log", "log_warning.log",
                               "log_trace.log", "log_error.log", "log_fatal.log"};

    el::Level list[6] = {el::Level::Global, el::Level::Debug, el::Level::Warning,
                         el::Level::Trace, el::Level::Error, el::Level::Fatal};

    mkdir(dir1.c_str(), S_IRWXU);
    mkdir(dir2.c_str(), S_IRWXU);
//    [&]() {
////        std::string tmp = dir2 + "/" + filename[0]+"*@%$";
//        std::string tmp = dir2 + "/" + filename[0] + "*$";
//        std::ofstream file;
//        file.open(tmp.c_str());
//        file << "test" << std::endl;
//        milvus::server::RolloutHandler(tmp.c_str(), 0, el::Level::Unknown);
//        tmp.append(".1");
//        std::ifstream file2;
//        file2.open(tmp);
//        std::string tmp2;
//        file2 >> tmp2;
//        ASSERT_EQ(tmp2, "test");
//    }();

    for (int i = 0; i < 6; ++i) {
        std::string tmp = dir2 + "/" + filename[i];

        std::ofstream file;
        file.open(tmp.c_str());
        file << "test" << std::endl;

        milvus::server::RolloutHandler(tmp.c_str(), 0, list[i]);

        tmp.append(".1");
        std::ifstream file2;
        file2.open(tmp);

        std::string tmp2;
        file2 >> tmp2;
        ASSERT_EQ(tmp2, "test");
    }

    [&]() {
        std::string tmp = dir2 + "/" + filename[0];
        std::ofstream file;
        file.open(tmp.c_str());
        file << "test" << std::endl;
        milvus::server::RolloutHandler(tmp.c_str(), 0, el::Level::Unknown);
        tmp.append(".1");
        std::ifstream file2;
        file2.open(tmp);
        std::string tmp2;
        file2 >> tmp2;
        ASSERT_EQ(tmp2, "test");
    }();

    boost::filesystem::remove_all(dir2);
}

TEST(UtilTest, THREADPOOL_TEST) {
    auto thread_pool_ptr = std::make_unique<milvus::ThreadPool>(3);
    auto fun = [](int i) {
        sleep(1);
    };
    for (int i = 0; i < 10; ++i) {
        thread_pool_ptr->enqueue(fun, i);
    }

    fiu_init(0);
    fiu_enable("ThreadPool.enqueue.stop_is_true", 1, NULL, 0);
    try {
        thread_pool_ptr->enqueue(fun, -1);
    } catch (std::exception& err) {
        std::cout << "catch an error here" << std::endl;
    }
    fiu_disable("ThreadPool.enqueue.stop_is_true");

    thread_pool_ptr.reset();
}
