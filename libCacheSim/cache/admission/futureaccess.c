//
// Created by Sherman on 11/3/25.
//

#include <glib.h>

#include "libCacheSim/admissionAlgo.h"

#ifdef __cplusplus
extern "C" {
#endif

// assumes the trace has the no. of future accesses to the object stored in
// the first feature
#define FUTUREACCESS_FEATURE_IDX 0

typedef struct futureaccess_admissioner {
  int64_t numaccess_threshold;
  GHashTable *seen_times;
} futureaccess_admission_params_t;

bool futureaccess_admit(admissioner_t *admissioner, const request_t *req) {
  futureaccess_admission_params_t *pa =
      (futureaccess_admission_params_t *)admissioner->params;
  // admit the object if the no. of future accesses to it is >= the threshold
  if (req->features[FUTUREACCESS_FEATURE_IDX] >= pa->numaccess_threshold) {
    return true;
  }

  // fallback to bloomfilter
  gpointer key = GINT_TO_POINTER(req->obj_id);
  gpointer n_times =
      g_hash_table_lookup(pa->seen_times, GSIZE_TO_POINTER(req->obj_id));
  if (n_times == NULL) {
    g_hash_table_insert(pa->seen_times, key, GINT_TO_POINTER(1));
    return false;
  } else {
    g_hash_table_insert(pa->seen_times, key,
                        GINT_TO_POINTER(GPOINTER_TO_INT(n_times) + 1));
    return true;
  }
}

static void futureaccess_admissioner_parse_params(
    const char *init_params, futureaccess_admission_params_t *pa) {
  // mostly copied-pasted from size.c
  if (init_params == NULL) {
    pa->numaccess_threshold = 0;
    INFO("use default numaccess_threshold for admission: %ld\n",
         (long)pa->numaccess_threshold);
  } else {
    char *params_str = strdup(init_params);
    char *old_params_str = params_str;
    char *end;

    while (params_str != NULL && params_str[0] != '\0') {
      /* different parameters are separated by comma,
       * key and value are separated by = */
      char *key = strsep((char **)&params_str, "=");
      char *value = strsep((char **)&params_str, ",");

      // skip the white space
      while (params_str != NULL && *params_str == ' ') {
        params_str++;
      }

      if (strcasecmp(key, "numaccess") == 0) {
        pa->numaccess_threshold = strtoll(value, &end, 0);
        if (strlen(end) > 2) {
          ERROR("param parsing error, find string \"%s\" after number\n", end);
        }
        INFO("use numaccess threshold: %ld\n", (long)pa->numaccess_threshold);
      } else {
        ERROR("numaccess admission does not have parameter numaccess\n");
      }
    }
    free(old_params_str);
  }
}

admissioner_t *clone_futureaccess_admissioner(admissioner_t *admissioner) {
  return create_futureaccess_admissioner(admissioner->init_params);
}

void free_futureaccess_admissioner(admissioner_t *admissioner) {
  futureaccess_admission_params_t *pa = admissioner->params;
  g_hash_table_destroy(pa->seen_times);
  free(pa);
  if (admissioner->init_params) {
    free(admissioner->init_params);
  }
  free(admissioner);
}

admissioner_t *create_futureaccess_admissioner(const char *init_params) {
  futureaccess_admission_params_t *pa =
      (futureaccess_admission_params_t *)malloc(
          sizeof(futureaccess_admission_params_t));
  memset(pa, 0, sizeof(futureaccess_admission_params_t));
  futureaccess_admissioner_parse_params(init_params, pa);

  pa->seen_times = g_hash_table_new(g_direct_hash, g_direct_equal);

  admissioner_t *admissioner = (admissioner_t *)malloc(sizeof(admissioner_t));
  memset(admissioner, 0, sizeof(admissioner_t));
  admissioner->params = pa;
  admissioner->admit = futureaccess_admit;
  admissioner->free = free_futureaccess_admissioner;
  admissioner->clone = clone_futureaccess_admissioner;
  if (init_params != NULL) admissioner->init_params = strdup(init_params);

  strncpy(admissioner->admissioner_name, "FutureAccess", CACHE_NAME_LEN - 1);
  admissioner->admissioner_name[CACHE_NAME_LEN - 1] = '\0';
  return admissioner;
}

#ifdef __cplusplus
}
#endif
