#include <list>
#include <unordered_map>

#include "dataStructure/hashtable/hashtable.h"
#include "libCacheSim/cache.h"
#include "libCacheSim/cacheObj.h"

namespace eviction {
class MAGIC {
 public:
  MAGIC() = default;

  void insert_obj(cache_obj_t *obj) {
    lru_list.push_front(obj);
    lru_map[obj] = lru_list.begin();
  }

  void remove_obj(cache_obj_t *obj) {
    auto itr = lru_map[obj];
    lru_list.erase(itr);
    lru_map.erase(obj);
  }

  void move_obj_to_head(cache_obj_t *obj) {
    remove_obj(obj);
    insert_obj(obj);
  }

  cache_obj_t *evict_obj() {
    cache_obj_t *obj = lru_list.back();
    remove_obj(obj);
    return obj;
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

static void MAGIC_free(cache_t *cache);
static bool MAGIC_get(cache_t *cache, const request_t *req);
static cache_obj_t *MAGIC_find(cache_t *cache, const request_t *req,
                               const bool update_cache);
static cache_obj_t *MAGIC_insert(cache_t *cache, const request_t *req);
static void MAGIC_evict(cache_t *cache, const request_t *req);
static bool MAGIC_remove(cache_t *cache, const obj_id_t obj_id);

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
cache_t *MAGIC_init(const common_cache_params_t ccache_params,
                    const char *cache_specific_params) {
  cache_t *cache =
      cache_struct_init("MAGIC", ccache_params, cache_specific_params);
  auto *magic = new eviction::MAGIC();
  cache->eviction_params = magic;

  cache->cache_init = MAGIC_init;
  cache->cache_free = MAGIC_free;
  cache->get = MAGIC_get;
  cache->find = MAGIC_find;
  cache->insert = MAGIC_insert;
  cache->evict = MAGIC_evict;
  cache->remove = MAGIC_remove;

  cache->obj_md_size = 0;

  return cache;
}

/**
 * free resources used by this cache
 *
 * @param cache
 */
static void MAGIC_free(cache_t *cache) {
  auto *magic = static_cast<eviction::MAGIC *>(cache->eviction_params);
  delete magic;
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
static bool MAGIC_get(cache_t *cache, const request_t *req) {
  return cache_get_base(cache, req);
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
static cache_obj_t *MAGIC_find(cache_t *cache, const request_t *req,
                               const bool update_cache) {
  auto *magic = static_cast<eviction::MAGIC *>(cache->eviction_params);

  cache_obj_t *obj = cache_find_base(cache, req, update_cache);
  if (obj != nullptr && update_cache) {
    magic->move_obj_to_head(obj);
  }
  return obj;
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
static cache_obj_t *MAGIC_insert(cache_t *cache, const request_t *req) {
  auto *magic = static_cast<eviction::MAGIC *>(cache->eviction_params);

  cache_obj_t *obj = cache_insert_base(cache, req);
  magic->insert_obj(obj);
  return obj;
}

/**
 * @brief find the object to be evicted
 *
 * @param cache the cache
 * @return the object to be evicted
 */
static void MAGIC_evict(cache_t *cache, const request_t *req) {
  auto *magic = static_cast<eviction::MAGIC *>(cache->eviction_params);

  cache_obj_t *obj_to_evict = magic->evict_obj();
  cache_evict_base(cache, obj_to_evict, true);
}

static void MAGIC_remove_obj(cache_t *cache, cache_obj_t *obj) {
  auto *magic = static_cast<eviction::MAGIC *>(cache->eviction_params);

  magic->remove_obj(obj);
  cache_remove_obj_base(cache, obj, true);
}

static bool MAGIC_remove(cache_t *cache, const obj_id_t obj_id) {
  auto *magic = static_cast<eviction::MAGIC *>(cache->eviction_params);

  cache_obj_t *obj = hashtable_find_obj_id(cache->hashtable, obj_id);
  if (obj == nullptr) {
    return false;
  }

  magic->remove_obj(obj);
  cache_remove_obj_base(cache, obj, true);
  return true;
}

#ifdef __cplusplus
}
#endif
