#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include "command_parser.h"
#include "schema.hpp"
#include "types.h"

// -----------------------------------------------------------------------------
// Test Fixture: String Builders and Helpers
// -----------------------------------------------------------------------------
class CommandParserTest : public ::testing::Test
{
protected:
    // Deterministic float generator for embeddings (e.g., 0.0, 0.001, 0.002...)
    std::string GenerateFloats(int count)
    {
        std::ostringstream oss;
        for (int i = 0; i < count; ++i)
        {
            oss << (i * 0.001f);
            if (i != count - 1)
                oss << " ";
        }
        return oss.str();
    }

    std::string BuildValidInsertCommand(const std::string &id = "valid_id",
                                        const std::string &dims = "1024",
                                        const std::vector<std::pair<std::string, std::string>> &meta = {},
                                        int num_floats = 1024)
    {
        std::ostringstream oss;
        oss << "INSERT " << id << " " << dims;
        for (const auto &kv : meta)
        {
            oss << " " << kv.first << "=" << kv.second;
        }
        if (num_floats > 0)
        {
            oss << " " << GenerateFloats(num_floats);
        }
        return oss.str();
    }

    std::string BuildValidQueryCommand(const std::string &top_k = "10",
                                       const std::string &dims = "1024",
                                       const std::vector<std::pair<std::string, std::string>> &meta = {},
                                       int num_floats = 1024)
    {
        std::ostringstream oss;
        oss << "QUERY " << top_k << " " << dims;
        for (const auto &kv : meta)
        {
            oss << " " << kv.first << "=" << kv.second;
        }
        if (num_floats > 0)
        {
            oss << " " << GenerateFloats(num_floats);
        }
        return oss.str();
    }
};

// =============================================================================
// insert_parsing Tests
// =============================================================================

TEST_F(CommandParserTest, InsertParsing_MalformedCommandWord_ReturnsError)
{
    Vector v;
    std::string cmd = "INSRT valid_id 1024 " + GenerateFloats(1024);
    Parse_result res = insert_parsing(v, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Incorrect format for 'INSERT'>\n");
}

TEST_F(CommandParserTest, InsertParsing_IdSizeZero_ReturnsError)
{
    Vector v;
    std::string cmd = "INSERT  1024 " + GenerateFloats(1024); // Double space = empty ID
    Parse_result res = insert_parsing(v, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Id size can not be zero>\n");
}

TEST_F(CommandParserTest, InsertParsing_IdExactly32Chars_Succeeds)
{
    Vector v;
    std::string exact_id(32, 'a');
    std::string cmd = BuildValidInsertCommand(exact_id);
    Parse_result res = insert_parsing(v, cmd);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(v.id, exact_id);
}

TEST_F(CommandParserTest, InsertParsing_IdExceeds32Chars_ReturnsError)
{
    Vector v;
    std::string long_id(33, 'a');
    std::string cmd = BuildValidInsertCommand(long_id);
    Parse_result res = insert_parsing(v, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Id size can not greater than 32>\n");
}

TEST_F(CommandParserTest, InsertParsing_DimsWrongDigitCount_ReturnsError)
{
    Vector v;
    std::string cmd = BuildValidInsertCommand("id", "99"); // 2 digits instead of 4
    Parse_result res = insert_parsing(v, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Incorrect 'Dimension' value entered>\n");
}

TEST_F(CommandParserTest, InsertParsing_DimsCorrectDigitCountWrongValue_ReturnsError)
{
    Vector v;
    std::string cmd = BuildValidInsertCommand("id", "0999"); // 4 digits, but not 1024
    Parse_result res = insert_parsing(v, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Invalid dimensions entered\n>");
}

TEST_F(CommandParserTest, InsertParsing_MetaKeyEmpty_ReturnsError)
{
    Vector v;
    std::string cmd = BuildValidInsertCommand("id", "1024", {{"", "val"}});
    Parse_result res = insert_parsing(v, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR<Meta-data key size is 0>\n");
}

TEST_F(CommandParserTest, InsertParsing_MetaKeyExceeds32Chars_ReturnsError)
{
    Vector v;
    std::string long_key(33, 'k');
    std::string cmd = BuildValidInsertCommand("id", "1024", {{long_key, "val"}});
    Parse_result res = insert_parsing(v, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR<Meta-data key size exceeds 32>\n");
}

TEST_F(CommandParserTest, InsertParsing_MetaValueEmpty_ReturnsError)
{
    Vector v;
    std::string cmd = BuildValidInsertCommand("id", "1024", {{"key", ""}});
    Parse_result res = insert_parsing(v, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR<Meta-data value size is 0>\n");
}

TEST_F(CommandParserTest, InsertParsing_MetaValueExceeds32Chars_ReturnsError)
{
    Vector v;
    std::string long_val(33, 'v');
    std::string cmd = BuildValidInsertCommand("id", "1024", {{"key", long_val}});
    Parse_result res = insert_parsing(v, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR<Meta-data value size exceeds 32>\n");
}

TEST_F(CommandParserTest, InsertParsing_MetaZeroPairs_Succeeds)
{
    Vector v;
    std::string cmd = BuildValidInsertCommand("id", "1024", {});
    Parse_result res = insert_parsing(v, cmd);
    EXPECT_TRUE(res.success);
}

TEST_F(CommandParserTest, InsertParsing_MetaExactly3Pairs_Succeeds)
{
    Vector v;
    std::vector<std::pair<std::string, std::string>> meta = {
        {"k1", "v1"}, {"k2", "v2"}, {"k3", "v3"}};
    std::string cmd = BuildValidInsertCommand("id", "1024", meta);
    Parse_result res = insert_parsing(v, cmd);
    EXPECT_TRUE(res.success);
}

TEST_F(CommandParserTest, InsertParsing_MetadataMultipleEquals_Succeeds_DocumentedBehavior)
{
    Vector v;
    // Unlike query_parsing, insert_parsing doesn't explicitly reject multiple '='.
    // It captures up to the first '=', then takes the rest as the value.
    std::string cmd = BuildValidInsertCommand("id", "1024", {{"key", "val=ue"}});
    Parse_result res = insert_parsing(v, cmd);
    EXPECT_TRUE(res.success);
    EXPECT_STREQ(v.metadata[0].key, "key");
    EXPECT_STREQ(v.metadata[0].value, "val=ue");
}

TEST_F(CommandParserTest, InsertParsing_EmbeddingsFewerThan1024_ReturnsError)
{
    Vector v;
    std::string cmd = BuildValidInsertCommand("id", "1024", {}, 1023); // Missing 1 float
    Parse_result res = insert_parsing(v, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Float values do not match the dimenstions>\n");
}

TEST_F(CommandParserTest, InsertParsing_EmbeddingsNonNumeric_ReturnsError)
{
    Vector v;
    std::string cmd = "INSERT id 1024 abc " + GenerateFloats(1023); // Bad leading float
    Parse_result res = insert_parsing(v, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Parsing failed: Non-numeric value encountered>\n");
}

TEST_F(CommandParserTest, InsertParsing_ExtraTrailingFloats_SilentlyTruncates_DocumentedBehavior)
{
    Vector v;
    // Generate 1030 floats. stof() parsing the last valid index will ignore trailing space/data.
    std::string cmd = BuildValidInsertCommand("id", "1024", {}, 1030);
    Parse_result res = insert_parsing(v, cmd);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(v.data.size(), schema::DIMENSIONS);
    // Verify the 1024th element strictly extracted the correct float
    EXPECT_FLOAT_EQ(v.data[1023], 1023 * 0.001f);
}

TEST_F(CommandParserTest, InsertParsing_FullVerification_Success)
{
    Vector v;
    std::vector<std::pair<std::string, std::string>> meta = {
        {"meta_k1", "meta_v1"}, {"k2", "v2"}, {"long_key_name_3", "long_value_name_3"}};
    std::string cmd = BuildValidInsertCommand("exact_32_char_id_string_12345678", "1024", meta, 1024);
    Parse_result res = insert_parsing(v, cmd);

    EXPECT_TRUE(res.success);
    EXPECT_EQ(v.id, "exact_32_char_id_string_12345678");
    EXPECT_EQ(v.dims, schema::DIMENSIONS);

    EXPECT_STREQ(v.metadata[0].key, "meta_k1");
    EXPECT_STREQ(v.metadata[0].value, "meta_v1");
    EXPECT_STREQ(v.metadata[1].key, "k2");
    EXPECT_STREQ(v.metadata[1].value, "v2");
    EXPECT_STREQ(v.metadata[2].key, "long_key_name_3");
    EXPECT_STREQ(v.metadata[2].value, "long_value_name_3");

    ASSERT_EQ(v.data.size(), schema::DIMENSIONS);
    for (size_t i = 0; i < schema::DIMENSIONS; i++)
    {
        EXPECT_FLOAT_EQ(v.data[i], i * 0.001f);
    }
}

// =============================================================================
// query_parsing Tests
// =============================================================================

TEST_F(CommandParserTest, QueryParsing_MalformedCommandWord_ReturnsError)
{
    Vector v;
    size_t top_k;
    std::string cmd = "QURY 10 1024 " + GenerateFloats(1024);
    Parse_result res = query_parsing(v, top_k, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Incorrect format for 'QUERY'>\n");
}

TEST_F(CommandParserTest, QueryParsing_TopKEmpty_ReturnsError)
{
    Vector v;
    size_t top_k;
    std::string cmd = "QUERY  1024 " + GenerateFloats(1024); // Double space = missing top_k
    Parse_result res = query_parsing(v, top_k, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Value of 'top_k' not provided>\n");
}

TEST_F(CommandParserTest, QueryParsing_TopKNegative_ReturnsError)
{
    Vector v;
    size_t top_k;
    std::string cmd = BuildValidQueryCommand("-5");
    Parse_result res = query_parsing(v, top_k, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <top_k must be a positive number>\n");
}

TEST_F(CommandParserTest, QueryParsing_TopKExceedsMax_SilentlyClamps_Succeeds)
{
    Vector v;
    size_t top_k;
    std::string cmd = BuildValidQueryCommand("999");
    Parse_result res = query_parsing(v, top_k, cmd);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(top_k, 30); // MAX_TOP_K is 30
}

TEST_F(CommandParserTest, QueryParsing_DimsWrongDigitCount_Succeeds_DocumentedBehavior)
{
    Vector v;
    size_t top_k;
    // query_parsing lacks the strict DIMENSIONS_NO_OF_DIGITS length check that insert_parsing uses.
    // "01024" evaluates to 1024 mathematically, bypassing the length check entirely.
    std::string cmd = BuildValidQueryCommand("10", "01024");
    Parse_result res = query_parsing(v, top_k, cmd);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(v.dims, schema::DIMENSIONS);
}

TEST_F(CommandParserTest, QueryParsing_MetadataMultipleEquals_ReturnsError)
{
    Vector v;
    size_t top_k;
    // Unlike insert_parsing, query_parsing strictly checks for multiple '='
    std::string cmd = BuildValidQueryCommand("10", "1024", {{"key", "val=ue"}});
    Parse_result res = query_parsing(v, top_k, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Metadata token 'key=val=ue' has multiple '='>\n");
}

TEST_F(CommandParserTest, QueryParsing_FewerThan3PairsFollowedByFloats_Succeeds)
{
    Vector v;
    size_t top_k;
    // Verifies lookahead logic correctly breaks the metadata loop and falls through to embedding loop
    std::string cmd = BuildValidQueryCommand("10", "1024", {{"key1", "val1"}});
    Parse_result res = query_parsing(v, top_k, cmd);
    EXPECT_TRUE(res.success);
    EXPECT_STREQ(v.metadata[0].key, "key1");
    EXPECT_STREQ(v.metadata[0].value, "val1");
    // Verify embedding loop picked up at the right spot
    EXPECT_FLOAT_EQ(v.data[0], 0.0f);
}

TEST_F(CommandParserTest, QueryParsing_FullVerification_Success)
{
    Vector v;
    size_t top_k;
    std::vector<std::pair<std::string, std::string>> meta = {
        {"qk1", "qv1"}, {"qk2", "qv2"}, {"qk3", "qv3"}};
    std::string cmd = BuildValidQueryCommand("15", "1024", meta, 1024);
    Parse_result res = query_parsing(v, top_k, cmd);

    EXPECT_TRUE(res.success);
    EXPECT_EQ(top_k, 15);
    EXPECT_EQ(v.dims, schema::DIMENSIONS);
    EXPECT_EQ(v.id, ""); // Query does not populate ID

    EXPECT_STREQ(v.metadata[0].key, "qk1");
    EXPECT_STREQ(v.metadata[0].value, "qv1");
    EXPECT_STREQ(v.metadata[1].key, "qk2");
    EXPECT_STREQ(v.metadata[1].value, "qv2");
    EXPECT_STREQ(v.metadata[2].key, "qk3");
    EXPECT_STREQ(v.metadata[2].value, "qv3");

    ASSERT_EQ(v.data.size(), schema::DIMENSIONS);
    for (size_t i = 0; i < schema::DIMENSIONS; i++)
    {
        EXPECT_FLOAT_EQ(v.data[i], i * 0.001f);
    }
}

// =============================================================================
// delete_parsing Tests
// =============================================================================

TEST_F(CommandParserTest, DeleteParsing_MalformedCommandWord_ReturnsError)
{
    std::string id;
    std::string cmd = "DELET valid_id";
    Parse_result res = delete_parsing(id, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Incorrect format for 'DELETE'>\n");
}

TEST_F(CommandParserTest, DeleteParsing_IdSizeZero_ReturnsError)
{
    std::string id;
    std::string cmd = "DELETE ";
    Parse_result res = delete_parsing(id, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Id size can not be zero>\n");
}

TEST_F(CommandParserTest, DeleteParsing_IdExceeds32Chars_ReturnsError)
{
    std::string id;
    std::string cmd = "DELETE " + std::string(33, 'x');
    Parse_result res = delete_parsing(id, cmd);
    EXPECT_FALSE(res.success);
    // Note: The source code in delete_parsing lacks a space after ERROR
    EXPECT_EQ(res.message, "ERROR<Id size can not greater than 32>\n");
}

TEST_F(CommandParserTest, DeleteParsing_IdExactly32Chars_Succeeds)
{
    std::string id_out;
    std::string exact_id(32, 'a');
    std::string cmd = "DELETE " + exact_id;
    Parse_result res = delete_parsing(id_out, cmd);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(id_out, exact_id);
}

TEST_F(CommandParserTest, DeleteParsing_ShortId_Succeeds)
{
    std::string id_out;
    std::string cmd = "DELETE abcde";
    Parse_result res = delete_parsing(id_out, cmd);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(id_out, "abcde");
}

// =============================================================================
// save_parsing Tests
// =============================================================================

TEST_F(CommandParserTest, SaveParsing_State0_CleanSave_Succeeds)
{
    std::string cmd = "SAVE";
    Parse_result res = save_parsing(cmd, 0);
    EXPECT_TRUE(res.success);
}

TEST_F(CommandParserTest, SaveParsing_State0_SpacesStripped_Succeeds_DocumentedBehavior)
{
    std::string cmd = "S A V E";
    // save_parsing permanently mutates `cmd` and wipes spaces before length checking
    Parse_result res = save_parsing(cmd, 0);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(cmd, "SAVE");
}

TEST_F(CommandParserTest, SaveParsing_State0_WrongLength_ReturnsError)
{
    std::string cmd = "SAV";
    Parse_result res = save_parsing(cmd, 0);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Invalid format for SAVE>\n");
}

TEST_F(CommandParserTest, SaveParsing_State1_CleanLoad_Succeeds)
{
    std::string cmd = "LOAD";
    Parse_result res = save_parsing(cmd, 1);
    EXPECT_TRUE(res.success);
}

TEST_F(CommandParserTest, SaveParsing_State1_WrongLength_ReturnsError)
{
    std::string cmd = "LOD";
    Parse_result res = save_parsing(cmd, 1);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Invalid format for LOAD>\n");
}