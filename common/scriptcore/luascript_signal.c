/*****************************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*****************************************************************************/

/*****************************************************************************
  Signals implementation.

  New signal types can be declared with script_signal_create. Each
  signal should have a unique name string.
  All signal declarations are in signals_create, for convenience.

  A signal may have any number of Lua callback functions connected to it
  at any given time.

  A signal emission invokes all associated callbacks in the order they were
  connected:

  * A callback can stop the current signal emission, preventing the callbacks
    connected after it from being invoked.

  * A callback can detach itself from its associated signal.

  Lua callbacks functions are able to do these via their return values.

  All Lua callback functions can return a value. Example:
    return false

  If the value is 'true' the current signal emission will be stopped.
*****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdarg.h>

/* utility */
#include "deprecations.h"
#include "log.h"

/* common/scriptcore */
#include "luascript.h"
#include "luascript_types.h"

#include "luascript_signal.h"

struct signal;
struct signal_callback;

/* get 'struct signal_callback_list' and related functions: */
#define SPECLIST_TAG signal_callback
#define SPECLIST_TYPE struct signal_callback
#include "speclist.h"

#define signal_callback_list_iterate(list, pcallback)                        \
  TYPED_LIST_ITERATE(struct signal_callback, list, pcallback)
#define signal_callback_list_iterate_end                                     \
  LIST_ITERATE_END

static struct signal_callback *signal_callback_new(const char *name);
static void signal_callback_destroy(struct signal_callback *pcallback);
static struct signal *signal_new(int nargs, enum api_types *parg_types);
static void signal_destroy(struct signal *psignal);

/* Signal datastructure. */
struct signal {
  int nargs;                              /* Number of arguments to pass */
  enum api_types *arg_types;              /* Argument types */
  struct signal_callback_list *callbacks; /* Connected callbacks */
  struct signal_deprecator deprecator;
};

/* Signal callback datastructure. */
struct signal_callback {
  char *name;                             /* callback function name */
};

/*****************************************************************************
  Signal hash table.
*****************************************************************************/
#define SPECHASH_TAG luascript_signal
#define SPECHASH_ASTR_KEY_TYPE
#define SPECHASH_IDATA_TYPE struct signal *
#define SPECHASH_IDATA_FREE signal_destroy
#include "spechash.h"

#define signal_hash_iterate(phash, key, data)                                \
  TYPED_HASH_ITERATE(char *, struct signal *, phash, key, data)
#define signal_hash_iterate_end                                              \
  HASH_ITERATE_END

/* get 'struct luascript_signal_name_list' and related functions: */
#define SPECLIST_TAG luascript_signal_name
#define SPECLIST_TYPE char
#include "speclist.h"

#define luascript_signal_name_list_iterate(list, pname)                      \
  TYPED_LIST_ITERATE(struct signal_callback, list, pcallback)
#define luascript_signal_name_list_iterate_end                               \
  LIST_ITERATE_END

/**********************************************************************//**
  Create a new signal callback.
**************************************************************************/
static struct signal_callback *signal_callback_new(const char *name)
{
  struct signal_callback *pcallback = fc_malloc(sizeof(*pcallback));

  pcallback->name = fc_strdup(name);
  return pcallback;
}

/**********************************************************************//**
  Free a signal callback.
**************************************************************************/
static void signal_callback_destroy(struct signal_callback *pcallback)
{
  free(pcallback->name);
  free(pcallback);
}

/**********************************************************************//**
  Create a new signal.
**************************************************************************/
static struct signal *signal_new(int nargs, enum api_types *parg_types)
{
  struct signal *psignal = fc_malloc(sizeof(*psignal));

  psignal->nargs = nargs;
  psignal->arg_types = parg_types;
  psignal->callbacks
    = signal_callback_list_new_full(signal_callback_destroy);
  psignal->deprecator.depr_msg = nullptr;
  psignal->deprecator.retired = nullptr;

  return psignal;
}

/**********************************************************************//**
  Free a signal.
**************************************************************************/
static void signal_destroy(struct signal *psignal)
{
  if (psignal->arg_types) {
    free(psignal->arg_types);
  }
  if (psignal->deprecator.depr_msg) {
    free(psignal->deprecator.depr_msg);
  }
  if (psignal->deprecator.retired) {
    free(psignal->deprecator.retired);
  }
  signal_callback_list_destroy(psignal->callbacks);
  free(psignal);
}

/**********************************************************************//**
  Invoke all the callback functions attached to a given signal.
**************************************************************************/
void luascript_signal_emit_valist(struct fc_lua *fcl,
                                  const char *signal_name,
                                  va_list args)
{
  struct signal *psignal;

  fc_assert_ret(fcl);
  fc_assert_ret(fcl->signals);

  if (luascript_signal_hash_lookup(fcl->signals, signal_name, &psignal)) {
    signal_callback_list_iterate(psignal->callbacks, pcallback) {
      va_list args_cb;

      va_copy(args_cb, args);
      if (luascript_callback_invoke(fcl, pcallback->name, psignal->nargs,
                                    psignal->arg_types, args_cb)) {
        va_end(args_cb);
        break;
      }
      va_end(args_cb);
    } signal_callback_list_iterate_end;
  } else {
    luascript_log(fcl, LOG_ERROR, "Signal \"%s\" does not exist, so cannot "
                                  "be invoked.", signal_name);
  }
}

/**********************************************************************//**
  Invoke all the callback functions attached to a given signal.
**************************************************************************/
void luascript_signal_emit(struct fc_lua *fcl,
                           const char *signal_name, ...)
{
  va_list args;

  va_start(args, signal_name);
  luascript_signal_emit_valist(fcl, signal_name, args);
  va_end(args);
}

/**********************************************************************//**
  Create a new signal type.
**************************************************************************/
static struct signal *luascript_signal_create_valist(struct fc_lua *fcl,
                                                     const char *signal_name,
                                                     int nargs, va_list args)
{
  struct signal *psignal;

  fc_assert_ret_val(fcl, nullptr);
  fc_assert_ret_val(fcl->signals, nullptr);

  if (luascript_signal_hash_lookup(fcl->signals, signal_name, &psignal)) {
    luascript_log(fcl, LOG_ERROR, "Signal \"%s\" was already created.",
                  signal_name);
    return nullptr;
  } else {
    enum api_types *parg_types;
    char *sn = fc_malloc(strlen(signal_name) + 1);
    struct signal *created;

    if (nargs > 0) {
      int i;

      parg_types = fc_calloc(nargs, sizeof(*parg_types));

      for (i = 0; i < nargs; i++) {
        *(parg_types + i) = va_arg(args, int);
      }
    } else {
      parg_types = nullptr;
    }

    created = signal_new(nargs, parg_types);
    luascript_signal_hash_insert(fcl->signals, signal_name,
                                 created);
    strcpy(sn, signal_name);
    luascript_signal_name_list_append(fcl->signal_names, sn);

    return created;
  }
}

/**********************************************************************//**
  Create a new signal type.
**************************************************************************/
struct signal_deprecator *luascript_signal_create(struct fc_lua *fcl,
                                                  const char *signal_name,
                                                  int nargs, ...)
{
  va_list args;
  struct signal *created;

  va_start(args, nargs);
  created = luascript_signal_create_valist(fcl, signal_name, nargs, args);
  va_end(args);

  if (created != nullptr) {
    return &(created->deprecator);
  }

  return nullptr;
}

/**********************************************************************//**
  Mark signal deprecated.
**************************************************************************/
void deprecate_signal(struct signal_deprecator *deprecator,
                      char *signal_name, char *replacement,
                      char *deprecated_since, char *retired_since)
{
  if (deprecator != nullptr) {
    char buffer[1024];
    char *deprtype
      = ((retired_since != nullptr) ? "Retired:" : "Deprecated:");

    if (deprecated_since != nullptr && replacement != nullptr) {
      if (retired_since != nullptr) {
        fc_snprintf(buffer, sizeof(buffer),
                    "%s lua signal \"%s\", retired since \"%s\", "
                    "and deprecated already since \"%s\", used. "
                    "Use \"%s\" instead",
                    deprtype, signal_name, retired_since, deprecated_since,
                    replacement);
      } else {
        fc_snprintf(buffer, sizeof(buffer),
                    "%s lua signal \"%s\", deprecated since \"%s\", used. "
                    "Use \"%s\" instead",
                    deprtype, signal_name, deprecated_since, replacement);
      }
    } else if (replacement != nullptr) {
      fc_snprintf(buffer, sizeof(buffer),
                  "%s lua signal \"%s\" used. Use \"%s\" instead",
                  deprtype, signal_name, replacement);
    } else {
      fc_snprintf(buffer, sizeof(buffer),
                  "%s lua signal \"%s\" used.", deprtype, signal_name);
    }

    deprecator->depr_msg = fc_strdup(buffer);

    if (retired_since != nullptr) {
      deprecator->retired = fc_strdup(retired_since);
    }
  }
}

/**********************************************************************//**
  Connects a callback function to a certain signal.
**************************************************************************/
void luascript_signal_callback(struct fc_lua *fcl, const char *signal_name,
                               const char *callback_name, bool create)
{
  struct signal *psignal;
  struct signal_callback *pcallback_found = nullptr;

  fc_assert_ret(fcl != nullptr);
  fc_assert_ret(fcl->signals != nullptr);

  if (luascript_signal_hash_lookup(fcl->signals, signal_name, &psignal)) {

    if (psignal->deprecator.depr_msg != nullptr) {
      log_deprecation("%s", psignal->deprecator.depr_msg);
    }

    if (psignal->deprecator.retired != nullptr) {
      luascript_error(fcl->state, "Signal \"%s\" has been retired.",
                      signal_name);
      return;
    }

    /* Check for a duplicate callback */
    signal_callback_list_iterate(psignal->callbacks, pcallback) {
      if (!strcmp(pcallback->name, callback_name)) {
        pcallback_found = pcallback;
        break;
      }
    } signal_callback_list_iterate_end;

    if (create) {
      if (pcallback_found) {
        luascript_error(fcl->state, "Signal \"%s\" already has a callback "
                                    "called \"%s\".", signal_name,
                        callback_name);
      } else {
        signal_callback_list_append(psignal->callbacks,
                                    signal_callback_new(callback_name));
      }
    } else {
      if (pcallback_found) {
        signal_callback_list_remove(psignal->callbacks, pcallback_found);
      }
    }
  } else {
    luascript_error(fcl->state, "Signal \"%s\" does not exist.",
                    signal_name);
  }
}

/**********************************************************************//**
  Returns if a callback function to a certain signal is defined.
**************************************************************************/
bool luascript_signal_callback_defined(struct fc_lua *fcl,
                                       const char *signal_name,
                                       const char *callback_name)
{
  struct signal *psignal;

  fc_assert_ret_val(fcl != nullptr, FALSE);
  fc_assert_ret_val(fcl->signals != nullptr, FALSE);

  if (luascript_signal_hash_lookup(fcl->signals, signal_name, &psignal)) {
    /* Check for a duplicate callback */
    signal_callback_list_iterate(psignal->callbacks, pcallback) {
      if (!strcmp(pcallback->name, callback_name)) {
        return TRUE;
      }
    } signal_callback_list_iterate_end;
  }

  return FALSE;
}

/**********************************************************************//**
  Callback for freeing memory where luascript_signal_name_list has signal
  name.
**************************************************************************/
static void sn_free(char *name)
{
  FC_FREE(name);
}

/**********************************************************************//**
  Initialize script signals and callbacks.
**************************************************************************/
void luascript_signal_init(struct fc_lua *fcl)
{
  fc_assert_ret(fcl != nullptr);

  if (fcl->signals == nullptr) {
    fcl->signals = luascript_signal_hash_new();
    fcl->signal_names = luascript_signal_name_list_new_full(sn_free);
  }
}

/**********************************************************************//**
  Free script signals and callbacks.
**************************************************************************/
void luascript_signal_free(struct fc_lua *fcl)
{
  if (fcl != nullptr && fcl->signals != nullptr) {
    luascript_signal_hash_destroy(fcl->signals);

    luascript_signal_name_list_destroy(fcl->signal_names);

    fcl->signals = nullptr;
  }
}

/**********************************************************************//**
  Return the name of the signal with the given index.
**************************************************************************/
const char *luascript_signal_by_index(struct fc_lua *fcl, int sindex)
{
  fc_assert_ret_val(fcl != nullptr, nullptr);
  fc_assert_ret_val(fcl->signal_names != nullptr, nullptr);

  return luascript_signal_name_list_get(fcl->signal_names, sindex);
}

/**********************************************************************//**
  Return the name of the 'index' callback function of the signal with the
  name 'signal_name'.
**************************************************************************/
const char *luascript_signal_callback_by_index(struct fc_lua *fcl,
                                               const char *signal_name,
                                               int sindex)
{
  struct signal *psignal;

  fc_assert_ret_val(fcl != nullptr, nullptr);
  fc_assert_ret_val(fcl->signals != nullptr, nullptr);

  if (luascript_signal_hash_lookup(fcl->signals, signal_name, &psignal)) {
    struct signal_callback *pcallback
      = signal_callback_list_get(psignal->callbacks, sindex);
    if (pcallback) {
      return pcallback->name;
    }
  }

  return nullptr;
}
