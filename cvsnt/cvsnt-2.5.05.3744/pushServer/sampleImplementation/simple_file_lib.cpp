#include <stdio.h>
#include <string>
#include <memory>
#include "file_functions.h"

//we can implement remapping of hashes, or even different hash structure (hash_type->root_folder) with symlinks to same content

static const char *root_path = ".";
static const char *accepted_hash_type = nullptr;
void set_root(const char *p){root_path = p;}
void set_hash_type(const char *p){accepted_hash_type = p;}

static inline bool is_valid_hash_type(const char* hash_type) {return accepted_hash_type == 0 || (strncmp(accepted_hash_type, hash_type, 6) == 0);}

static inline std::string get_file_path(const char* /*hash_type*/, const char* hash_hex_string)
{
  if (strlen(hash_hex_string) < 64)
    return std::string();
  char buf[68];
  //sprintf(buf,"/%2s/%2s/%.60s", hash_hex_string, hash_hex_string+2, hash_hex_string+4);
  //todo: folders!
  sprintf(buf,"/%.64s", hash_hex_string);
  return std::string(root_path) + buf;
}

size_t file_get_hash_blob_size(const char* hash_type, const char* hash_hex_string)
{
  if (!is_valid_hash_type(hash_type))
    return 0;
  return blob_get_file_size(get_file_path(hash_type, hash_hex_string).c_str());
}

bool file_does_hash_blob_exist(const char* hash_type, const char* hash_hex_string)
{
  if (!is_valid_hash_type(hash_type))
    return false;
  return blob_is_file_readable(get_file_path(hash_type, hash_hex_string).c_str());
}

struct TempFilePush
{
  std::string filepath;
  std::string temp_file_name;
  FILE *fp;
};

extern uintptr_t file_start_push_data(const char* hash_type, const char* hash_hex_string, uint64_t size)
{
  if (!is_valid_hash_type(hash_type))
    return 0;
  std::string filepath = get_file_path(hash_type, hash_hex_string);
  if (blob_is_file_readable(filepath.c_str()))
    return 1;//magic number. we dont need to do anything, but that's not an error
  std::string temp_file_name;
  FILE * tmpf = blob_get_temp_file(temp_file_name);
  printf("try open <%s> tmp = %p\n",temp_file_name.c_str(), tmpf);
  if (!tmpf)
    return 0;
  return uintptr_t(new TempFilePush{std::move(filepath), std::move(temp_file_name), tmpf});
}

extern bool file_push_data(const void *data, size_t data_size, uintptr_t up)
{
  printf("file_push_data %p %d\n", up, int(data_size));
  if (up == 1)
    return true;
  if (up == 0)
    return false;
  TempFilePush* fp = (TempFilePush*)up;
  if (!fp->fp)
    return false;
  if (fwrite(data, 1, data_size, fp->fp) == data_size)
  {
    printf("written %p data_size = %d\n", fp, data_size);
    //todo:
    //we can (and should) decode & update hash on the fly
    return true;
  }
  printf("cant write %p\n", fp);
  fclose(fp->fp);
  blob_unlink_file(fp->temp_file_name.c_str());
  fp->fp = nullptr;
  return false;
}

extern bool file_end_push_data(uintptr_t up, bool ok)
{
  if (up == 1)
    return true;
  if (!up)
    return false;
  printf("end push %p %d\n", up, ok);
  std::unique_ptr<TempFilePush> fp((TempFilePush*)up);
  if (!fp->fp)
    return false;
  fclose(fp->fp);
  if (!ok)
  {
    blob_unlink_file(fp->temp_file_name.c_str());
    return false;
  }
  if (blob_is_file_readable(fp->filepath.c_str()))//was pushed by someone else
  {
    blob_unlink_file(fp->temp_file_name.c_str());
    return true;
  }
  //todo: validate hash! We can't trust it!
  return blob_rename_file(fp->temp_file_name.c_str(), fp->filepath.c_str());
}

#if !_WIN32
struct FilePullData
{
  const char* begin;
  uint64_t size;
  int fd;
};
uintptr_t file_start_pull_data(const char* hash_type, const char* hash_hex_string, uint64_t &blob_sz)
{
  if (!is_valid_hash_type(hash_type))
    return 0;
  blob_sz = blob_get_file_size(filepath.c_str());
  if (!blob_sz)
    return 0;
  std::string filepath = get_file_path(hash_type, hash_hex_string);
  int fd = open(argv[1], O_RDONLY);
  if (fd == -1)
    return 0;

  const char* begin = (const char*)(mmap(NULL, blob_sz, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0));
  return uintptr_t(new FilePullData{begin, blob_sz, fd});
}

const char *file_pull_data(uintptr_t up, uint64_t from, size_t &data_pulled)
{
  FilePullData* fp = (FilePullData*)up;
  if (!fp)
    return nullptr;
  const int64_t left = fp->size - from;
  if (left < 0)
    return nullptr;
  data_pulled = left;
  return fp->begin;
}

extern bool file_end_pull_data(uintptr_t up)
{
  if (!up)
    return false;
  std::unique_ptr<FilePullData> fp((FilePullData*)up);
  close(fp->fd);
  return true;
}

#else

struct FilePullData
{
  FILE *fp;
  std::unique_ptr<char[]> tempBuf;
  uint64_t pos;
  enum {SIZE = 1<<10};
};
uintptr_t file_start_pull_data(const char* hash_type, const char* hash_hex_string, uint64_t &blob_sz)
{
  if (!is_valid_hash_type(hash_type))
    return 0;
  std::string filepath = get_file_path(hash_type, hash_hex_string);
  blob_sz = blob_get_file_size(filepath.c_str());
  if (!blob_sz)
    return 0;
  return uintptr_t(new FilePullData{fopen(filepath.c_str(), "rb"), std::make_unique<char[]>(FilePullData::SIZE), 0});
}

const char *file_pull_data(uintptr_t up, uint64_t from, size_t &data_pulled)
{
  FilePullData* fp = (FilePullData*)up;
  if (!fp)
    return nullptr;
  if (fp->pos != from)
    fseek(fp->fp, int64_t(from) - int64_t(fp->pos), SEEK_CUR);
  data_pulled = fread(fp->tempBuf.get(), 1, FilePullData::SIZE, fp->fp);
  return fp->tempBuf.get();
}

extern bool file_end_pull_data(uintptr_t up)
{
  if (!up)
    return false;
  std::unique_ptr<FilePullData> fp((FilePullData*)up);
  fclose(fp->fp);
  return true;
}
#endif
