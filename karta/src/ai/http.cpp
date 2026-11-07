#include "http.h"

#include <curl/curl.h>

#include <mutex>

namespace karta {
namespace {

struct WriteState {
  const ChunkCallback* on_chunk = nullptr;
  std::string* sink = nullptr;
};

std::size_t write_callback(char* data, std::size_t size, std::size_t count, void* user) {
  static const std::size_t kMaxBufferedBody = 64 * 1024 * 1024;
  auto* state = static_cast<WriteState*>(user);
  const std::size_t length = size * count;
  if (state->on_chunk && *state->on_chunk) {
    if (!(*state->on_chunk)(data, length)) return 0;  // Abort the transfer.
  } else if (state->sink) {
    if (state->sink->size() + length > kMaxBufferedBody) return 0;
    state->sink->append(data, length);
  }
  return length;
}

int progress_callback(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  const auto* cancel = static_cast<const std::atomic<bool>*>(clientp);
  return (cancel && cancel->load()) ? 1 : 0;
}

void ensure_global_init() {
  static std::once_flag once;
  std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

}  // namespace

HttpResponse http_post_stream(const std::string& url, const std::vector<std::string>& headers,
                              const std::string& body, const ChunkCallback& on_chunk,
                              const std::atomic<bool>* cancel) {
  ensure_global_init();

  HttpResponse result;
  CURL* curl = curl_easy_init();
  if (!curl) {
    result.error = "failed to initialize libcurl";
    return result;
  }

  curl_slist* header_list = nullptr;
  for (const std::string& header : headers) header_list = curl_slist_append(header_list, header.c_str());

  WriteState state;
  state.on_chunk = &on_chunk;

  std::string error_buffer;
  error_buffer.resize(CURL_ERROR_SIZE);

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer.data());
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "karta/0.1");
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
  if (cancel) {
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancel);
  }

  const CURLcode code = curl_easy_perform(curl);
  if (code != CURLE_OK) {
    if (code == CURLE_ABORTED_BY_CALLBACK && cancel && cancel->load()) {
      result.error = "aborted";
    } else {
      result.error = error_buffer.empty() ? curl_easy_strerror(code) : error_buffer;
    }
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
  curl_slist_free_all(header_list);
  curl_easy_cleanup(curl);
  return result;
}

HttpResponse http_get(const std::string& url, const std::vector<std::string>& headers,
                      const std::atomic<bool>* cancel) {
  ensure_global_init();

  HttpResponse result;
  CURL* curl = curl_easy_init();
  if (!curl) {
    result.error = "failed to initialize libcurl";
    return result;
  }

  curl_slist* header_list = nullptr;
  for (const std::string& header : headers) header_list = curl_slist_append(header_list, header.c_str());

  WriteState state;
  state.sink = &result.body;

  std::string error_buffer;
  error_buffer.resize(CURL_ERROR_SIZE);

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer.data());
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "karta/0.1");
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  if (cancel) {
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancel);
  }

  const CURLcode code = curl_easy_perform(curl);
  if (code != CURLE_OK) {
    if (code == CURLE_ABORTED_BY_CALLBACK && cancel && cancel->load()) {
      result.error = "aborted";
    } else {
      result.error = error_buffer.empty() ? curl_easy_strerror(code) : error_buffer;
    }
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
  curl_slist_free_all(header_list);
  curl_easy_cleanup(curl);
  return result;
}

}  // namespace karta
