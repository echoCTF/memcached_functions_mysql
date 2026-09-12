/* Copyright (C) 2009 Patrick Galbraith, Brian Aker

  See COPYING file found with distribution for license.

*/

#include <mysql.h>
#include <string.h>

#include <stdio.h>
#include <err.h>

#include <libmemcached/memcached.h>
#include "common.h"

my_bool memc_mset_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
long long memc_mset(UDF_INIT *initid, UDF_ARGS *args, char *is_null, char *error);
void memc_mset_deinit(UDF_INIT *initid);

/*
  memc_mset('k1', 'v1', 'k2', 'v2', ...)

  Sets multiple key/value pairs in a single UDF call, reusing one
  memcached_st clone across all of them instead of paying per-call
  UDF init/deinit overhead (and a fresh clone from master_memc) for
  every key individually.

  Return value: a count of how many of the N pairs were stored
  successfully (0..N), not the 1/0 flag memc_set() uses for a single
  key. A single boolean would throw away exactly the information a
  bulk call is useful for, i.e. which ones failed. This is a UDF, it
  can only return one scalar, so if you need to know *which* keys
  failed, follow up with memc_get() on the ones you care about.

  No optional trailing expiration argument like the single-key ops
  have: every argument here is a key or a value in a pair, so an odd
  argument count is a caller mistake (an unpaired key), not "the last
  one is actually an expiration" - treating it as the latter would
  silently reinterpret an ordinary mistake (forgetting a value) as a
  different, valid-looking call. All pairs are stored with
  expiration 0 (no expiry).
*/
my_bool memc_mset_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
  unsigned int x;
  memcached_return rc;
  memc_function_st *container;

  if (args->arg_count < 2 || (args->arg_count % 2) != 0)
  {
    strncpy(message,
            "an even number of arguments (at least 2) must be supplied: "
            "memc_mset('<key1>', '<value1>', '<key2>', '<value2>', ...)",
            MYSQL_ERRMSG_SIZE);
    return 1;
  }

  for (x= 0; x < args->arg_count; x++)
    args->arg_type[x]= STRING_RESULT;

  if ((container= calloc(1, sizeof(memc_function_st))) == NULL)
    err(1, NULL);

  /* Init the memcached_st we will use for this pass */
  rc= memc_get_servers(&container->memc);

  initid->ptr= (char *)container;

  return 0;
}

long long memc_mset(UDF_INIT *initid, UDF_ARGS *args,
                     __attribute__ ((unused)) char *is_null,
                     __attribute__ ((unused)) char *error)
{
  memcached_return rc;
  unsigned int i;
  long long stored= 0;
  memc_function_st *container= (memc_function_st *)initid->ptr;

  for (i= 0; i < args->arg_count; i+= 2)
  {
    char *key= args->args[i];
    char *value= args->args[i + 1];
    size_t key_length= (size_t) args->lengths[i];
    size_t value_length= (size_t) args->lengths[i + 1];

    if (key == NULL)
    {
      /*
        A NULL key has no sane meaning here. None of the single-key
        UDFs in this codebase guard against this either (only a NULL
        value is special-cased, working around a documented crash),
        but a NULL key handed straight to memcached_set() would still
        be dereferenced for hashing, so skip it rather than risk it.
      */
      fprintf(stderr, "memc_mset: skipping pair %u, key is NULL\n", i / 2);
      continue;
    }

    if (value == NULL)
      value_length= 0;

    rc= memcached_set(&container->memc, key, key_length,
                       value, value_length, 0, (uint16_t) 0);

    if (rc == MEMCACHED_SUCCESS)
      stored++;
    else
      fprintf(stderr, "memc_mset: failed to store key '%s': %s\n",
              key, memcached_strerror(&container->memc, rc));
  }

  return stored;
}

void memc_mset_deinit(UDF_INIT *initid)
{
  memc_function_st *container= (memc_function_st *)initid->ptr;

  memcached_free(&container->memc);
  free(container);
}
