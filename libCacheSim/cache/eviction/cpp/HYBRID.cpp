#include <iostream>
#include <list>
#include <unordered_map>

#include "dataStructure/hashtable/hashtable.h"
#include "libCacheSim/cache.h"
#include "libCacheSim/cacheObj.h"

namespace eviction {
class HYBRID {
 public:
  HYBRID() = default;

  void insert_obj(cache_obj_t *obj) {
    lru_list.push_front(obj);
    lru_map[obj] = lru_list.begin();
  }

  void remove_obj(cache_obj_t *obj) {
    lru_list.erase(lru_map[obj]);
    lru_map.erase(obj);
  }

  void move_obj_to_head(cache_obj_t *obj) {
    remove_obj(obj);
    insert_obj(obj);
  }

  cache_obj_t *evict_obj(const request_t *req) {
    cache_obj_t *obj = lru_list.back();
    remove_obj(obj);
    return obj;
  }

  void print_stats() {
    std::cout << "HYBRID LRU cache statistics:" << std::endl;
    std::cout << "  Current size: " << lru_list.size() << " objects"
              << std::endl;
  }

 private:
  std::list<cache_obj_t *> lru_list{};
  std::unordered_map<cache_obj_t *, std::list<cache_obj_t *>::iterator>
      lru_map{};
};
}  // namespace eviction

#ifdef __cplusplus
extern "C" {
#endif

// ***********************************************************************
// ****                                                               ****
// ****                   function declarations                       ****
// ****                                                               ****
// ***********************************************************************

static void HYBRID_free(cache_t *cache);
static bool HYBRID_get(cache_t *cache, const request_t *req);
static cache_obj_t *HYBRID_find(cache_t *cache, const request_t *req,
                                const bool update_cache);
static bool HYBRID_can_insert(cache_t *cache, const request_t *req);
static cache_obj_t *HYBRID_insert(cache_t *cache, const request_t *req);
static void HYBRID_evict(cache_t *cache, const request_t *req);
static bool HYBRID_remove(cache_t *cache, const obj_id_t obj_id);

// ***********************************************************************
// ****                                                               ****
// ****                   end user facing functions                   ****
// ****                                                               ****
// ****                       init, free, get                         ****
// ***********************************************************************

/**
 * @brief initialize the cache
 *
 * @param ccache_params some common cache parameters
 * @param cache_specific_params cache specific parameters, see parse_params
 * function or use -e "print" with the cachesim binary
 */
cache_t *HYBRID_init(const common_cache_params_t ccache_params,
                     const char *cache_specific_params) {
  cache_t *cache =
      cache_struct_init("HYBRID", ccache_params, cache_specific_params);
  auto *hybrid = new eviction::HYBRID();
  cache->eviction_params = hybrid;

  cache->cache_init = HYBRID_init;
  cache->cache_free = HYBRID_free;
  cache->get = HYBRID_get;
  cache->find = HYBRID_find;
  cache->can_insert = HYBRID_can_insert;
  cache->insert = HYBRID_insert;
  cache->evict = HYBRID_evict;
  cache->remove = HYBRID_remove;

  cache->obj_md_size = 0;

  return cache;
}

/**
 * free resources used by this cache
 *
 * @param cache
 */
static void HYBRID_free(cache_t *cache) {
  auto *hybrid = static_cast<eviction::HYBRID *>(cache->eviction_params);
  hybrid->print_stats();
  delete hybrid;
  cache_struct_free(cache);
}

/**
 * @brief this function is the user facing API
 * it performs the following logic
 *
 * ```
 * if obj in cache:
 *    update_metadata
 *    return true
 * else:
 *    if cache does not have enough space:
 *        evict until it has space to insert
 *    insert the object
 *    return false
 * ```
 *
 * @param cache
 * @param req
 * @return true if cache hit, false if cache miss
 */
static bool HYBRID_get(cache_t *cache, const request_t *req) {
  cache->n_req += 1;

  VERBOSE("******* %s req %ld, obj %ld, obj_size %ld, cache size %ld/%ld\n",
          cache->cache_name, cache->n_req, req->obj_id, req->obj_size,
          cache->get_occupied_byte(cache), cache->cache_size);

  cache_obj_t *obj = cache->find(cache, req, true);
  bool hit = (obj != NULL);

  // REMOVED ALL INSERTION, EVICTION LOGIC.
  // We only insert/evict in DRAM eviction hook.

  return hit;
}

// ***********************************************************************
// ****                                                               ****
// ****       developer facing APIs (used by cache developer)         ****
// ****                                                               ****
// ***********************************************************************

/**
 * @brief find an object in the cache
 *
 * @param cache
 * @param req
 * @param update_cache whether to update the cache,
 *  if true, the object is promoted
 *  and if the object is expired, it is removed from the cache
 * @return the object or NULL if not found
 */
static cache_obj_t *HYBRID_find(cache_t *cache, const request_t *req,
                                const bool update_cache) {
  auto *hybrid = static_cast<eviction::HYBRID *>(cache->eviction_params);

  cache_obj_t *obj = cache_find_base(cache, req, update_cache);
  if (obj != nullptr && update_cache) {
    hybrid->move_obj_to_head(obj);
  }
  return obj;
}

static bool HYBRID_can_insert(cache_t *cache, const request_t *req) {
  if (req->obj_size + cache->obj_md_size > cache->cache_size) {
    WARN_ONCE("%ld req, obj %lu, size %lu larger than cache size %lu\n",
              (long)cache->n_req, (unsigned long)req->obj_id,
              (unsigned long)req->obj_size, (unsigned long)cache->cache_size);
    return false;
  }

  return true;
}

/**
 * @brief insert an object into the cache,
 * update the hash table and cache metadata
 * this function assumes the cache has enough space
 * eviction should be
 * performed before calling this function
 *
 * @param cache
 * @param req
 * @return the inserted object
 */
static cache_obj_t *HYBRID_insert(cache_t *cache, const request_t *req) {
  auto *hybrid = static_cast<eviction::HYBRID *>(cache->eviction_params);

  cache_obj_t *obj = cache_insert_base(cache, req);
  hybrid->insert_obj(obj);
  return obj;
}

/**
 * @brief find the object to be evicted
 *
 * @param cache the cache
 * @return the object to be evicted
 */
static void HYBRID_evict(cache_t *cache, const request_t *req) {
  auto *hybrid = static_cast<eviction::HYBRID *>(cache->eviction_params);

  cache_obj_t *obj_to_evict = hybrid->evict_obj(req);
  cache_evict_base(cache, obj_to_evict, true);
}

static void HYBRID_remove_obj(cache_t *cache, cache_obj_t *obj) {
  auto *hybrid = static_cast<eviction::HYBRID *>(cache->eviction_params);

  hybrid->remove_obj(obj);
  cache_remove_obj_base(cache, obj, true);
}

static bool HYBRID_remove(cache_t *cache, const obj_id_t obj_id) {
  auto *hybrid = static_cast<eviction::HYBRID *>(cache->eviction_params);

  cache_obj_t *obj = hashtable_find_obj_id(cache->hashtable, obj_id);
  if (obj == nullptr) {
    return false;
  }

  hybrid->remove_obj(obj);
  cache_remove_obj_base(cache, obj, true);
  return true;
}

#ifdef __cplusplus
}
#endif
