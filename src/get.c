/* Copyright (C) 2008 Patrick Galbraith, Brian Aker

  See COPYING file found with distribution for license.

*/

#include <mysql.h>
#include <string.h>

#include <stdio.h>
#include <err.h>

#include <libmemcached/memcached.h>
#include "common.h"

my_bool memc_get_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
char *memc_get(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, char *is_null, char *error);
void memc_get_deinit(UDF_INIT *initid);


my_bool memc_get_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
  unsigned int x;
  memcached_return rc;
  memc_function_st *container;


  /* this is how to fail */
  if (args->arg_count != 1)
  {
    strncpy(message,
            "one argument must be supplied: memc_get('<key>').",
            MYSQL_ERRMSG_SIZE);
    return 1;
  }

  args->arg_type[0]= STRING_RESULT;

  initid->max_length= MEMC_UDF_MAX_SIZE;

  if ((container= calloc(1, sizeof(memc_function_st))) == NULL)
                   err(1, NULL);
  /* Init the memcached_st we will use for this pass */
  rc= memc_get_servers(&container->memc);
  memcached_result_create(&container->memc, &container->results);

  initid->ptr= (char *)container;

  return 0;
}

/*
  memc_get
  get cached object, takes hash-key arg
*/
char *memc_get(UDF_INIT *initid, UDF_ARGS *args,
                __attribute__ ((unused)) char *result,
               unsigned long *length,
                char *is_null,
                __attribute__ ((unused)) char *error)
{
  /* how do I utilise this? Print out in case of error? */
  memcached_return rc;
  /* We'll just hard-code now? */

  memc_function_st *container= (memc_function_st *)initid->ptr;

  rc= memcached_mget(&container->memc, (const char * const *)args->args, (size_t *)args->lengths, 1);
  if(rc!=MEMCACHED_SUCCESS)
  {
    *is_null= 1;
    fprintf(stderr,"Couldn't get key: %s error was %s\n",args->args[0],memcached_strerror(&container->memc, rc));
    return NULL;
  }
  memcached_fetch_result(&container->memc, &container->results, &rc);

  *length= memcached_result_length(&container->results);

  if (! *length)
  {
    *is_null= 1;
  }

  return (char *)memcached_result_value(&container->results);
}

/* de-init UDF */
void memc_get_deinit(UDF_INIT *initid)
{
  /* if we allocated initid->ptr, free it here */
  memc_function_st *container= (memc_function_st *)initid->ptr;

  memcached_result_free(&container->results);
  memcached_free(&container->memc);
  free(container);

  return;
}

my_bool memc_get_by_key_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
  memcached_return rc;
  memc_function_st *container;

  /* this is how to fail */
  if (args->arg_count != 2)
  {
    strncpy(message,
            "2 arguments must be supplied: memc_get_by_key('<master key>', '<key>')",
            MYSQL_ERRMSG_SIZE);
    return 1;
  }

  args->arg_type[0]= args->arg_type[1]= STRING_RESULT;

  initid->max_length= MEMC_UDF_MAX_SIZE;
  if((container=calloc(1,sizeof(memc_function_st)))==NULL)
    err(1, NULL);
  /* Init the memcached_st we will use for this pass */
  rc= memc_get_servers(&container->memc);
  memcached_result_create(&container->memc, &container->results);

  initid->ptr= (char *)container;

  return 0;
}

/*
  memc_get
  get cached object, takes hash-key arg
*/
char *memc_get_by_key(UDF_INIT *initid, UDF_ARGS *args,
                      __attribute__ ((unused)) char *result,
                      unsigned long *length,
                      char *is_null,
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
                              (const char * const*)keys,
                              lengths,
                              args->arg_count - 1);

  memcached_fetch_result(&container->memc, &container->results, &rc);
  *length= memcached_result_length(&container->results);
  if (! *length)
  {
    *is_null= 1;
  }

  return (char *)(memcached_result_value(&container->results));
}

/* de-init UDF */
void memc_get_by_key_deinit(UDF_INIT *initid)
{
  /* if we allocated initid->ptr, free it here */
  memc_function_st *container= (memc_function_st *)initid->ptr;

  memcached_result_free(&container->results);
  memcached_free(&container->memc);
  free(container);

  return;
}
