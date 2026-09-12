/* Copyright (C) 2008 Patrick Galbraith, Brian Aker

  See COPYING file found with distribution for license.

*/

#include <mysql.h>
#include <string.h>

#include <stdio.h>
#include <err.h>

#include <libmemcached/memcached.h>
#include "common.h"

my_bool memc_mget_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
char *memc_mget(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, char *is_null, char *error);
void memc_mget_deinit(UDF_INIT *initid);

my_bool memc_mget_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
  unsigned int x;
  memcached_return rc;
  memc_function_st *container;


  /* this is how to fail */
  if (args->arg_count < 1)
  {
    strncpy(message,
            "At least one argument must be supplied: memc_mget('<key,keyN,...>').",
            MYSQL_ERRMSG_SIZE);
    return 1;
  }

  for (x= 0; x < args->arg_count; x++)
  {
    args->arg_type[x]= STRING_RESULT;
  }

  initid->max_length= MEMC_UDF_MAX_SIZE;
  if ((container = calloc(1,sizeof(memc_function_st))) == NULL)
    err(1, NULL);


  /* Init the memcached_st we will use for this pass */
  rc= memc_get_servers(&container->memc);
  memcached_result_create(&container->memc, &container->results);

  initid->ptr= (char *)container;

  return 0;
}

/*
  memc_mget
  get cached object, takes hash-key arg
*/
char *memc_mget(UDF_INIT *initid, UDF_ARGS *args,
                __attribute__ ((unused)) char *result,
               unsigned long *length,
                char *is_null,
                char *error)
{
  memcached_return rc;
  memc_function_st *container= (memc_function_st *)initid->ptr;

  rc= memcached_mget(&container->memc, args->args,
                     (size_t *)args->lengths,
                     args->arg_count);

  /*
    SECURITY FIX: this used to `return ((long long)rc)` from a function
    declared to return char*, and never set *length. MySQL would then
    read *length (uninitialized) bytes starting at address `rc` (a small
    memcached_return enum value), i.e. a near-NULL pointer read -
    a guaranteed crash on every call. Fetch and return the actual first
    result instead, same pattern as memc_get().
  */
  if (rc != MEMCACHED_SUCCESS)
  {
    fprintf(stderr, "memc_mget: request failed: %s\n",
            memcached_strerror(&container->memc, rc));
    *is_null= 1;
    *error= 1;
    *length= 0;
    return NULL;
  }

  memcached_fetch_result(&container->memc, &container->results, &rc);
  *length= memcached_result_length(&container->results);
  if (! *length)
  {
    *is_null= 1;
    return NULL;
  }

  return (char *)memcached_result_value(&container->results);
}

/* de-init UDF */
void memc_mget_deinit(UDF_INIT *initid)
{
  /* if we allocated initid->ptr, free it here */
  memc_function_st *container= (memc_function_st *)initid->ptr;

  memcached_result_free(&container->results);
  memcached_free(&container->memc);
  free(container);

  return;
}

my_bool memc_mget_by_key_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
  memcached_return rc;
  memc_function_st *container;

  /* this is how to fail */
  if (args->arg_count != 2)
  {
    strncpy(message,
            "2 arguments must be supplied: memc_mget_by_key('<master key>', '<key>')",
            MYSQL_ERRMSG_SIZE);
    return 1;
  }

  args->arg_type[0]= args->arg_type[1]= STRING_RESULT;

  initid->max_length= MEMC_UDF_MAX_SIZE;
  if ((container = calloc(1,sizeof(memc_function_st))) == NULL)
    err(1, NULL);

  /* Init the memcached_st we will use for this pass */
  rc= memc_get_servers(&container->memc);
  memcached_result_create(&container->memc, &container->results);

  initid->ptr= (char *)container;

  return 0;
}

/*
  memc_ge
  get cached object, takes hash-key arg
*/
char *memc_mget_by_key(UDF_INIT *initid, UDF_ARGS *args,
                __attribute__ ((unused)) char *result,
               unsigned long *length,
                __attribute__ ((unused)) char *is_null,
                __attribute__ ((unused)) char *error)
{
  /* how do I utilise this? Print out in case of error? */
  memcached_return rc;
  char *value;
  char **keys;
  size_t *lengths;
  keys= args->args;
  keys++;
  lengths= (size_t*)args->lengths;
  lengths++;

  memc_function_st *container= (memc_function_st *)initid->ptr;

  rc= memcached_mget_by_key(&container->memc,
                              args->args[0],
                              (size_t )args->lengths[0],
                              keys,
                              lengths,
                              args->arg_count - 1);

  memcached_fetch_result(&container->memc, &container->results, &rc);
  *length= memcached_result_length(&container->results);

  return (memcached_result_value(&container->results));
}

/* de-init UDF */
void memc_mget_by_key_deinit(UDF_INIT *initid)
{
  /*
    SECURITY FIX: this used to stash initid->ptr into a global and never
    free it, leaking a memc_function_st (plus its cloned memcached_st
    connections and result struct) on every single call - a memory/FD
    exhaustion DoS under repeated use. Free properly instead.
  */
  memc_function_st *container= (memc_function_st *)initid->ptr;

  memcached_result_free(&container->results);
  memcached_free(&container->memc);
  free(container);

  return;
}
