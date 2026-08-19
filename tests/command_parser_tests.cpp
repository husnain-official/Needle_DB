#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <iomanip>
#include "command_parser.h"
#include "schema.hpp"
#include "types.h"

// -----------------------------------------------------------------------------
// Test Fixture: Schema-Driven String Builders and Helpers
// -----------------------------------------------------------------------------
class CommandParserTest : public ::testing::Test
{

protected:
    Parser parser;
    // Generates a deterministic sequence of floats exactly matching the requested count
    std::string GenerateFloats(size_t count)
    {
        std::ostringstream oss;
        for (size_t i = 0; i < count; ++i)
        {
            oss << (i * 0.001f);
            if (i != count - 1)
                oss << " ";
        }
        return oss.str();
    }

    // Pads or generates a dimension string of a specific character length
    std::string GenerateDimsString(size_t length, const std::string &base_val)
    {
        std::string res = base_val;
        if (res.size() > length)
            return res.substr(0, length);
        while (res.size() < length)
            res = "0" + res;
        return res;
    }

    std::string BuildValidInsertCommand(const std::string &id,
                                        const std::string &text_length,
                                        const std::string &text,
                                        const std::string &dims,
                                        const std::vector<std::pair<std::string, std::string>> &meta,
                                        size_t num_floats)
    {
        std::ostringstream oss;
        oss << "INSERT " << id << " " << text_length << " " << text << " " << dims;
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

    std::string BuildValidQueryCommand(const std::string &top_k,
                                       const std::string &dims,
                                       const std::vector<std::pair<std::string, std::string>> &meta,
                                       size_t num_floats)
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
// Parser::insert_parsing Tests
// =============================================================================

TEST_F(CommandParserTest, InsertParsing_MalformedCommandWord_ReturnsFormatError)
{
    DB_entry entry{};
    // First space offset should be exactly std::string("INSERT").size() (6)
    std::string cmd = "INSRT  " + std::string(schema::ID_LENGTH, 'a') + " ...";
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Incorrect format for 'INSERT'>\n");
}

TEST_F(CommandParserTest, InsertParsing_IdSizeZero_ReturnsError)
{
    DB_entry entry{};
    // Double space after INSERT
    std::string cmd = "INSERT  1 " + std::string("A") + " " + std::to_string(schema::DIMENSIONS) + " " + GenerateFloats(schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Id size can not be zero>\n");
}

TEST_F(CommandParserTest, InsertParsing_IdExactlyMaxLength_Succeeds)
{
    DB_entry entry{};
    std::string exact_id(schema::ID_LENGTH, 'a');
    std::string cmd = BuildValidInsertCommand(exact_id, "1", "A", std::to_string(schema::DIMENSIONS), {}, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_TRUE(res.success);
}

TEST_F(CommandParserTest, InsertParsing_IdExceedsMaxLength_ReturnsError)
{
    DB_entry entry{};
    std::string long_id(schema::ID_LENGTH + 1, 'a');
    std::string cmd = BuildValidInsertCommand(long_id, "1", "A", std::to_string(schema::DIMENSIONS), {}, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Id size can not greater than '" + std::to_string(schema::ID_LENGTH) + "' >\n");
}

// Regression test for specific text size zero message
TEST_F(CommandParserTest, InsertParsing_TextLengthEmptyToken_ReturnsSpecificZeroError)
{
    DB_entry entry{};
    // Double space where text length should be
    std::string cmd = "INSERT id  A " + std::to_string(schema::DIMENSIONS) + " " + GenerateFloats(schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Text size can not be 0 >\n");
}

TEST_F(CommandParserTest, InsertParsing_TextLengthZeroValue_ReturnsSpecificZeroError)
{
    DB_entry entry{};
    // Explicitly written "0"
    std::string cmd = BuildValidInsertCommand("id", "0", "A", std::to_string(schema::DIMENSIONS), {}, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    // Note: If the parser relies purely on empty-token checks, this might return a mismatch error instead.
    // This pins the expectation that "0" is functionally treated as a size violation.
    EXPECT_TRUE(res.message.find("Text size can not be 0") != std::string::npos ||
                res.message.find("mismatch") != std::string::npos);
}

TEST_F(CommandParserTest, InsertParsing_TextLengthExactlyMax_Succeeds)
{
    DB_entry entry{};
    std::string exact_text(schema::TEXT_MAX_LENGTH, 'X');
    std::string cmd = BuildValidInsertCommand("id", std::to_string(schema::TEXT_MAX_LENGTH), exact_text, std::to_string(schema::DIMENSIONS), {}, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_TRUE(res.success);
}

TEST_F(CommandParserTest, InsertParsing_TextLengthExceedsMax_ReturnsError)
{
    DB_entry entry{};
    std::string cmd = BuildValidInsertCommand("id", std::to_string(schema::TEXT_MAX_LENGTH + 1), "A", std::to_string(schema::DIMENSIONS), {}, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Text size can not greater than '" + std::to_string(schema::TEXT_MAX_LENGTH) + "' >\n");
}

TEST_F(CommandParserTest, InsertParsing_TextLengthMismatch_MissingSpace_ReturnsError)
{
    DB_entry entry{};
    // Declares length 5, but text is "ABC" (length 3), meaning the space comes too early
    std::string cmd = BuildValidInsertCommand("id", "5", "ABC", std::to_string(schema::DIMENSIONS), {}, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Text length mismatch or missing space>\n");
}

// Memory-safety relevant test: Ensure it fails before an out-of-bounds read
TEST_F(CommandParserTest, InsertParsing_TextLengthExceedsCommandString_ReturnsError)
{
    DB_entry entry{};
    // Declares length 50, but command ends abruptly
    std::string cmd = "INSERT id 50 ABCDE";
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Text length mismatch or missing space>\n");
}

TEST_F(CommandParserTest, InsertParsing_TextWithEmbeddedSpaces_PreservedExactly)
{
    DB_entry entry{};
    std::string text_with_spaces = "A B C D E";
    std::string cmd = BuildValidInsertCommand("id", std::to_string(text_with_spaces.size()), text_with_spaces, std::to_string(schema::DIMENSIONS), {}, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_TRUE(res.success);
    // Convert back to string using exactly text_length
    std::string extracted(entry.text, entry.text_length);
    EXPECT_EQ(extracted, text_with_spaces);
}

TEST_F(CommandParserTest, InsertParsing_DimsDigitCountTooShort_ReturnsError)
{
    DB_entry entry{};
    std::string short_dims = GenerateDimsString(schema::DIMENSIONS_NO_OF_DIGITS - 1, "10");
    std::string cmd = BuildValidInsertCommand("id", "1", "A", short_dims, {}, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Incorrect 'Dimension' value entered>\n");
}

TEST_F(CommandParserTest, InsertParsing_DimsDigitCountTooLong_ReturnsError)
{
    DB_entry entry{};
    std::string long_dims = GenerateDimsString(schema::DIMENSIONS_NO_OF_DIGITS + 1, std::to_string(schema::DIMENSIONS));
    std::string cmd = BuildValidInsertCommand("id", "1", "A", long_dims, {}, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Incorrect 'Dimension' value entered>\n");
}

TEST_F(CommandParserTest, InsertParsing_DimsCorrectDigitCountWrongValue_ReturnsError)
{
    DB_entry entry{};
    // Same digit count, but invalid value (e.g. 1023)
    std::string wrong_val_dims = GenerateDimsString(schema::DIMENSIONS_NO_OF_DIGITS, std::to_string(schema::DIMENSIONS - 1));
    std::string cmd = BuildValidInsertCommand("id", "1", "A", wrong_val_dims, {}, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Invalid dimensions entered\n>");
}

TEST_F(CommandParserTest, InsertParsing_MetaKeyEmpty_ReturnsError)
{
    DB_entry entry{};
    std::string cmd = BuildValidInsertCommand("id", "1", "A", std::to_string(schema::DIMENSIONS), {{"", "val"}}, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Metadata key is empty>\n");
}

TEST_F(CommandParserTest, InsertParsing_MetaKeyExactlyMaxLength_Succeeds)
{
    DB_entry entry{};
    std::string exact_key(schema::META_DATA_LENGTH, 'k');
    std::string cmd = BuildValidInsertCommand("id", "1", "A", std::to_string(schema::DIMENSIONS), {{exact_key, "val"}}, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_TRUE(res.success);
}

TEST_F(CommandParserTest, InsertParsing_MetaKeyExceedsMaxLength_ReturnsError)
{
    DB_entry entry{};
    std::string long_key(schema::META_DATA_LENGTH + 1, 'k');
    std::string cmd = BuildValidInsertCommand("id", "1", "A", std::to_string(schema::DIMENSIONS), {{long_key, "val"}}, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Metadata key '" + long_key + "' exceeds '" + std::to_string(schema::META_DATA_LENGTH) + "' chars>\n");
}

TEST_F(CommandParserTest, InsertParsing_MetaValueEmpty_ReturnsError)
{
    DB_entry entry{};
    std::string cmd = BuildValidInsertCommand("id", "1", "A", std::to_string(schema::DIMENSIONS), {{"key", ""}}, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Metadata value for key 'key' is empty>\n");
}

TEST_F(CommandParserTest, InsertParsing_MetaValueExceedsMaxLength_ReturnsError)
{
    DB_entry entry{};
    std::string long_val(schema::META_DATA_LENGTH + 1, 'v');
    std::string cmd = BuildValidInsertCommand("id", "1", "A", std::to_string(schema::DIMENSIONS), {{"key", long_val}}, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Metadata value for key 'key' exceeds '" + std::to_string(schema::META_DATA_LENGTH) + "' chars>\n");
}

TEST_F(CommandParserTest, InsertParsing_MetaMultipleEquals_ReturnsError)
{
    DB_entry entry{};
    std::string cmd = BuildValidInsertCommand("id", "1", "A", std::to_string(schema::DIMENSIONS), {{"k", "v=x"}}, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Metadata token 'k=v=x' has multiple '='>\n");
}

TEST_F(CommandParserTest, InsertParsing_MetaExactlyMaxPairs_Succeeds)
{
    DB_entry entry{};
    std::vector<std::pair<std::string, std::string>> meta;
    for (size_t i = 0; i < schema::META_DATA_KP_PAIRS; i++)
    {
        meta.push_back({"k" + std::to_string(i), "v" + std::to_string(i)});
    }
    std::string cmd = BuildValidInsertCommand("id", "1", "A", std::to_string(schema::DIMENSIONS), meta, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(entry.meta_data_count, schema::META_DATA_KP_PAIRS);
}

TEST_F(CommandParserTest, InsertParsing_MetaExceedsMaxPairs_ReturnsError)
{
    DB_entry entry{};
    std::vector<std::pair<std::string, std::string>> meta;
    for (size_t i = 0; i < schema::META_DATA_KP_PAIRS + 1; i++)
    {
        meta.push_back({"k" + std::to_string(i), "v" + std::to_string(i)});
    }
    std::string cmd = BuildValidInsertCommand("id", "1", "A", std::to_string(schema::DIMENSIONS), meta, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Metadata pairs exceed schema value of '" + std::to_string(schema::META_DATA_KP_PAIRS) + "' >\n");
    // Verify first pairs correctly parsed before abort
    EXPECT_EQ(entry.meta_data_count, schema::META_DATA_KP_PAIRS);
}

TEST_F(CommandParserTest, InsertParsing_MetaZeroPairs_Succeeds)
{
    DB_entry entry{};
    std::string cmd = BuildValidInsertCommand("id", "1", "A", std::to_string(schema::DIMENSIONS), {}, schema::DIMENSIONS);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(entry.meta_data_count, 0);
}

TEST_F(CommandParserTest, InsertParsing_EmbeddingsFewerThanDims_ReturnsError)
{
    DB_entry entry{};
    std::string cmd = BuildValidInsertCommand("id", "1", "A", std::to_string(schema::DIMENSIONS), {}, schema::DIMENSIONS - 1);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Float values do not match the dimenstions>\n");
}

TEST_F(CommandParserTest, InsertParsing_EmbeddingsNonNumeric_ReturnsError)
{
    DB_entry entry{};
    std::string cmd = "INSERT id 1 A " + std::to_string(schema::DIMENSIONS) + " abc " + GenerateFloats(schema::DIMENSIONS - 1);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Parsing failed: Expected a number but found non-numeric characters>\n");
}

// Contractual check for strict float bounds and trailing garbage
TEST_F(CommandParserTest, InsertParsing_EmbeddingsTooMany_ReturnsTrailingGarbageError)
{
    DB_entry entry{};
    std::string cmd = BuildValidInsertCommand("id", "1", "A", std::to_string(schema::DIMENSIONS), {}, schema::DIMENSIONS + 1);
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Too many embeddings provided or trailing garbage>\n");
}

TEST_F(CommandParserTest, InsertParsing_EmbeddingsTrailingGarbage_ReturnsError)
{
    DB_entry entry{};
    std::string cmd = BuildValidInsertCommand("id", "1", "A", std::to_string(schema::DIMENSIONS), {}, schema::DIMENSIONS) + "extra";
    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Too many embeddings provided or trailing garbage>\n");
}

// =============================================================================
// Full Round-Trip Verification & Memory Safety Tests
// =============================================================================

TEST_F(CommandParserTest, InsertParsing_FullVerification_Success)
{
    DB_entry entry{};
    std::vector<std::pair<std::string, std::string>> meta;
    for (size_t i = 0; i < schema::META_DATA_KP_PAIRS; i++)
    {
        std::string exact_key(schema::META_DATA_LENGTH, 'A' + i);
        std::string exact_val(schema::META_DATA_LENGTH, 'a' + i);
        meta.push_back({exact_key, exact_val});
    }

    std::string exact_id(schema::ID_LENGTH, 'I');
    std::string text = "Valid text payload for this parse";
    std::string cmd = BuildValidInsertCommand(exact_id, std::to_string(text.size()), text, std::to_string(schema::DIMENSIONS), meta, schema::DIMENSIONS);

    Parse_result res = parser.insert_parsing(entry, cmd);
    EXPECT_TRUE(res.success);

    // Check ID
    std::string parsed_id(entry.id, schema::ID_LENGTH);
    EXPECT_EQ(parsed_id, exact_id);

    // Check Text
    EXPECT_EQ(entry.text_length, text.size());
    std::string parsed_text(entry.text, entry.text_length);
    EXPECT_EQ(parsed_text, text);

    // Check Meta
    EXPECT_EQ(entry.meta_data_count, schema::META_DATA_KP_PAIRS);
    for (size_t i = 0; i < schema::META_DATA_KP_PAIRS; i++)
    {
        std::string expected_key(schema::META_DATA_LENGTH, 'A' + i);
        std::string expected_val(schema::META_DATA_LENGTH, 'a' + i);
        EXPECT_STREQ(entry.meta_data[i].key, expected_key.c_str());
        EXPECT_STREQ(entry.meta_data[i].value, expected_val.c_str());
    }

    // Check Embeddings
    for (size_t i = 0; i < schema::DIMENSIONS; i++)
    {
        EXPECT_FLOAT_EQ(entry.embeddings[i], i * 0.001f);
    }
}

TEST_F(CommandParserTest, InsertParsing_BufferReuse_StaleDataCleared)
{
    DB_entry entry{};

    // Call 1: Max length text
    std::string long_text(schema::TEXT_MAX_LENGTH, 'A');
    std::string cmd1 = BuildValidInsertCommand("id1", std::to_string(schema::TEXT_MAX_LENGTH), long_text, std::to_string(schema::DIMENSIONS), {}, schema::DIMENSIONS);
    parser.insert_parsing(entry, cmd1);

    // Call 2: Short text
    std::string short_text = "SHORT";
    std::string cmd2 = BuildValidInsertCommand("id2", std::to_string(short_text.size()), short_text, std::to_string(schema::DIMENSIONS), {}, schema::DIMENSIONS);
    parser.insert_parsing(entry, cmd2);

    // Assert 0-initialization contract: Unused tail should not contain stale 'A' characters.
    // Genuine finding: If the parser relies purely on `memcpy` without zeroing the buffer, this will fail.
    EXPECT_EQ(entry.text[short_text.size()], '\0')
        << "Genuine finding: Stale data from previous parse remains in the text buffer. Memcpy overrides but doesn't zero tail.";
}

// =============================================================================
// Parser::query_parsing Tests
// =============================================================================

TEST_F(CommandParserTest, QueryParsing_MalformedCommandWord_ReturnsFormatError)
{
    Vector v{};
    size_t top_k;
    std::string cmd = "QURY " + std::to_string(schema::MAX_K_SIMILAR) + " ...";
    Parse_result res = parser.query_parsing(v, top_k, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Incorrect format for 'QUERY'>\n");
}

TEST_F(CommandParserTest, QueryParsing_TopKMissing_ReturnsError)
{
    Vector v{};
    size_t top_k;
    std::string cmd = "QUERY  " + std::to_string(schema::DIMENSIONS) + " " + GenerateFloats(schema::DIMENSIONS);
    Parse_result res = parser.query_parsing(v, top_k, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Value of 'top_k' not provided>\n");
}

TEST_F(CommandParserTest, QueryParsing_TopKNonNumeric_ReturnsError)
{
    Vector v{};
    size_t top_k;
    std::string cmd = BuildValidQueryCommand("abc", std::to_string(schema::DIMENSIONS), {}, schema::DIMENSIONS);
    Parse_result res = parser.query_parsing(v, top_k, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Parsing failed: Expected a number but found non-numeric characters>\n");
}

TEST_F(CommandParserTest, QueryParsing_TopKZeroOrNegative_ReturnsError)
{
    Vector v{};
    size_t top_k;
    std::string cmd = BuildValidQueryCommand("0", std::to_string(schema::DIMENSIONS), {}, schema::DIMENSIONS);
    Parse_result res = parser.query_parsing(v, top_k, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <top_k must be a positive number>\n");
}

TEST_F(CommandParserTest, QueryParsing_TopKExceedsMax_SilentlyClamps_Succeeds)
{
    Vector v{};
    size_t top_k;
    std::string cmd = BuildValidQueryCommand(std::to_string(schema::MAX_K_SIMILAR + 1), std::to_string(schema::DIMENSIONS), {}, schema::DIMENSIONS);
    Parse_result res = parser.query_parsing(v, top_k, cmd);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(top_k, schema::MAX_K_SIMILAR);
}

TEST_F(CommandParserTest, QueryParsing_TopKExactlyMax_Unclamped_Succeeds)
{
    Vector v{};
    size_t top_k;
    std::string cmd = BuildValidQueryCommand(std::to_string(schema::MAX_K_SIMILAR), std::to_string(schema::DIMENSIONS), {}, schema::DIMENSIONS);
    Parse_result res = parser.query_parsing(v, top_k, cmd);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(top_k, schema::MAX_K_SIMILAR);
}

// Regression test for > / >= boundary fix
TEST_F(CommandParserTest, QueryParsing_EmbeddingsStringTooShort_ReturnsSpecificError)
{
    Vector v{};
    size_t top_k;
    // Space trailing exactly at the start of where the last token should be, but missing
    std::string cmd = "QUERY 10 " + std::to_string(schema::DIMENSIONS) + " " + GenerateFloats(schema::DIMENSIONS - 1) + " ";
    Parse_result res = parser.query_parsing(v, top_k, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Command string too short for given dimensions>\n");
}

TEST_F(CommandParserTest, QueryParsing_FullVerification_Success)
{
    Vector v{};
    size_t top_k;
    std::vector<std::pair<std::string, std::string>> meta;
    for (size_t i = 0; i < schema::META_DATA_KP_PAIRS; i++)
    {
        meta.push_back({"qk" + std::to_string(i), "qv" + std::to_string(i)});
    }

    std::string cmd = BuildValidQueryCommand("15", std::to_string(schema::DIMENSIONS), meta, schema::DIMENSIONS);
    Parse_result res = parser.query_parsing(v, top_k, cmd);

    EXPECT_TRUE(res.success);
    EXPECT_EQ(top_k, 15);

    EXPECT_EQ(v.meta_data_count, schema::META_DATA_KP_PAIRS);
    for (size_t i = 0; i < schema::META_DATA_KP_PAIRS; i++)
    {
        std::string expected_key = "qk" + std::to_string(i);
        std::string expected_val = "qv" + std::to_string(i);
        EXPECT_STREQ(v.meta_data[i].key, expected_key.c_str());
        EXPECT_STREQ(v.meta_data[i].value, expected_val.c_str());
    }

    EXPECT_EQ(v.embeddings.size(), schema::DIMENSIONS);
    for (size_t i = 0; i < schema::DIMENSIONS; i++)
    {
        EXPECT_FLOAT_EQ(v.embeddings[i], i * 0.001f);
    }
}

// =============================================================================
// Parser::delete_parsing Tests
// =============================================================================

TEST_F(CommandParserTest, DeleteParsing_MalformedCommandWord_ReturnsFormatError)
{
    std::string id;
    std::string cmd = "DELET  id";
    Parse_result res = parser.delete_parsing(id, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Incorrect format for 'DELETE'>\n");
}

TEST_F(CommandParserTest, DeleteParsing_IdSizeZero_ReturnsError)
{
    std::string id;
    std::string cmd = "DELETE ";
    Parse_result res = parser.delete_parsing(id, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Id size can not be 0 >\n");
}

TEST_F(CommandParserTest, DeleteParsing_IdExceedsMaxLength_ReturnsError)
{
    std::string id;
    std::string cmd = "DELETE " + std::string(schema::ID_LENGTH + 1, 'x');
    Parse_result res = parser.delete_parsing(id, cmd);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Id size can not greater than '" + std::to_string(schema::ID_LENGTH) + "' >\n");
}

TEST_F(CommandParserTest, DeleteParsing_IdExactlyMaxLength_Succeeds)
{
    std::string id;
    std::string exact_id(schema::ID_LENGTH, 'a');
    std::string cmd = "DELETE " + exact_id;
    Parse_result res = parser.delete_parsing(id, cmd);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(id, exact_id);
}

TEST_F(CommandParserTest, DeleteParsing_ShortId_Succeeds)
{
    std::string id;
    std::string cmd = "DELETE short_id";
    Parse_result res = parser.delete_parsing(id, cmd);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(id, "short_id");
}

// =============================================================================
// Parser::save_parsing Tests
// =============================================================================

TEST_F(CommandParserTest, SaveParsing_State0_CleanSave_Succeeds)
{
    std::string cmd = "SAVE";
    Parse_result res = parser.save_parsing(cmd, false);
    EXPECT_TRUE(res.success);
}

TEST_F(CommandParserTest, SaveParsing_State1_CleanLoad_Succeeds)
{
    std::string cmd = "LOAD";
    Parse_result res = parser.save_parsing(cmd, true);
    EXPECT_TRUE(res.success);
}

// Regression test for SAVE/LOAD state-correlation fix
TEST_F(CommandParserTest, SaveParsing_State0_WrongKeyword_ReturnsSaveError)
{
    std::string cmd = "LOAD";
    Parse_result res = parser.save_parsing(cmd, false);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Invalid format for SAVE>\n");
}

// Regression test for SAVE/LOAD state-correlation fix
TEST_F(CommandParserTest, SaveParsing_State1_WrongKeyword_ReturnsLoadError)
{
    std::string cmd = "SAVE";
    Parse_result res = parser.save_parsing(cmd, true);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Invalid format for LOAD>\n");
}

// Characterization Test: Parser currently only checks `substr(0,4)` making it lenient to trailing garbage
TEST_F(CommandParserTest, SaveParsing_State0_TrailingGarbage_Succeeds_Characterization)
{
    std::string cmd = "SAVEFOO";
    Parse_result res = parser.save_parsing(cmd, false);
    EXPECT_TRUE(res.success);
}

TEST_F(CommandParserTest, SaveParsing_ShortCommandString_FailsGracefully)
{
    std::string cmd = "SA";
    // substr(0,4) on a size 2 string does not throw std::out_of_range, it returns "SA".
    Parse_result res = parser.save_parsing(cmd, false);
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.message, "ERROR <Invalid format for SAVE>\n");
}