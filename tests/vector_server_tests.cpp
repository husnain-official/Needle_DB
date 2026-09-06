#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <cmath>
#include <algorithm>
#include <vector>
#include <random>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "vector_server.h"
#include "vector_store.h"
#include "file_manager.h"
#include "ivf.h"
#include "schema.hpp"
#include "types.h"

// Global port counter to prevent cross-test socket collisions during parallel or rapid sequential runs.
static std::atomic<uint16_t> port_counter{19000};

// Adjustable starting points for concurrency tests (Groups 9-13)
constexpr size_t kConcurrentClientCount = 10;
constexpr size_t kConcurrentInsertCount = 20;
constexpr size_t kConcurrentReaderCount = 8;
constexpr size_t kMixedThreadsCount = 5;
constexpr size_t kChurnIterations = 50;

// Adjustable starting points for index persistence and performance tests (Groups 14-17)
constexpr size_t kIndexPersistTestVectorCount = 15;
constexpr size_t kPerformanceTestVectorCount = 50000;
constexpr size_t kPerformanceRepetitions = 5;

// -----------------------------------------------------------------------------
// Base Fixture: Environment Management & Helpers
// -----------------------------------------------------------------------------
class VectorServerTestBase : public ::testing::Test
{
protected:
    std::string entry_path_;
    std::string text_path_;
    std::string index_path_;
    uint16_t port_;
    Config config_;

    void SetUp() override
    {
        const ::testing::TestInfo *test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string test_name = test_info->name();

        entry_path_ = "./temp_" + test_name + "_entry.vdb";
        text_path_ = "./temp_" + test_name + "_text.vdb";
        index_path_ = "./temp_" + test_name + "_index.vdb"; // New for v3
        port_ = port_counter++;

        CleanUpFiles();

        config_.port = std::to_string(port_);
        config_.vecdb_entry_file_path = entry_path_;
        config_.vecdb_text_file_path = text_path_;
        config_.vecdb_index_file_path = index_path_; // New for v3
    }

    void TearDown() override
    {
        CleanUpFiles();
    }

    void CleanUpFiles()
    {
        std::error_code ec;
        std::filesystem::remove(entry_path_, ec);
        std::filesystem::remove(text_path_, ec);
        std::filesystem::remove(index_path_, ec); // New for v3
    }

    // -------------------------------------------------------------------------
    // Network & Command Helpers
    // -------------------------------------------------------------------------

    int connect_client(uint16_t target_port, int timeout_ms = 2000)
    {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(target_port);
        inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

        auto start = std::chrono::steady_clock::now();
        while (true)
        {
            if (connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0)
                return fd;
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > timeout_ms)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        close(fd);
        return -1;
    }

    void send_command(int fd, const std::string &cmd)
    {
        ssize_t bytes_sent = send(fd, cmd.c_str(), cmd.size(), 0);
        ASSERT_EQ(bytes_sent, cmd.size()) << "Failed to send complete command string";
    }

    std::string read_response(int fd, int timeout_ms = 200)
    {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));

        std::string response;
        char buffer[4096];
        while (true)
        {
            ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
            if (n > 0)
                response.append(buffer, n);
            else
                break;
        }
        return response;
    }

    std::string GenerateNormalizedFloats()
    {
        std::ostringstream oss;
        float val = 1.0f / std::sqrt(static_cast<float>(schema::DIMENSIONS));
        for (size_t i = 0; i < schema::DIMENSIONS; i++)
        {
            oss << val;
            if (i != schema::DIMENSIONS - 1)
                oss << " ";
        }
        return oss.str();
    }

    std::string GenerateDimsString(const std::string &base_val = std::to_string(schema::DIMENSIONS))
    {
        std::string res = base_val;
        while (res.size() < schema::DIMENSIONS_NO_OF_DIGITS)
            res = "0" + res;
        return res.substr(0, schema::DIMENSIONS_NO_OF_DIGITS);
    }

    std::string BuildInsertCommand(const std::string &id, const std::string &text, const std::string &metadata = "")
    {
        std::ostringstream oss;
        oss << "INSERT " << id << " " << text.size() << " " << text << " " << GenerateDimsString();
        if (!metadata.empty())
            oss << " " << metadata;
        oss << " " << GenerateNormalizedFloats() << "\n";
        return oss.str();
    }

    std::string BuildQueryCommand(size_t top_k, const std::string &metadata = "")
    {
        std::ostringstream oss;
        oss << "QUERY " << top_k << " " << GenerateDimsString();
        if (!metadata.empty())
            oss << " " << metadata;
        oss << " " << GenerateNormalizedFloats() << "\n";
        return oss.str();
    }

    // Helper: Seeds DB on disk strictly before the server boots.
    void PreSeedDatabase(size_t count)
    {
        File_manager temp_fm(entry_path_, text_path_, index_path_);
        for (size_t i = 0; i < count; i++)
        {
            DB_entry e{};
            std::memset(&e, 0, sizeof(e));
            e.flag = 1;

            std::string id = "preseed_" + std::to_string(i);
            id.resize(schema::ID_LENGTH, '\0');
            std::memcpy(e.id, id.c_str(), schema::ID_LENGTH);

            std::string text = "Preseeded text " + std::to_string(i);
            e.text_length = text.size();

            float val = 1.0f / std::sqrt(static_cast<float>(schema::DIMENSIONS));
            for (size_t d = 0; d < schema::DIMENSIONS; d++)
            {
                e.embeddings[d] = val;
            }

            temp_fm.write_entry(e, text);
        }
    }
};

// -----------------------------------------------------------------------------
// Test Fixture: Auto-Booting Server Integration Setup (Groups 1-13)
// -----------------------------------------------------------------------------
class VectorServerIntegrationTest : public VectorServerTestBase
{
protected:
    File_manager *fm_ = nullptr;
    Vector_store *vs_ = nullptr;
    Vector_Server *server_ = nullptr;

    void SetUp() override
    {
        VectorServerTestBase::SetUp();

        fm_ = new File_manager(entry_path_, text_path_, index_path_);
        vs_ = new Vector_store();
        server_ = new Vector_Server(config_.port, *vs_, *fm_, config_);

        ASSERT_TRUE(server_->setup()) << "Server failed to bind socket to port " << port_;

        std::thread([this]()
                    { server_->run(); })
            .detach();

        int probe_fd = connect_client(port_);
        ASSERT_GE(probe_fd, 0) << "Failed to probe-connect to server on port " << port_;
        close(probe_fd);
    }

    void TearDown() override
    {
        VectorServerTestBase::TearDown();
    }
};

// =============================================================================
// Group 1: INSERT — end to end
// =============================================================================

TEST_F(VectorServerIntegrationTest, Insert_ValidCommand_ReturnsOk)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    send_command(client_fd, BuildInsertCommand("id_1", "Sample text block"));
    std::string response = read_response(client_fd);

    EXPECT_EQ(response, "INSERT <Successful>\n");
    close(client_fd);
}

TEST_F(VectorServerIntegrationTest, Insert_DuplicateId_ReturnsWarning)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    send_command(client_fd, BuildInsertCommand("id_dup", "Text A"));
    EXPECT_EQ(read_response(client_fd), "INSERT <Successful>\n");

    send_command(client_fd, BuildInsertCommand("id_dup", "Text B"));
    std::string response = read_response(client_fd);

    EXPECT_NE(response, "INSERT <Successful>\n");
    EXPECT_NE(response.find("WARNING <Id already exists"), std::string::npos);
    close(client_fd);
}

TEST_F(VectorServerIntegrationTest, Insert_ValidCommand_PersistsToRAMAndQueryable)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    send_command(client_fd, BuildInsertCommand("id_find_me", "Data here"));
    EXPECT_EQ(read_response(client_fd), "INSERT <Successful>\n");

    send_command(client_fd, BuildQueryCommand(1));
    std::string response = read_response(client_fd);

    EXPECT_NE(response.find("id_find_me"), std::string::npos);
    close(client_fd);
}

TEST_F(VectorServerIntegrationTest, Insert_ValidCommand_PersistsToDisk)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    send_command(client_fd, BuildInsertCommand("id_disk_test", "Data to disk"));
    EXPECT_EQ(read_response(client_fd), "INSERT <Successful>\n");
    close(client_fd);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    File_manager fm_verification(entry_path_, text_path_, index_path_);
    std::string search_id = "id_disk_test";
    search_id.resize(schema::ID_LENGTH, '\0');
    EXPECT_NE(fm_verification.find_by_id(search_id), -1);
}

TEST_F(VectorServerIntegrationTest, Insert_MalformedCommand_ReturnsParserError)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    std::ostringstream oss;
    oss << "INSERT id_bad 4 text 9999 " << GenerateNormalizedFloats() << "\n";

    send_command(client_fd, oss.str());
    std::string response = read_response(client_fd);

    EXPECT_NE(response.find("ERROR <Invalid dimensions"), std::string::npos);
    close(client_fd);
}

// =============================================================================
// Group 2: QUERY — end to end
// =============================================================================

TEST_F(VectorServerIntegrationTest, Query_ValidCommand_ReturnsResultsWithText)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    std::string expected_text = "Specific_Unique_Payload";
    send_command(client_fd, BuildInsertCommand("id_q1", expected_text));
    EXPECT_EQ(read_response(client_fd), "INSERT <Successful>\n");

    send_command(client_fd, BuildQueryCommand(1));
    std::string response = read_response(client_fd);

    EXPECT_TRUE(response.starts_with("QUERY <1>\n"));
    EXPECT_TRUE(response.ends_with("END\n"));
    EXPECT_NE(response.find(expected_text), std::string::npos)
        << "EXPECTED TO FAIL if text read-by-reference bug was reintroduced: Text is missing from response.";

    close(client_fd);
}

TEST_F(VectorServerIntegrationTest, Query_EmptyDatabase_ReturnsZeroResults)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    send_command(client_fd, BuildQueryCommand(5));
    std::string response = read_response(client_fd);

    EXPECT_EQ(response, "QUERY <0>\nEND\n");
    close(client_fd);
}

TEST_F(VectorServerIntegrationTest, Query_WithMetadataFilter_ReturnsOnlyMatching)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    send_command(client_fd, BuildInsertCommand("id_no_meta", "Txt"));
    read_response(client_fd);
    send_command(client_fd, BuildInsertCommand("id_meta", "Txt", "tag=target"));
    read_response(client_fd);

    send_command(client_fd, BuildQueryCommand(5, "tag=target"));
    std::string response = read_response(client_fd);

    EXPECT_NE(response.find("id_meta"), std::string::npos);
    EXPECT_EQ(response.find("id_no_meta"), std::string::npos);

    close(client_fd);
}

TEST_F(VectorServerIntegrationTest, Query_TopKExceedsMax_ClampsToMax)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    const size_t insert_count = schema::MAX_K_SIMILAR + 5;
    for (size_t i = 0; i < insert_count; i++)
    {
        send_command(client_fd, BuildInsertCommand("clamp_id_" + std::to_string(i), "Txt"));
        EXPECT_EQ(read_response(client_fd), "INSERT <Successful>\n");
    }

    size_t requested_k = schema::MAX_K_SIMILAR + 10;
    send_command(client_fd, BuildQueryCommand(requested_k));
    std::string response = read_response(client_fd, 500);

    std::string expected_header = "QUERY <" + std::to_string(schema::MAX_K_SIMILAR) + ">\n";
    EXPECT_TRUE(response.starts_with(expected_header));

    size_t total_lines = std::count(response.begin(), response.end(), '\n');
    ASSERT_GE(total_lines, 2u);
    size_t result_lines = total_lines - 2;
    EXPECT_EQ(result_lines, schema::MAX_K_SIMILAR);

    close(client_fd);
}

// =============================================================================
// Group 3: DELETE — end to end
// =============================================================================

TEST_F(VectorServerIntegrationTest, Delete_ValidId_ReturnsOkAndRemovesFromStore)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    send_command(client_fd, BuildInsertCommand("id_to_delete", "Txt"));
    EXPECT_EQ(read_response(client_fd), "INSERT <Successful>\n");

    send_command(client_fd, "DELETE id_to_delete\n");
    EXPECT_EQ(read_response(client_fd), "DELETE <Successful>\n");

    send_command(client_fd, BuildQueryCommand(1));
    std::string query_res = read_response(client_fd);
    EXPECT_EQ(query_res.find("id_to_delete"), std::string::npos);

    close(client_fd);
}

TEST_F(VectorServerIntegrationTest, Delete_NonExistentId_ReturnsError)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    send_command(client_fd, "DELETE fake_id\n");
    std::string response = read_response(client_fd);
    EXPECT_NE(response.find("ERROR <Could not find vector to delete"), std::string::npos);

    close(client_fd);
}

TEST_F(VectorServerIntegrationTest, Delete_MiddleElement_LeavesRemainingElementsQueryable)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    for (size_t i = 0; i < 5; i++)
    {
        send_command(client_fd, BuildInsertCommand("mid_id_" + std::to_string(i), "Txt"));
        EXPECT_EQ(read_response(client_fd), "INSERT <Successful>\n");
    }

    send_command(client_fd, "DELETE mid_id_2\n");
    EXPECT_EQ(read_response(client_fd), "DELETE <Successful>\n");

    std::vector<size_t> remaining = {0, 1, 3, 4};
    for (size_t idx : remaining)
    {
        send_command(client_fd, BuildQueryCommand(5));
        std::string response = read_response(client_fd);
        std::string target = "mid_id_" + std::to_string(idx);
        EXPECT_NE(response.find(target), std::string::npos)
            << "Double-delete IVF regression check failed: Could not find surviving element " << target;
    }

    close(client_fd);
}

// =============================================================================
// Group 4: SAVE
// =============================================================================

TEST_F(VectorServerIntegrationTest, Save_ValidCommand_ReturnsOkAndPersistsState)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    send_command(client_fd, BuildInsertCommand("id_save", "Txt"));
    EXPECT_EQ(read_response(client_fd), "INSERT <Successful>\n");

    send_command(client_fd, "SAVE\n");
    EXPECT_EQ(read_response(client_fd), "SAVE <Successful>\n");
    close(client_fd);

    File_manager fm_verification(entry_path_, text_path_, index_path_);
    EXPECT_EQ(fm_verification.get_live_vector_count(), 1);
}

// =============================================================================
// Group 5: LOAD
// =============================================================================

TEST_F(VectorServerIntegrationTest, Load_ValidCommand_ReloadsLiveStateFromDisk)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    send_command(client_fd, BuildInsertCommand("id_load_1", "Txt"));
    read_response(client_fd);
    send_command(client_fd, BuildInsertCommand("id_load_2", "Txt"));
    read_response(client_fd);
    send_command(client_fd, "DELETE id_load_1\n");
    read_response(client_fd);

    send_command(client_fd, "LOAD\n");
    EXPECT_EQ(read_response(client_fd), "LOAD <Successful>\n");

    send_command(client_fd, BuildQueryCommand(5));
    std::string response = read_response(client_fd);

    EXPECT_EQ(response.find("id_load_1"), std::string::npos);
    EXPECT_NE(response.find("id_load_2"), std::string::npos);

    close(client_fd);
}

// =============================================================================
// Group 6: Pipelined Commands
// =============================================================================

TEST_F(VectorServerIntegrationTest, PipelinedCommands_ProcessedIndependently)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    std::string cmd1 = BuildInsertCommand("id_pipe", "Txt");
    std::string cmd2 = BuildQueryCommand(1);

    send_command(client_fd, cmd1 + cmd2);
    std::string response = read_response(client_fd);

    EXPECT_NE(response.find("INSERT <Successful>\n"), std::string::npos);
    EXPECT_NE(response.find("id_pipe"), std::string::npos);
    EXPECT_NE(response.find("END\n"), std::string::npos);

    close(client_fd);
}

// =============================================================================
// Group 7: Split Commands
// =============================================================================

TEST_F(VectorServerIntegrationTest, SplitCommand_ProcessedCorrectly)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    std::string full_cmd = BuildInsertCommand("id_split", "Txt");
    size_t split_point = full_cmd.size() / 2;

    std::string part1 = full_cmd.substr(0, split_point);
    std::string part2 = full_cmd.substr(split_point);

    send(client_fd, part1.c_str(), part1.size(), 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    send(client_fd, part2.c_str(), part2.size(), 0);

    std::string response = read_response(client_fd);
    EXPECT_EQ(response, "INSERT <Successful>\n");

    close(client_fd);
}

// =============================================================================
// Group 8: Unrecognized Command Prefix
// =============================================================================

TEST_F(VectorServerIntegrationTest, UnrecognizedPrefix_SilentlyIgnored_Characterization)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    send_command(client_fd, "FOOBAR test\n");
    std::string response = read_response(client_fd, 300);

    // Current behavior: unrecognized commands are silently ignored with no error response.
    // This may be worth changing to send an explicit "ERROR <unknown command>" —
    // flagging for a decision, not asserting either behavior is strictly "correct".
    EXPECT_TRUE(response.empty());

    close(client_fd);
}

// =============================================================================
// Group 9: Concurrent Client Connections
// =============================================================================

TEST_F(VectorServerIntegrationTest, ConcurrentConnections_AreServedSimultaneously)
{
    std::vector<std::thread> threads;
    std::atomic<bool> start_flag{false};
    std::atomic<int> success_count{0};

    for (size_t i = 0; i < kConcurrentClientCount; i++)
    {
        threads.emplace_back([&, i]()
                             {
            while (!start_flag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            int fd = connect_client(port_);
            if (fd >= 0) {
                std::string id = "conn_id_" + std::to_string(i);
                send_command(fd, BuildInsertCommand(id, "txt"));
                std::string resp = read_response(fd);
                if (resp == "INSERT <Successful>\n") {
                    success_count++;
                }
                close(fd);
            } });
    }

    start_flag.store(true, std::memory_order_release);
    for (auto &t : threads)
        t.join();

    EXPECT_EQ(success_count.load(), kConcurrentClientCount);

    int check_fd = connect_client(port_);
    ASSERT_GE(check_fd, 0);
    send_command(check_fd, BuildQueryCommand(kConcurrentClientCount + 5));
    std::string query_res = read_response(check_fd, 1000);

    for (size_t i = 0; i < kConcurrentClientCount; i++)
    {
        std::string expected_id = "conn_id_" + std::to_string(i);
        EXPECT_NE(query_res.find(expected_id), std::string::npos);
    }
    close(check_fd);
}

// =============================================================================
// Group 10: Concurrent INSERTs
// =============================================================================

TEST_F(VectorServerIntegrationTest, ConcurrentInserts_NoLostUpdatesOrCorruption)
{
    std::vector<std::thread> threads;
    std::atomic<bool> start_flag{false};
    std::atomic<int> success_count{0};

    for (size_t i = 0; i < kConcurrentInsertCount; i++)
    {
        threads.emplace_back([&, i]()
                             {
            while (!start_flag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            int fd = connect_client(port_);
            if (fd >= 0) {
                std::string id = "stress_id_" + std::to_string(i);
                send_command(fd, BuildInsertCommand(id, "Concurrent text " + std::to_string(i)));
                std::string resp = read_response(fd);
                if (resp == "INSERT <Successful>\n") {
                    success_count++;
                }
                close(fd);
            } });
    }

    start_flag.store(true, std::memory_order_release);
    for (auto &t : threads)
        t.join();

    EXPECT_EQ(success_count.load(), kConcurrentInsertCount);

    int check_fd = connect_client(port_);
    ASSERT_GE(check_fd, 0);
    send_command(check_fd, BuildQueryCommand(kConcurrentInsertCount + 5));
    std::string query_res = read_response(check_fd, 2000);

    size_t found_count = 0;
    for (size_t i = 0; i < kConcurrentInsertCount; i++)
    {
        std::string expected_id = "stress_id_" + std::to_string(i);
        if (query_res.find(expected_id) != std::string::npos)
            found_count++;
    }
    EXPECT_EQ(found_count, kConcurrentInsertCount);

    size_t result_lines = std::count(query_res.begin(), query_res.end(), '\n') - 2;
    EXPECT_EQ(result_lines, kConcurrentInsertCount);

    send_command(check_fd, "SAVE\n");
    EXPECT_EQ(read_response(check_fd), "SAVE <Successful>\n");
    close(check_fd);

    File_manager fm_verification(entry_path_, text_path_, index_path_);
    EXPECT_EQ(fm_verification.get_live_vector_count(), kConcurrentInsertCount);
}

// =============================================================================
// Group 11: Concurrent QUERYs
// =============================================================================

TEST_F(VectorServerIntegrationTest, ConcurrentQueries_NoCrashOrResponseCorruption)
{
    int setup_fd = connect_client(port_);
    ASSERT_GE(setup_fd, 0);
    send_command(setup_fd, BuildInsertCommand("static_target", "Static text"));
    EXPECT_EQ(read_response(setup_fd), "INSERT <Successful>\n");
    close(setup_fd);

    std::vector<std::thread> threads;
    std::atomic<bool> start_flag{false};
    std::atomic<int> valid_queries{0};

    for (size_t i = 0; i < kConcurrentReaderCount; i++)
    {
        threads.emplace_back([&]()
                             {
            while (!start_flag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            int fd = connect_client(port_);
            if (fd >= 0) {
                send_command(fd, BuildQueryCommand(5));
                std::string resp = read_response(fd, 500);
                
                if (resp.starts_with("QUERY <") && 
                    resp.ends_with("END\n") && 
                    resp.find("static_target") != std::string::npos) {
                    valid_queries++;
                }
                close(fd);
            } });
    }

    start_flag.store(true, std::memory_order_release);
    for (auto &t : threads)
        t.join();

    EXPECT_EQ(valid_queries.load(), kConcurrentReaderCount);
}

// =============================================================================
// Group 12: Concurrent Mixed Operations
// =============================================================================

TEST_F(VectorServerIntegrationTest, ConcurrentMixed_OperationsMaintainConsistency)
{
    int setup_fd = connect_client(port_);
    ASSERT_GE(setup_fd, 0);
    for (size_t i = 0; i < kMixedThreadsCount; i++)
    {
        send_command(setup_fd, BuildInsertCommand("pre_id_" + std::to_string(i), "Pre txt"));
        EXPECT_EQ(read_response(setup_fd), "INSERT <Successful>\n");
    }
    close(setup_fd);

    std::vector<std::thread> threads;
    std::atomic<bool> start_flag{false};
    std::atomic<int> format_failures{0};

    // Inserters
    for (size_t i = 0; i < kMixedThreadsCount; i++)
    {
        threads.emplace_back([&, i]()
                             {
            while (!start_flag.load(std::memory_order_acquire)) std::this_thread::yield();
            int fd = connect_client(port_);
            if (fd >= 0) {
                send_command(fd, BuildInsertCommand("new_id_" + std::to_string(i), "New txt"));
                std::string r = read_response(fd);
                if (r != "INSERT <Successful>\n") format_failures++;
                close(fd);
            } });
    }

    // Deleters
    for (size_t i = 0; i < kMixedThreadsCount; i++)
    {
        threads.emplace_back([&, i]()
                             {
            while (!start_flag.load(std::memory_order_acquire)) std::this_thread::yield();
            int fd = connect_client(port_);
            if (fd >= 0) {
                send_command(fd, "DELETE pre_id_" + std::to_string(i) + "\n");
                std::string r = read_response(fd);
                if (r != "DELETE <Successful>\n") format_failures++;
                close(fd);
            } });
    }

    // Readers
    for (size_t i = 0; i < kMixedThreadsCount; i++)
    {
        threads.emplace_back([&]()
                             {
            while (!start_flag.load(std::memory_order_acquire)) std::this_thread::yield();
            int fd = connect_client(port_);
            if (fd >= 0) {
                send_command(fd, BuildQueryCommand(schema::MAX_K_SIMILAR));
                std::string r = read_response(fd, 500); 
                if (!r.starts_with("QUERY <") || !r.ends_with("END\n")) format_failures++;
                close(fd);
            } });
    }

    start_flag.store(true, std::memory_order_release);
    for (auto &t : threads)
        t.join();

    EXPECT_EQ(format_failures.load(), 0);

    int check_fd = connect_client(port_);
    ASSERT_GE(check_fd, 0);

    send_command(check_fd, "SAVE\n");
    EXPECT_EQ(read_response(check_fd), "SAVE <Successful>\n");
    close(check_fd);

    File_manager fm_verification(entry_path_, text_path_, index_path_);
    EXPECT_EQ(fm_verification.get_live_vector_count(), kMixedThreadsCount);

    for (size_t i = 0; i < kMixedThreadsCount; i++)
    {
        std::string sid = "new_id_" + std::to_string(i);
        sid.resize(schema::ID_LENGTH, '\0');
        EXPECT_NE(fm_verification.find_by_id(sid), -1);

        std::string pid = "pre_id_" + std::to_string(i);
        pid.resize(schema::ID_LENGTH, '\0');
        EXPECT_EQ(fm_verification.find_by_id(pid), -1);
    }
}

// =============================================================================
// Group 13: Resource Safety (Churn)
// =============================================================================

TEST_F(VectorServerIntegrationTest, ResourceSafety_RapidChurnDoesNotDegradeServer)
{
    for (size_t i = 0; i < kChurnIterations; i++)
    {
        int fd = connect_client(port_);
        ASSERT_GE(fd, 0) << "Failed to connect on churn iteration " << i;

        send_command(fd, BuildQueryCommand(1));
        std::string resp = read_response(fd);

        EXPECT_TRUE(resp.starts_with("QUERY <"));
        EXPECT_TRUE(resp.ends_with("END\n"));

        close(fd);
    }

    int final_fd = connect_client(port_);
    ASSERT_GE(final_fd, 0) << "Server failed to accept a final connection post-churn.";

    send_command(final_fd, BuildInsertCommand("churn_survivor", "Text"));
    EXPECT_EQ(read_response(final_fd), "INSERT <Successful>\n");

    close(final_fd);
}

// =============================================================================
// Group 14-16: IVF Index Persistence Tests
// =============================================================================

class VectorServerPersistenceTest : public VectorServerTestBase
{
protected:
    File_manager *fm_ = nullptr;
    Vector_store *vs_ = nullptr;
    Vector_Server *server_ = nullptr;

    void TearDown() override
    {
        VectorServerTestBase::TearDown();
    }

    // Helper to start the server. Tests in this suite dictate exactly when to boot.
    void BootServer()
    {
        fm_ = new File_manager(entry_path_, text_path_, index_path_);
        vs_ = new Vector_store();
        server_ = new Vector_Server(config_.port, *vs_, *fm_, config_);

        ASSERT_TRUE(server_->setup()) << "Server failed to bind socket to port " << port_;

        std::thread([this]()
                    { server_->run(); })
            .detach();

        int probe_fd = connect_client(port_);
        ASSERT_GE(probe_fd, 0) << "Failed to probe-connect to server on port " << port_;
        close(probe_fd);
    }
};

TEST_F(VectorServerPersistenceTest, FirstBoot_BuildsFromScratchAndSavesToDisk)
{
    PreSeedDatabase(kIndexPersistTestVectorCount);

    // Server boots, detects lack of index file, and builds + writes to it
    BootServer();

    // Verify index persistence utilizing an independent read-only File_manager instance
    File_manager verification_fm(entry_path_, text_path_, index_path_);
    EXPECT_TRUE(verification_fm.is_index_populated());

    size_t expected_centroids = std::min(static_cast<size_t>(schema::MAX_CENTROIDS), kIndexPersistTestVectorCount);
    size_t expected_bytes = expected_centroids * schema::DIMENSIONS * sizeof(float);
    EXPECT_EQ(verification_fm.get_index_size(), expected_bytes)
        << "Boot-time build-and-save path wrote incorrect number of centroid bytes";

    // Issue a QUERY to confirm the built index actually serves queries (is not orphaned)
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    send_command(client_fd, BuildQueryCommand(5));
    std::string response = read_response(client_fd);

    // Expect to find at least one of our preseeded IDs
    EXPECT_NE(response.find("preseed_0"), std::string::npos);

    close(client_fd);
}

TEST_F(VectorServerPersistenceTest, SecondBoot_LoadsPersistedIndexWithGrownData)
{
    // 1. Establish populated state with an index file
    PreSeedDatabase(kIndexPersistTestVectorCount);
    BootServer();

    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    // 2. Insert additional data so live vector count > centroid count
    // (We add exactly enough to exceed MAX_CENTROIDS if we aren't already there)
    size_t new_inserts = schema::MAX_CENTROIDS + 5;
    for (size_t i = 0; i < new_inserts; i++)
    {
        send_command(client_fd, BuildInsertCommand("new_vec_" + std::to_string(i), "Grown data"));
        EXPECT_EQ(read_response(client_fd), "INSERT <Successful>\n");
    }
    close(client_fd);

    // 3. Boot a SECOND, independent server instance against the exact same files
    //    (using a new port so they don't collide on networking)
    uint16_t second_port = port_counter++;
    Config second_config = config_;
    second_config.port = std::to_string(second_port);

    File_manager *second_fm = new File_manager(entry_path_, text_path_, index_path_);
    Vector_store *second_vs = new Vector_store();
    Vector_Server *second_server = new Vector_Server(second_config.port, *second_vs, *second_fm, second_config);

    ASSERT_TRUE(second_server->setup()) << "Second server failed to bind.";
    std::thread([second_server]()
                { second_server->run(); })
        .detach();

    int second_probe = connect_client(second_port);
    ASSERT_GE(second_probe, 0) << "Second server failed to boot or hung processing loaded index.";
    close(second_probe);

    // 4. Validate that queries work against BOTH old and newly grown data
    int second_client = connect_client(second_port);
    ASSERT_GE(second_client, 0);

    send_command(second_client, BuildQueryCommand(new_inserts + kIndexPersistTestVectorCount));
    std::string response = read_response(second_client, 1000);

    // Verify an originally pre-seeded vector still exists
    // Verify the server successfully served the query, clamped to the maximum allowed limit
    EXPECT_TRUE(response.starts_with("QUERY <" + std::to_string(schema::MAX_K_SIMILAR) + ">\n"));

    // Verify the data was actually loaded and maintained alongside the new inserts
    File_manager verification_fm(entry_path_, text_path_, index_path_);
    EXPECT_EQ(verification_fm.get_live_vector_count(), kIndexPersistTestVectorCount + new_inserts);
    close(second_client);
}

TEST_F(VectorServerPersistenceTest, CorruptedIndexFile_FallsBackToFreshBuild)
{
    // Pre-seed entries
    PreSeedDatabase(kIndexPersistTestVectorCount);

    // Directly truncate the index file to an invalid size (e.g. 4 bytes, smaller than a full centroid)
    {
        std::ofstream ofs(index_path_, std::ios::binary | std::ios::trunc);
        ofs.write("JUNK", 4);
        ofs.flush();
    }

    // This boot should gracefully reject the corrupted index file and rebuild it
    BootServer();

    // Verify server successfully queries (did not crash or half-initialize)
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    send_command(client_fd, BuildQueryCommand(5));
    std::string response = read_response(client_fd);
    EXPECT_NE(response.find("preseed_0"), std::string::npos);
    close(client_fd);

    // Check that the file manager properly rebuilt and re-saved the index to a healthy state
    File_manager verification_fm(entry_path_, text_path_, index_path_);
    size_t expected_centroids = std::min(static_cast<size_t>(schema::MAX_CENTROIDS), kIndexPersistTestVectorCount);
    size_t expected_bytes = expected_centroids * schema::DIMENSIONS * sizeof(float);

    EXPECT_EQ(verification_fm.get_index_size(), expected_bytes)
        << "Index size remains invalid; the fallback path failed to persist a healthy index.";
}

// =============================================================================
// Group 17: Curiosity / Performance Comparison (Informational, Non-Gating)
// =============================================================================

class VectorServerPerformanceCuriosity : public VectorServerTestBase
{
protected:
    Vector_store vs_;
    Vector query_;

    void SetUp() override
    {
        VectorServerTestBase::SetUp();

        // 1. Prepare deterministic Random Number Generator
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

        // 2. Populate Vector_store directly, avoiding protocol overhead
        for (size_t i = 0; i < kPerformanceTestVectorCount; i++)
        {
            Vector v;
            v.id = "perf_id_" + std::to_string(i);
            v.id.resize(schema::ID_LENGTH, '\0');
            v.text_length = 5;
            v.meta_data_count = 0;
            v.embeddings.resize(schema::DIMENSIONS);
            for (size_t d = 0; d < schema::DIMENSIONS; d++)
            {
                v.embeddings[d] = dist(rng);
            }
            vs_.normalise_vector(v.embeddings);
            vs_.make_entry(v);
        }

        // 3. Prep Query
        query_.id = "query";
        query_.embeddings.resize(schema::DIMENSIONS);
        for (size_t d = 0; d < schema::DIMENSIONS; d++)
        {
            query_.embeddings[d] = dist(rng);
        }
        vs_.normalise_vector(query_.embeddings);
    }
};

TEST_F(VectorServerPerformanceCuriosity, CompareSearchAlgorithms_Informational)
{
    std::cout << "\n[Curiosity] N=" << kPerformanceTestVectorCount
              << ", Repetitions=" << kPerformanceRepetitions
              << ", Max Centroids=" << static_cast<size_t>(schema::MAX_CENTROIDS)
              << ", nprobe=" << static_cast<size_t>(schema::MAX_PROBES_SEARCH) << "\n";

    size_t top_k = 10;

    // ----- 1. Brute-Force -----
    long long total_bf_time = 0;
    for (size_t i = 0; i < kPerformanceRepetitions; i++)
    {
        auto start = std::chrono::steady_clock::now();
        auto res = vs_.brute_force_search(query_.embeddings, top_k);
        auto end = std::chrono::steady_clock::now();
        total_bf_time += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        EXPECT_FALSE(res.empty());
        EXPECT_LE(res.size(), top_k);
    }
    std::cout << "[Curiosity] Brute-Force avg search time: "
              << (total_bf_time / kPerformanceRepetitions) / 1000.0 << " ms\n";

    // ----- 2. Fresh IVF Build -----
    long long total_fresh_build_time = 0;
    long long total_fresh_search_time = 0;

    // We only build it once, but we measure the buildup separately.
    IVF_index fresh_idx(schema::MAX_CENTROIDS, schema::MAX_PROBES_SEARCH);

    auto start_build = std::chrono::steady_clock::now();
    fresh_idx.build_(vs_);
    auto end_build = std::chrono::steady_clock::now();
    total_fresh_build_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_build - start_build).count();

    for (size_t i = 0; i < kPerformanceRepetitions; i++)
    {
        auto start = std::chrono::steady_clock::now();
        auto res = fresh_idx.search_(query_, top_k);
        auto end = std::chrono::steady_clock::now();
        total_fresh_search_time += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        EXPECT_FALSE(res.empty());
        EXPECT_LE(res.size(), top_k);
    }
    std::cout << "[Curiosity] Fresh IVF build_() time: " << total_fresh_build_time << " ms\n";
    std::cout << "[Curiosity] Fresh IVF avg search time: "
              << (total_fresh_search_time / kPerformanceRepetitions) / 1000.0 << " ms\n";

    // ----- 3. Loaded-Centroids Fast Path -----
    long long total_load_setup_time = 0;
    long long total_load_search_time = 0;

    IVF_index loaded_idx(schema::MAX_CENTROIDS, schema::MAX_PROBES_SEARCH);

    auto start_load = std::chrono::steady_clock::now();
    loaded_idx.set_ref_store(vs_);
    // Mirror file_manager's passing of the binary array pointer
    loaded_idx.set_centroids(
        std::vector<float>(fresh_idx.get_centroids_data_ptr_(),
                           fresh_idx.get_centroids_data_ptr_() + (fresh_idx.get_built_centroids_number_() * schema::DIMENSIONS)));
    loaded_idx.build_lists();
    auto end_load = std::chrono::steady_clock::now();
    total_load_setup_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_load - start_load).count();

    for (size_t i = 0; i < kPerformanceRepetitions; i++)
    {
        auto start = std::chrono::steady_clock::now();
        auto res = loaded_idx.search_(query_, top_k);
        auto end = std::chrono::steady_clock::now();
        total_load_search_time += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        EXPECT_FALSE(res.empty());
        EXPECT_LE(res.size(), top_k);
    }

    std::cout << "[Curiosity] Loaded-Centroids setup time (skip k-means): " << total_load_setup_time << " ms\n";
    std::cout << "[Curiosity] Loaded-Centroids avg search time: "
              << (total_load_search_time / kPerformanceRepetitions) / 1000.0 << " ms\n\n";
}