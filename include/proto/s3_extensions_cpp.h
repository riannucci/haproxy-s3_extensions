// vim: sw=2 sts=2 expandtab smartindent
#pragma once

#include <openssl/hmac.h>

#ifdef __cplusplus
#include <map>
#include <set>
#include <string>

class HeaderSorter {
  std::string m_prefix;

  typedef std::map<std::string, std::multiset<std::string> > headers_t;
  std::map<std::string, std::multiset<std::string> > m_headers;

public:
  HeaderSorter(const std::string& prefix = "") : m_prefix(prefix) { }

  void add(const std::string& header); 
  void update(HMAC_CTX *ctx);
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

void* HeaderSorter_new(char *prefix);
void  HeaderSorter_add(void *obj, char * s, size_t len);
void  HeaderSorter_update(void *obj, HMAC_CTX *ctx);
void  HeaderSorter_delete(void *obj);

#ifdef __cplusplus
}
#endif

