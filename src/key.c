/*
 * Copyright (C) 2009 Arnaud "arnau" Fontaine <arnau@mini-dweeb.org>
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

#include <stdlib.h>
#include <string.h>

#include "key.h"
#include "structs.h"

void
util_lock_mask_get_reply(xcb_get_modifier_mapping_cookie_t cookie)
{
  xcb_get_modifier_mapping_reply_t *modmap_r;
  xcb_keycode_t *modmap, kc;
  xcb_keycode_t *numlockcodes = xcb_key_symbols_get_keycode(globalconf.keysyms, XK_Num_Lock);
  xcb_keycode_t *shiftlockcodes = xcb_key_symbols_get_keycode(globalconf.keysyms, XK_Shift_Lock);
  xcb_keycode_t *capslockcodes = xcb_key_symbols_get_keycode(globalconf.keysyms, XK_Caps_Lock);
  xcb_keycode_t *modeswitchcodes = xcb_key_symbols_get_keycode(globalconf.keysyms, XK_Mode_switch);

  modmap_r = xcb_get_modifier_mapping_reply(globalconf.connection, cookie, NULL);
  modmap = xcb_get_modifier_mapping_keycodes(modmap_r);

  /* Reset the lock masks */
  memset(&globalconf.key_masks, 0, sizeof(globalconf.key_masks));

  int i;
  for(i = 0; i < 8; i++)
    for(int j = 0; j < modmap_r->keycodes_per_modifier; j++)
      {
	kc = modmap[i * modmap_r->keycodes_per_modifier + j];

#define LOOK_FOR(mask, codes)					\
	if(mask == 0 && codes)					\
	  for(xcb_keycode_t *ktest = codes; *ktest; ktest++)	\
	    if(*ktest == kc)					\
	      {							\
		mask = (uint16_t) (1 << i);			\
		break;						\
	      }

          LOOK_FOR(globalconf.key_masks.numlock, numlockcodes)
	  LOOK_FOR(globalconf.key_masks.shiftlock, shiftlockcodes)
	  LOOK_FOR(globalconf.key_masks.capslock, capslockcodes)
	  LOOK_FOR(globalconf.key_masks.modeswitch, modeswitchcodes)
#undef LOOK_FOR
	  }

  free(numlockcodes);
  free(shiftlockcodes);
  free(capslockcodes);
  free(modeswitchcodes);
  free(modmap_r);
}

xcb_keysym_t
util_key_getkeysym(const xcb_keycode_t detail, const uint16_t state)
{
  xcb_keysym_t k0, k1;

  /* 'col'  (third  parameter)  is  used  to  get  the  proper  KeySym
   * according  to  modifier (XCB  doesn't  provide  an equivalent  to
   * XLookupString()).
   *
   * If Mode_Switch is ON we look into second group.
   */
  if(state & globalconf.key_masks.modeswitch)
    {
      k0 = xcb_key_symbols_get_keysym(globalconf.keysyms, detail, 2);
      k1 = xcb_key_symbols_get_keysym(globalconf.keysyms, detail, 3);
    }
  else
    {
      k0 = xcb_key_symbols_get_keysym(globalconf.keysyms, detail, 0);
      k1 = xcb_key_symbols_get_keysym(globalconf.keysyms, detail, 1);
    }

  /* If the second column does not exists use the first one. */
  if(k1 == XCB_NO_SYMBOL)
    k1 = k0;

  /* The  numlock modifier is  on and  the second  KeySym is  a keypad
   * KeySym */
  if((state & globalconf.key_masks.numlock) && xcb_is_keypad_key(k1))
    {
      /* The Shift modifier  is on, or if the Lock  modifier is on and
       * is interpreted as ShiftLock, use the first KeySym */
      if((state & XCB_MOD_MASK_SHIFT) ||
	 (state & XCB_MOD_MASK_LOCK &&
	  (state & globalconf.key_masks.shiftlock)))
	return k0;
      else
	return k1;
    }

  /* The Shift and Lock modifers are both off, use the first KeySym */
  else if(!(state & XCB_MOD_MASK_SHIFT) && !(state & XCB_MOD_MASK_LOCK))
    return k0;

  /* The Shift  modifier is  off and  the Lock modifier  is on  and is
   * interpreted as CapsLock */
  else if(!(state & XCB_MOD_MASK_SHIFT) &&
	  (state & XCB_MOD_MASK_LOCK && (state & globalconf.key_masks.capslock)))
    /* The  first Keysym  is  used  but if  that  KeySym is  lowercase
     * alphabetic,  then the  corresponding uppercase  KeySym  is used
     * instead */
    return k1;

  /* The Shift modifier is on, and the Lock modifier is on and is
   * interpreted as CapsLock */
  else if((state & XCB_MOD_MASK_SHIFT) &&
	  (state & XCB_MOD_MASK_LOCK && (state & globalconf.key_masks.capslock)))
    /* The  second Keysym  is used  but  if that  KeySym is  lowercase
     * alphabetic,  then the  corresponding uppercase  KeySym  is used
     * instead */
    return k1;

  /* The  Shift modifer  is on,  or  the Lock  modifier is  on and  is
   * interpreted as ShiftLock, or both */
  else if((state & XCB_MOD_MASK_SHIFT) ||
	  (state & XCB_MOD_MASK_LOCK && (state & globalconf.key_masks.shiftlock)))
    return k1;

  return XCB_NO_SYMBOL;
}
