// vim: sw=2 sts=2 expandtab smartindent
#include <proto/s3_extensions_cpp.h>
#include <algorithm>
#include <boost/foreach.hpp>

void HeaderSorter::add(const std::string& header) { 
  size_t cur_idx = header.find(':');
  std::string name = std::string(header, 0, cur_idx);
  std::transform(name.begin(), name.end(), name.begin(), tolower);

  if(name.find(m_prefix) == 0) {
    // skip LWS
    std::string::const_iterator it = header.begin() + cur_idx, 
                                end_of_value, trimmed_end;
    while(isspace(*it)) ++it;

    end_of_value = it;
    while(end_of_value != header.end() && it != header.end()) {
      while(*(end_of_value+1) != ',' && end_of_value != header.end()) {
        ++end_of_value;
      }
      trimmed_end = end_of_value;
      while(isspace(*trimmed_end)) --trimmed_end;

      m_headers[name].insert(std::string(it, trimmed_end));
      it = end_of_value;
      if(*it == ',') ++it;
      while(it != header.end() && isspace(*it)) ++it;
    }
  }
}

void HeaderSorter::update(HMAC_CTX *ctx) {
  BOOST_FOREACH(const headers_t::value_type& pair, m_headers) {
    HMAC_Update(ctx, (unsigned const char*)pair.first.c_str(), pair.first.size());
    HMAC_Update(ctx, (unsigned const char*)":", 1);
    bool first = true;
    BOOST_FOREACH(const std::string& value, pair.second) {
      if(!first) {
        HMAC_Update(ctx, (unsigned const char*)",", 1);
      }
      HMAC_Update(ctx, (unsigned const char*)value.c_str(), value.size());
      first = false;
    }
    HMAC_Update(ctx, (unsigned const char*)"\n", 1);
  }
}

void* HeaderSorter_new(char *prefix) { return (void*)(new HeaderSorter(prefix)); }

void  HeaderSorter_add(void *obj, char * s, size_t len) { ((HeaderSorter*)obj)->add(std::string(s, len)); }

void  HeaderSorter_update(void *obj, HMAC_CTX *ctx) { ((HeaderSorter*)obj)->update(ctx); }

void  HeaderSorter_delete(void *obj) { delete ((HeaderSorter*)obj); }

void CanonicalizeResource(HMAC_CTX *ctx, char *bucket, char *uri_begin, size_t uri_len) {
  // Bucket can either be in Host, or it can be the beginning of URI (if Host is s3.amazonaws.com).

  // "/" Bucket URI< up to query string. excludes bucket, if present >

  // Query params sorted by param name and interleaved with &, prepend with ?.
  // Values must be urldecoded (but haproxy has inplace url_decode. #%@! YES!):
  //   acl, lifecycle, location, logging, notification, partNumber, policy, requestPayment, torrent, uploadId, uploads, versionId, versioning, versions, website
  //   response-content-type, response-content-language, response-expires, response-cache-control, response-content-disposition, response-content-encoding
}
