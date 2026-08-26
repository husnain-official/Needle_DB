#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <cmath>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "vector_server.h"
#include "vector_store.h"
#include "file_manager.h"
#include "schema.hpp"
#include "types.h"

// Global port counter to prevent cross-test socket collisions during parallel or rapid sequential runs.
// Note: This relies on these high ephemeral ports being available on the host machine.
static std::atomic<uint16_t> port_counter{19000};

// -----------------------------------------------------------------------------
// Test Fixture: End-to-End Server TCP Integration Setup
// -----------------------------------------------------------------------------
class VectorServerIntegrationTest : public ::testing::Test
{
protected:
    std::string entry_path_;
    std::string text_path_;
    uint16_t port_;
    Config config_;

    std::unique_ptr<File_manager> fm_;
    std::unique_ptr<Vector_store> vs_;
    std::unique_ptr<Vector_Server> server_;
    std::thread server_thread_; // FIX: Member thread to manage lifecycle

    void SetUp() override
    {
        const ::testing::TestInfo *test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string test_name = test_info->name();

        entry_path_ = "./temp_" + test_name + "_entry.vdb";
        text_path_ = "./temp_" + test_name + "_text.vdb";
        port_ = port_counter++;

        CleanUpFiles();

        config_.port = std::to_string(port_);
        config_.vecdb_entry_file_path = entry_path_;
        config_.vecdb_text_file_path = text_path_;

        fm_ = std::make_unique<File_manager>(entry_path_, text_path_);
        vs_ = std::make_unique<Vector_store>();
        server_ = std::make_unique<Vector_Server>(config_.port, *vs_, *fm_, config_);

        ASSERT_TRUE(server_->setup()) << "Server failed to bind socket to port " << port_;

        // FIX: Store the thread and join it later to prevent use-after-free
        // and the resulting "accept: Bad file descriptor" spam loop.
        server_thread_ = std::thread([this]()
                                     { server_->run(); });

        // Probe-connect to ensure the server thread has reached listen() before we proceed
        int probe_fd = connect_client(port_);
        ASSERT_GE(probe_fd, 0) << "Failed to probe-connect to server on port " << port_;
        close(probe_fd);
    }

    void TearDown() override
    {
        if (server_)
        {
            server_->stop(); // Closes server_fd, interrupting the block
        }

        // FIX: Ensure the thread cleanly exits before the fixture destructs
        if (server_thread_.joinable())
        {
            server_thread_.join();
        }

        CleanUpFiles();
    }

    void CleanUpFiles()
    {
        std::error_code ec;
        std::filesystem::remove(entry_path_, ec);
        std::filesystem::remove(text_path_, ec);
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
            {
                return fd;
            }
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > timeout_ms)
            {
                break;
            }
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

    // Accumulates bytes until no more arrive within the timeout window.
    // Tradeoff: This guarantees we capture varied framing (like QUERY's multi-line output),
    // but makes every read call wait exactly `timeout_ms` at the tail end.
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
            {
                response.append(buffer, n);
            }
            else
            {
                break; // Timeout reached or connection closed
            }
        }
        return response;
    }

    std::string GenerateNormalizedFloats()
    {
        std::ostringstream oss;
        // 1.0 / sqrt(DIMENSIONS) ensures L2 magnitude is exactly 1.0 (normalized)
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

    EXPECT_EQ(response, "OK\n");
    close(client_fd);
}

TEST_F(VectorServerIntegrationTest, Insert_DuplicateId_ReturnsWarning)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    send_command(client_fd, BuildInsertCommand("id_dup", "Text A"));
    EXPECT_EQ(read_response(client_fd), "OK\n");

    send_command(client_fd, BuildInsertCommand("id_dup", "Text B"));
    std::string response = read_response(client_fd);

    EXPECT_NE(response, "OK\n");
    EXPECT_NE(response.find("WARNING <Id already exists"), std::string::npos);

    close(client_fd);
}

TEST_F(VectorServerIntegrationTest, Insert_ValidCommand_PersistsToRAMAndQueryable)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    send_command(client_fd, BuildInsertCommand("id_find_me", "Data here"));
    EXPECT_EQ(read_response(client_fd), "OK\n");

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
    EXPECT_EQ(read_response(client_fd), "OK\n");
    close(client_fd);

    // Give OS brief moment to flush file streams natively via File_manager
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Construct a secondary File_manager to prove persistence independent of the server's RAM
    File_manager fm_verification(entry_path_, text_path_);

    std::string search_id = "id_disk_test";
    search_id.resize(schema::ID_LENGTH, '\0');
    EXPECT_NE(fm_verification.find_by_id(search_id), -1);
}

TEST_F(VectorServerIntegrationTest, Insert_MalformedCommand_ReturnsParserError)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    std::ostringstream oss;
    oss << "INSERT id_bad 4 text 9999 " << GenerateNormalizedFloats() << "\n"; // 9999 instead of expected dims

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
    EXPECT_EQ(read_response(client_fd), "OK\n");

    send_command(client_fd, BuildQueryCommand(1));
    std::string response = read_response(client_fd);

    // Format assertion checks
    EXPECT_TRUE(response.starts_with("QUERY <1>\n"));
    EXPECT_TRUE(response.ends_with("END\n"));

    // Direct regression check: The text payload must exist in the result stream
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

    // Search strictly for target metadata
    send_command(client_fd, BuildQueryCommand(5, "tag=target"));
    std::string response = read_response(client_fd);

    EXPECT_NE(response.find("id_meta"), std::string::npos);
    EXPECT_EQ(response.find("id_no_meta"), std::string::npos); // Excluded

    close(client_fd);
}

TEST_F(VectorServerIntegrationTest, Query_TopKExceedsMax_ClampsToMax)
{
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);

    size_t requested_k = schema::MAX_K_SIMILAR + 10;
    send_command(client_fd, BuildQueryCommand(requested_k));
    std::string response = read_response(client_fd);

    std::string expected_header = "QUERY <" + std::to_string(schema::MAX_K_SIMILAR) + ">\n";
    EXPECT_TRUE(response.starts_with(expected_header))
        << "Server failed to clamp top_k response header to schema::MAX_K_SIMILAR";

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
    EXPECT_EQ(read_response(client_fd), "OK\n");

    send_command(client_fd, "DELETE id_to_delete\n");
    EXPECT_EQ(read_response(client_fd), "OK\n");

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

    // Insert 5
    for (size_t i = 0; i < 5; i++)
    {
        send_command(client_fd, BuildInsertCommand("mid_id_" + std::to_string(i), "Txt"));
        EXPECT_EQ(read_response(client_fd), "OK\n");
    }

    // Delete index 2 (Middle)
    send_command(client_fd, "DELETE mid_id_2\n");
    EXPECT_EQ(read_response(client_fd), "OK\n");

    // Query 0, 1, 3, 4 individually
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
    EXPECT_EQ(read_response(client_fd), "OK\n");

    send_command(client_fd, "SAVE\n");
    EXPECT_EQ(read_response(client_fd), "OK\n");
    close(client_fd);

    File_manager fm_verification(entry_path_, text_path_);
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
    EXPECT_EQ(read_response(client_fd), "OK\n");

    send_command(client_fd, BuildQueryCommand(5));
    std::string response = read_response(client_fd);

    // id_load_1 was deleted before the LOAD. LOAD pulls from disk, which holds the deletion flag.
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

    // Send concatenated in one socket call
    send_command(client_fd, cmd1 + cmd2);

    // Wait for the combined response
    std::string response = read_response(client_fd);

    EXPECT_NE(response.find("OK\n"), std::string::npos);
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

    // Send first half
    send(client_fd, part1.c_str(), part1.size(), 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Send second half
    send(client_fd, part2.c_str(), part2.size(), 0);

    std::string response = read_response(client_fd);
    EXPECT_EQ(response, "OK\n");

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