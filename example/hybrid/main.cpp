//
// Created by Sherman Lim on 11/10/25.
//

#include <libCacheSim.h>

#include <string>

cache_t *dramcache;
cache_t *flashcache;
request_t *evict_req = new_request();

void dram_evict_hook(cache_obj_t *obj_to_evict) {
  evict_req->obj_id = obj_to_evict->obj_id;
  evict_req->obj_size = obj_to_evict->obj_size;

  cache_obj_t *obj = flashcache->find(flashcache, evict_req, false);
  bool hit = (obj != NULL);

  if (hit) {
  } else if (!flashcache->can_insert(flashcache, evict_req)) {
  } else {
    while (flashcache->get_occupied_byte(flashcache) + evict_req->obj_size +
               flashcache->obj_md_size >
           flashcache->cache_size) {
      flashcache->evict(flashcache, evict_req);
    }
    flashcache->insert(flashcache, evict_req);
  }
}

// --------- copied-pasted from cache.c ---------
bool dram_get(cache_t *cache, const request_t *req) {
  cache->n_req += 1;

  cache_obj_t *obj = cache->find(cache, req, true);
  bool hit = (obj != NULL);

  if (cache->admissioner && cache->admissioner->update) {
    cache->admissioner->update(cache->admissioner, req, cache->cache_size);
  }

  if (hit) {
  } else if (!cache->can_insert(cache, req)) {
  } else {
    while (cache->get_occupied_byte(cache) + req->obj_size +
               cache->obj_md_size >
           cache->cache_size) {
      // inserted by us: add a hook to know when DRAM is evicting
      cache_obj_t *obj_to_evict = cache->to_evict(cache, req);
      dram_evict_hook(obj_to_evict);
      // inserted by us: add a hook to know when DRAM is evicting
      cache->evict(cache, req);
    }
    cache->insert(cache, req);
  }

  if (cache->prefetcher && cache->prefetcher->prefetch) {
    cache->prefetcher->prefetch(cache, req);
  }

  return hit;
}

int main(int argc, char **argv) {
  /* setup a reader */
  reader_t *reader = open_trace(argv[1], LCS_TRACE, NULL);

  /* set up a request */
  request_t *req = new_request();

  /* setup a DRAM cache */
  common_cache_params_t dramcache_cc_params = {
      .cache_size = std::stoull(argv[2]),
      .default_ttl = 86400 * 300,
      .hashpower = 24,
      .consider_obj_metadata = false,
  };
  dramcache = LRU_init(dramcache_cc_params, NULL);

  /* setup a flashcache */
  common_cache_params_t flashcache_cc_params = {
      .cache_size = std::stoull(argv[3]),
      .default_ttl = 86400 * 300,
      .hashpower = 24,
      .consider_obj_metadata = false,
  };
  flashcache = HYBRID_init(flashcache_cc_params, NULL);

  int64_t n_miss = 0, n_req = 0;
  while (read_one_req(reader, req) == 0) {
    if (!dram_get(dramcache, req)) {
      if (!flashcache->get(flashcache, req)) {
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
