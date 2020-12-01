#pragma once
#include <stdint.h>

//these are functions implemented in library, see simple_file_lib.cpp
extern size_t file_get_hash_blob_size(const char* hash_type, const char* hash_hex_string);//hash_type = "blake3" hash_hex_string = "fe12bc56...."
extern bool file_does_hash_blob_exist(const char* hash_type, const char* hash_hex_string);//hash_type = "blake3" hash_hex_string = "fe12bc56...."
extern uintptr_t file_start_push_data(const char* hash_type, const char* hash_hex_string, uint64_t size);//hash_type = "blake3" hash_hex_string = "fe12bc56...."
extern bool file_push_data(const void *data, size_t data_size, uintptr_t up);
extern bool file_end_push_data(uintptr_t up, bool ok);

extern uintptr_t file_start_pull_data(const char* hash_type, const char* hash_hex_string, uint64_t &blob_sz);//allows memory mapping. blob_sz is the size of blob. if invalid returns nullptr
extern const char *file_pull_data(uintptr_t readBlob, uint64_t from, size_t &data_pulled);//data_pulled != 0, unless error
extern bool file_end_pull_data(uintptr_t tempBlob);