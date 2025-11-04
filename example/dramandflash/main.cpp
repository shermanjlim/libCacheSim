//
// Created by Sherman Lim on 11/3/25.
//

#include <libCacheSim.h>

#include <string>

int main(int argc, char **argv) {
  /* setup a reader */
  reader_t *reader = open_trace(argv[1], LCS_TRACE, NULL);

  /* set up a request */
  request_t *req = new_request();

  /* read one request and print */
  read_one_req(reader, req);
  print_request(req);

  /* setup a DRAM cache */
  common_cache_params_t dramcache_cc_params = {
      .cache_size = std::stoull(argv[2]),
      .default_ttl = 86400 * 300,
      .hashpower = 24,
      .consider_obj_metadata = false,
  };
  cache_t *dramcache = LRU_init(dramcache_cc_params, NULL);

  /* setup a flashcache */
  common_cache_params_t flashcache_cc_params = {
      .cache_size = std::stoull(argv[3]),
      .default_ttl = 86400 * 300,
      .hashpower = 24,
      .consider_obj_metadata = false,
  };
  cache_t *flashcache = MAGIC_init(flashcache_cc_params, NULL);
  flashcache->admissioner =
      create_admissioner(argv[4], argc > 5 ? argv[5] : NULL);

  int64_t n_miss = 0, n_req = 0;
  while (read_one_req(reader, req) == 0) {
    if (!flashcache->get(flashcache, req)) {
      if (!dramcache->get(dramcache, req)) {
        n_miss++;
      }
    }
    n_req++;
  }

  printf("miss ratio: %lf\n", (double)n_miss / n_req);

  free_request(req);
  dramcache->cache_free(dramcache);
  flashcache->cache_free(flashcache);
  close_reader(reader);

  return 0;
}
