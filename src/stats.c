/* Copyright (C) 2008 Patrick Galbraith, Brian Aker

  See COPYING file found with distribution for license.

*/

#include <mysql.h>
#include <string.h>

#include <stdio.h>
#include <err.h>

#include <libmemcached/memcached.h>
#include "common.h"

my_bool memc_stats_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
char *memc_stats(UDF_INIT *initid, UDF_ARGS *args,
                 char *result,
                 unsigned long *length,
                 char *is_null,
                 char *error);
void memc_stats_deinit(UDF_INIT *initid);
my_bool memc_stat_get_keys_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
char *memc_stat_get_keys(UDF_INIT *initid, UDF_ARGS *args,
                         char *result,
                         unsigned long *length,
                         char *is_null,
                         char *error);
void memc_stat_get_keys_deinit(UDF_INIT *initid);
my_bool memc_stat_get_value_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
char *memc_stat_get_value(UDF_INIT *initid, UDF_ARGS *args,
                          __attribute__ ((unused)) char *result,
                          unsigned long *length,
                          char *is_null,
                          char *error);
void memc_stat_get_value_deinit(UDF_INIT *initid);

my_bool memc_stats_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
  unsigned int x;
  memcached_return rc;
  memc_function_st *container;

  /* this is how to fail */
  if (args->arg_count != 1)
  {
    strncpy(message,
            "1 argument required: servers, comma-separated: memc_stats(<servers>)",
            MYSQL_ERRMSG_SIZE);
    return 1;
  }

  args->arg_type[0]= STRING_RESULT;

  initid->max_length= MEMC_UDF_MAX_SIZE;
  if ((container = calloc(1, sizeof(memc_function_st))) == NULL)
    err(1, NULL);

  /* Init the memcached_st we will use for this pass */
  rc= memc_get_servers(&container->memc);

  /* Now setup the string */
  container->stats_string= string_create(1024);

  initid->ptr= (char *)container;

  return 0;
}

char *memc_stats(UDF_INIT *initid, UDF_ARGS *args,
                __attribute__ ((unused)) char *result,
               unsigned long *length,
                char *is_null,
                char *error)
{
  unsigned int x;
  memcached_return rc;
  char buf[100];

  memcached_stat_st *stat;
  memcached_server_st *servers;
  /*memcached_server_st *server_list;*/

  memc_function_st *container= (memc_function_st *)initid->ptr;
  string_reset(container->stats_string);

  servers= memcached_servers_parse(args->args[0]);
  memcached_server_push(&container->memc, servers);
  memcached_server_list_free(servers);

  stat= memcached_stat(&container->memc, NULL, &rc);

  if (rc != MEMCACHED_SUCCESS && rc != MEMCACHED_SOME_ERRORS)
  {
    /*
      SECURITY FIX: error is a pointer to a single byte per MySQL's UDF
      ABI, not a string buffer. The original sprintf(error, ...) here
      overflowed it (bounded by memcached_strerror()'s fixed strings,
      but still real stack corruption), and then returned that same
      1-byte address as if it were a valid result buffer. Log to
      stderr and use the documented is_null/error flags instead.
    */
    fprintf(stderr, "memc_stats: failure to communicate with servers (%s)\n",
            memcached_strerror(&container->memc, rc));
    if (stat != NULL)
      free(stat);
    *is_null= 1;
    *error= 1;
    *length= 0;
    return NULL;
  }

  /*server_list= memcached_server_list(&container->memc);*/

  sprintf(buf, "Listing %u Server\n\n", memcached_server_count(&container->memc));
  string_append(container->stats_string, buf);
  for (x= 0; x < memcached_server_count(&container->memc); x++)
  {
    char **list;
    char **ptr;
    memcached_server_instance_st instance=
        (memcached_server_instance_st) memcached_server_instance_by_position(&container->memc, x);

    list= memcached_stat_get_keys(&container->memc, &stat[x], &rc);

    sprintf(buf, "Server: %s (%u)\n",
            memcached_server_name(instance),
            memcached_server_port(instance));


    string_append(container->stats_string, buf);

    for (ptr= list; *ptr; ptr++)
    {
      memcached_return rc;
      char *value= memcached_stat_get_value(&container->memc, &stat[x], *ptr, &rc);

      sprintf(buf, "\t %s: %s\n", *ptr, value);
      free(value);
      string_append(container->stats_string, buf);
    }

    free(list);
    string_append(container->stats_string,"\n");
  }
  *length= container->stats_string->length;
  free(stat);
  return container->stats_string->string;
}

void memc_stats_deinit(UDF_INIT *initid)
{
  /* if we allocated initid->ptr, free it here */
  memc_function_st *container= (memc_function_st *)initid->ptr;

  free_string(container->stats_string);
  memcached_free(&container->memc);
  free(container);

  return;
}


my_bool memc_stat_get_value_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
  char **list;
  char **ptr;
  memcached_return rc;
  int exists= 0;
  memc_function_st *container;
  memcached_stat_st *stat;
  memcached_server_st *servers;


  /* this is how to fail */
  if (args->arg_count != 2)
  {
    strncpy(message,
            "two arguments must be supplied: memc_stat_get_value('<server>', '<stat name>')",
            MYSQL_ERRMSG_SIZE);
    return 1;
  }

  initid->max_length= MEMC_UDF_MAX_SIZE;
  if ((container = calloc(1, sizeof(memc_function_st))) == NULL)
    err(1, NULL);

  /* Init the memcached_st we will use for this pass */
  rc= memc_get_servers(&container->memc);
  servers= memcached_servers_parse(args->args[0]);
  memcached_server_push(&container->memc, servers);
  memcached_server_list_free(servers);

  stat= memcached_stat(&container->memc, NULL, &rc);
  if (stat == NULL)
  {
    strncpy(message, "ERROR: unable to retrieve stats from server", MYSQL_ERRMSG_SIZE);
    memcached_free(&container->memc);
    free(container);
    return 1;
  }

  list= memcached_stat_get_keys(&container->memc, &stat[0], &rc);
  for (ptr= list; *ptr; ptr++)
  {
    if (!strcmp(args->args[1], *ptr))
    {
      exists++;
    }
  }
  if (!exists)
  {
    /*
      SECURITY FIX (primary finding): the original code did
      sprintf(err_buf, "...%s...", args->args[1]) into a fixed
      50-byte stack buffer, where args->args[1] is the raw,
      attacker-controlled "stat name" argument from the SQL call
      with no length limit. Any stat name longer than about 9
      characters overflowed err_buf on the stack. snprintf() with a
      precision on %s bounds how much of args->args[1] is read/copied
      regardless of its actual length, and writes directly into
      `message`, which MySQL already sized to MYSQL_ERRMSG_SIZE -
      removing the intermediate undersized buffer entirely.
    */
    snprintf(message, MYSQL_ERRMSG_SIZE,
             "ERROR: the stat key '%.100s' is not a valid stat!",
             args->args[1]);
    free(list);
    free(stat);
    memcached_free(&container->memc);
    free(container);
    return 1;
  }

  free(list);
  free(stat);
  initid->ptr= (char *)container;

  return 0;
}
/*
  memc_get
  get cached object, takes hash-key arg
*/
char *memc_stat_get_value(UDF_INIT *initid, UDF_ARGS *args,
                __attribute__ ((unused)) char *result,
               unsigned long *length,
                char *is_null,
                char *error)
{
  memcached_return rc;
  char *value= NULL;
  char **list;
  char **ptr;
  int exists= 0;

  memcached_stat_st *stat;
  memcached_server_st *servers;

  memc_function_st *container= (memc_function_st *)initid->ptr;

  servers= memcached_servers_parse(args->args[0]);
  memcached_server_push(&container->memc, servers);
  memcached_server_list_free(servers);

  stat= memcached_stat(&container->memc, NULL, &rc);
  if (stat == NULL)
  {
    fprintf(stderr, "memc_stat_get_value: unable to retrieve stats\n");
    *is_null= 1;
    *error= 1;
    *length= 0;
    return NULL;
  }

  list= memcached_stat_get_keys(&container->memc, &stat[0], &rc);
  for (ptr= list; *ptr; ptr++)
  {
    if (!strcmp(args->args[1], *ptr))
    {
      exists++;
    }
  }

  if (exists)
  {
    value= memcached_stat_get_value(&container->memc, &stat[0], args->args[1], &rc);
    if (value != NULL)
    {
      /*
        libmemcached hands back a freshly malloc'd string here. Copy it
        into a buffer the container owns and frees in _deinit (reused
        across calls), then free libmemcached's copy, instead of
        returning its raw allocation straight to MySQL with nothing
        left to ever free it.
      */
      if (container->stats_string == NULL)
        container->stats_string= string_create(strlen(value) + 1);
      else
        string_reset(container->stats_string);
      string_append(container->stats_string, value);
      free(value);
      *length= container->stats_string->length;
      value= container->stats_string->string;
    }
    else
    {
      *is_null= 1;
      *length= 0;
    }
  }
  else
  {
    /*
      SECURITY FIX: same bug class as memc_stat_get_value_init, found
      while patching it - sprintf(error, "...%s...", args->args[1])
      wrote the attacker-controlled, unbounded "stat name" argument
      into the single-byte error flag pointer. Log to stderr and use
      the documented is_null/error flags instead.
    */
    fprintf(stderr, "memc_stat_get_value: stat key '%s' is not valid\n",
            args->args[1]);
    *is_null= 1;
    *error= 1;
    *length= 0;
    value= NULL;
  }

  free(list);
  free(stat);

  return value;
}

/* de-init UDF */
void memc_stat_get_value_deinit(UDF_INIT *initid)
{
  /* if we allocated initid->ptr, free it here */
  memc_function_st *container= (memc_function_st *)initid->ptr;

  if (container->stats_string != NULL)
    free_string(container->stats_string);
  memcached_free(&container->memc);
  free(container);

  return;
}

my_bool memc_stat_get_keys_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
  unsigned int x;
  memcached_return rc;
  memc_function_st *container;


  /* this is how to fail */
  if (args->arg_count > 1)
  {
    strncpy(message, "This function takes no arguments: memc_stat_get_keys()", MYSQL_ERRMSG_SIZE);
    return 1;
  }

  initid->max_length= MEMC_UDF_MAX_SIZE;
  if ((container = calloc(1, sizeof(memc_function_st))) == NULL)
    err(1, NULL);

  /* Init the memcached_st we will use for this pass */
  rc= memc_get_servers(&container->memc);

  /* Now setup the string */
  container->stats_string= string_create(1024);

  initid->ptr= (char *)container;

  return 0;
}

char *memc_stat_get_keys(UDF_INIT *initid, UDF_ARGS *args,
                __attribute__ ((unused)) char *result,
               unsigned long *length,
                char *is_null,
                char *error)
{
/*
  memc_stat
  get cached object, takes hash-key arg
*/
  char **list;
  char **ptr;
  memcached_stat_st *stat;
  memcached_return rc;
  memc_function_st *container= (memc_function_st *)initid->ptr;

  /*
    SECURITY FIX: this used to pass an uninitialized, stack-allocated
    memcached_stat_st straight into memcached_stat_get_keys() without
    ever calling memcached_stat() to populate it (unlike memc_stats()
    and memc_stat_get_value() elsewhere in this file). libmemcached
    would then walk garbage stack memory as if it were valid stat
    data - undefined behavior, likely a crash or a read of whatever
    happened to be on the stack. Populate it properly first.
  */
  stat= memcached_stat(&container->memc, NULL, &rc);
  if (stat == NULL)
  {
    fprintf(stderr, "memc_stat_get_keys: unable to retrieve stats\n");
    *is_null= 1;
    *error= 1;
    *length= 0;
    return NULL;
  }

  string_reset(container->stats_string);

  list= memcached_stat_get_keys(&container->memc, &stat[0], &rc);
  for (ptr= list; *ptr; ptr++)
  {
    string_append(container->stats_string, *ptr);
    string_append(container->stats_string, "\n");
  }
  free(list);
  free(stat);

  *length= container->stats_string->length;
  return container->stats_string->string;
}
/* de-init UDF */
void memc_stat_get_keys_deinit(UDF_INIT *initid)
{
  /* if we allocated initid->ptr, free it here */
  memc_function_st *container= (memc_function_st *)initid->ptr;

  free_string(container->stats_string);
  memcached_free(&container->memc);
  free(container);

  return;
}
