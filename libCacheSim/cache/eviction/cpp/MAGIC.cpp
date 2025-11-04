#include <iostream>
#include <list>
#include <unordered_map>

#include "dataStructure/hashtable/hashtable.h"
#include "libCacheSim/cache.h"
#include "libCacheSim/cacheObj.h"

#define FUTUREACCESS_FEATURE_IDX 0
#define ISM_FEATURE_IDX 1
#define NUMACCESS_THRESHOLD 1
#define PIGGYBACK_THRESHOLD 6 * 60 * 60  // 6 hours

namespace eviction {
class MAGIC {
 public:
  MAGIC() = default;

  void insert_obj(cache_obj_t *obj) {
    if (next_insert_futurebased) {
      lru_list_future.push_front(obj);
      lru_map_future[obj] = lru_list_future.begin();
    } else {
      lru_list.push_front(obj);
      lru_map[obj] = lru_list.begin();
    }
  }

  void remove_obj(cache_obj_t *obj) {
    if (lru_map_future.count(obj) > 0) {
      auto itr = lru_map_future[obj];
      lru_list_future.erase(itr);
      lru_map_future.erase(obj);
    } else {
      auto itr = lru_map[obj];
      lru_list.erase(itr);
      lru_map.erase(obj);
    }
  }

  void move_obj_to_head(cache_obj_t *obj) {
    next_insert_futurebased = lru_map_future.count(obj) > 0;
    remove_obj(obj);
    insert_obj(obj);
  }

  cache_obj_t *evict_obj(const request_t *req) {
    cache_obj_t *obj;
    if (lru_list_future.size() > (lru_list.size() / 10)) {
      obj = lru_list_future.back();
    } else {
      obj = lru_list.back();
    }
    remove_obj(obj);

    // track evicted objects and the time they were evicted
    evicted_objs[obj->obj_id] = req->clock_time;

    return obj;
  }

  bool can_piggyback(const request_t *req) {
    num_req++;
    if ((req->features[ISM_FEATURE_IDX] == 1)) {
      num_maintenance++;
      if ((evicted_objs.count(req->obj_id) > 0) &&
          ((req->clock_time - evicted_objs[req->obj_id]) <
           PIGGYBACK_THRESHOLD)) {
        num_piggyback++;
        return true;
      }
    }
    return false;
  }

  bool can_insert(const request_t *req) {
    ++obj_freq[req->obj_id];

    if (req->features[FUTUREACCESS_FEATURE_IDX] >= NUMACCESS_THRESHOLD) {
      next_insert_futurebased = true;
      return true;
    }

    next_insert_futurebased = false;
    return obj_freq[req->obj_id] > 1;
  }

  void print_stats() {
    std::cout << "Number of requests: " << num_req << std::endl;
    std::cout << "Number of maintenance requests: " << num_maintenance
              << std::endl;
    std::cout << "Number of piggyback requests: " << num_piggyback << std::endl;

    std::cout << "LRU list size: " << lru_list.size() << std::endl;
    std::cout << "LRU list future size: " << lru_list_future.size()
              << std::endl;
  }

 private:
  std::list<cache_obj_t *> lru_list{};
  std::unordered_map<cache_obj_t *, std::list<cache_obj_t *>::iterator>
      lru_map{};

  std::list<cache_obj_t *> lru_list_future{};
  std::unordered_map<cache_obj_t *, std::list<cache_obj_t *>::iterator>
      lru_map_future{};

  std::unordered_map<obj_id_t, int64_t> evicted_objs{};

  std::unordered_map<obj_id_t, size_t> obj_freq{};

  size_t num_req{0};
  size_t num_maintenance{0};
  size_t num_piggyback{0};

  bool next_insert_futurebased{false};
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
  magic->print_stats();
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
  cache->n_req += 1;

  // insert our MAGIC logic here.
  // If the request is labeled maintenance, we pretend that it can be re-ordered
  // earlier in time and would have piggybacked on the blocks that have entered
  // the flash cache. These requests will not have any effect on the cache
  // state. If we can't do the piggyback, proceed as usual.
  auto *magic = static_cast<eviction::MAGIC *>(cache->eviction_params);
  if (magic->can_piggyback(req)) {
    return true;
  }

  // --------- copied-pasted from cache.c ---------
  VERBOSE("******* %s req %ld, obj %ld, obj_size %ld, cache size %ld/%ld\n",
          cache->cache_name, cache->n_req, req->obj_id, req->obj_size,
          cache->get_occupied_byte(cache), cache->cache_size);

  cache_obj_t *obj = cache->find(cache, req, true);
  bool hit = (obj != NULL);

  if (cache->admissioner && cache->admissioner->update) {
    cache->admissioner->update(cache->admissioner, req, cache->cache_size);
  }

  if (hit) {
    VERBOSE("req %ld, obj %ld --- cache hit\n", cache->n_req, req->obj_id);
  } else if (!magic->can_insert(req)) {  // WE REPLACED THIS WITH MAGIC
    VERBOSE("req %ld, obj %ld --- cache miss cannot insert\n", cache->n_req,
            req->obj_id);
  } else {
    while (cache->get_occupied_byte(cache) + req->obj_size +
               cache->obj_md_size >
           cache->cache_size) {
      cache->evict(cache, req);
    }
    cache->insert(cache, req);
  }

  if (cache->prefetcher && cache->prefetcher->prefetch) {
    cache->prefetcher->prefetch(cache, req);
  }

  return hit;
  // --------- copied-pasted from cache.c ---------
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

  cache_obj_t *obj_to_evict = magic->evict_obj(req);
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
