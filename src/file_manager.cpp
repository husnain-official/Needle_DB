#include "file_manager.h"
// --- Constructor
File_manager::File_manager(const std::string &path) : path_(path)
{
    // 1. Initilize elements of 'File_manager' instance being created.
    header_ = DB_header(); // Create a instance of a 'Header' with pre-filled data, from 'schema.hpp'.
    record_size_ = sizeof(DB_entry);
    // 2. If no file, then create it and write 'header_' to it, else just open it and read 'header_'.
    if (!(std::filesystem::exists(path)))
    {
        this->file_.open(path, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!(file_.is_open()))
            throw std::runtime_error("[File_manager()] | Cannot create DB file: " + path);
        flush_header();
        std::cout << "[File_manager()] | Data-Base Created\n";
    }
    else
    {
        file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
    }
    if (!file_.is_open())
        throw std::runtime_error("[File_manager()] | Cannot create DB file: " + path);
    header_ = read_header();
    // 3. Check if schema matches, else throw error.
    if (std::strncmp(header_.magic_number, schema::MAGIC_NUMBER, 4) != 0)
    {
        throw std::runtime_error("[File_manager()] | Invalid file format: Magic number mismatch.");
    }
    if (header_.version != schema::VERSION)
    {
        throw std::runtime_error("[File_manager()] | Schema mismatch: Incompatible database version. Expected Version 4.");
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
bool File_manager::write_entry(const DB_entry &entry)
{
    // Note: Responsibility of caller to use a fresh/new entry refrence
    file_.clear();
    // 1.   move the write cursor to the eof
    file_.seekp(0, std::ios::end);
    // 2.   move the entire struct to disk in one operation
    file_.write(reinterpret_cast<const char *>(&entry), sizeof(DB_entry));
    // 3.   validation
    if (!file_.good())
        return false;
    // 4.   cleanup
    header_.total_vector_count++;
    header_.live_vector_count++;
    flush_header();
    return file_.good();
}
bool File_manager::read_entry(size_t index, DB_entry &entry)
{
    // Note: Again responsibility of caller to provide a fresh/new entry refrence
    file_.clear();
    // 0.   check-01
    if (index >= ((header_.total_vector_count)))
        return false;
    // 1.   move to the offset
    file_.seekg(get_record_offset(index));
    // 2.   read only the flag, and continue accordingly
    file_.read(reinterpret_cast<char *>(&entry.flag), sizeof(uint8_t));
    if (!file_.good() or entry.flag == 0)
        return false;
    // 3.   read the rest of entry in one dist operation
    file_.read(reinterpret_cast<char *>(&entry) + 1, sizeof(DB_entry) - 1);
    // 4.   return file state
    return file_.good();
}
bool File_manager::delete_entry(size_t index)
{
    file_.clear();
    // 0.   Check
    if (index >= ((header_.total_vector_count)))
        return false;
    // 1.   move to intended Record
    file_.seekp(get_record_offset(index));
    // 2.   change flag to 0
    char x = 0;
    file_.write(&x, 1);
    // 3.   cleanup
    header_.live_vector_count--;
    file_.flush();
    flush_header();
    // 4.   return file-state
    return file_.good();
}
bool File_manager::compact()
{
    try
    {
        // 1. Create a temporary new file
        std::string temp_path = "./data/temp_database.vdb";
        std::fstream new_file;
        new_file.open(temp_path, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!new_file.is_open())
            throw std::runtime_error("[File_manager] | Couldn't create new file " + temp_path);

        // 2. [Header]
        DB_header h_new = read_header();
        h_new.total_vector_count = h_new.live_vector_count;
        new_file.write(reinterpret_cast<const char *>(&h_new), sizeof(DB_header));

        // 3. [Entries]
        DB_entry entry;
        for (size_t i = 0; i < header_.total_vector_count; i++)
        {
            memset(&entry, 0, sizeof(DB_entry));

            if (!read_entry(i, entry))
                continue;

            new_file.write(reinterpret_cast<const char *>(&entry), sizeof(DB_entry));

            if (!new_file.good())
                throw std::runtime_error("[File_manager] | Couldn't write entry to new file.");
        }

        // 4. [File Replacements]
        file_.close();
        new_file.close();

        if (!delete_file(path_))
            throw std::runtime_error("[File_manager] | Couldn't delete old database.");

        if (!rename_file(temp_path, path_))
            throw std::runtime_error("[File_manager] | Couldn't rename new database.");

        // 5. Re-initialize the manager so the rest of the app can keep running
        file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
        if (!file_.is_open())
            throw std::runtime_error("[File_manager] | Couldn't reopen database after compaction.");

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
// --- Text-File
// bool File_manager::write_text()
// bool File_manager::read_text()
// bool File_manager::delete_text()
// bool File