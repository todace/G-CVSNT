#include <stdio.h>
#include "sha_blob_reference.h"
#include <vector>
#include <string>
#include "concurrent_queue.h"
#include <thread>
#include "blob_network_processor.h"
#include "../ca_blobs_fs/streaming_blobs.h"

#include "error.h"
extern int change_mode(const char *filename, const char *mode_string, int respect_umask);
extern void change_utime(const char* filename, time_t timestamp);
extern int unlink_file(const char* filename);

struct BlobTask
{
  std::string message, dirpath, filename, encoded_hash, file_mode;
  time_t timestamp;
  bool compress;
  enum class Type {Download, Upload} type;
};

static bool download_blob_ref_file(BlobNetworkProcessor *processor, const BlobTask &task);
static bool upload_blob_ref_file(BlobNetworkProcessor *processor, const BlobTask &task);

static bool process_blob_task(BlobNetworkProcessor *processor, const BlobTask &task)
{
  if (task.type == BlobTask::Type::Download)
    return download_blob_ref_file(processor, task);
  return upload_blob_ref_file(processor, task);
}

struct BackgroundProcessor
{
  BackgroundProcessor():queue(&threads){}
  ~BackgroundProcessor()
  {
  }
  void finishDownloads()
  {
    queue.finishWork();
  }

  bool is_inited() const {return !clients.empty();}
  void init();
  void wait();

  static void processor_thread_loop(BackgroundProcessor *processors, BlobNetworkProcessor *processor)
  {
    BlobTask task;
    while (processors->queue.wait_and_pop(task))
      process_blob_task(processor, task);
  }
  void emplace(BlobTask &&task)
  {
    if (!is_inited())
      return;
    if (threads.empty())
      process_blob_task(clients[0].get(), task);
    else
    {
      //we can't guarantee, that user will finish download (not cancel it, kill the process or whatever)
      //if he won't, then entries would still be updated with new version, but the file will be old (while be shown as Modified)
      //the correct way would be to keep old entries, and update them only when download is finished
      //that's doable, but will require to:
      //  a) implement locking (so same entries are not updated from different threads, including main)
      //  b) format of entries has to be known here
      //  While not a big deal, is still not that easy task
      // so we use simplest solution - we simply kill old binary file. It can also be renamed, but better to not bother
      if (task.type == BlobTask::Type::Download)
        unlink_file(task.filename.c_str());
      else
      {
        //we preferrably lock file (by opening file handler) to prevent changing during commit
        //but total amount of file handlers is limited, so we trust people to be reasonable
      }
      queue.emplace(std::move(task));
    }
  }
  std::vector<std::thread> threads;//soa
  std::vector<std::unique_ptr<BlobNetworkProcessor>> clients;//soa
  bool uploadEnabled = false;

  concurrent_queue<BlobTask> queue;
  std::string base_repo;
  protected:
#if defined(_WIN32) && 0
  httplib::detail::WSInit wsinit_;
#endif
};

static std::unique_ptr<BackgroundProcessor> processor;

//link time resolved dependency
extern void get_download_source(const char *&url, int &port, const char *&auth_user, const char *&auth_passwd, const char *&repo, int &threads_count);

extern BlobNetworkProcessor *get_http_processor(const char *url, int port, const char* repo, const char *user, const char *passwd);
extern BlobNetworkProcessor *get_kv_processor(const char *url, int port, const char* repo, const char *user, const char *passwd);

void BackgroundProcessor::init()
{
  if (is_inited())
    return;
  const char *url, *repo, *user = nullptr, *passwd = nullptr; int port;
  int threads_count = std::min(8, std::max(1, (int)std::thread::hardware_concurrency()-1));//limit concurrency to fixed
  get_download_source(url, port, user, passwd, repo, threads_count);
  base_repo = repo;
  if (base_repo[0] != '/')
    base_repo = "/" + base_repo;
  if (base_repo[base_repo.length()] != '/')
    base_repo += "/";
  clients.resize(std::max(1, threads_count));
  bool use_http = strcmp(url, "http://") == 0;
  int http_port = use_http ? port : 80;
  uploadEnabled = true;
  for (auto &cli : clients)
  {
    BlobNetworkProcessor * pr = nullptr;
    if (!use_http)
    {
      pr = get_kv_processor(url, port, base_repo.c_str(), user, passwd);
      if (!pr)
      {
        use_http = true;
        http_port = 80;
      }
    }
    if (use_http)
    {
      pr = get_http_processor(url, http_port, base_repo.c_str(), user, passwd);
      uploadEnabled = false;
    }
    cli.reset(pr);
  }
  for (int ti = 0; ti < threads_count; ++ti)
    threads.emplace_back(std::thread(processor_thread_loop, this, clients[ti].get()));
}

void BackgroundProcessor::wait()
{
  for (auto &t:threads)
    t.join();
}


extern char *xgetwd();
extern void blob_free(void*);
void add_upload_queue(const char *filename, bool compress, const char *message)
{
  if (!processor)
  {
  	processor.reset(new BackgroundProcessor);
    processor->init();
  }
  if (!processor->uploadEnabled)
    return;
  char *cdir = xgetwd();
  processor->emplace(BlobTask{message, cdir, filename, "", "", 0, compress, BlobTask::Type::Upload});
  blob_free(cdir);
}

void add_download_queue(const char *message, const char *filename, const char *encoded_hash, const char *file_mode, time_t timestamp)
{
  if (!processor)
  {
  	processor.reset(new BackgroundProcessor);
    processor->init();
  }
  char *cdir = xgetwd();
  processor->emplace(BlobTask{message, cdir, filename, encoded_hash, file_mode, timestamp, false, BlobTask::Type::Download});
  blob_free(cdir);
}

void wait_threads()
{
  if (processor)
    processor->finishDownloads();
  processor.reset();

}
void rename_file (const char *from, const char *to);

static bool download_blob_ref_file(BlobNetworkProcessor *processor, const BlobTask &task)
{
  std::string temp_filename = task.dirpath +"/_new_";
  temp_filename += task.filename;
  FILE *tmp = fopen(temp_filename.c_str(), "wb");
  if (!tmp)
    return false;
  std::string err;
  caddressed_fs::DownloadBlobInfo info;
  if (!processor->download(task.encoded_hash.data(),
      [&](const char *data, size_t data_length) {
        return caddressed_fs::decode_stream_blob_data(info, data, data_length,
          [&](const void *data, size_t sz) { return fwrite(data, 1, sz, tmp) == sz; });//we can easily add hash validation here. but seems unnessasry
      },
      err))
  {
    unlink_file(temp_filename.c_str());
    printf("can't download <%.64s>, err = %s\n", task.encoded_hash.data(), err.c_str());
    return false;
  }
  if (tmp)
    fclose(tmp);

  change_utime(temp_filename.c_str(), task.timestamp);
  {
    int status = change_mode (temp_filename.c_str(), task.file_mode.c_str(), 1);
    if (status != 0)
      error (0, status, "cannot change mode of %s", task.filename.c_str());
  }
  std::string fullPath = (task.dirpath+"/")+task.filename;
  rename_file (temp_filename.c_str(), fullPath.c_str());
  printf("Ud %s\n", task.message.c_str());
  return true;
}
bool is_blob_file_sent(const char* file, char* hash_encoded);
void finish_send_blob_file(const char* file, const char* hash_encoded);

static bool upload_blob_ref_file(BlobNetworkProcessor *processor, const BlobTask &task)
{
  std::string fullPath = (task.dirpath+"/")+task.filename;
  char hash[65]; hash[0]=hash[64] = 0;
  if (is_blob_file_sent(fullPath.c_str(), hash))
    return true;
  std::string err;
  if (!processor->upload(fullPath.c_str(), task.compress, hash, err))
  {
    printf("can't upload file <%s>, err = %s\n", fullPath.c_str(), err.c_str());
    return false;
  }
  finish_send_blob_file(task.filename.c_str(), hash);
  printf("Cd %s\n", task.message.c_str());

  return true;
}

