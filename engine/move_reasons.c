/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This is GNU GO, a Go program. Contact gnugo@gnu.org, or see       *
 * http://www.gnu.org/software/gnugo/ for more information.          *
 *                                                                   *
 * Copyright 1999, 2000, 2001, 2002 by the Free Software Foundation. *
 *                                                                   *
 * This program is free software; you can redistribute it and/or     *
 * modify it under the terms of the GNU General Public License as    *
 * published by the Free Software Foundation - version 2             *
 *                                                                   *
 * This program is distributed in the hope that it will be useful,   *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of    *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the     *
 * GNU General Public License in file COPYING for more details.      *
 *                                                                   *
 * You should have received a copy of the GNU General Public         *
 * License along with this program; if not, write to the Free        *
 * Software Foundation, Inc., 59 Temple Place - Suite 330,           *
 * Boston, MA 02111, USA.                                            *
\* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "gnugo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "liberty.h"
#include "gg_utils.h"
#include "random.h"
#include "move_reasons.h"


/* All these data structures are declared in move_reasons.h */

struct move_data move[BOARDMAX];
struct move_reason move_reasons[MAX_MOVE_REASONS];
int next_reason;

/* Worms */
int worms[MAX_WORMS];
int next_worm;

/* Dragons */
int dragons[MAX_DRAGONS];
int next_dragon;

/* Connections */
int conn_dragon1[MAX_CONNECTIONS];
int conn_dragon2[MAX_CONNECTIONS];
int next_connection;

/* Unordered worm pairs */
int worm_pair1[MAX_WORM_PAIRS];
int worm_pair2[MAX_WORM_PAIRS];
int next_worm_pair;

/* Unordered sets (currently pairs) of move reasons / targets */
Reason_set either_data[MAX_EITHER];
int        next_either;
Reason_set all_data[MAX_ALL];
int        next_all;

/* Eye shapes */
int eyes[MAX_EYES];
int eyecolor[MAX_EYES];
int next_eye;

/* Lunches */
int lunch_dragon[MAX_LUNCHES]; /* eater */
int lunch_worm[MAX_LUNCHES];   /* food */
int next_lunch;

/* Point redistribution */
int replacement_map[BOARDMAX];

/* Helper functions to check conditions in discard rules. */
typedef int (*discard_condition_fn_ptr)(int pos, int what);

struct discard_rule {
  int reason_type[MAX_REASONS];
  discard_condition_fn_ptr condition;
  int flags;
  char trace_message[MAX_TRACE_LENGTH];
};


/* Initialize move reason data structures. */
void
clear_move_reasons(void)
{
  int i;
  int j;
  int ii;
  int k;
  next_reason = 0;
  next_worm = 0;
  next_dragon = 0;
  next_connection = 0;
  next_worm_pair = 0;
  next_either = 0;
  next_all = 0;
  next_eye = 0;
  next_lunch = 0;
  
  for (i = 0; i < board_size; i++)
    for (j = 0; j < board_size; j++) {
      ii = POS(i, j);

      move[ii].value                  = 0.0;
      move[ii].final_value            = 0.0;
      move[ii].additional_ko_value    = 0.0;
      move[ii].territorial_value      = 0.0;
      move[ii].strategical_value      = 0.0;
      move[ii].maxpos_shape           = 0.0;
      move[ii].numpos_shape           = 0;
      move[ii].maxneg_shape           = 0.0;
      move[ii].numneg_shape           = 0;
      move[ii].followup_value         = 0.0;
      move[ii].reverse_followup_value = 0.0;
      move[ii].secondary_value        = 0.0;
      move[ii].min_value              = 0.0;
      move[ii].max_value              = HUGE_MOVE_VALUE;
      move[ii].min_territory          = 0.0;
      move[ii].max_territory          = HUGE_MOVE_VALUE;
      for (k = 0; k < MAX_REASONS; k++)     
	move[ii].reason[k]            = -1;
      move[ii].move_safety            = 0;
      move[ii].worthwhile_threat      = 0;
      /* The reason we assign a random number to each move immediately
       * is to avoid dependence on which moves are evaluated when it
       * comes to choosing between multiple moves of the same value.
       * In this way we can get consistent results for use in the
       * regression tests.
       */
      move[ii].random_number          = gg_drand();

      /* Do not send away the points (yet). */
      replacement_map[ii] = NO_MOVE;
    }
}

/*
 * Find the index of a worm in the list of worms. If necessary,
 * add a new entry. (str) must point to the origin of the worm.
 */
int
find_worm(int str)
{
  int k;

  ASSERT_ON_BOARD1(str);
  for (k = 0; k < next_worm; k++)
    if (worms[k] == str)
      return k;
  
  /* Add a new entry. */
  gg_assert(next_worm < MAX_WORMS);
  worms[next_worm] = str;
  next_worm++;
  return next_worm - 1;
}

/*
 * Find the index of a dragon in the list of dragons. If necessary,
 * add a new entry. (str) must point to the origin of the dragon.
 */
int
find_dragon(int str)
{
  int k;
  ASSERT_ON_BOARD1(str);
  for (k = 0; k < next_dragon; k++)
    if (dragons[k] == str)
      return k;
  
  /* Add a new entry. */
  gg_assert(next_dragon < MAX_DRAGONS);
  dragons[next_dragon] = str;
  next_dragon++;
  return next_dragon - 1;
}

/*
 * Find the index of a connection in the list of connections.
 * If necessary, add a new entry.
 */
static int
find_connection(int dragon1, int dragon2)
{
  int k;
  
  if (dragon1 > dragon2) {
    /* Swap to canonical order. */
    int tmp = dragon1;
    dragon1 = dragon2;
    dragon2 = tmp;
  }
  
  for (k = 0; k < next_connection; k++)
    if ((conn_dragon1[k] == dragon1) && (conn_dragon2[k] == dragon2))
      return k;
  
  /* Add a new entry. */
  gg_assert(next_connection < MAX_CONNECTIONS);
  conn_dragon1[next_connection] = dragon1;
  conn_dragon2[next_connection] = dragon2;
  next_connection++;
  return next_connection - 1;
}


#if 0

/*
 * Find the index of an unordered pair of worms in the list of worm pairs.
 * If necessary, add a new entry.
 */
static int
find_worm_pair(int worm1, int worm2)
{
  int k;
  
  /* Make sure the worms are ordered canonically. */
  if (worm1 > worm2) {
    int tmp = worm1;
    worm1 = worm2;
    worm2 = tmp;
  }
  
  for (k = 0; k < next_worm_pair; k++)
    if ((worm_pair1[k] == worm1) && (worm_pair2[k] == worm2))
      return k;
  
  /* Add a new entry. */
  gg_assert(next_worm_pair < MAX_WORM_PAIRS);
  worm_pair1[next_worm_pair] = worm1;
  worm_pair2[next_worm_pair] = worm2;
  next_worm_pair++;
  return next_worm_pair - 1;
}

#endif

static int
find_either_data(int reason1, int what1, int reason2, int what2)
{
  int k;
  
  /* Make sure the worms are ordered canonically. */
  if (what1 > what2) {
    int tmp = what1;
    what1 = what2;
    what2 = tmp;
  }

  for (k = 0; k < next_either; k++)
    if (either_data[k].reason1    == reason1
	&& either_data[k].what1   == what1
	&& either_data[k].reason2 == reason2
	&& either_data[k].what2   == what2)
      return k;
  
  /* Add a new entry. */
  gg_assert(next_either < MAX_EITHER);
  either_data[next_either].reason1 = reason1;
  either_data[next_either].what1   = what1;
  either_data[next_either].reason2 = reason2;
  either_data[next_either].what2   = what2;
  next_either++;
  return next_either - 1;
}

static int
find_all_data(int reason1, int what1, int reason2, int what2)
{
  int k;
  
  /* Make sure the worms are ordered canonically. */
  if (what1 > what2) {
    int tmp = what1;
    what1 = what2;
    what2 = tmp;
  }

  for (k = 0; k < next_all; k++)
    if (all_data[k].reason1    == reason1
	&& all_data[k].what1   == what1
	&& all_data[k].reason2 == reason2
	&& all_data[k].what2   == what2)
      return k;
  
  /* Add a new entry. */
  gg_assert(next_all < MAX_ALL);
  all_data[next_all].reason1 = reason1;
  all_data[next_all].what1   = what1;
  all_data[next_all].reason2 = reason2;
  all_data[next_all].what2   = what2;
  next_all++;
  return next_all - 1;
}

/*
 * Find the index of an eye space in the list of eye spaces.
 * If necessary, add a new entry.
 */
static int
find_eye(int eye, int color)
{
  int k;
  ASSERT_ON_BOARD1(eye);
  
  for (k = 0; k < next_eye; k++)
    if (eyes[k] == eye && eyecolor[k] == color)
      return k;
  
  /* Add a new entry. */
  gg_assert(next_eye < MAX_EYES);
  eyes[next_eye] = eye;
  eyecolor[next_eye] = color;
  next_eye++;
  return next_eye - 1;
}

/* Interprets the object of a reason and returns its position.
 * If the object is a pair (of worms or dragons), the position of the first
 * object is returned. (This is only used for trace outputs.) Returns
 * NO_MOVE if move does not point to a location.
 * FIXME: This new function produces some code duplication with other
 * trace output function. Do some code cleanup here.
 */
static int
get_pos(int reason, int what)
{
  switch (reason) {
  case ATTACK_MOVE:
  case DEFEND_MOVE:
  case ATTACK_THREAT:
  case DEFEND_THREAT:
  case ATTACK_MOVE_GOOD_KO:
  case ATTACK_MOVE_BAD_KO:
  case DEFEND_MOVE_GOOD_KO:
  case DEFEND_MOVE_BAD_KO:
    return worms[what];
  case SEMEAI_MOVE:
  case SEMEAI_THREAT:
  case VITAL_EYE_MOVE:
  case STRATEGIC_ATTACK_MOVE:
  case STRATEGIC_DEFEND_MOVE:
  case OWL_ATTACK_MOVE:
  case OWL_DEFEND_MOVE:
  case OWL_ATTACK_THREAT:
  case OWL_DEFEND_THREAT:
  case OWL_PREVENT_THREAT:
  case UNCERTAIN_OWL_ATTACK:
  case UNCERTAIN_OWL_DEFENSE:
  case OWL_ATTACK_MOVE_GOOD_KO:
  case OWL_ATTACK_MOVE_BAD_KO:
  case OWL_DEFEND_MOVE_GOOD_KO:
  case OWL_DEFEND_MOVE_BAD_KO:
    return dragons[what];
  case EITHER_MOVE:
    /* FIXME: What should we return here? */
    return worms[either_data[what].what1];
  case ALL_MOVE:
    /* FIXME: What should we return here? */
    return worms[all_data[what].what1];
  case CONNECT_MOVE:
  case CUT_MOVE:
    return dragons[conn_dragon1[what]];
  case ANTISUJI_MOVE:
  case BLOCK_TERRITORY_MOVE:
  case EXPAND_TERRITORY_MOVE:
  case EXPAND_MOYO_MOVE:
  case MY_ATARI_ATARI_MOVE:
  case YOUR_ATARI_ATARI_MOVE:
    return NO_MOVE;
  }
  /* We shoud never get here: */
  gg_assert(1>2);
  return 0; /* To keep gcc happy. */
}

/*
 * See if a lunch is already in the list of lunches, otherwise add a new
 * entry. A lunch is in this context a pair of eater (a dragon) and food
 * (a worm).
 */
void
add_lunch(int eater, int food)
{
  int k;
  int dragon1 = find_dragon(dragon[eater].origin);
  int worm1   = find_worm(worm[food].origin);
  ASSERT_ON_BOARD1(eater);
  ASSERT_ON_BOARD1(food);
  
  for (k = 0; k < next_lunch; k++)
    if ((lunch_dragon[k] == dragon1) && (lunch_worm[k] == worm1))
      return;
  
  /* Add a new entry. */
  gg_assert(next_lunch < MAX_LUNCHES);
  lunch_dragon[next_lunch] = dragon1;
  lunch_worm[next_lunch] = worm1;
  next_lunch++;
  return;
}

/*
 * Remove a lunch from the list of lunches.  A lunch is in this context a pair
 * of eater (a dragon) and food (a worm).  
 */
void
remove_lunch(int eater, int food)
{
  int k;
  int dragon1 = find_dragon(dragon[eater].origin);
  int worm1   = find_worm(worm[food].origin);
  ASSERT_ON_BOARD1(eater);
  ASSERT_ON_BOARD1(food);
  
  for (k = 0; k < next_lunch; k++)
    if ((lunch_dragon[k] == dragon1) && (lunch_worm[k] == worm1))
      break;
  
  if (k == next_lunch)
    return; /* Not found */
  
  /* Remove entry k. */
  lunch_dragon[k] = lunch_dragon[next_lunch - 1];
  lunch_worm[k] = lunch_worm[next_lunch - 1];
  next_lunch--;
}


/* ---------------------------------------------------------------- */


/*
 * Add a move reason for (pos) if it's not already there or the
 * table is full.
 */ 
static void
add_move_reason(int pos, int type, int what)
{
  int k;

  ASSERT_ON_BOARD1(pos);
  if (stackp == 0) {
    ASSERT1(board[pos] == EMPTY, pos);
  }

  for (k = 0; k < MAX_REASONS; k++) {
    int r = move[pos].reason[k];
    if (r < 0)
      break;
    if (move_reasons[r].type == type
	&& move_reasons[r].what == what)
      return;  /* Reason already listed. */
  }

  /* Reason not found, add it if there is place left in both lists. */
  gg_assert(k<MAX_REASONS);
  gg_assert(next_reason < MAX_MOVE_REASONS);
  /* Add a new entry. */
  move[pos].reason[k] = next_reason;
  move_reasons[next_reason].type = type;
  move_reasons[next_reason].what = what;
  move_reasons[next_reason].status = ACTIVE;
  next_reason++;
}

/*
 * Remove a move reason for (pos). Ignore silently if the reason
 * wasn't there.
 */ 
static void
remove_move_reason(int pos, int type, int what)
{
  int k;
  int n = -1; /* Position of the move reason to be deleted. */

  ASSERT_ON_BOARD1(pos);
  for (k = 0; k < MAX_REASONS; k++) {
    int r = move[pos].reason[k];
    if (r < 0)
      break;
    if (move_reasons[r].type == type
	&& move_reasons[r].what == what)
      n = k;
  }
  
  if (n == -1)
    return; /* Move reason wasn't there. */
  
  /* Now move the last move reason to position n, thereby removing the
   * one we were looking for.
   */
  k--;
  move[pos].reason[n] = move[pos].reason[k];
  move[pos].reason[k] = -1;
}


/*
 * Check whether a move reason already is recorded for a move.
 * A negative value for 'what' means only match 'type'.
 */
int
move_reason_known(int pos, int type, int what)
{
  int k;
  int r;

  ASSERT_ON_BOARD1(pos);
  for (k = 0; k < MAX_REASONS; k++) {
    r = move[pos].reason[k];
    if (r < 0)
      break;
    if (move_reasons[r].type == type
	&& (what < 0
	    || move_reasons[r].what == what))
      return 1;
  }
  return 0;
}

/* ---------------------------------------------------------------- */

/* Functions used in discard_rules follow below. */

/*
 * Check whether an attack move reason already is recorded for a move.
 * A negative value for 'what' means only match 'type'.
 */
int
attack_move_reason_known(int pos, int what)
{
  return (move_reason_known(pos, ATTACK_MOVE, what)
	  || move_reason_known(pos, ATTACK_MOVE_GOOD_KO, what)
	  || move_reason_known(pos, ATTACK_MOVE_BAD_KO, what));
}

/*
 * Check whether a defense move reason already is recorded for a move.
 * A negative value for 'what' means only match 'type'.
 */
int
defense_move_reason_known(int pos, int what)
{
  return (move_reason_known(pos, DEFEND_MOVE, what)
	  || move_reason_known(pos, DEFEND_MOVE_GOOD_KO, what)
	  || move_reason_known(pos, DEFEND_MOVE_BAD_KO, what));
}

/* Check whether a dragon consists of only one worm. If so, check
 * whether we know of a tactical attack or defense move.
 */
static int
tactical_move_vs_whole_dragon_known(int pos, int what)
{
  int aa = dragons[what];
  return ((worm[aa].size == dragon[aa].size)
	  && (attack_move_reason_known(pos, find_worm(aa))
	      || defense_move_reason_known(pos, find_worm(aa))));
}

/*
 * Check whether an owl attack move reason already is recorded for a move.
 * A negative value for 'what' means only match 'type'.
 */
int
owl_attack_move_reason_known(int pos, int what)
{
  return (move_reason_known(pos, OWL_ATTACK_MOVE, what)
	  || move_reason_known(pos, OWL_ATTACK_MOVE_GOOD_KO, what)
	  || move_reason_known(pos, OWL_ATTACK_MOVE_BAD_KO, what));
}

/*
 * Check whether an owl defense move reason already is recorded for a move.
 * A negative value for 'what' means only match 'type'.
 */
int
owl_defense_move_reason_known(int pos, int what)
{
  return (move_reason_known(pos, OWL_DEFEND_MOVE, what)
	  || move_reason_known(pos, OWL_DEFEND_MOVE_GOOD_KO, what)
	  || move_reason_known(pos, OWL_DEFEND_MOVE_BAD_KO, what));
}

/*
 * Check whether an owl attack/defense move reason is recorded for a move.
 * A negative value for 'what' means only match 'type'.
 */
static int
owl_move_reason_known(int pos, int what)
{
  return (owl_attack_move_reason_known(pos, what)
          || owl_defense_move_reason_known(pos, what));
}

/*
 * Check whether we have an owl attack/defense reason for a move that
 * involves a specific worm.
 */
static int
owl_move_vs_worm_known(int pos, int what)
{
  return owl_move_reason_known(pos, find_dragon(dragon[worms[what]].origin));
}

/* Check whether a worm listed in worms[] is inessential */
static int
concerns_inessential_worm(int pos, int what)
{
  UNUSED(pos);
  return DRAGON2(worms[what]).safety == INESSENTIAL
        || worm[worms[what]].inessential;
}

/* Check whether a dragon listed in dragons[] is inessential */
static int
concerns_inessential_dragon(int pos, int what)
{
  UNUSED(pos);
  return DRAGON2(dragons[what]).safety == INESSENTIAL; 
}

static int
move_is_marked_unsafe(int pos, int what)
{
  UNUSED(what);
  return !move[pos].move_safety;
}

static int
either_move_redundant(int pos, int what)
{
  return ((either_data[what].reason1 == ATTACK_STRING
	   && attack_move_reason_known(pos, either_data[what].what1))
	  || (either_data[what].reason2 == ATTACK_STRING
	      && attack_move_reason_known(pos, either_data[what].what2)));
}

#if 0

static int
all_move_redundant(int pos, int what)
{
  return ((all_data[what].reason1 == DEFEND_STRING
	   && defense_move_reason_known(pos, all_data[what].what1))
	  || (all_data[what].reason2 == DEFEND_STRING
	      && defense_move_reason_known(pos, all_data[what].what2)));
}

#endif

/* ---------------------------------------------------------------- */


/*
 * Add to the reasons for the move at (pos) that it attacks the worm
 * at (ww).
 */
void
add_attack_move(int pos, int ww, int code)
{
  int worm_number = find_worm(worm[ww].origin);

  ASSERT_ON_BOARD1(ww);
  if (code == WIN)
    add_move_reason(pos, ATTACK_MOVE, worm_number);
  else if (code == KO_A)
    add_move_reason(pos, ATTACK_MOVE_GOOD_KO, worm_number);
  else if (code == KO_B)
    add_move_reason(pos, ATTACK_MOVE_BAD_KO, worm_number);
}

/*
 * Add to the reasons for the move at (pos) that it defends the worm
 * at (ww).
 */
void
add_defense_move(int pos, int ww, int code)
{
  int worm_number = find_worm(worm[ww].origin);

  ASSERT_ON_BOARD1(ww);
  if (code == WIN)
    add_move_reason(pos, DEFEND_MOVE, worm_number);
  else if (code == KO_A)
    add_move_reason(pos, DEFEND_MOVE_GOOD_KO, worm_number);
  else if (code == KO_B)
    add_move_reason(pos, DEFEND_MOVE_BAD_KO, worm_number);
}

/*
 * Add to the reasons for the move at (pos) that it threatens to
 * attack the worm at (ww). 
 */
void
add_attack_threat_move(int pos, int ww, int code)
{
  int worm_number = find_worm(worm[ww].origin);
  UNUSED(code);
  
  ASSERT_ON_BOARD1(ww);
  add_move_reason(pos, ATTACK_THREAT, worm_number);
}

/* Remove an attack threat move reason. */

void
remove_attack_threat_move(int pos, int ww)
{
  int worm_number = find_worm(worm[ww].origin);

  ASSERT_ON_BOARD1(ww);
  remove_move_reason(pos, ATTACK_THREAT, worm_number);
}

/*
 * Add to the reasons for the move at (pos) that it defends the worm
 * at (ww).
 */
void
add_defense_threat_move(int pos, int ww, int code)
{
  int worm_number = find_worm(worm[ww].origin);
  UNUSED(code);

  ASSERT_ON_BOARD1(ww);
  add_move_reason(pos, DEFEND_THREAT, worm_number);
}


/* Report all, or up to max_strings, strings that are threatened 
 * at (pos).
 */
int
get_attack_threats(int pos, int max_strings, int strings[])
{
  int k;
  int num_strings;

  num_strings = 0;
  for (k = 0; k < MAX_REASONS; k++) {
    int r = move[pos].reason[k];
    if (r < 0)
      break;

    if (move_reasons[r].type == ATTACK_THREAT)
      strings[num_strings++] = worms[move_reasons[r].what];

    if (num_strings == max_strings)
      break;
  }

  return num_strings;
}

/* Report all, or up to max_strings, strings that might be defended 
 * at (pos).
 */
int
get_defense_threats(int pos, int max_strings, int strings[])
{
  int k;
  int num_strings;

  num_strings = 0;
  for (k = 0; k < MAX_REASONS; k++) {
    int r = move[pos].reason[k];
    if (r < 0)
      break;

    if (move_reasons[r].type == DEFEND_THREAT)
      strings[num_strings++] = worms[move_reasons[r].what];

    if (num_strings == max_strings)
      break;
  }

  return num_strings;
}

/* Report the biggest dragon that is owl-affected (possibily with ko)
 * by a move at (pos).
 */
int
get_biggest_owl_target(int pos)
{
  int k;
  int biggest_target = -1;
  float target_size = 0.0;
  for (k = 0; k < MAX_REASONS; k++) {
    int r = move[pos].reason[k];
    if (r < 0)
      break;

    switch (move_reasons[r].type) {
    case OWL_ATTACK_MOVE:
    case OWL_ATTACK_MOVE_GOOD_KO:
    case OWL_ATTACK_MOVE_BAD_KO:
    case OWL_ATTACK_THREAT:
    case OWL_DEFEND_MOVE:
    case OWL_DEFEND_MOVE_GOOD_KO:
    case OWL_DEFEND_MOVE_BAD_KO:
    case OWL_DEFEND_THREAT:
    case OWL_PREVENT_THREAT:
      if (dragon[dragons[move_reasons[r].what]].effective_size
          > target_size) {
        biggest_target = move_reasons[r].what;
        target_size = dragon[dragons[move_reasons[r].what]].effective_size;
      }
    }
  }
  return biggest_target;
}

/*
 * Add to the reasons for the move at (pos) that it connects the
 * dragons at (dr1) and (dr2). Require that the dragons are
 * distinct.
 */
void
add_connection_move(int pos, int dr1, int dr2)
{
  int dragon1 = find_dragon(dragon[dr1].origin);
  int dragon2 = find_dragon(dragon[dr2].origin);
  int connection;

  ASSERT_ON_BOARD1(dr1);
  ASSERT_ON_BOARD1(dr2);
  gg_assert(dragon[dr1].color == dragon[dr2].color);
  if (dragon1 == dragon2)
    return;
  connection = find_connection(dragon1, dragon2);
  add_move_reason(pos, CONNECT_MOVE, connection);
}

/*
 * Add to the reasons for the move at (pos) that it cuts the
 * dragons at (dr1) and (dr2). Require that the dragons are
 * distinct.
 */
void
add_cut_move(int pos, int dr1, int dr2)
{
  int dragon1 = find_dragon(dragon[dr1].origin);
  int dragon2 = find_dragon(dragon[dr2].origin);
  int connection;

  ASSERT_ON_BOARD1(dr1);
  ASSERT_ON_BOARD1(dr2);
  gg_assert(dragon[dr1].color == dragon[dr2].color);
  if (dragon1 == dragon2)
    return;
  connection = find_connection(dragon1, dragon2);
  
  /*
   * Ignore the cut or connection if either (dr1) or (dr2)
   * points to a tactically captured worm.
   */
  if ((worm[dr1].attack_codes[0] != 0 && worm[dr1].defend_codes[0] == 0)
      || (worm[dr2].attack_codes[0] != 0 && worm[dr2].defend_codes[0] == 0))
    return;
  
  add_move_reason(pos, CUT_MOVE, connection);
}

/*
 * Add to the reasons for the move at (pos) that it is an anti-suji.
 * This means that it's a locally inferior move or for some other reason
 * must *not* be played.
 */
void
add_antisuji_move(int pos)
{
  add_move_reason(pos, ANTISUJI_MOVE, 0);
}

/*
 * Add to the reasons for the move at (pos) that it wins the
 * dragon (friendly or not) at (dr) in semeai. Since it is
 * possible that in some semeai one player can kill but the
 * other can only make seki, it is possible that one dragon
 * is already alive in seki. Therefore separate move reasons
 * must be added for the two dragons.
 */
void
add_semeai_move(int pos, int dr)
{
  int the_dragon = find_dragon(dragon[dr].origin);

  ASSERT_ON_BOARD1(dr);
  add_move_reason(pos, SEMEAI_MOVE, the_dragon);
}

/*
 * Add to the reasons for the move at (pos) that given two
 * moves in a row a move here can win the dragon (friendly or
 * not) at (dr) in semeai. Such a move can be used as a 
 * ko threat, and it is also given some value due to uncertainty
 * in the counting of liberties.
 */
void
add_semeai_threat(int pos, int dr)
{
  int the_dragon = find_dragon(dragon[dr].origin);

  ASSERT_ON_BOARD1(dr);
  add_move_reason(pos, SEMEAI_THREAT, the_dragon);
}

/*
 * Add to the reasons for the move at (pos) that it's the vital
 * point for the eye space at (eyespace) of color.
 */
void
add_vital_eye_move(int pos, int eyespace, int color)
{
  int eye;

  ASSERT_ON_BOARD1(eyespace);
  if (color == WHITE)
    eye = find_eye(white_eye[eyespace].origin, color);
  else
    eye = find_eye(black_eye[eyespace].origin, color);
  add_move_reason(pos, VITAL_EYE_MOVE, eye);
}

/*
 * Add to the reasons for the move at (pos) that it will accomplish
 * one of two things: either (reason1) on (target1) or (reason2) on 
 * (target2).  
 *
 * At this time, (reason) can only be ATTACK_STRING.
 * However, more reasons will be implemented in the future.
 *
 * FIXME: Implement at least ATTACK_MOVE_GOOD_KO, ATTACK_MOVE_BAD_KO,
 *         DEFEND_MOVE and associates, CONNECT_MOVE, OWL_ATTACK_MOVE,
 *         OWL_DEFEND_MOVE, and possibly more.
 *
 * FIXME: Generalize to more than 2 parameters.
 *        When that is done, this will be a good way to add 
 *        atari_atari moves.
 */
void
add_either_move(int pos, int reason1, int target1, int reason2, int target2)
{
  int  what1 = 0;
  int  what2 = 0;
  int  index;

  ASSERT_ON_BOARD1(target1);
  ASSERT_ON_BOARD1(target2);
  if (reason1 == reason2 && target1 == target2)
    return;
  
  /* For now. */
  gg_assert(reason1 == ATTACK_STRING);
  gg_assert(reason2 == ATTACK_STRING);

  switch (reason1) {
  case ATTACK_STRING:
    {
      what1 = find_worm(worm[target1].origin);

      /* If this string is already attacked, and with no defense, then
       * there is no additional value of this move reason. */
      if (worm[target1].attack_codes[0] != 0
	  && worm[target1].defend_codes[0] == 0)
	return;
    }
    break;

  default:
    break;
  }

  switch (reason2) {
  case ATTACK_STRING:
    {
      what2 = find_worm(worm[target2].origin);

      /* If this string is already attacked, and with no defense, then
       * there is no additional value of this move reason. */
      if (worm[target2].attack_codes[0] != 0 
	  && worm[target2].defend_codes[0] == 0)
	return;
    }
    break;

  default:
    break;
  }

  index = find_either_data(reason1, what1, reason2, what2);
  add_move_reason(pos, EITHER_MOVE, index);
}


/*
 * Add to the reasons for the move at (pos) that it will accomplish
 * both of two things: (reason1) on (target1) and (reason2) on 
 * (target2).  
 *
 * At this time, (reason) can only be DEFEND_STRING.
 * However, more reasons will be implemented in the future.
 *
 * FIXME: Implement at least ATTACK_MOVE_GOOD_KO, ATTACK_MOVE_BAD_KO,
 *         DEFEND_MOVE and associates, CONNECT_MOVE, OWL_ATTACK_MOVE,
 *         OWL_DEFEND_MOVE, and possibly more.
 *
 * FIXME: Generalize to more than 2 parameters.
 *        When that is done, this will be a good way to add 
 *        atari_atari moves.
 */
void
add_all_move(int pos, int reason1, int target1, int reason2, int target2)
{
  int  what1 = 0;
  int  what2 = 0;
  int  index;

  ASSERT_ON_BOARD1(target1);
  ASSERT_ON_BOARD1(target2);
  if (reason1 == reason2 && target1 == target2)
    return;
  
  /* For now. */
  gg_assert(reason1 == DEFEND_STRING);
  gg_assert(reason2 == DEFEND_STRING);

  switch (reason1) {
  case DEFEND_STRING:
    what1 = find_worm(worm[target1].origin);
    break;

  default:
    break;
  }

  switch (reason2) {
  case DEFEND_STRING:
    what2 = find_worm(worm[target2].origin);
    break;

  default:
    break;
  }

  index = find_all_data(reason1, what1, reason2, what2);
  add_move_reason(pos, ALL_MOVE, index);
}


/*
 * Add to the reasons for the move at (pos) that it secures
 * territory by blocking.
 */
void
add_block_territory_move(int pos)
{
  add_move_reason(pos, BLOCK_TERRITORY_MOVE, 0);
}

/*
 * Add to the reasons for the move at (pos) that it expands
 * territory.
 */
void
add_expand_territory_move(int pos)
{
  add_move_reason(pos, EXPAND_TERRITORY_MOVE, 0);
}

/*
 * Add to the reasons for the move at (pos) that it expands
 * moyo.
 */
void
add_expand_moyo_move(int pos)
{
  add_move_reason(pos, EXPAND_MOYO_MOVE, 0);
}

/*
 * This function is called when a shape value for the move at (pos)
 * is found. 
 * 
 * We keep track of the largest positive shape value found, and the
 * total number of positive contributions, as well as the largest
 * negative shape value found, and the total number of negative
 * shape contributions.
 */
void
add_shape_value(int pos, float value)
{
  ASSERT_ON_BOARD1(pos);
  if (value > 0.0) {
    if (value > move[pos].maxpos_shape)
      move[pos].maxpos_shape = value;
    move[pos].numpos_shape += 1;
  }
  else if (value < 0.0) {
    value = -value;
    if (value > move[pos].maxneg_shape)
      move[pos].maxneg_shape = value;
    move[pos].numneg_shape += 1;
  }
}

/*
 * Flag that this move is worthwhile to play as a pure threat move.
 */
void
add_worthwhile_threat_move(int pos)
{
  move[pos].worthwhile_threat = 1;
}

/* 
 * This function computes the shape factor, which multiplies
 * the score of a move. We take the largest positive contribution
 * to shape and add 1 for each additional positive contribution found.
 * Then we take the largest negative contribution to shape, and
 * add 1 for each additional negative contribution. The resulting
 * number is raised to the power 1.05.
 *
 * The rationale behind this complicated scheme is that every
 * shape point is very significant. If two shape contributions
 * with values (say) 5 and 3 are found, the second contribution
 * should be devalued to 1. Otherwise the engine is too difficult to
 * tune since finding multiple contributions to shape can cause
 * significant overvaluing of a move.
 */

float
compute_shape_factor(int pos)
{
  float exponent = move[pos].maxpos_shape - move[pos].maxneg_shape;

  ASSERT_ON_BOARD1(pos);
  if (move[pos].numpos_shape > 1)
    exponent += move[pos].numpos_shape - 1;
  if (move[pos].numneg_shape > 1)
    exponent -= move[pos].numneg_shape - 1;
  return pow(1.05, exponent);
}


/*
 * Add to the reasons for the move at (pos) that it attacks
 * the dragon (dr) on a strategical level.
 */
void
add_strategical_attack_move(int pos, int dr)
{
  int dragon1 = find_dragon(dragon[dr].origin);

  ASSERT_ON_BOARD1(dr);
  add_move_reason(pos, STRATEGIC_ATTACK_MOVE, dragon1);
}

/*
 * Add to the reasons for the move at (pos) that it defends
 * the dragon (dr) on a strategical level.
 */
void
add_strategical_defense_move(int pos, int dr)
{
  int dragon1 = find_dragon(dragon[dr].origin);

  ASSERT_ON_BOARD1(dr);
  add_move_reason(pos, STRATEGIC_DEFEND_MOVE, dragon1);
}

/*
 * Add to the reasons for the move at (pos) that the owl
 * code reports an attack on the dragon (dr).
 */
void
add_owl_attack_move(int pos, int dr, int code)
{
  int dragon1 = find_dragon(dragon[dr].origin);

  ASSERT_ON_BOARD1(dr);
  if (code == WIN)
    add_move_reason(pos, OWL_ATTACK_MOVE, dragon1);
  else if (code == KO_A)
    add_move_reason(pos, OWL_ATTACK_MOVE_GOOD_KO, dragon1);
  else if (code == KO_B)
    add_move_reason(pos, OWL_ATTACK_MOVE_BAD_KO, dragon1);
}

/*
 * Add to the reasons for the move at (pos) that the owl
 * code reports a defense of the dragon (dr).
 */
void
add_owl_defense_move(int pos, int dr, int code)
{
  int dragon1 = find_dragon(dragon[dr].origin);

  ASSERT_ON_BOARD1(dr);
  if (code == WIN)
    add_move_reason(pos, OWL_DEFEND_MOVE, dragon1);
  else if (code == KO_A)
    add_move_reason(pos, OWL_DEFEND_MOVE_GOOD_KO, dragon1);
  else if (code == KO_B)
    add_move_reason(pos, OWL_DEFEND_MOVE_BAD_KO, dragon1);
}

/*
 * Add to the reasons for the move at (pos) that the owl
 * code reports a move threatening to attack the dragon enemy (dr).
 * That is, if the attacker is given two moves in a row, (pos)
 * can be the first move.
 */
void
add_owl_attack_threat_move(int pos, int dr, int code)
{
  int dragon1 = find_dragon(dragon[dr].origin);
  UNUSED(code);
  
  ASSERT_ON_BOARD1(dr);
  add_move_reason(pos, OWL_ATTACK_THREAT, dragon1);
  add_worthwhile_threat_move(pos);
}

/* The owl code found the friendly dragon alive, or the unfriendly dragon
 * dead, and an extra point of attack or defense was found, so this might be a
 * good place to play.  
 */
void
add_owl_uncertain_defense_move(int pos, int dr)
{
  int dragon1 = find_dragon(dragon[dr].origin);

  ASSERT_ON_BOARD1(dr);
  add_move_reason(pos, UNCERTAIN_OWL_DEFENSE, dragon1);
}

/* The owl code found the opponent dragon alive, or the friendly
 * dragon dead, but was uncertain, and this move reason propose
 * an attack or defense which is expected to fail but might succeed.
 */
void
add_owl_uncertain_attack_move(int pos, int dr)
{
  int dragon1 = find_dragon(dragon[dr].origin);

  ASSERT_ON_BOARD1(dr);
  add_move_reason(pos, UNCERTAIN_OWL_ATTACK, dragon1);
}

/*
 * Add to the reasons for the move at (pos) that the owl
 * code reports a move threatening to rescue the dragon (dr).
 * That is, if the defender is given two moves in a row, (pos)
 * can be the first move.
 */
void
add_owl_defense_threat_move(int pos, int dr, int code)
{
  int dragon1 = find_dragon(dragon[dr].origin);
  UNUSED(code);

  ASSERT_ON_BOARD1(dr);
  add_move_reason(pos, OWL_DEFEND_THREAT, dragon1);
  add_worthwhile_threat_move(pos);
}

/* Add to the reasons for the move at (pos) that it captures
 * at least one of a set of worms which individually are tactically
 * safe (such as a double atari). Only one such move reason is
 * permitted per move.
 */
void
add_my_atari_atari_move(int pos, int size)
{
  add_move_reason(pos, MY_ATARI_ATARI_MOVE, size);
}

/* Add to the reasons for the move at (pos) that it stops a
 * combination attack for the opponent.
 */
void
add_your_atari_atari_move(int pos, int size)
{
  add_move_reason(pos, YOUR_ATARI_ATARI_MOVE, size);
}


/*
 * Add to the reasons for the move at (pos) that the owl
 * code reports a move threatening to defend the dragon enemy (dr),
 * and that (pos) is a move which attacks the dragon. 
 * That is, if the defender is given two moves in a row, (pos)
 * can be the first move. Hopefully playing at (pos) makes it harder 
 * for the dragon to live.
 */
void
add_owl_prevent_threat_move(int pos, int dr)
{
  int dragon1 = find_dragon(dragon[dr].origin);

  ASSERT_ON_BOARD1(dr);
  add_move_reason(pos, OWL_PREVENT_THREAT, dragon1);
}

/*
 * Add value of followup moves. 
 */
void
add_followup_value(int pos, float value)
{
  ASSERT_ON_BOARD1(pos);
  if (value > move[pos].followup_value)
    move[pos].followup_value = value;
}

/*
 * Add value of reverse followup moves. 
 */
void
add_reverse_followup_value(int pos, float value)
{
  ASSERT_ON_BOARD1(pos);
  if (value > move[pos].reverse_followup_value)
    move[pos].reverse_followup_value = value;
}

/*
 * Set a minimum allowed value for the move.
 */
int
set_minimum_move_value(int pos, float value)
{
  ASSERT_ON_BOARD1(pos);
  if (value > move[pos].min_value) {
    move[pos].min_value = value;
    return 1;
  }
  return 0;
}

/*
 * Set a maximum allowed value for the move.
 */
void
set_maximum_move_value(int pos, float value)
{
  ASSERT_ON_BOARD1(pos);
  if (value < move[pos].max_value)
    move[pos].max_value = value;
}

/*
 * Set a minimum allowed territorial value for the move.
 */
void
set_minimum_territorial_value(int pos, float value)
{
  ASSERT_ON_BOARD1(pos);
  if (value > move[pos].min_territory)
    move[pos].min_territory = value;
}

/*
 * Set a maximum allowed territorial value for the move.
 */
void
set_maximum_territorial_value(int pos, float value)
{
  ASSERT_ON_BOARD1(pos);
  if (value < move[pos].max_territory)
    move[pos].max_territory = value;
}

/* 
 * Add a point redistribution rule, sending the points from (from)
 * to (to). 
 */
void
add_replacement_move(int from, int to)
{
  int cc;
  int m, n;
  int ii;

  ASSERT_ON_BOARD1(from);
  ASSERT_ON_BOARD1(to);
  cc = replacement_map[to];

  /* First check for an incompatible redistribution rule. */
  if (replacement_map[from] != NO_MOVE) {
    int dd = replacement_map[from];

    /* Crash if the old rule isn't compatible with the new one. */
    ASSERT1(dd == to || to == replacement_map[dd], from);
    /* There already is a compatible redistribution in effect so we
     * have nothing more to do.
     */
    return;
  }

  TRACE("Move at %1m is replaced by %1m.\n", from, to);    

  /* Verify that we don't introduce a cyclic redistribution. */
  if (cc == from) {
    gprintf("Cyclic point redistribution detected.\n");
    ASSERT1(0, from);
  }

  /* Update the replacement map. Make sure that all replacements
   * always are directed immediately to the final destination.
   */
  if (cc != NO_MOVE)
    replacement_map[from] = cc;
  else
    replacement_map[from] = to;
  
  for (m = 0; m < board_size; m++)
    for (n = 0; n < board_size; n++) {
      ii = POS(m, n);
      if (replacement_map[ii] == from)
	replacement_map[ii] = replacement_map[from];
    }
}


/* Find worms rescued by a move at (pos). */
void
get_saved_worms(int pos, int saved[BOARDMAX])
{
  int k;
  memset(saved, 0, sizeof(saved[0]) * BOARDMAX);
  
  for (k = 0; k < MAX_REASONS; k++) {
    int r = move[pos].reason[k];
    int what;

    if (r < 0)
      break;
    
    what = move_reasons[r].what;
    /* We exclude the ko contingent defenses, to avoid that the
     * confirm_safety routines spot an attack with ko and thinks the
     * move is unsafe.
     */
    if (move_reasons[r].type == DEFEND_MOVE) {
      int origin = worm[worms[what]].origin;
      int ii;
      for (ii = BOARDMIN; ii < BOARDMAX; ii++)
	if (IS_STONE(board[ii]) && worm[ii].origin == origin)
	  saved[ii] = 1;
    }
  }    
}


/* Find dragons rescued by a move at (pos). */
void
get_saved_dragons(int pos, int saved[BOARDMAX])
{
  int k;
  memset(saved, 0, sizeof(saved[0]) * BOARDMAX);
  
  for (k = 0; k < MAX_REASONS; k++) {
    int r = move[pos].reason[k];
    int what;

    if (r < 0)
      break;
    
    what = move_reasons[r].what;
    /* We exclude the ko contingent defenses, to avoid that the
     * confirm_safety routines spot an attack with ko and thinks the
     * move is unsafe.
     */
    if (move_reasons[r].type == OWL_DEFEND_MOVE) {
      int origin = dragon[dragons[what]].origin;
      int ii;
      for (ii = BOARDMIN; ii < BOARDMAX; ii++)
	if (IS_STONE(board[ii]) && dragon[ii].origin == origin)
	  saved[ii] = 1;
    }
  }    
}


/* List the move reasons for (color). */
void
list_move_reasons(int color)
{
  int m;
  int n;
  int pos;
  int k;
  int reason1;
  int reason2;
  int aa = NO_MOVE;
  int bb = NO_MOVE;
  int dragon1 = -1;
  int dragon2 = -1;
  int worm1 = -1;
  int worm2 = -1;
  int ecolor = 0;
  
  gprintf("\nMove reasons:\n");
  
  for (n = 0; n < board_size; n++)
    for (m = board_size-1; m >= 0; m--) {
      pos = POS(m, n);

      for (k = 0; k < MAX_REASONS; k++) {
	int r = move[pos].reason[k];

	if (r < 0)
	  break;
	
	switch (move_reasons[r].type) {
	case ATTACK_MOVE:
	  aa = worms[move_reasons[r].what];
	  gprintf("Move at %1m attacks %1m%s\n", pos, aa,
		  (worm[aa].defend_codes[0] == 0) ? " (defenseless)" : "");
	  break;
	case ATTACK_MOVE_GOOD_KO:
	  aa = worms[move_reasons[r].what];
	  gprintf("Move at %1m attacks %1m%s with good ko\n", pos, aa,
		  (worm[aa].defend_codes[0] == 0) ? " (defenseless)" : "");
	  break;
	case ATTACK_MOVE_BAD_KO:
	  aa = worms[move_reasons[r].what];
	  gprintf("Move at %1m attacks %1m%s with bad ko\n", pos, aa,
		  (worm[aa].defend_codes[0] == 0) ? " (defenseless)" : "");
	  break;
	  
	case DEFEND_MOVE:
	  aa = worms[move_reasons[r].what];
	  gprintf("Move at %1m defends %1m\n", pos, aa);
	  break;
	case DEFEND_MOVE_GOOD_KO:
	  aa = worms[move_reasons[r].what];
	  gprintf("Move at %1m defends %1m with good ko\n", pos, aa);
	  break;
	case DEFEND_MOVE_BAD_KO:
	  aa = worms[move_reasons[r].what];
	  gprintf("Move at %1m defends %1m with bad ko\n", pos, aa);
	  break;
	  
	case ATTACK_THREAT:
	case DEFEND_THREAT:
	  aa = worms[move_reasons[r].what];
	  
	  if (move_reasons[r].type == ATTACK_THREAT)
	    gprintf("Move at %1m threatens to attack %1m\n", pos, aa);
	  else if (move_reasons[r].type == DEFEND_THREAT)
	    gprintf("Move at %1m threatens to defend %1m\n", pos, aa);
	  break;

	case UNCERTAIN_OWL_DEFENSE:
	  aa = dragons[move_reasons[r].what];
	  if (board[aa] == color)
	    gprintf("%1m found alive but not certainly, %1m defends it again\n",
		    aa, pos);
	  else
	    gprintf("%1m found dead but not certainly, %1m attacks it again\n",
		    aa, pos);
	  break;	  

	case CONNECT_MOVE:
	case CUT_MOVE:
	  dragon1 = conn_dragon1[move_reasons[r].what];
	  dragon2 = conn_dragon2[move_reasons[r].what];
	  aa = dragons[dragon1];
	  bb = dragons[dragon2];
	  if (move_reasons[r].type == CONNECT_MOVE)
	    gprintf("Move at %1m connects %1m and %1m\n", pos, aa, bb);
	  else
	    gprintf("Move at %1m cuts %1m and %1m\n", pos, aa, bb);
	  break;
	  
	case ANTISUJI_MOVE:
	  gprintf("Move at %1m is an antisuji\n", pos);
	  break;
	  
	case SEMEAI_MOVE:
	  aa = dragons[move_reasons[r].what];
	  gprintf("Move at %1m wins semeai for %1m\n", pos, aa);
	  break;
	  
	case SEMEAI_THREAT:
	  aa = dragons[move_reasons[r].what];
	  gprintf("Move at %1m threatens to win semeai for %1m\n", pos, aa);
	  break;
	  
	case VITAL_EYE_MOVE:
	  aa = eyes[move_reasons[r].what];
	  ecolor = eyecolor[move_reasons[r].what];
	  if (ecolor == WHITE)
	    gprintf("Move at %1m vital eye point for dragon %1m (eye %1m)\n",
		    pos, white_eye[aa].dragon, aa);
	  else
	    gprintf("Move at %1m vital eye point for dragon %1m (eye %1m)\n",
		    pos, black_eye[aa].dragon, aa);
	  break;
	  
	case EITHER_MOVE:
	case ALL_MOVE:
	  /* FIXME: Generalize this. */
	  /* FIXME: This is broken.  EITHER_MOVE should reference either_data 
	         see, for example: 
		 http://www.public32.com/regress/?tstfile=nngs&num=850&move=R9*/
	  reason1 = all_data[move_reasons[r].what].reason1;
	  reason2 = all_data[move_reasons[r].what].reason2;
	  worm1 = all_data[move_reasons[r].what].what1;
	  worm2 = all_data[move_reasons[r].what].what2;
	  aa = worms[worm1];
	  bb = worms[worm2];
	  gprintf("Move at %1m %s %s %1m or %s %1m\n", 
		  pos, move_reasons[r].type == EITHER_MOVE ? "either" : "both",
		  reason1 == ATTACK_STRING ? "attacks" : "defends", aa, 
		  reason2 == ATTACK_STRING ? "attacks" : "defends", bb);
	  break;

	case OWL_ATTACK_MOVE:
	  aa = dragons[move_reasons[r].what];
	  gprintf("Move at %1m owl-attacks %1m\n", pos, aa);
	  break;
	case OWL_ATTACK_MOVE_GOOD_KO:
	  aa = dragons[move_reasons[r].what];
	  gprintf("Move at %1m owl-attacks %1m with good ko\n", pos, aa);
	  break;
	case OWL_ATTACK_MOVE_BAD_KO:
	  aa = dragons[move_reasons[r].what];
	  gprintf("Move at %1m owl-attacks %1m with bad ko\n", pos, aa);
	  break;
	  
	case OWL_DEFEND_MOVE:
	  aa = dragons[move_reasons[r].what];
	  gprintf("Move at %1m owl-defends %1m\n", pos, aa);
	  break;
	case OWL_DEFEND_MOVE_GOOD_KO:
	  aa = dragons[move_reasons[r].what];
	  gprintf("Move at %1m owl-defends %1m with good ko\n", pos, aa);
	  break;
	case OWL_DEFEND_MOVE_BAD_KO:
	  aa = dragons[move_reasons[r].what];
	  gprintf("Move at %1m owl-defends %1m with bad ko\n", pos, aa);
	  break;
	  
	case OWL_ATTACK_THREAT:
	  aa = dragons[move_reasons[r].what];
	  gprintf("Move at %1m owl-threatens to attack %1m\n", pos, aa);
	  break;
	  
	case OWL_DEFEND_THREAT:
	  aa = dragons[move_reasons[r].what];
	  gprintf("Move at %1m owl-threatens to defend %1m\n", pos, aa);
	  break;
	  
	case OWL_PREVENT_THREAT:
	  aa = dragons[move_reasons[r].what];
	  gprintf("Move at %1m owl-prevents a threat to attack or defend %1m\n", 
		  pos, aa);
	  break;

	case BLOCK_TERRITORY_MOVE:
	  gprintf("Move at %1m blocks territory\n", pos);
	  break;
	  
	case EXPAND_TERRITORY_MOVE:
	  gprintf("Move at %1m expands territory\n", pos);
	  break;
	  
	case EXPAND_MOYO_MOVE:
	  gprintf("Move at %1m expands moyo\n", pos);
	  break;
	  
	case STRATEGIC_ATTACK_MOVE:
	case STRATEGIC_DEFEND_MOVE:
	  aa = dragons[move_reasons[r].what];
	  
	  if (move_reasons[r].type == STRATEGIC_ATTACK_MOVE)
	    gprintf("Move at %1m strategically attacks %1m\n", pos, aa);
	  else
	    gprintf("Move at %1m strategically defends %1m\n", pos, aa);
	  break;
	  
	case MY_ATARI_ATARI_MOVE:
	  gprintf("Move at %1m captures something\n", pos);

	case YOUR_ATARI_ATARI_MOVE:
	  gprintf("Move at %1m defends threat to capture something\n", pos);
	}
      }
      if (k > 0 && move[pos].move_safety == 0)
	gprintf("Move at %1m strategically or tactically unsafe\n", pos);
    }
}




/* This array lists rules according to which we set the status
 * flags of a move reasons.
 * The format is:
 * { List of reasons to which the rule applies, condition of the rule,
 * flags to be set, trace message }
 * The condition must be of type discard_condition_fn_ptr, that is a pointer
 * to a function with parameters (pos, what).
 */

static struct discard_rule discard_rules[] =
{
  { { ATTACK_MOVE, ATTACK_MOVE_GOOD_KO,
      ATTACK_MOVE_BAD_KO, ATTACK_THREAT,
      DEFEND_MOVE, DEFEND_MOVE_GOOD_KO,
      DEFEND_MOVE_BAD_KO, DEFEND_THREAT, -1 },
    owl_move_vs_worm_known, TERRITORY_REDUNDANT,
    "  %1m: 0.0 - (threat of) attack/defense of %1m (owl attack/defense as well)\n" },
  { { SEMEAI_MOVE, SEMEAI_THREAT, -1 },
    owl_move_reason_known, REDUNDANT,
    "  %1m: 0.0 - (threat to) win semai involving %1m (owl move as well)\n"},
  { { SEMEAI_MOVE, SEMEAI_THREAT, -1 },
    tactical_move_vs_whole_dragon_known, REDUNDANT,
    "  %1m: 0.0 - (threat to) win semai involving %1m (tactical move as well)\n"},
  { { EITHER_MOVE, -1 },
    either_move_redundant, REDUNDANT,
    "  %1m: 0.0 - either move is redundant at %1m (direct att./def. as well)\n"},
  /* FIXME: Add handling of ALL_MOVE: All single attacks/defenses should
   *        be removed when there is also a corresponding ALL_MOVE.
   */
  /* FIXME: Add handling of ALL and EITHER moves for inessential worms. */
  { { ATTACK_MOVE, ATTACK_MOVE_GOOD_KO,
      ATTACK_MOVE_BAD_KO, ATTACK_THREAT,
      DEFEND_MOVE, DEFEND_MOVE_GOOD_KO,
      DEFEND_MOVE_BAD_KO, DEFEND_THREAT, -1 },
    concerns_inessential_worm, TERRITORY_REDUNDANT,
    "  %1m: 0.0 - attack/defense of %1m (inessential)\n"},
  { { OWL_ATTACK_MOVE, OWL_ATTACK_MOVE_GOOD_KO,
      OWL_ATTACK_MOVE_BAD_KO, OWL_ATTACK_THREAT,
      OWL_DEFEND_MOVE, OWL_DEFEND_MOVE_GOOD_KO,
      OWL_DEFEND_MOVE_BAD_KO, UNCERTAIN_OWL_DEFENSE, -1 },
    concerns_inessential_dragon, REDUNDANT,
    "  %1m: 0.0 - (uncertain) owl attack/defense of %1m (inessential)\n"},
  { { ATTACK_MOVE, ATTACK_MOVE_GOOD_KO, ATTACK_MOVE_BAD_KO,
      DEFEND_MOVE, DEFEND_MOVE_GOOD_KO, DEFEND_MOVE_BAD_KO, -1},
    move_is_marked_unsafe, REDUNDANT,
    "  %1m: 0.0 - tactical move vs %1m (unsafe move)\n"},
  { { -1 }, NULL, 0, ""}  /* Keep this entry at end of the list. */
};

/* This function checks the list of move reasons for redundant move
 * reasons and marks them accordingly in their status field.
 */
void
discard_redundant_move_reasons(int pos)
{
  int k1, k2;
  int l;
  for (k1 = 0; !(discard_rules[k1].reason_type[0] == -1); k1++) {
    for (k2 = 0; !(discard_rules[k1].reason_type[k2] == -1); k2++) {
      for (l = 0; l < MAX_REASONS; l++) {

        int r = move[pos].reason[l];
        if (r < 0)
          break;
        if ((move_reasons[r].type == discard_rules[k1].reason_type[k2])
            && (discard_rules[k1].condition(pos, move_reasons[r].what))) {
          DEBUG(DEBUG_MOVE_REASONS, discard_rules[k1].trace_message,
                pos, get_pos(move_reasons[r].type, move_reasons[r].what)); 
          move_reasons[r].status |= discard_rules[k1].flags;
        }
      } 
    }
  }
}


/* Look through the move reasons to see whether (pos) is an antisuji move. */
int
is_antisuji_move(int pos)
{
  int k;
  for (k = 0; k < MAX_REASONS; k++) {
    int r = move[pos].reason[k];
    if (r < 0)
      break;
    if (move_reasons[r].type == ANTISUJI_MOVE)
      return 1; /* This move must not be played. End of story. */
  }

  return 0;
}


/* Count how many distinct strings are (solidly) connected by the move
 * at (pos). Add a bonus for strings with few liberties. Also add
 * bonus for opponent strings put in atari or removed.
 */
int
move_connects_strings(int pos, int color)
{
  int ss[4];
  int strings = 0;
  int own_strings = 0;
  int k, l;
  int fewlibs = 0;

  for (k = 0; k < 4; k++) {
    int ii = pos + delta[k];
    int origin;

    if (!ON_BOARD(ii) || board[ii] == EMPTY)
      continue;

    origin = find_origin(ii);

    for (l = 0; l < strings; l++)
      if (ss[l] == origin)
	break;

    if (l == strings) {
      ss[strings] = origin;
      strings++;
    }
  }

  for (k = 0; k < strings; k++) {
    if (board[ss[k]] == color) {
      int newlibs = approxlib(pos, color, MAXLIBS, NULL);
      own_strings++;
      if (newlibs >= countlib(ss[k])) {
	if (countlib(ss[k]) <= 4)
	  fewlibs++;
	if (countlib(ss[k]) <= 2)
	  fewlibs++;
      }
    }
    else {
      if (countlib(ss[k]) <= 2)
	fewlibs++;
      if (countlib(ss[k]) <= 1)
	fewlibs++;
    }
  }

  /* Do some thresholding. */
  if (fewlibs > 4)
    fewlibs = 4;
  if (fewlibs == 0 && own_strings == 1)
    own_strings = 0;

  return own_strings + fewlibs;
}


/* Find saved dragons and worms, then call confirm_safety(). */
int
move_reasons_confirm_safety(int move, int color, int minsize)
{
  int saved_dragons[BOARDMAX];
  int saved_worms[BOARDMAX];

  get_saved_dragons(move, saved_dragons);
  get_saved_worms(move, saved_worms);
  
  return confirm_safety(move, color, minsize, NULL,
			saved_dragons, saved_worms);
}


/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
