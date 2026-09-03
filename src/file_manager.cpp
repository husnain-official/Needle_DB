#include "file_manager.h"
// --- Constructor
File_manager::File_manager(const std::string &path, const std::string &text_path) : path_(path), text_file_path_(text_path)
{
    // 1. Initilize elements of 'File_manager' instance being created.
    header_ = DB_header(); // Create a instance of a 'Header' with pre-filled data, from 'schema.hpp'.
    record_size_ = sizeof(DB_entry);
    // 2. If no file, then create it and write 'header_' to it, else just open it and read 'header_'.
    if ((!std::filesystem::exists(path)) or (!std::filesystem::exists(text_path)))
    {
        this->file_.open(path, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!(file_.is_open()))
            throw std::runtime_error("[File_manager()] | Cannot create DB entry file: " + path);
        flush_header();
        std::cout << "[File_manager()] | Entry-Data-Base Created\n";
        this->text_file_.open(text_path, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!text_file_.is_open())
            throw std::runtime_error("[File_manager()] | Cannot create DB text file: " + text_path);
        std::cout << "[File_manager()] | Text-Data-Base Created\n";
    }
    else
    {
        file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
        text_file_.open(text_path, std::ios::in | std::ios::out | std::ios::binary);
    }
    if (!file_.is_open())
        throw std::runtime_error("[File_manager()] | Cannot open DB entry file: " + path);
    if (!text_file_.is_open())
        throw std::runtime_error("[File_manager()] | Cannot open DB text file: " + text_path);
    header_ = read_header();
    // 3. Check if schema matches, else throw error.
    if (std::strncmp(header_.magic_number, schema::MAGIC_NUMBER, 4) != 0)
    {
        throw std::runtime_error("[File_manager()] | Invalid file format: Magic number mismatch.");
    }
    if (header_.version != schema::VERSION)
    {
        throw std::runtime_error("[File_manager()] | Schema mismatch: Incompatible database version. Expected Version '" + std::to_string(schema::VERSION) + "' .");
    }
    if (header_.dimensions != schema::DIMENSIONS or header_.id_length != schema::ID_LENGTH or header_.kv_length != schema::META_DATA_LENGTH or header_.max_kv != schema::META_DATA_KP_PAIRS)
    {
        throw std::runtime_error("[File_manager()] | Schema mismatch: File dimensions/ID length/Meta-data do not match provided arguments.");
    }
    std::cout << "[File_manager()] | Data-Base path opened successfully\n";
}
// --- Header-Related-Functions
DB_header File_manager::read_header()
{
    DB_header h;
    file_.clear();
    file_.seekg(0);
    file_.read(reinterpret_cast<char *>(&h), sizeof(DB_header));
    return h;
}
bool File_manager::flush_header()
{
    file_.clear();
    file_.seekp(0);
    file_.write(reinterpret_cast<char *>(&header_), sizeof(DB_header));
    file_.flush();
    return file_.good();
}
// --- Getters
size_t File_manager::get_live_vector_count() const { return header_.live_vector_count; }
size_t File_manager::get_total_vector_count() const { return header_.total_vector_count; }
size_t File_manager::get_record_offset(size_t index) const { return (sizeof(DB_header) + index * record_size_); }
// --- Helpers
bool rename_file(const std::string &old_name, const std::string &new_name)
{
    std::error_code ec;
    // The ec variable will capture any errors (like "File not found")
    std::filesystem::rename(old_name, new_name, ec);
    if (ec)
    {
        std::cerr << "Error renaming: " << ec.message() << "\n";
        return false;
    }
    return true;
}
bool delete_file(const std::string &filepath)
{
    std::error_code ec;
    // remove() returns true if a file was deleted, false if it didn't exist
    bool success = std::filesystem::remove(filepath, ec);
    if (ec)
    {
        std::cerr << "Error deleting: " << ec.message() << "\n";
        return false;
    }
    return success;
}
// --- Core-Operations
bool File_manager::write_entry(DB_entry &entry, std::string &text)
{
    // NOTE: Responsibility of caller to use a fresh/new entry refrence
    // --- Text-DB ---
    // Setup
    text_file_.clear();
    // Move to eof
    text_file_.seekp(0, std::ios::end);
    entry.text_offset = text_file_.tellp();
    // Flag writing
    text_file_.write(reinterpret_cast<const char *>(&entry.flag), 1);
    if (!text_file_.good())
        return false;
    // Text writing
    text_file_.write(reinterpret_cast<const char *>(text.data()), text.size());
    if (!text_file_.good())
        return false;
    // --- Entry-DB ---
    file_.clear();
    file_.seekp(0, std::ios::end);
    file_.write(reinterpret_cast<const char *>(&entry), sizeof(DB_entry));
    if (!file_.good())
        return false;
    // Clean-up
    header_.total_vector_count++;
    header_.live_vector_count++;
    flush_header();
    return file_.good();
}
bool File_manager::read_entry(size_t index, DB_entry &entry, std::string &text)
{
    // Note: Again responsibility of caller to provide a fresh/new entry refrence
    // Note: Since, text size is'nt known before hand so, this function will resize 'text' itself
    // --- Entry-DB ---
    file_.clear();
    // Check-01
    if (index >= ((header_.total_vector_count)))
        return false;
    // Move to offset
    file_.seekg(get_record_offset(index));
    // Read entry
    file_.read(reinterpret_cast<char *>(&entry), sizeof(DB_entry));
    if (!file_.good() or entry.flag == 0)
        return false;
    // --- Text-DB ---
    text_file_.clear();
    text_file_.seekg(entry.text_offset);
    // Redundant but safety ?
    char flag = 0;
    text_file_.read((&flag), 1);
    if (!text_file_.good() or flag == 0)
        return false;
    // Redundancy ends here, we could trust the entry's flag check that was done above
    text.resize(entry.text_length);
    text_file_.read(reinterpret_cast<char *>(text.data()), entry.text_length);
    return text_file_.good();
}
bool File_manager::delete_entry(size_t index)
{
    // --- Entry-DB ---
    file_.clear();
    //  Check
    if (index >= ((header_.total_vector_count)))
        return false;
    //  Check - Already deleted ?
    DB_entry entry_read;
    file_.seekg(get_record_offset(index));
    file_.read(reinterpret_cast<char *>(&entry_read), sizeof(DB_entry));
    if (!file_.good())
        return false;
    if (!entry_read.flag)
        return true;
    //  Move to intended Record
    file_.seekp(get_record_offset(index));
    //  Change flag to 0
    char flag = 0;
    file_.write(&flag, 1);
    if (!file_.good())
        return false;
    //  Cleanup
    header_.live_vector_count--;
    file_.flush();
    flush_header();
    // --- Text-DB ---
    text_file_.clear();
    text_file_.seekp(entry_read.text_offset);
    text_file_.write(&flag, 1);
    text_file_.flush();
    return text_file_.good();
}
bool File_manager::read_text(const size_t text_length, const size_t text_offset, std::string &text)
{
    text_file_.clear();
    text_file_.seekp(0, std::ios::end);
    size_t file_size = static_cast<size_t>(text_file_.tellp());
    if (text_offset > file_size || (file_size - text_offset) < (text_length + 1))
        return false;
    // Move to offset
    text_file_.seekg(text_offset);
    // Read the flag
    char flag = 0;
    text_file_.read(&flag, 1);
    if (!text_file_.good() or flag == 0)
        return false;
    // Read the text
    text.resize(text_length);
    text_file_.read(reinterpret_cast<char *>(text.data()), text_length);
    return text_file_.good();
}
bool File_manager::compact()
{
    try
    {
        // 1. Create a temporary new file
        std::string temp_path = "./data/temp_database.vdb";
        std::string temp_text_path = "./data/temp_text_database.vdb";
        std::fstream new_file;
        std::fstream new_text_file;
        new_file.open(temp_path, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!new_file.is_open())
            throw std::runtime_error("[File_manager] | Couldn't create new file " + temp_path);
        new_text_file.open(temp_text_path, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!new_text_file.is_open())
            throw std::runtime_error("[File_manager] | Couldn't create new file " + temp_path);
        text_file_.seekg(0);

        // 2. [Header]
        DB_header h_new = read_header();
        h_new.total_vector_count = h_new.live_vector_count;
        new_file.write(reinterpret_cast<const char *>(&h_new), sizeof(DB_header));

        // 3. [Entries]
        DB_entry entry;
        std::string temp_str;
        char flag = 1;
        for (size_t i = 0; i < header_.total_vector_count; i++)
        {
            temp_str.clear();
            memset(&entry, 0, sizeof(DB_entry));

            if (!read_entry(i, entry, temp_str))
                continue;

            entry.text_offset = new_text_file.tellp();
            new_text_file.write(&flag, 1);
            new_text_file.write(reinterpret_cast<const char *>(temp_str.data()), temp_str.size()); // Note: temp_str.size() == entry.text_length;
            new_file.write(reinterpret_cast<const char *>(&entry), sizeof(DB_entry));

            if (!new_file.good())
                throw std::runtime_error("[File_manager] | Couldn't write entry to new file.");
            if (!new_text_file.good())
                throw std::runtime_error("[File_manager] | Couldn't write entry to new file.");
        }

        // 4. [File Replacements]
        // Currently, this is a bad function as it allows data corruption in case of shutdown, later on shift to a better implementation.
        file_.close();
        text_file_.close();
        new_file.close();
        new_text_file.close();

        if (!delete_file(path_) or !(delete_file(text_file_path_)))
            throw std::runtime_error("[File_manager] | Couldn't delete old database.");

        if (!rename_file(temp_path, path_))
            throw std::runtime_error("[File_manager] | Couldn't rename new database.");
        if (!rename_file(temp_text_path, text_file_path_))
            throw std::runtime_error("[File_manager] | Couldn't rename new text database.");

        // 5. Re-initialize the manager so the rest of the app can keep running
        file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
        if (!file_.is_open())
            throw std::runtime_error("[File_manager] | Couldn't reopen database after compaction.");

        text_file_.open(text_file_path_, std::ios::in | std::ios::out | std::ios::binary);
        if (!text_file_.is_open())
            throw std::runtime_error("[File_manager] | Couldn't reopen text database after compaction.");

        // 6. Re-initilize the RAM cache
        header_ = h_new;

        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error Contracting: " << e.what() << "\n";
        return false;
    }
}
long File_manager::find_by_id(const std::string &id)
{
    // Prevent false matches if search ID is too long
    if (id.length() > header_.id_length)
        return -1;
    file_.clear();
    //     Loop through each 'Record' and compare ids
    std::vector<char> id_extracted(header_.id_length, '\0');
    for (size_t i = 0; i < header_.total_vector_count; i++)
    {
        file_.seekg(get_record_offset(i));

        char flag_state;
        // Ensure read was successful
        if (!file_.read(&flag_state, 1))
            break;

        if (flag_state == 1)
        {
            file_.read(id_extracted.data(), header_.id_length);
            if (std::strncmp(id_extracted.data(), id.c_str(), std::min(id.length() + 1, (size_t)header_.id_length)) == 0)
                return i;
        }
    }
    return -1;
}
