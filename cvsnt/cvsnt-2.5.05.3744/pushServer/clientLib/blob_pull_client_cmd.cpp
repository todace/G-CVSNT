#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <algorithm>
#include "../include/blob_sockets.h"
#include "../include/blob_push_protocol.h"
#include "../include/blob_common_net.h"
#include "../include/blob_hash_util.h"
#include "../blob_push_log.h"
#include "../include/blob_client_lib.h"

using namespace blob_push_proto;

int64_t blob_pull_from_server(intptr_t &sockfd, const char *hash_type, const char *hash_hex_str,
  uint64_t from, uint64_t sz,
  std::function<bool (const char *data, uint64_t at, uint64_t size)> cb)
{
  unsigned char blob_hash[hash_len];
  if (!encode_hash_str_to_blob_hash_s(hash_type, hash_hex_str, blob_hash, sizeof(blob_hash)))
  {
    blob_logmessage(LOG_ERROR, "<%s:%s> is not a valid hash_type:hex pair", hash_type, hash_hex_str);
    return -1;
  }
  char command[pull_command_len + command_len];

  uint32_t chunk = uint32_t(from/pull_chunk_size);
  void *to = command;
  memcpy_to(to, pull_command, command_len);//copy command
  memcpy_to(to, blob_hash, hash_len);//copy hash
  memcpy_to(to, &sz, sizeof(uint64_t));
  memcpy_to(to, &chunk, sizeof(chunk));
  if (!send_exact(sockfd, command, sizeof(command)))
  {
    stop_blob_push_client(sockfd);
    return -1;
  }

  char responce[responce_len+1];
  if (!recv_exact(sockfd, responce, responce_len))
  {
    stop_blob_push_client(sockfd);
    return -1;
  }
  responce[responce_len] = 0;
  if (strncmp(responce, none_responce, responce_len) == 0)
  {
    blob_logmessage(LOG_NOTIFY, "%s:%s is not on server, can't pull", hash_type, hash_hex_str);
    return -1;
  }//no such file on server, can't pull data

  if (strncmp(responce, take_responce, responce_len) != 0)
  {
    blob_logmessage(LOG_ERROR, "unknown or error responce %s on pull request for <%s:%s>", responce, hash_type, hash_hex_str);
    if (!is_error_responce(responce))
      stop_blob_push_client(sockfd);
    return -1;
  }
  unsigned char got_hash_size_from[take_responce_len-responce_len];
  if (!recv_exact(sockfd, got_hash_size_from, sizeof(got_hash_size_from)))
    return -1;
  char got_hash_type[7], got_hash_hex[65];
  decode_blob_hash_to_hex_hash(got_hash_size_from, got_hash_type, got_hash_hex);
  const void *from_resp = got_hash_size_from + hash_len;
  memcpy_from(&sz, from_resp, sizeof(sz));
  memcpy_from(&chunk, from_resp, sizeof(uint32_t));
  from = uint64_t(chunk) * pull_chunk_size;
  cb(nullptr, from, sz);
  blob_logmessage(LOG_NOTIFY, "pull %d of <%s> from server", sz, hash_hex_str);

  if (memcmp(got_hash_size_from, blob_hash, hash_len) != 0)
  {
    blob_logmessage(LOG_WARNING, "for pull request for <%s:%s> we got<%s:%s>!", hash_type, hash_hex_str, got_hash_type, got_hash_hex);
    if (!recv_lambda(sockfd, sz, [&](const char *data, int data_len) {from+=data_len;}))
    {
      blob_logmessage(LOG_ERROR, "can't skip data");
      stop_blob_push_client(sockfd);
    }
    return -1;
  }

  if (!recv_lambda(sockfd, sz, [&](const char *data, int data_len) {cb(data, from, data_len);from+=data_len;}))
  {
    blob_logmessage(LOG_ERROR, "can't read data");
    stop_blob_push_client(sockfd);
    return -1;
  }
  return sz;
}
