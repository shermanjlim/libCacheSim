//
// Created by Sherman Lim on 11/10/25.
//

#include <libCacheSim.h>

#include <string>

int main(int argc, char **argv) {
  /* setup a reader */
  reader_t *reader = open_trace(argv[1], LCS_TRACE, NULL);

  /* set up a request */
  request_t *req = new_request();

  /* setup a cache */
  common_cache_params_t cache_cc_params = {
      .cache_size = std::stoull(argv[2]),
      .default_ttl = 86400 * 300,
      .hashpower = 24,
      .consider_obj_metadata = false,
  };
  cache_t *cache = HYBRID_init(cache_cc_params, NULL);

  int64_t n_miss = 0, n_req = 0;
  while (read_one_req(reader, req) == 0) {
    if (!cache->get(cache, req)) {
      n_miss++;
    }
    n_req++;
  }

  printf("miss ratio: %lf\n", (double)n_miss / n_req);

  free_request(req);
  cache->cache_free(cache);
  close_reader(reader);

  return 0;
}
