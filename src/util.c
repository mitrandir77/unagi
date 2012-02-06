/* -*-mode:c;coding:utf-8; c-basic-offset:2;fill-column:70;c-file-style:"gnu"-*-
 *
 * Copyright (C) 2012 Gaël Le Mignot <kilobug@kilobug.org>
 *               2009-2012 Arnaud "arnau" Fontaine <arnau@mini-dweeb.org>
 *
 * This  program is  free  software: you  can  redistribute it  and/or
 * modify  it under the  terms of  the GNU  General Public  License as
 * published by the Free Software  Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT  ANY  WARRANTY;  without   even  the  implied  warranty  of
 * MERCHANTABILITY or  FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 *
 * You should have  received a copy of the  GNU General Public License
 *  along      with      this      program.      If      not,      see
 *  <http://www.gnu.org/licenses/>.
 */

/** \file
 *  \brief Miscellaneous helpers not related to X
 */

#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include "structs.h"
#include "util.h"

#define DO_DISPLAY_MESSAGE(LABEL)                       \
  {                                                     \
    va_list ap;                                         \
    va_start(ap, fmt);                                  \
    fprintf(stderr, LABEL ": %s:%d: ", func, line);     \
    vfprintf(stderr, fmt, ap);                          \
    va_end(ap);                                         \
    putc('\n', stderr);                                 \
  }

/** Fatal error message which exits the program
 *
 * \param line Line number
 * \param func Calling function string
 * \param fmt Format of the message
 */
void
_fatal(const bool do_exit, const int line, const char *func, const char *fmt, ...)
{
  DO_DISPLAY_MESSAGE("FATAL");

  if(do_exit)
    exit(EXIT_FAILURE);
}

/** Warning message
 *
 * \param line Line number
 * \param func Calling function string
 * \param fmt Format of the message
 */
void
_warn(const int line, const char *func, const char *fmt, ...)
{
  DO_DISPLAY_MESSAGE("WARN");
}

/** Debugging message
 *
 * \param line Line number
 * \param func Calling function string
 * \param fmt Format of the message
 */
void
#ifdef __DEBUG__
_debug(const int line, const char *func, const char *fmt, ...)
{
  DO_DISPLAY_MESSAGE("DEBUG")
}
#else
_debug(const int line __attribute__((unused)),
       const char *func __attribute__((unused)),
       const char *fmt __attribute__((unused)),
       ...)
{
}
#endif /* __DEBUG__ */

/** Implementation of  a lightweight  balanced Binary Tree  (AVL) with
 *  uint32_t as key and void * as values, meaningful when lookups need
 *  to be efficient (for instance when  getting a window in each event
 *  handler)
 */

#define ROTATE_RIGHT 1
#define ROTATE_LEFT 0

/** Create a new  empty tree. In fact, just return  NULL because empty
 *  tree is NULL, but this is implementation-specific.
 */
util_itree_t *
util_itree_new(void)
{
  return NULL;
}

/** Create a new tree with a single node.
 *
 * \return NULL in case of error (malloc error)
 */
static util_itree_t *
util_itree_new_node(uint32_t key, void *value)
{
  util_itree_t *node;

  node = malloc(sizeof(*node));
  if(node == NULL)
    return NULL;

  node->key = key;
  node->value = value;
  node->height = 1;
  node->left = NULL;
  node->right = NULL;
  node->parent = NULL;

  return node;
}

/** Get the size (number of elements) of a tree */
static int
util_itree_height(util_itree_t *tree)
{
  if(tree == NULL)
    return 0;
  return tree->height;
}

/** Get the balance factor of a node */
static int
util_itree_balance(util_itree_t *node)
{
  int left, right;

  left = util_itree_height(node->left);
  right = util_itree_height(node->right);
  return left - right;
}

/** Fix the height value of a node */
static void
util_itree_fix_height(util_itree_t *node)
{
  int left, right;

  left = util_itree_height(node->left);
  right = util_itree_height(node->right);
  node->height = ((left > right) ? left : right) + 1;
}

/** Perform a rotate in the tree, return new root  */
static util_itree_t *
util_itree_rotate(util_itree_t *tree, util_itree_t *node, int direction)
{
  util_itree_t *new, *parent = node->parent;

  if(direction == ROTATE_RIGHT)
    {
      new = node->left;
      node->left = new->right;
      if(new->right)
	new->right->parent = node;

      node->parent = new;
      new->right = node;
    }
  else
    {
      new = node->right;
      node->right = new->left;
      if(new->left)
	new->left->parent = node;

      node->parent = new;
      new->left = node;
    }

  new->parent = parent;
  if(parent)
    {
      if(parent->left == node)
	parent->left = new;
      if(parent->right == node)
	parent->right = new;
    }
  else
    tree = new;

  util_itree_fix_height(node);
  util_itree_fix_height(new);
  return tree;
}

/** Rebalance a tree starting from a node - subnodes must be clean */
static util_itree_t *
util_itree_rebalance(util_itree_t *tree, util_itree_t *node)
{
  int balance;

  for(; node; node = node->parent)
    {
      util_itree_fix_height(node);
      balance = util_itree_balance(node);

      if(balance <= -2)
	{
	  if(util_itree_balance(node->right) == 1)
	    tree = util_itree_rotate(tree, node->right, ROTATE_RIGHT);
	  tree = util_itree_rotate(tree, node, ROTATE_LEFT);
	}

      if(balance >= 2)
	{
	  if(util_itree_balance(node->left) == -1)
	    tree = util_itree_rotate(tree, node->left, ROTATE_LEFT);
	  tree = util_itree_rotate(tree, node, ROTATE_RIGHT);
	}
    }

  return tree;
}

/** Internal function : does a lookup on the tree.
 *
 *  It'll return a pointer to where the is supposed to be, wherever it
 *  was found or not.  Optionally, it can also return a pointer to the
 *  parent node.
 */
static util_itree_t **
util_itree_lookup(util_itree_t **tree, util_itree_t **parent, uint32_t key)
{
  if((*tree) == NULL)
    return tree;

  if((*tree)->key == key)
    return tree;

  if(parent)
    *parent= *tree;

  if((*tree)->key > key)
    return util_itree_lookup(&((*tree)->left), parent, key);
  else
    return util_itree_lookup(&((*tree)->right), parent, key);
}

/** Insert a value in the tree.
 *
 * \return NULL in case of malloc error
 */
util_itree_t *
util_itree_insert(util_itree_t *tree, uint32_t key, void *value)
{
  util_itree_t *res = tree, *parent = NULL;
  util_itree_t **node = util_itree_lookup(&res, &parent, key);

  /* Already here, just returns the existing */
  if(*node)
    return tree;

  *node = util_itree_new_node(key, value);
  if(*node == NULL)
    return NULL;

  (*node)->parent = parent;

  return util_itree_rebalance(res, parent);
}

/* Get the value corresponding to a key.
 *
 * \return NULL if key is not found.
 */
void *
util_itree_get(util_itree_t *tree, uint32_t key)
{
  util_itree_t **node = util_itree_lookup(&tree, NULL, key);

  if((*node) != NULL)
    return (*node)->value;

  return NULL;
}

/** Remove a key from the tree.
 *
 * \return the new root, NULL if the tree is empty
 */
util_itree_t *
util_itree_remove(util_itree_t *tree, uint32_t key)
{
  util_itree_t *parent = NULL;
  util_itree_t **slot = util_itree_lookup(&tree, &parent, key);
  util_itree_t *node = *slot;

  if(node == NULL)
    return tree;

  if((node->left == NULL) || (node->right == NULL))
    {
      /* Easy case, we reach an almost empty node */
      if(node->left)
	*slot = node->left;
      else
	*slot = node->right;
      if(*slot)
	(*slot)->parent = node->parent;
    }
  else
    {
      /* Hard case... */

      /* Locate the node just before ours, in search order */
      util_itree_t *new = node->left;
      for(; new->right; new = new->right);

      /* Ok, that node has to be but in our place */
      parent = new->parent;
      if(parent != node)
	{
	  parent->right = new->left;
	  if(parent->right)
	    parent->right->parent = parent;
	  new->left = node->left;
	  new->left->parent = new;
	}
      else
	parent = new;
      *slot = new;
      new->parent = node->parent;
      new->right = node->right;
      new->right->parent = new;
    }
  free(node);

  return util_itree_rebalance(tree, parent);
}

/** Destroy a tree completly. Be  careful, you need to manually handle
 *  the freeing of values
 */
void
util_itree_free(util_itree_t *tree)
{
  if(tree == NULL)
    return;

  util_itree_free(tree->left);
  util_itree_free(tree->right);
  free(tree);
}

/** Get the size of a tree. */
uint32_t
util_itree_size(util_itree_t *tree)
{
  if(tree == NULL)
    return 0;

  return util_itree_size(tree->left) + util_itree_size(tree->right) + 1;
}

#ifdef __DEBUG__
/** Print the tree, inner function */
static void
util_itree_print_rec(FILE *stream, util_itree_t *tree, uint32_t indent)
{
  uint32_t i;
  for(i = 0; i < indent; i++)
    fprintf(stream, " | ");

  if(!tree)
    fprintf(stream, " + NULL\n");
  else
    {
      fprintf(stream, " + %d (%p, %d)\n", tree->key, tree->value, tree->height);
      util_itree_print_rec(stream, tree->left, indent + 1);
      util_itree_print_rec(stream, tree->right, indent + 1);
    }
}

/** Print a tree (for debug purpose) */
void
util_itree_print(FILE *stream, util_itree_t *tree)
{
  util_itree_print_rec(stream, tree, 0);
  fprintf(stream, "\n");
}

/* Perform a self-check on the tree (for debug purpose)
   Write potential errors on stream, return 0 in case of success */
int
util_itree_check(FILE *stream, util_itree_t *tree)
{
  int balance, local = 0, left, right, height;

  if(tree == NULL)
    return 0;

  left = util_itree_check(stream, tree->left);
  right = util_itree_check(stream, tree->right);

  balance = util_itree_balance(tree);
  if((balance >= 2) || (balance <= -2))
    {
      fprintf(stream, "ERROR : At node %d, balance is %d\n", tree->key, balance);
      local = 2;
    }

  height = tree->height;
  util_itree_fix_height(tree);
  if(height != tree->height)
    {
      fprintf(stream,
	      "WARNING : At node %d, height was %d, should have been %d\n",
	      tree->key, height, tree->height);
      local = 1;
    }

  if(tree->left)
    {
      if(tree->left->key >= tree->key)
	{
	  fprintf(stream, "ERROR : At node %d, left tree has higher key %d\n",
		  tree->key, tree->left->key);
	  local = 2;
	}
      if(tree->left->parent != tree)
	{
	  fprintf(stream,
		  "ERROR : At node %d, left tree has parent %d\n",
		  tree->key, tree->left->parent->key);
	  local = 2;
	}
    }

  if(tree->right)
    {
      if(tree->right->key <= tree->key)
	{
	  fprintf(stream, "ERROR : At node %d, right tree has lower key %d\n",
		  tree->key, tree->right->key);
	  local = 2;
	}
      if(tree->right->parent != tree)
	{
	  fprintf(stream,
		  "ERROR : At node %d, right tree has parent %d\n",
		  tree->key, tree->right->parent->key);
	  local = 2;
	}
    }

  if(left > local)
    local = left;

  if(right > local)
    local = right;

  return local;
}

#endif /* __DEBUG__ */
