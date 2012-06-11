// vim: sw=2 sts=2 expandtab smartindent
#include <proto/s3_extensions_cpp.h>
#include <algorithm>
#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>

// Forward declare because including the header includes ebtree headers,
// which cause massive sadness in C++ mode.
extern "C" int url_decode(char *string);

void HeaderSorter::add(const std::string& header) { 
  size_t cur_idx = header.find(':');
  std::string name = std::string(header, 0, cur_idx);
  std::transform(name.begin(), name.end(), name.begin(), tolower);

  if(name.find(m_prefix) == 0) {
    std::string::const_iterator it = header.begin() + cur_idx + 1,
                                end = header.end();
    // trim WS
    while(isspace(*it)) ++it;
    while(isspace(*(end-1))) --end;

    // TODO: Handle headers spanning multiple lines

    m_headers[name].insert(std::string(it, end));
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

void CanonicalizeResource(HMAC_CTX *ctx, char *uri_begin, size_t uri_len) {
  using namespace boost;
  using namespace boost::algorithm;

  static std::set<iterator_range<const char*> > important_keywords;
  if(important_keywords.empty()) {
    static const char* keywords[] = {
      "acl","lifecycle","location","logging","notification","partNumber","policy","requestPayment",
      "torrent","uploadId","uploads","versionId","versioning","versions","website",
      "response-content-type","response-content-language","response-expires","response-cache-control",
      "response-content-disposition","response-content-encoding"};
    BOOST_FOREACH(const char* word, keywords) {
      important_keywords.insert(iterator_range<const char*>(word, word+strlen(word)));
    }
  }

  char*  query_start = std::find(uri_begin, uri_begin+uri_len, '?');
  size_t query_len   = uri_len - ( query_start - uri_begin );

  // Bucket is always at the beginning of URI (has been fixed by regex at this point)
  HMAC_Update(ctx, (unsigned char*)uri_begin, uri_len - query_len);

  // Skip the '?'
  ++query_start;
  --query_len;

  // Query params sorted by param name and interleaved with &, prepend with ?.
  if(query_len > 0) {
    typedef std::map<iterator_range<char*>, std::string > query_params_t;
    query_params_t query_params;

    std::list<iterator_range<char*> > param_list;
    iterator_range<char*> query_range(query_start, query_start+query_len);
    split(param_list, query_range, is_any_of("&"), token_compress_on);

    BOOST_FOREACH(const iterator_range<char*> &key_value, param_list) {
      iterator_range<char*> key(key_value.begin(), std::find(key_value.begin(), key_value.end(), '='));
      if(!important_keywords.count(key)) { continue; }

      std::string value;
      if(key != key_value) {
        iterator_range<char*> value_r(key.end()+1, key_value.end());
        if(starts_with(key, "response-")) {
          char * value_decoded = strndup(value_r.begin(), value_r.size());
          url_decode(value_decoded);
          value.assign(value_decoded);
          free(value_decoded);
        } else {
          value.assign(value_r.begin(), value_r.end());
        }
      }
      query_params[key] = value;
    }

    bool nth = false;
    BOOST_FOREACH(const query_params_t::value_type &key_value, query_params) {
      HMAC_Update(ctx, (unsigned const char*)(nth ? "&" : "?"), 1);
      HMAC_Update(ctx, (unsigned const char*)key_value.first.begin(), key_value.first.size());
      if(!key_value.second.empty()) {
        HMAC_Update(ctx, (unsigned const char*)"=", 1);
        HMAC_Update(ctx, (unsigned const char*)key_value.second.c_str(), key_value.second.size());
      }
      nth = true;
    }
  }

}
