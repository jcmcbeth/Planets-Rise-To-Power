#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "mud.h"

#define BFS_MARK         BV01

extern int top_exit;
extern int top_ed;
extern int top_affect;
extern int cur_qobjs;
extern int cur_qchars;
extern CHAR_DATA *gch_prev;
extern OBJ_DATA *gobj_prev;

CHAR_DATA *cur_char = NULL;
ROOM_INDEX_DATA *cur_room = NULL;
bool cur_char_died = FALSE;
ch_ret global_retcode = rNONE;

int cur_obj = 0;
int cur_obj_serial = 0;
bool cur_obj_extracted = FALSE;
obj_ret global_objcode = rNONE;

static bool is_wizvis( const CHAR_DATA * ch, const CHAR_DATA * victim );

static OBJ_DATA *group_object( OBJ_DATA * obj1, OBJ_DATA * obj2 );

static bool is_wizvis( const CHAR_DATA * ch, const CHAR_DATA * victim )
{
  if( !IS_NPC( victim )
      && IS_SET( victim->act, PLR_WIZINVIS )
      && get_trust( ch ) < victim->pcdata->wizinvis )
    return FALSE;

  return TRUE;
}

short get_trust( const CHAR_DATA * ch )
{
  if( !ch )
    return 0;

  if( ch->desc )
    if( ch->desc->original )
      ch = ch->desc->original;

  if( ch->trust != 0 )
    return ch->trust;

  if( IS_NPC( ch ) )
    return 1;

  if( IS_RETIRED( ch ) )
    return 1;

  return ch->top_level;
}

/*
 * Retrieve a character's age.
 */
short get_age( const CHAR_DATA * ch )
{
  return 17 + ( ch->played + ( current_time - ch->logon ) ) / 14400;
}

/*
 * Retrieve character's current strength.
 */
short get_curr_str( const CHAR_DATA * ch )
{
  return URANGE( 3, ch->perm_str + ch->mod_str, 25 );
}

/*
 * Retrieve character's current intelligence.
 */
short get_curr_int( const CHAR_DATA * ch )
{
  return URANGE( 3, ch->perm_int + ch->mod_int, 25 );
}

/*
 * Retrieve character's current wisdom.
 */
short get_curr_wis( const CHAR_DATA * ch )
{
  return URANGE( 3, ch->perm_wis + ch->mod_wis, 25 );
}

/*
 * Retrieve character's current dexterity.
 */
short get_curr_dex( const CHAR_DATA * ch )
{
  return URANGE( 3, ch->perm_dex + ch->mod_dex, 25 );
}

/*
 * Retrieve character's current constitution.
 */
short get_curr_con( const CHAR_DATA * ch )
{
  return URANGE( 3, ch->perm_con + ch->mod_con, 25 );
}

/*
 * Retrieve character's current charisma.
 */
short get_curr_cha( const CHAR_DATA * ch )
{
  return URANGE( 3, ch->perm_cha + ch->mod_cha, 25 );
}

/*
 * Retrieve character's current luck.
 */
short get_curr_lck( const CHAR_DATA * ch )
{
  return URANGE( 3, ch->perm_lck + ch->mod_lck, 25 );
}

short get_curr_frc( const CHAR_DATA * ch )
{
  return URANGE( 0, ch->perm_frc + ch->mod_frc, 25 );
}


/*
 * Retrieve a character's carry capacity.
 * Vastly reduced (finally) due to containers		-Thoric
 */
int can_carry_n( const CHAR_DATA * ch )
{
  int penalty = 0;

  if( !IS_NPC( ch ) && IS_IMMORTAL( ch ) )
    return get_trust( ch ) * 200;

  if( IS_NPC( ch ) && IS_SET( ch->act, ACT_PET ) )
    return 0;

  if( get_eq_char( ch, WEAR_WIELD ) )
    ++penalty;
  if( get_eq_char( ch, WEAR_DUAL_WIELD ) )
    ++penalty;
  if( get_eq_char( ch, WEAR_MISSILE_WIELD ) )
    ++penalty;
  if( get_eq_char( ch, WEAR_HOLD ) )
    ++penalty;
  if( get_eq_char( ch, WEAR_SHIELD ) )
    ++penalty;
  return URANGE( 5, get_curr_dex( ch ) - penalty, 20 );
}



/*
 * Retrieve a character's carry capacity.
 */
int can_carry_w( const CHAR_DATA * ch )
{
  if( !IS_NPC( ch ) && IS_IMMORTAL( ch ) )
    return 1000000;

  if( IS_NPC( ch ) && IS_SET( ch->act, ACT_PET ) )
    return 0;

  return str_app[get_curr_str( ch )].carry;
}


/*
 * See if a player/mob can take a piece of prototype eq		-Thoric
 */
bool can_take_proto( const CHAR_DATA * ch )
{
  if( IS_IMMORTAL( ch ) )
    return TRUE;
  else if( IS_NPC( ch ) && IS_SET( ch->act, ACT_PROTOTYPE ) )
    return TRUE;
  else
    return FALSE;
}

/*
 * Apply or remove an affect to a character.
 */
void affect_modify( CHAR_DATA * ch, AFFECT_DATA * paf, bool fAdd )
{
  OBJ_DATA *wield = NULL;
  int mod = paf->modifier;
  SKILLTYPE *skill = NULL;
  ch_ret retcode = rNONE;

  if( fAdd )
  {
    SET_BIT( ch->affected_by, paf->bitvector );
  }
  else
  {
    REMOVE_BIT( ch->affected_by, paf->bitvector );
    /*
     * might be an idea to have a duration removespell which returns
     * the spell after the duration... but would have to store
     * the removed spell's information somewhere...         -Thoric
     */
    if( ( paf->location % REVERSE_APPLY ) == APPLY_REMOVESPELL )
      return;
    switch ( paf->location % REVERSE_APPLY )
    {
      case APPLY_AFFECT:
	REMOVE_BIT( ch->affected_by, mod );
	return;
      case APPLY_RESISTANT:
	REMOVE_BIT( ch->resistant, mod );
	return;
      case APPLY_IMMUNE:
	REMOVE_BIT( ch->immune, mod );
	return;
      case APPLY_SUSCEPTIBLE:
	REMOVE_BIT( ch->susceptible, mod );
	return;
      case APPLY_WEARSPELL:	/* affect only on wear */
	return;
      case APPLY_REMOVE:
	SET_BIT( ch->affected_by, mod );
	return;
    }
    mod = 0 - mod;
  }

  switch ( paf->location % REVERSE_APPLY )
  {
    default:
      bug( "Affect_modify: unknown location %d.", paf->location );
      return;

    case APPLY_NONE:
      break;
    case APPLY_STR:
      ch->mod_str += mod;
      break;
    case APPLY_DEX:
      ch->mod_dex += mod;
      break;
    case APPLY_INT:
      ch->mod_int += mod;
      break;
    case APPLY_WIS:
      ch->mod_wis += mod;
      break;
    case APPLY_CON:
      ch->mod_con += mod;
      break;
    case APPLY_CHA:
      ch->mod_cha += mod;
      break;
    case APPLY_LCK:
      ch->mod_lck += mod;
      break;
    case APPLY_SEX:
      ch->sex = ( ch->sex + mod ) % 3;
      if( ch->sex < 0 )
	ch->sex += 2;
      ch->sex = URANGE( 0, ch->sex, 2 );
      break;
    case APPLY_LEVEL:
      break;
    case APPLY_AGE:
      break;
    case APPLY_HEIGHT:
      ch->height += mod;
      break;
    case APPLY_WEIGHT:
      ch->weight += mod;
      break;
    case APPLY_MANA:
      ch->max_mana += mod;
      break;
    case APPLY_HIT:
      ch->max_hit += mod;
      break;
    case APPLY_MOVE:
      ch->max_move += mod;
      break;
    case APPLY_GOLD:
      break;
    case APPLY_EXP:
      break;
    case APPLY_AC:
      ch->armor += mod;
      break;
    case APPLY_HITROLL:
      ch->hitroll += mod;
      break;
    case APPLY_DAMROLL:
      ch->damroll += mod;
      break;
    case APPLY_SAVING_POISON:
      ch->saving_poison_death += mod;
      break;
    case APPLY_SAVING_ROD:
      ch->saving_wand += mod;
      break;
    case APPLY_SAVING_PARA:
      ch->saving_para_petri += mod;
      break;
    case APPLY_SAVING_BREATH:
      ch->saving_breath += mod;
      break;
    case APPLY_SAVING_SPELL:
      ch->saving_spell_staff += mod;
      break;
    case APPLY_AFFECT:
      SET_BIT( ch->affected_by, mod );
      break;
    case APPLY_RESISTANT:
      SET_BIT( ch->resistant, mod );
      break;
    case APPLY_IMMUNE:
      SET_BIT( ch->immune, mod );
      break;
    case APPLY_SUSCEPTIBLE:
      SET_BIT( ch->susceptible, mod );
      break;
    case APPLY_WEAPONSPELL:	/* see fight.c */
      break;
    case APPLY_REMOVE:
      REMOVE_BIT( ch->affected_by, mod );
      break;

    case APPLY_FULL:
      if( !IS_NPC( ch ) )
	ch->pcdata->condition[COND_FULL] =
	  URANGE( 0, ch->pcdata->condition[COND_FULL] + mod, 48 );
      break;

    case APPLY_THIRST:
      if( !IS_NPC( ch ) )
	ch->pcdata->condition[COND_THIRST] =
	  URANGE( 0, ch->pcdata->condition[COND_THIRST] + mod, 48 );
      break;

    case APPLY_DRUNK:
      if( !IS_NPC( ch ) )
	ch->pcdata->condition[COND_DRUNK] =
	  URANGE( 0, ch->pcdata->condition[COND_DRUNK] + mod, 48 );
      break;

    case APPLY_MENTALSTATE:
      ch->mental_state = URANGE( -100, ch->mental_state + mod, 100 );
      break;
    case APPLY_EMOTION:
      ch->emotional_state = URANGE( -100, ch->emotional_state + mod, 100 );
      break;

    case APPLY_STRIPSN:
      if( IS_VALID_SN( mod ) )
	affect_strip( ch, mod );
      else
	bug( "affect_modify: APPLY_STRIPSN invalid sn %d", mod );
      break;

      /* spell cast upon wear/removal of an object	-Thoric */
    case APPLY_WEARSPELL:
    case APPLY_REMOVESPELL:
      if( IS_SET( ch->immune, RIS_MAGIC ) || saving_char == ch	/* so save/quit doesn't trigger */
	  || loading_char == ch )	/* so loading doesn't trigger */
	return;

      mod = abs( mod );
      if( IS_VALID_SN( mod )
	  && ( skill = skill_table[mod] ) != NULL
	  && skill->type == SKILL_SPELL )
	if( ( retcode =
	      ( *skill->spell_fun ) ( mod, ch->top_level, ch,
				      ch ) ) == rCHAR_DIED
	    || char_died( ch ) )
	  return;
      break;


      /* skill apply types	-Thoric */

    case APPLY_PALM:		/* not implemented yet */
      break;
    case APPLY_TRACK:
      if( !IS_NPC( ch ) && character_skill_level( ch, gsn_track ) > 0 )
	set_skill_level( ch, gsn_track, UMAX( 1, character_skill_level( ch, gsn_track ) + mod ) );
      break;
    case APPLY_HIDE:
      if( !IS_NPC( ch ) && character_skill_level( ch, gsn_hide ) > 0 )
	set_skill_level( ch, gsn_hide, UMAX( 1, character_skill_level( ch, gsn_hide ) + mod ) );
      break;
    case APPLY_STEAL:
      if( !IS_NPC( ch ) && character_skill_level( ch, gsn_steal ) > 0 )
	set_skill_level( ch, gsn_steal, UMAX( 1, character_skill_level( ch, gsn_steal ) + mod ) );
      break;
    case APPLY_SNEAK:
      if( !IS_NPC( ch ) && character_skill_level( ch, gsn_sneak ) > 0 )
	set_skill_level( ch, gsn_sneak, UMAX( 1, character_skill_level( ch, gsn_sneak ) + mod ) );
      break;
    case APPLY_PICK:
      if( !IS_NPC( ch ) && character_skill_level( ch, gsn_pick_lock ) > 0 )
	set_skill_level( ch, gsn_pick_lock, UMAX( 1, character_skill_level( ch, gsn_pick_lock ) + mod ) );
      break;
    case APPLY_BACKSTAB:
      if( !IS_NPC( ch ) && character_skill_level( ch, gsn_backstab ) > 0 )
	set_skill_level( ch, gsn_backstab, UMAX( 1, character_skill_level( ch, gsn_backstab ) + mod ) );
      break;
    case APPLY_DODGE:
      if( !IS_NPC( ch ) && character_skill_level( ch, gsn_dodge ) > 0 )
	set_skill_level( ch, gsn_dodge, UMAX( 1, character_skill_level( ch, gsn_dodge ) + mod ) );
      break;
    case APPLY_PEEK:
      if( !IS_NPC( ch ) && character_skill_level( ch, gsn_peek ) > 0 )
	set_skill_level( ch, gsn_peek, UMAX( 1, character_skill_level( ch, gsn_peek ) + mod ) );
      break;
    case APPLY_GOUGE:
      if( !IS_NPC( ch ) && character_skill_level( ch, gsn_gouge ) > 0 )
	set_skill_level( ch, gsn_gouge, UMAX( 1, character_skill_level( ch, gsn_gouge ) + mod ) );
      break;
    case APPLY_MOUNT:
      if( !IS_NPC( ch ) && character_skill_level( ch, gsn_mount ) > 0 )
	set_skill_level( ch, gsn_mount, UMAX( 1, character_skill_level( ch, gsn_mount ) + mod ) );
      break;
    case APPLY_DISARM:
      if( !IS_NPC( ch ) && character_skill_level( ch, gsn_disarm ) > 0 )
	set_skill_level( ch, gsn_disarm, UMAX( 1, character_skill_level( ch, gsn_disarm ) + mod ) );
      break;
    case APPLY_KICK:
      if( !IS_NPC( ch ) && character_skill_level( ch, gsn_kick ) > 0 )
	set_skill_level( ch, gsn_kick, UMAX( 1, character_skill_level( ch, gsn_kick ) + mod ) );
      break;
    case APPLY_PARRY:
      if( !IS_NPC( ch ) && character_skill_level( ch, gsn_parry ) > 0 )
	set_skill_level( ch, gsn_parry, UMAX( 1, character_skill_level( ch, gsn_parry ) + mod ) );
      break;
    case APPLY_CLIMB:
      if( !IS_NPC( ch ) && character_skill_level( ch, gsn_climb ) > 0 )
	set_skill_level( ch, gsn_climb, UMAX( 1, character_skill_level( ch, gsn_climb ) + mod ) );
      break;
    case APPLY_GRIP:
      if( !IS_NPC( ch ) && character_skill_level( ch, gsn_grip ) > 0 )
	set_skill_level( ch, gsn_grip, UMAX( 1, character_skill_level( ch, gsn_grip ) + mod ) );
      break;
  }

  /*
   * Check for weapon wielding.
   * Guard against recursion (for weapons with affects).
   */
  if( !IS_NPC( ch )
      && saving_char != ch
      && ( wield = get_eq_char( ch, WEAR_WIELD ) ) != NULL
      && get_obj_weight( wield ) > str_app[get_curr_str( ch )].wield )
  {
    static int depth;

    if( depth == 0 )
    {
      depth++;
      act( AT_ACTION, "You are too weak to wield $p any longer.",
	  ch, wield, NULL, TO_CHAR );
      act( AT_ACTION, "$n stops wielding $p.", ch, wield, NULL, TO_ROOM );
      unequip_char( ch, wield );
      depth--;
    }
  }
}

/*
 * Give an affect to a char.
 */
void affect_to_char( CHAR_DATA * ch, AFFECT_DATA * paf )
{
  AFFECT_DATA *paf_new = NULL;

  if( !ch )
  {
    bug( "Affect_to_char: NULL ch!", 0 );
    return;
  }

  if( !paf )
  {
    bug( "Affect_to_char: NULL paf!", 0 );
    return;
  }

  CREATE( paf_new, AFFECT_DATA, 1 );
  LINK( paf_new, ch->first_affect, ch->last_affect, next, prev );
  paf_new->type = paf->type;
  paf_new->duration = paf->duration;
  paf_new->location = paf->location;
  paf_new->modifier = paf->modifier;
  paf_new->bitvector = paf->bitvector;

  affect_modify( ch, paf_new, TRUE );
  return;
}


/*
 * Remove an affect from a char.
 */
void affect_remove( CHAR_DATA * ch, AFFECT_DATA * paf )
{
  if( !ch->first_affect )
  {
    bug( "Affect_remove: no affect.", 0 );
    return;
  }

  affect_modify( ch, paf, FALSE );

  UNLINK( paf, ch->first_affect, ch->last_affect, next, prev );
  DISPOSE( paf );
  return;
}

/*
 * Strip all affects of a given sn.
 */
void affect_strip( CHAR_DATA * ch, int sn )
{
  AFFECT_DATA *paf = NULL;
  AFFECT_DATA *paf_next = NULL;

  for( paf = ch->first_affect; paf; paf = paf_next )
  {
    paf_next = paf->next;
    if( paf->type == sn )
      affect_remove( ch, paf );
  }

  return;
}



/*
 * Return true if a char is affected by a spell.
 */
bool is_affected( const CHAR_DATA * ch, int sn )
{
  AFFECT_DATA *paf = NULL;

  for( paf = ch->first_affect; paf; paf = paf->next )
    if( paf->type == sn )
      return TRUE;

  return FALSE;
}


/*
 * Add or enhance an affect.
 * Limitations put in place by Thoric, they may be high... but at least
 * they're there :)
 */
void affect_join( CHAR_DATA * ch, AFFECT_DATA * paf )
{
  AFFECT_DATA *paf_old = NULL;

  for( paf_old = ch->first_affect; paf_old; paf_old = paf_old->next )
    if( paf_old->type == paf->type )
    {
      paf->duration = UMIN( 1000000, paf->duration + paf_old->duration );
      if( paf->modifier )
	paf->modifier = UMIN( 5000, paf->modifier + paf_old->modifier );
      else
	paf->modifier = paf_old->modifier;
      affect_remove( ch, paf_old );
      break;
    }

  affect_to_char( ch, paf );
  return;
}


/*
 * Move a char out of a room.
 */
void char_from_room( CHAR_DATA * ch )
{
  OBJ_DATA *obj = NULL;

  if( !ch )
  {
    bug( "Char_from_room: NULL char.", 0 );
    return;
  }

  if( !ch->in_room )
  {
    bug( "Char_from_room: NULL room: %s", ch->name );
    return;
  }


  if( ( obj = get_eq_char( ch, WEAR_LIGHT ) ) != NULL
      && obj->item_type == ITEM_LIGHT
      && obj->value[2] != 0 && ch->in_room->light > 0 )
    --ch->in_room->light;

  UNLINK( ch, ch->in_room->first_person, ch->in_room->last_person,
      next_in_room, prev_in_room );
  ch->in_room = NULL;
  ch->next_in_room = NULL;
  ch->prev_in_room = NULL;

  if( !IS_NPC( ch ) && get_timer( ch, TIMER_SHOVEDRAG ) > 0 )
    remove_timer( ch, TIMER_SHOVEDRAG );

  return;
}


/*
 * Move a char into a room.
 */
void char_to_room( CHAR_DATA * ch, ROOM_INDEX_DATA * pRoomIndex )
{
  OBJ_DATA *obj = NULL;

  if( !ch )
  {
    bug( "Char_to_room: NULL ch!", 0 );
    return;
  }
  if( !pRoomIndex )
  {
    char buf[MAX_STRING_LENGTH];

    sprintf( buf,
	"Char_to_room: %s -> NULL room!  Putting char in limbo (%d)",
	ch->name, ROOM_VNUM_LIMBO );
    bug( buf, 0 );
    /* This used to just return, but there was a problem with crashing
       and I saw no reason not to just put the char in limbo. -Narn */
    pRoomIndex = get_room_index( ROOM_VNUM_LIMBO );
  }

  ch->in_room = pRoomIndex;
  LINK( ch, pRoomIndex->first_person, pRoomIndex->last_person,
      next_in_room, prev_in_room );


  if( ( obj = get_eq_char( ch, WEAR_LIGHT ) ) != NULL
      && obj->item_type == ITEM_LIGHT && obj->value[2] != 0 )
    ++ch->in_room->light;

  if( !IS_NPC( ch )
      && IS_SET( ch->in_room->room_flags, ROOM_SAFE )
      && get_timer( ch, TIMER_SHOVEDRAG ) <= 0 )
    add_timer( ch, TIMER_SHOVEDRAG, 10, NULL, 0 );     /*-30 Seconds-*/

  return;
}

/*
 * Give an obj to a char.
 */
OBJ_DATA *obj_to_char( OBJ_DATA * obj, CHAR_DATA * ch )
{
  OBJ_DATA *otmp = NULL;
  OBJ_DATA *oret = obj;
  bool skipgroup = FALSE, grouped = FALSE;
  int oweight = get_obj_weight( obj );
  int onum = get_obj_number( obj );
  int wear_loc = obj->wear_loc;
  int extra_flags = obj->extra_flags;

  if( IS_OBJ_STAT( obj, ITEM_PROTOTYPE ) )
  {
    if( !IS_IMMORTAL( ch )
	&& ( !IS_NPC( ch ) || !IS_SET( ch->act, ACT_PROTOTYPE ) ) )
      return obj_to_room( obj, ch->in_room );
  }

  if( loading_char == ch )
  {
    int x = 0, y = 0;

    for( x = 0; x < MAX_WEAR; x++ )
      for( y = 0; y < MAX_LAYERS; y++ )
	if( save_equipment[x][y] == obj )
	{
	  skipgroup = TRUE;
	  break;
	}
  }

  if( !skipgroup )
    for( otmp = ch->first_carrying; otmp; otmp = otmp->next_content )
      if( ( oret = group_object( otmp, obj ) ) == otmp )
      {
	grouped = TRUE;
	break;
      }

  if( !grouped )
  {
    LINK( obj, ch->first_carrying, ch->last_carrying, next_content,
	prev_content );
    obj->carried_by = ch;
    obj->in_room = NULL;
    obj->in_obj = NULL;
  }
  if( wear_loc == WEAR_NONE )
  {
    ch->carry_number += onum;
    ch->carry_weight += oweight;
  }
  else if( !IS_SET( extra_flags, ITEM_MAGIC ) )
    ch->carry_weight += oweight;
  return ( oret ? oret : obj );
}



/*
 * Take an obj from its character.
 */
void obj_from_char( OBJ_DATA * obj )
{
  CHAR_DATA *ch = NULL;

  if( ( ch = obj->carried_by ) == NULL )
  {
    bug( "Obj_from_char: null ch.", 0 );
    return;
  }

  if( obj->wear_loc != WEAR_NONE )
    unequip_char( ch, obj );

  /* obj may drop during unequip... */
  if( !obj->carried_by )
    return;

  UNLINK( obj, ch->first_carrying, ch->last_carrying, next_content,
      prev_content );

  if( IS_OBJ_STAT( obj, ITEM_COVERING ) && obj->first_content )
    empty_obj( obj, NULL, NULL );

  obj->in_room = NULL;
  obj->carried_by = NULL;
  ch->carry_number -= get_obj_number( obj );
  ch->carry_weight -= get_obj_weight( obj );
  return;
}


/*
 * Find the ac value of an obj, including position effect.
 */
int apply_ac( const OBJ_DATA * obj, int iWear )
{
  if( obj->item_type != ITEM_ARMOR )
    return 0;

  switch ( iWear )
  {
    case WEAR_BODY:
      return 3 * obj->value[0];
    case WEAR_HEAD:
      return 2 * obj->value[0];
    case WEAR_LEGS:
      return 2 * obj->value[0];
    case WEAR_FEET:
      return obj->value[0];
    case WEAR_HANDS:
      return obj->value[0];
    case WEAR_ARMS:
      return obj->value[0];
    case WEAR_SHIELD:
      return obj->value[0];
    case WEAR_FINGER_L:
      return obj->value[0];
    case WEAR_FINGER_R:
      return obj->value[0];
    case WEAR_NECK_1:
      return obj->value[0];
    case WEAR_NECK_2:
      return obj->value[0];
    case WEAR_ABOUT:
      return 2 * obj->value[0];
    case WEAR_WAIST:
      return obj->value[0];
    case WEAR_WRIST_L:
      return obj->value[0];
    case WEAR_WRIST_R:
      return obj->value[0];
    case WEAR_HOLD:
      return obj->value[0];
    case WEAR_EYES:
      return obj->value[0];
  }

  return 0;
}



/*
 * Find a piece of eq on a character.
 * Will pick the top layer if clothing is layered.		-Thoric
 */
OBJ_DATA *get_eq_char( const CHAR_DATA * ch, int iWear )
{
  OBJ_DATA *obj = NULL, *maxobj = NULL;

  for( obj = ch->first_carrying; obj; obj = obj->next_content )
    if( obj->wear_loc == iWear )
    {
      if( !obj->pIndexData->layers )
      {
	return obj;
      }
      else if( !maxobj
	  || obj->pIndexData->layers > maxobj->pIndexData->layers )
      {
	maxobj = obj;
      }
    }

  return maxobj;
}

/*
 * Equip a char with an obj.
 */
void equip_char( CHAR_DATA * ch, OBJ_DATA * obj, int iWear )
{
  AFFECT_DATA *paf = NULL;
  OBJ_DATA *otmp = NULL;

  if( ( otmp = get_eq_char( ch, iWear ) ) != NULL
      && ( !otmp->pIndexData->layers || !obj->pIndexData->layers ) )
  {
    bug( "Equip_char: already equipped (%d).", iWear );
    return;
  }

  separate_obj( obj );		/* just in case */

  ch->armor -= apply_ac( obj, iWear );
  obj->wear_loc = iWear;

  ch->carry_number -= get_obj_number( obj );
  if( IS_SET( obj->extra_flags, ITEM_MAGIC ) )
    ch->carry_weight -= get_obj_weight( obj );

  for( paf = obj->pIndexData->first_affect; paf; paf = paf->next )
    affect_modify( ch, paf, TRUE );
  for( paf = obj->first_affect; paf; paf = paf->next )
    affect_modify( ch, paf, TRUE );

  if( obj->item_type == ITEM_LIGHT && obj->value[2] != 0 && ch->in_room )
    ++ch->in_room->light;

  return;
}



/*
 * Unequip a char with an obj.
 */
void unequip_char( CHAR_DATA * ch, OBJ_DATA * obj )
{
  AFFECT_DATA *paf = NULL;

  if( obj->wear_loc == WEAR_NONE )
  {
    bug( "Unequip_char: already unequipped.", 0 );
    return;
  }

  ch->carry_number += get_obj_number( obj );
  if( IS_SET( obj->extra_flags, ITEM_MAGIC ) )
    ch->carry_weight += get_obj_weight( obj );

  ch->armor += apply_ac( obj, obj->wear_loc );
  obj->wear_loc = -1;

  for( paf = obj->pIndexData->first_affect; paf; paf = paf->next )
    affect_modify( ch, paf, FALSE );
  if( obj->carried_by )
    for( paf = obj->first_affect; paf; paf = paf->next )
      affect_modify( ch, paf, FALSE );

  if( !obj->carried_by )
    return;

  if( obj->item_type == ITEM_LIGHT
      && obj->value[2] != 0 && ch->in_room && ch->in_room->light > 0 )
    --ch->in_room->light;

  return;
}



/*
 * Count occurrences of an obj in a list.
 */
int count_obj_list( const OBJ_INDEX_DATA * pObjIndex, const OBJ_DATA * list )
{
  const OBJ_DATA *obj = NULL;
  int nMatch = 0;

  for( obj = list; obj; obj = obj->next_content )
    if( obj->pIndexData == pObjIndex )
      nMatch++;

  return nMatch;
}



/*
 * Move an obj out of a room.
 */
void write_corpses( const CHAR_DATA * ch, const char *name );

int falling = 0;

void obj_from_room( OBJ_DATA * obj )
{
  ROOM_INDEX_DATA *in_room = NULL;

  if( ( in_room = obj->in_room ) == NULL )
  {
    bug( "obj_from_room: NULL.", 0 );
    return;
  }

  UNLINK( obj, in_room->first_content, in_room->last_content,
      next_content, prev_content );

  if( IS_OBJ_STAT( obj, ITEM_COVERING ) && obj->first_content )
    empty_obj( obj, NULL, obj->in_room );

  obj->carried_by = NULL;
  obj->in_obj = NULL;
  obj->in_room = NULL;
  if( obj->pIndexData->vnum == OBJ_VNUM_CORPSE_PC && falling == 0 )
    write_corpses( NULL, obj->short_descr + 14 );
  return;
}


/*
 * Move an obj into a room.
 */
OBJ_DATA *obj_to_room( OBJ_DATA * obj, ROOM_INDEX_DATA * pRoomIndex )
{
  OBJ_DATA *otmp = NULL, *oret = NULL;

  for( otmp = pRoomIndex->first_content; otmp; otmp = otmp->next_content )
    if( ( oret = group_object( otmp, obj ) ) == otmp )
      return oret;

  LINK( obj, pRoomIndex->first_content, pRoomIndex->last_content,
      next_content, prev_content );
  obj->in_room = pRoomIndex;
  obj->carried_by = NULL;
  obj->in_obj = NULL;
  falling++;
  obj_fall( obj, FALSE );
  falling--;
  if( obj->pIndexData->vnum == OBJ_VNUM_CORPSE_PC && falling == 0 )
    write_corpses( NULL, obj->short_descr + 14 );
  return obj;
}



/*
 * Move an object into an object.
 */
OBJ_DATA *obj_to_obj( OBJ_DATA * obj, OBJ_DATA * obj_to )
{
  OBJ_DATA *otmp = NULL, *oret = NULL;

  if( obj == obj_to )
  {
    bug( "Obj_to_obj: trying to put object inside itself: vnum %d",
	obj->pIndexData->vnum );
    return obj;
  }
  /* Big carry_weight bug fix here by Thoric */
  if( obj->carried_by != obj_to->carried_by )
  {
    if( obj->carried_by )
      obj->carried_by->carry_weight -= get_obj_weight( obj );
    if( obj_to->carried_by )
      obj_to->carried_by->carry_weight += get_obj_weight( obj );
  }

  for( otmp = obj_to->first_content; otmp; otmp = otmp->next_content )
    if( ( oret = group_object( otmp, obj ) ) == otmp )
      return oret;

  LINK( obj, obj_to->first_content, obj_to->last_content,
      next_content, prev_content );
  obj->in_obj = obj_to;
  obj->in_room = NULL;
  obj->carried_by = NULL;

  return obj;
}


/*
 * Move an object out of an object.
 */
void obj_from_obj( OBJ_DATA * obj )
{
  OBJ_DATA *obj_from = NULL;

  if( ( obj_from = obj->in_obj ) == NULL )
  {
    bug( "Obj_from_obj: null obj_from.", 0 );
    return;
  }

  UNLINK( obj, obj_from->first_content, obj_from->last_content,
      next_content, prev_content );

  if( IS_OBJ_STAT( obj, ITEM_COVERING ) && obj->first_content )
    empty_obj( obj, obj->in_obj, NULL );

  obj->in_obj = NULL;
  obj->in_room = NULL;
  obj->carried_by = NULL;

  for( ; obj_from; obj_from = obj_from->in_obj )
    if( obj_from->carried_by )
      obj_from->carried_by->carry_weight -= get_obj_weight( obj );

  return;
}



/*
 * Extract an obj from the world.
 */
void extract_obj( OBJ_DATA * obj )
{
  OBJ_DATA *obj_content = NULL;

  if( !obj )
  {
    bug( "extract_obj: !obj" );
    return;
  }

  if( obj_extracted( obj ) )
  {
    bug( "extract_obj: obj %d already extracted!", obj->pIndexData->vnum );
    return;
  }

  if( obj->carried_by )
    obj_from_char( obj );
  else if( obj->in_room )
    obj_from_room( obj );
  else if( obj->in_obj )
    obj_from_obj( obj );

  while( ( obj_content = obj->last_content ) != NULL )
    extract_obj( obj_content );

  {
    AFFECT_DATA *paf = NULL;
    AFFECT_DATA *paf_next = NULL;

    for( paf = obj->first_affect; paf; paf = paf_next )
    {
      paf_next = paf->next;
      DISPOSE( paf );
    }
    obj->first_affect = obj->last_affect = NULL;
  }

  {
    EXTRA_DESCR_DATA *ed = NULL;
    EXTRA_DESCR_DATA *ed_next = NULL;

    for( ed = obj->first_extradesc; ed; ed = ed_next )
    {
      ed_next = ed->next;
      STRFREE( ed->description );
      STRFREE( ed->keyword );
      DISPOSE( ed );
    }
    obj->first_extradesc = obj->last_extradesc = NULL;
  }

  if( obj == gobj_prev )
    gobj_prev = obj->prev;

  UNLINK( obj, first_object, last_object, next, prev );
  /* shove onto extraction queue */
  queue_extracted_obj( obj );

  obj->pIndexData->count -= obj->count;
  numobjsloaded -= obj->count;
  --physicalobjects;
  if( obj->serial == cur_obj )
  {
    cur_obj_extracted = TRUE;
    if( global_objcode == rNONE )
      global_objcode = rOBJ_EXTRACTED;
  }
  return;
}



/*
 * Extract a char from the world.
 */
void extract_char( CHAR_DATA * ch, bool fPull )
{
  CHAR_DATA *wch = NULL;
  char buf[MAX_STRING_LENGTH];
  ROOM_INDEX_DATA *location = NULL;

  if( !ch )
  {
    bug( "Extract_char: NULL ch.", 0 );
    return;			/* who removed this line? */
  }

  if( !ch->in_room )
  {
    bug( "Extract_char: NULL room.", 0 );
    return;
  }

  if( ch == supermob )
  {
    bug( "Extract_char: ch == supermob!", 0 );
    return;
  }

  if( char_died( ch ) )
  {
    sprintf( buf, "extract_char: %s already died!", ch->name );
    bug( buf, 0 );
    return;
  }

  if( ch == cur_char )
    cur_char_died = TRUE;

  /* shove onto extraction queue */
  queue_extracted_char( ch, fPull );

  if( gch_prev == ch )
    gch_prev = ch->prev;

  if( fPull && !IS_SET( ch->act, ACT_POLYMORPHED ) )
    die_follower( ch );

  stop_fighting( ch, TRUE );

  if( ch->mount )
  {
    REMOVE_BIT( ch->mount->act, ACT_MOUNTED );
    ch->mount = NULL;
    ch->position = POS_STANDING;
  }

  if( IS_NPC( ch ) && IS_SET( ch->act, ACT_MOUNTED ) )
  {
    for( wch = first_char; wch; wch = wch->next )
    {
      if( wch->mount == ch )
      {
	wch->mount = NULL;
	wch->position = POS_STANDING;
      }
    }

    REMOVE_BIT( ch->act, ACT_MOUNTED );
  }

  character_extract_carried_objects( ch );

  char_from_room( ch );

  if( !fPull )
  {
    location = NULL;

    if( !location )
      location = get_room_index( wherehome( ch ) );

    if( !location )
      location = get_room_index( 1 );

    char_to_room( ch, location );

    act( AT_MAGIC, "$n appears from some strange swirling mists!", ch, NULL,
	NULL, TO_ROOM );
    ch->position = POS_RESTING;
    return;
  }

  if( IS_NPC( ch ) )
  {
    --ch->pIndexData->count;
    --nummobsloaded;
  }

  if( ch->desc && ch->desc->original && IS_SET( ch->act, ACT_POLYMORPHED ) )
    do_revert( ch, STRLIT_EMPTY );

  if( ch->desc && ch->desc->original )
    do_return( ch, STRLIT_EMPTY );

  for( wch = first_char; wch; wch = wch->next )
    if( wch->reply == ch )
      wch->reply = NULL;

  UNLINK( ch, first_char, last_char, next, prev );

  if( ch->desc )
  {
    if( ch->desc->character != ch )
    {
      bug( "Extract_char: char's descriptor points to another char", 0 );
    }
    else
    {
      ch->desc->character = NULL;
      close_socket( ch->desc, FALSE );
      ch->desc = NULL;
    }
  }
}


/*
 * Find a char in the room.
 */
CHAR_DATA *get_char_room( const CHAR_DATA * ch, const char *argument )
{
  char arg[MAX_INPUT_LENGTH];
  CHAR_DATA *rch;
  int number, count, vnum;

  number = number_argument( argument, arg );
  if( !str_cmp( arg, "self" ) )
    return ( CHAR_DATA * ) ch;

  if( IS_IMMORTAL( ch ) && is_number( arg ) )
    vnum = atoi( arg );
  else
    vnum = -1;

  count = 0;

  for( rch = ch->in_room->first_person; rch; rch = rch->next_in_room )
    if( can_see( ch, rch )
	&& ( nifty_is_name( arg, rch->name )
	  || ( IS_NPC( rch ) && vnum == rch->pIndexData->vnum ) ) )
    {
      if( number == 0 && !IS_NPC( rch ) )
	return rch;
      else if( ++count == number )
	return rch;
    }

  if( vnum != -1 )
    return NULL;

  /* If we didn't find an exact match, run through the list of characters
     again looking for prefix matching, ie gu == guard.
     Added by Narn, Sept/96
     */
  count = 0;
  for( rch = ch->in_room->first_person; rch; rch = rch->next_in_room )
  {
    if( !can_see( ch, rch ) || !nifty_is_name_prefix( arg, rch->name ) )
      continue;
    if( number == 0 && !IS_NPC( rch ) )
      return rch;
    else if( ++count == number )
      return rch;
  }

  return NULL;
}




/*
 * Find a char in the world.
 */
CHAR_DATA *get_char_world( const CHAR_DATA * ch, const char *argument )
{
  char arg[MAX_INPUT_LENGTH];
  CHAR_DATA *wch;
  int number, count, vnum;

  number = number_argument( argument, arg );
  count = 0;

  if( !str_cmp( arg, "self" ) )
    return ( CHAR_DATA * ) ch;

  /*
   * Allow reference by vnum for saints+                      -Thoric
   */
  if( IS_IMMORTAL( ch ) && is_number( arg ) )
    vnum = atoi( arg );
  else
    vnum = -1;

  /* check the room for an exact match */
  for( wch = ch->in_room->first_person; wch; wch = wch->next_in_room )
    if( ( nifty_is_name( arg, wch->name )
	  || ( IS_NPC( wch ) && vnum == wch->pIndexData->vnum ) )
	&& is_wizvis( ch, wch ) )
    {
      if( number == 0 && !IS_NPC( wch ) )
	return wch;
      else if( ++count == number )
	return wch;
    }

  count = 0;

  /* check the world for an exact match */
  for( wch = first_char; wch; wch = wch->next )
    if( ( nifty_is_name( arg, wch->name )
	  || ( IS_NPC( wch ) && vnum == wch->pIndexData->vnum ) )
	&& is_wizvis( ch, wch ) )
    {
      if( number == 0 && !IS_NPC( wch ) )
	return wch;
      else if( ++count == number )
	return wch;
    }

  /* bail out if looking for a vnum match */
  if( vnum != -1 )
    return NULL;

  /*
   * If we didn't find an exact match, check the room for
   * for a prefix match, ie gu == guard.
   * Added by Narn, Sept/96
   */
  count = 0;
  for( wch = ch->in_room->first_person; wch; wch = wch->next_in_room )
  {
    if( !nifty_is_name_prefix( arg, wch->name ) )
      continue;
    if( number == 0 && !IS_NPC( wch ) && is_wizvis( ch, wch ) )
      return wch;
    else if( ++count == number && is_wizvis( ch, wch ) )
      return wch;
  }

  /*
   * If we didn't find a prefix match in the room, run through the full list
   * of characters looking for prefix matching, ie gu == guard.
   * Added by Narn, Sept/96
   */
  count = 0;
  for( wch = first_char; wch; wch = wch->next )
  {
    if( !nifty_is_name_prefix( arg, wch->name ) )
      continue;
    if( number == 0 && !IS_NPC( wch ) && is_wizvis( ch, wch ) )
      return wch;
    else if( ++count == number && is_wizvis( ch, wch ) )
      return wch;
  }

  return NULL;
}



/*
 * Find some object with a given index data.
 * Used by area-reset 'P', 'T' and 'H' commands.
 */
OBJ_DATA *get_obj_type( const OBJ_INDEX_DATA * pObjIndex )
{
  OBJ_DATA *obj;

  for( obj = last_object; obj; obj = obj->prev )
    if( obj->pIndexData == pObjIndex )
      return obj;

  return NULL;
}


/*
 * Find an obj in a list.
 */
OBJ_DATA *get_obj_list( const CHAR_DATA * ch, const char *argument,
    OBJ_DATA * list )
{
  char arg[MAX_INPUT_LENGTH];
  OBJ_DATA *obj;
  int number;
  int count;

  number = number_argument( argument, arg );
  count = 0;
  for( obj = list; obj; obj = obj->next_content )
    if( can_see_obj( ch, obj ) && nifty_is_name( arg, obj->name ) )
      if( ( count += obj->count ) >= number )
	return obj;

  /* If we didn't find an exact match, run through the list of objects
     again looking for prefix matching, ie swo == sword.
     Added by Narn, Sept/96
     */
  count = 0;
  for( obj = list; obj; obj = obj->next_content )
    if( can_see_obj( ch, obj ) && nifty_is_name_prefix( arg, obj->name ) )
      if( ( count += obj->count ) >= number )
	return obj;

  return NULL;
}

/*
 * Find an obj in a list...going the other way			-Thoric
 */
OBJ_DATA *get_obj_list_rev( const CHAR_DATA * ch, const char *argument,
    OBJ_DATA * list )
{
  char arg[MAX_INPUT_LENGTH];
  OBJ_DATA *obj;
  int number;
  int count;

  number = number_argument( argument, arg );
  count = 0;
  for( obj = list; obj; obj = obj->prev_content )
    if( can_see_obj( ch, obj ) && nifty_is_name( arg, obj->name ) )
      if( ( count += obj->count ) >= number )
	return obj;

  /* If we didn't find an exact match, run through the list of objects
     again looking for prefix matching, ie swo == sword.
     Added by Narn, Sept/96
     */
  count = 0;
  for( obj = list; obj; obj = obj->prev_content )
    if( can_see_obj( ch, obj ) && nifty_is_name_prefix( arg, obj->name ) )
      if( ( count += obj->count ) >= number )
	return obj;

  return NULL;
}



/*
 * Find an obj in player's inventory.
 */
OBJ_DATA *get_obj_carry( const CHAR_DATA * ch, const char *argument )
{
  char arg[MAX_INPUT_LENGTH];
  OBJ_DATA *obj;
  int number, count, vnum;

  number = number_argument( argument, arg );
  if( IS_IMMORTAL( ch ) && is_number( arg ) )
    vnum = atoi( arg );
  else
    vnum = -1;

  count = 0;
  for( obj = ch->last_carrying; obj; obj = obj->prev_content )
    if( obj->wear_loc == WEAR_NONE
	&& can_see_obj( ch, obj )
	&& ( nifty_is_name( arg, obj->name )
	  || obj->pIndexData->vnum == vnum ) )
      if( ( count += obj->count ) >= number )
	return obj;

  if( vnum != -1 )
    return NULL;

  /* If we didn't find an exact match, run through the list of objects
     again looking for prefix matching, ie swo == sword.
     Added by Narn, Sept/96
     */
  count = 0;
  for( obj = ch->last_carrying; obj; obj = obj->prev_content )
    if( obj->wear_loc == WEAR_NONE
	&& can_see_obj( ch, obj ) && nifty_is_name_prefix( arg, obj->name ) )
      if( ( count += obj->count ) >= number )
	return obj;

  return NULL;
}



/*
 * Find an obj in player's equipment.
 */
OBJ_DATA *get_obj_wear( const CHAR_DATA * ch, const char *argument )
{
  char arg[MAX_INPUT_LENGTH];
  OBJ_DATA *obj;
  int number, count, vnum;

  if( !ch )
  {
    bug( "get_obj_wear: null ch" );
  }

  number = number_argument( argument, arg );

  if( IS_IMMORTAL( ch ) && is_number( arg ) )
    vnum = atoi( arg );
  else
    vnum = -1;

  count = 0;
  for( obj = ch->last_carrying; obj; obj = obj->prev_content )
    if( obj->wear_loc != WEAR_NONE
	&& can_see_obj( ch, obj )
	&& ( nifty_is_name( arg, obj->name )
	  || obj->pIndexData->vnum == vnum ) )
      if( ++count == number )
	return obj;

  if( vnum != -1 )
    return NULL;

  /* If we didn't find an exact match, run through the list of objects
     again looking for prefix matching, ie swo == sword.
     Added by Narn, Sept/96
     */
  count = 0;
  for( obj = ch->last_carrying; obj; obj = obj->prev_content )
    if( obj->wear_loc != WEAR_NONE
	&& can_see_obj( ch, obj ) && nifty_is_name_prefix( arg, obj->name ) )
      if( ++count == number )
	return obj;

  return NULL;
}



/*
 * Find an obj in the room or in inventory.
 */
OBJ_DATA *get_obj_here( const CHAR_DATA * ch, const char *argument )
{
  OBJ_DATA *obj;

  if( !ch || !ch->in_room )
    return NULL;

  obj = get_obj_list_rev( ch, argument, ch->in_room->last_content );
  if( obj )
    return obj;

  if( ( obj = get_obj_carry( ch, argument ) ) != NULL )
    return obj;

  if( ( obj = get_obj_wear( ch, argument ) ) != NULL )
    return obj;

  return NULL;
}



/*
 * Find an obj in the world.
 */
OBJ_DATA *get_obj_world( const CHAR_DATA * ch, const char *argument )
{
  char arg[MAX_INPUT_LENGTH];
  OBJ_DATA *obj;
  int number, count, vnum;

  if( !ch )
    return NULL;

  if( ( obj = get_obj_here( ch, argument ) ) != NULL )
    return obj;

  number = number_argument( argument, arg );

  /*
   * Allow reference by vnum for saints+                      -Thoric
   */
  if( IS_IMMORTAL( ch ) && is_number( arg ) )
    vnum = atoi( arg );
  else
    vnum = -1;

  count = 0;
  for( obj = first_object; obj; obj = obj->next )
    if( can_see_obj( ch, obj ) && ( nifty_is_name( arg, obj->name )
	  || vnum == obj->pIndexData->vnum ) )
      if( ( count += obj->count ) >= number )
	return obj;

  /* bail out if looking for a vnum */
  if( vnum != -1 )
    return NULL;

  /* If we didn't find an exact match, run through the list of objects
     again looking for prefix matching, ie swo == sword.
     Added by Narn, Sept/96
     */
  count = 0;
  for( obj = first_object; obj; obj = obj->next )
    if( can_see_obj( ch, obj ) && nifty_is_name_prefix( arg, obj->name ) )
      if( ( count += obj->count ) >= number )
	return obj;

  return NULL;
}


/*
 * How mental state could affect finding an object		-Thoric
 * Used by get/drop/put/quaff/recite/etc
 * Increasingly freaky based on mental state and drunkeness
 */
bool ms_find_obj( const CHAR_DATA * ch )
{
  int ms = ch->mental_state;
  int drunk = IS_NPC( ch ) ? 0 : ch->pcdata->condition[COND_DRUNK];
  const char *t;

  /*
   * we're going to be nice and let nothing weird happen unless
   * you're a tad messed up
   */
  drunk = UMAX( 1, drunk );
  if( abs( ms ) + ( drunk / 3 ) < 30 )
    return FALSE;
  if( ( number_percent() + ( ms < 0 ? 15 : 5 ) ) >
      abs( ms ) / 2 + drunk / 4 )
    return FALSE;
  if( ms > 15 )			/* range 1 to 20 */
    switch ( number_range( UMAX( 1, ( ms / 5 - 15 ) ), ( ms + 4 ) / 5 ) )
    {
      default:
      case 1:
	t = "As you reach for it, you forgot what it was...\r\n";
	break;
      case 2:
	t = "As you reach for it, something inside stops you...\r\n";
	break;
      case 3:
	t = "As you reach for it, it seems to move out of the way...\r\n";
	break;
      case 4:
	t =
	  "You grab frantically for it, but can't seem to get a hold of it...\r\n";
	break;
      case 5:
	t = "It disappears as soon as you touch it!\r\n";
	break;
      case 6:
	t = "You would if it would stay still!\r\n";
	break;
      case 7:
	t = "Whoa!  It's covered in blood!  Ack!  Ick!\r\n";
	break;
      case 8:
	t = "Wow... trails!\r\n";
	break;
      case 9:
	t =
	  "You reach for it, then notice the back of your hand is growing something!\r\n";
	break;
      case 10:
	t =
	  "As you grasp it, it shatters into tiny shards which bite into your flesh!\r\n";
	break;
      case 11:
	t = "What about that huge dragon flying over your head?!?!?\r\n";
	break;
      case 12:
	t = "You stratch yourself instead...\r\n";
	break;
      case 13:
	t = "You hold the universe in the palm of your hand!\r\n";
	break;
      case 14:
	t = "You're too scared.\r\n";
	break;
      case 15:
	t = "Your mother smacks your hand... 'NO!'\r\n";
	break;
      case 16:
	t =
	  "Your hand grasps the worse pile of revoltingness than you could ever imagine!\r\n";
	break;
      case 17:
	t = "You stop reaching for it as it screams out at you in pain!\r\n";
	break;
      case 18:
	t =
	  "What about the millions of burrow-maggots feasting on your arm?!?!\r\n";
	break;
      case 19:
	t =
	  "That doesn't matter anymore... you've found the true answer to everything!\r\n";
	break;
      case 20:
	t = "A supreme entity has no need for that.\r\n";
	break;
    }
  else
  {
    int sub = URANGE( 1, abs( ms ) / 2 + drunk, 60 );
    switch ( number_range( 1, sub / 10 ) )
    {
      default:
      case 1:
	t = "In just a second...\r\n";
	break;
      case 2:
	t = "You can't find that...\r\n";
	break;
      case 3:
	t = "It's just beyond your grasp...\r\n";
	break;
      case 4:
	t = "...but it's under a pile of other stuff...\r\n";
	break;
      case 5:
	t = "You go to reach for it, but pick your nose instead.\r\n";
	break;
      case 6:
	t = "Which one?!?  I see two... no three...\r\n";
	break;
    }
  }
  send_to_char( t, ch );
  return TRUE;
}


/*
 * Generic get obj function that supports optional containers.	-Thoric
 * currently only used for "eat" and "quaff".
 */
OBJ_DATA *find_obj( CHAR_DATA * ch, char *argument, bool carryonly )
{
  char arg1[MAX_INPUT_LENGTH];
  char arg2[MAX_INPUT_LENGTH];
  OBJ_DATA *obj;

  argument = one_argument( argument, arg1 );
  argument = one_argument( argument, arg2 );

  if( !str_cmp( arg2, "from" ) && argument[0] != '\0' )
    argument = one_argument( argument, arg2 );

  if( arg2[0] == '\0' )
  {
    if( carryonly && ( obj = get_obj_carry( ch, arg1 ) ) == NULL )
    {
      send_to_char( "You do not have that item.\r\n", ch );
      return NULL;
    }
    else if( !carryonly && ( obj = get_obj_here( ch, arg1 ) ) == NULL )
    {
      act( AT_PLAIN, "I see no $T here.", ch, NULL, arg1, TO_CHAR );
      return NULL;
    }
    return obj;
  }
  else
  {
    OBJ_DATA *container;

    if( carryonly
	&& ( container = get_obj_carry( ch, arg2 ) ) == NULL
	&& ( container = get_obj_wear( ch, arg2 ) ) == NULL )
    {
      send_to_char( "You do not have that item.\r\n", ch );
      return NULL;
    }
    if( !carryonly && ( container = get_obj_here( ch, arg2 ) ) == NULL )
    {
      act( AT_PLAIN, "I see no $T here.", ch, NULL, arg2, TO_CHAR );
      return NULL;
    }

    if( !IS_OBJ_STAT( container, ITEM_COVERING )
	&& IS_SET( container->value[1], CONT_CLOSED ) )
    {
      act( AT_PLAIN, "The $d is closed.", ch, NULL, container->name,
	  TO_CHAR );
      return NULL;
    }

    obj = get_obj_list( ch, arg1, container->first_content );
    if( !obj )
      act( AT_PLAIN, IS_OBJ_STAT( container, ITEM_COVERING ) ?
	  "I see nothing like that beneath $p." :
	  "I see nothing like that in $p.", ch, container, NULL, TO_CHAR );
    return obj;
  }
  return NULL;
}

int get_obj_number( const OBJ_DATA * obj )
{
  return obj->count;
}

/*
 * Return weight of an object, including weight of contents.
 */
int get_obj_weight( const OBJ_DATA * obj )
{
  int weight = obj->count * obj->weight;

  for( obj = obj->first_content; obj; obj = obj->next_content )
    weight += get_obj_weight( obj );

  return weight;
}

/*
 * True if room is dark.
 */
bool room_is_dark( const ROOM_INDEX_DATA * pRoomIndex )
{
  if( !pRoomIndex )
  {
    bug( "room_is_dark: NULL pRoomIndex", 0 );
    return TRUE;
  }

  if( pRoomIndex->light > 0 )
    return FALSE;

  if( IS_SET( pRoomIndex->room_flags, ROOM_DARK ) )
    return TRUE;

  if( pRoomIndex->sector_type == SECT_INSIDE
      || pRoomIndex->sector_type == SECT_CITY )
    return FALSE;

  if( weather_info.sunlight == SUN_SET || weather_info.sunlight == SUN_DARK )
    return TRUE;

  return FALSE;
}



/*
 * True if room is private.
 */
bool room_is_private( const CHAR_DATA * ch,
    const ROOM_INDEX_DATA * pRoomIndex )
{
  const CHAR_DATA *rch;
  int count;

  if( !ch )
  {
    bug( "room_is_private: NULL ch", 0 );
    return FALSE;
  }

  if( !pRoomIndex )
  {
    bug( "room_is_private: NULL pRoomIndex", 0 );
    return FALSE;
  }

  if( IS_SET( pRoomIndex->room_flags, ROOM_PLR_HOME )
      && ch->plr_home != pRoomIndex )
    return TRUE;

  count = 0;

  for( rch = pRoomIndex->first_person; rch; rch = rch->next_in_room )
    count++;

  return FALSE;
}



/*
 * True if char can see victim.
 */
bool can_see( const CHAR_DATA * ch, const CHAR_DATA * victim )
{
  if( !victim )
    return FALSE;

  if( victim->position == POS_FIGHTING || victim->position < POS_SLEEPING )
    return TRUE;

  if( !ch )
  {
    if( IS_AFFECTED( victim, AFF_INVISIBLE )
	|| IS_AFFECTED( victim, AFF_HIDE )
	|| IS_SET( victim->act, PLR_WIZINVIS ) )
      return FALSE;
    else
      return TRUE;
  }

  if( ch == victim )
    return TRUE;

  if( !IS_NPC( victim )
      && IS_SET( victim->act, PLR_WIZINVIS )
      && get_trust( ch ) < victim->pcdata->wizinvis )
    return FALSE;

  if( victim->position == POS_FIGHTING || victim->position < POS_SLEEPING )
    return TRUE;

  if( victim->position == POS_FIGHTING || victim->position < POS_SLEEPING )
    return TRUE;

  /* SB */
  if( IS_NPC( victim )
      && IS_SET( victim->act, ACT_MOBINVIS )
      && get_trust( ch ) < victim->mobinvis )
    return FALSE;

  if( !IS_IMMORTAL( ch ) && !IS_NPC( victim ) && !victim->desc
      && get_timer( victim, TIMER_RECENTFIGHT ) > 0
      && ( !victim->switched
	|| !IS_AFFECTED( victim->switched, AFF_POSSESS ) ) )
    return FALSE;

  if( !IS_NPC( ch ) && IS_SET( ch->act, PLR_HOLYLIGHT ) )
    return TRUE;

  /* The miracle cure for blindness? -- Altrag */
  if( !IS_AFFECTED( ch, AFF_TRUESIGHT ) )
  {
    if( IS_AFFECTED( ch, AFF_BLIND ) )
      return FALSE;

    if( room_is_dark( ch->in_room ) && !IS_AFFECTED( ch, AFF_INFRARED ) )
      return FALSE;

    if( IS_AFFECTED( victim, AFF_HIDE )
	&& !IS_AFFECTED( ch, AFF_DETECT_HIDDEN ) && !victim->fighting )
      return FALSE;


    if( IS_AFFECTED( victim, AFF_INVISIBLE )
	&& !IS_AFFECTED( ch, AFF_DETECT_INVIS ) )
      return FALSE;

  }

  return TRUE;
}



/*
 * True if char can see obj.
 */
bool can_see_obj( const CHAR_DATA * ch, const OBJ_DATA * obj )
{
  if( !IS_NPC( ch ) && IS_SET( ch->act, PLR_HOLYLIGHT ) )
    return TRUE;

  if( IS_OBJ_STAT( obj, ITEM_BURRIED ) )
    return FALSE;

  if( IS_AFFECTED( ch, AFF_TRUESIGHT ) )
    return TRUE;

  if( IS_AFFECTED( ch, AFF_BLIND ) )
    return FALSE;

  if( IS_OBJ_STAT( obj, ITEM_HIDDEN ) )
    return FALSE;

  if( obj->item_type == ITEM_LIGHT && obj->value[2] != 0 )
    return TRUE;

  if( room_is_dark( ch->in_room ) && !IS_AFFECTED( ch, AFF_INFRARED ) )
    return FALSE;

  if( IS_OBJ_STAT( obj, ITEM_INVIS ) && !IS_AFFECTED( ch, AFF_DETECT_INVIS ) )
    return FALSE;

  return TRUE;
}



/*
 * True if char can drop obj.
 */
bool can_drop_obj( const CHAR_DATA * ch, const OBJ_DATA * obj )
{
  if( !IS_OBJ_STAT( obj, ITEM_NODROP ) )
    return TRUE;

  if( !IS_NPC( ch ) && IS_IMMORTAL( ch ) )
    return TRUE;

  if( IS_NPC( ch ) && ch->pIndexData->vnum == MOB_VNUM_SUPERMOB )
    return TRUE;

  return FALSE;
}


/*
 * Return ascii name of an item type.
 */
const char *item_type_name( const OBJ_DATA * obj )
{
  if( obj->item_type < 1 || obj->item_type > MAX_ITEM_TYPE )
  {
    bug( "Item_type_name: unknown type %d.", obj->item_type );
    return "(unknown)";
  }

  return o_types[obj->item_type];
}



/*
 * Return ascii name of an affect location.
 */
const char *affect_loc_name( int location )
{
  switch ( location )
  {
    case APPLY_NONE:
      return "none";
    case APPLY_STR:
      return "strength";
    case APPLY_DEX:
      return "dexterity";
    case APPLY_INT:
      return "intelligence";
    case APPLY_WIS:
      return "wisdom";
    case APPLY_CON:
      return "constitution";
    case APPLY_CHA:
      return "charisma";
    case APPLY_LCK:
      return "luck";
    case APPLY_SEX:
      return "sex";
    case APPLY_LEVEL:
      return "level";
    case APPLY_AGE:
      return "age";
    case APPLY_MANA:
      return "mana";
    case APPLY_HIT:
      return "hp";
    case APPLY_MOVE:
      return "moves";
    case APPLY_GOLD:
      return "gold";
    case APPLY_EXP:
      return "experience";
    case APPLY_AC:
      return "armor class";
    case APPLY_HITROLL:
      return "hit roll";
    case APPLY_DAMROLL:
      return "damage roll";
    case APPLY_SAVING_POISON:
      return "save vs poison";
    case APPLY_SAVING_ROD:
      return "save vs rod";
    case APPLY_SAVING_PARA:
      return "save vs paralysis";
    case APPLY_SAVING_BREATH:
      return "save vs breath";
    case APPLY_SAVING_SPELL:
      return "save vs spell";
    case APPLY_HEIGHT:
      return "height";
    case APPLY_WEIGHT:
      return "weight";
    case APPLY_AFFECT:
      return "affected_by";
    case APPLY_RESISTANT:
      return "resistant";
    case APPLY_IMMUNE:
      return "immune";
    case APPLY_SUSCEPTIBLE:
      return "susceptible";
    case APPLY_BACKSTAB:
      return "backstab";
    case APPLY_PICK:
      return "pick";
    case APPLY_TRACK:
      return "track";
    case APPLY_STEAL:
      return "steal";
    case APPLY_SNEAK:
      return "sneak";
    case APPLY_HIDE:
      return "hide";
    case APPLY_PALM:
      return "palm";
    case APPLY_DETRAP:
      return "detrap";
    case APPLY_DODGE:
      return "dodge";
    case APPLY_PEEK:
      return "peek";
    case APPLY_SCAN:
      return "scan";
    case APPLY_GOUGE:
      return "gouge";
    case APPLY_SEARCH:
      return "search";
    case APPLY_MOUNT:
      return "mount";
    case APPLY_DISARM:
      return "disarm";
    case APPLY_KICK:
      return "kick";
    case APPLY_PARRY:
      return "parry";
    case APPLY_BASH:
      return "bash";
    case APPLY_STUN:
      return "stun";
    case APPLY_PUNCH:
      return "punch";
    case APPLY_CLIMB:
      return "climb";
    case APPLY_GRIP:
      return "grip";
    case APPLY_SCRIBE:
      return "scribe";
    case APPLY_BREW:
      return "brew";
    case APPLY_WEAPONSPELL:
      return "weapon spell";
    case APPLY_WEARSPELL:
      return "wear spell";
    case APPLY_REMOVESPELL:
      return "remove spell";
    case APPLY_MENTALSTATE:
      return "mental state";
    case APPLY_EMOTION:
      return "emotional state";
    case APPLY_STRIPSN:
      return "dispel";
    case APPLY_REMOVE:
      return "remove";
    case APPLY_FULL:
      return "hunger";
    case APPLY_THIRST:
      return "thirst";
    case APPLY_DRUNK:
      return "drunk";
    case APPLY_BLOOD:
      return "blood";
  }

  bug( "Affect_location_name: unknown location %d.", location );
  return "(unknown)";
}



/*
 * Return ascii name of an affect bit vector.
 */
char *affect_bit_name( int vector )
{
  static char buf[512];

  buf[0] = '\0';

  if( vector & AFF_BLIND )
    strcat( buf, " blind" );

  if( vector & AFF_INVISIBLE )
    strcat( buf, " invisible" );

  if( vector & AFF_DETECT_EVIL )
    strcat( buf, " detect_evil" );

  if( vector & AFF_DETECT_INVIS )
    strcat( buf, " detect_invis" );

  if( vector & AFF_DETECT_MAGIC )
    strcat( buf, " detect_magic" );

  if( vector & AFF_DETECT_HIDDEN )
    strcat( buf, " detect_hidden" );

  if( vector & AFF_WEAKEN )
    strcat( buf, " weaken" );

  if( vector & AFF_SANCTUARY )
    strcat( buf, " sanctuary" );

  if( vector & AFF_FAERIE_FIRE )
    strcat( buf, " faerie_fire" );

  if( vector & AFF_INFRARED )
    strcat( buf, " infrared" );

  if( vector & AFF_CURSE )
    strcat( buf, " curse" );

  if( vector & AFF_FLAMING )
    strcat( buf, " flaming" );

  if( vector & AFF_POISON )
    strcat( buf, " poison" );

  if( vector & AFF_PROTECT )
    strcat( buf, " protect" );

  if( vector & AFF_PARALYSIS )
    strcat( buf, " paralysis" );

  if( vector & AFF_SLEEP )
    strcat( buf, " sleep" );

  if( vector & AFF_SNEAK )
    strcat( buf, " sneak" );

  if( vector & AFF_HIDE )
    strcat( buf, " hide" );

  if( vector & AFF_CHARM )
    strcat( buf, " charm" );

  if( vector & AFF_POSSESS )
    strcat( buf, " possess" );

  if( vector & AFF_FLYING )
    strcat( buf, " flying" );

  if( vector & AFF_PASS_DOOR )
    strcat( buf, " pass_door" );

  if( vector & AFF_FLOATING )
    strcat( buf, " floating" );

  if( vector & AFF_TRUESIGHT )
    strcat( buf, " true_sight" );

  if( vector & AFF_DETECTTRAPS )
    strcat( buf, " detect_traps" );

  if( vector & AFF_SCRYING )
    strcat( buf, " scrying" );

  if( vector & AFF_FIRESHIELD )
    strcat( buf, " fireshield" );

  if( vector & AFF_SHOCKSHIELD )
    strcat( buf, " shockshield" );

  if( vector & AFF_ICESHIELD )
    strcat( buf, " iceshield" );

  if( vector & AFF_POSSESS )
    strcat( buf, " possess" );

  if( vector & AFF_BERSERK )
    strcat( buf, " berserk" );

  if( vector & AFF_AQUA_BREATH )
    strcat( buf, " aqua_breath" );

  sprintf( buf, "%s", buf[0] != '\0' ? buf + 1 : "none" );

  return buf;
}



/*
 * Return ascii name of extra flags vector.
 */
char *extra_bit_name( int extra_flags )
{
  static char buf[512];

  buf[0] = '\0';
  if( extra_flags & ITEM_GLOW )
    strcat( buf, " glow" );
  if( extra_flags & ITEM_HUM )
    strcat( buf, " hum" );
  if( extra_flags & ITEM_DARK )
    strcat( buf, " dark" );
  if( extra_flags & ITEM_INVIS )
    strcat( buf, " invis" );
  if( extra_flags & ITEM_MAGIC )
    strcat( buf, " magic" );
  if( extra_flags & ITEM_NODROP )
    strcat( buf, " nodrop" );
  if( extra_flags & ITEM_BLESS )
    strcat( buf, " bless" );
  if( extra_flags & ITEM_NOREMOVE )
    strcat( buf, " noremove" );
  if( extra_flags & ITEM_INVENTORY )
    strcat( buf, " inventory" );
  if( extra_flags & ITEM_DEATHROT )
    strcat( buf, " deathrot" );
  if( extra_flags & ITEM_DONATION )
    strcat( buf, " donation" );
  if( extra_flags & ITEM_CLANOBJECT )
    strcat( buf, " clan" );
  if( extra_flags & ITEM_PROTOTYPE )
    strcat( buf, " prototype" );

  sprintf( buf, "%s", buf[0] != '\0' ? buf + 1 : "none" );
  return buf;
}

/*
 * Return ascii name of magic flags vector. - Scryn
 */
char *magic_bit_name( int magic_flags )
{
  static char buf[512];

  buf[0] = '\0';
  if( magic_flags & ITEM_RETURNING )
    strcat( buf, " returning" );

  sprintf( buf, "%s", buf[0] != '\0' ? buf + 1 : "none" );
  return buf;
}

/*
 * Remove an exit from a room					-Thoric
 */
void extract_exit( ROOM_INDEX_DATA * room, EXIT_DATA * pexit )
{
  UNLINK( pexit, room->first_exit, room->last_exit, next, prev );
  if( pexit->rexit )
    pexit->rexit->rexit = NULL;
  STRFREE( pexit->keyword );
  STRFREE( pexit->description );
  DISPOSE( pexit );
}

/*
 * Remove a room
 */
void extract_room( ROOM_INDEX_DATA * room )
{
  bug( "extract_room: not implemented", 0 );
  return;
}

/*
 * clean out a room (leave list pointers intact )		-Thoric
 */
void clean_room( ROOM_INDEX_DATA * room )
{
  EXTRA_DESCR_DATA *ed, *ed_next;
  EXIT_DATA *pexit, *pexit_next;

  STRFREE( room->description );
  STRFREE( room->name );
  room->description = NULL;
  room->name = NULL;

  for( ed = room->first_extradesc; ed; ed = ed_next )
  {
    ed_next = ed->next;
    STRFREE( ed->description );
    STRFREE( ed->keyword );
    DISPOSE( ed );
    top_ed--;
  }

  room->first_extradesc = NULL;
  room->last_extradesc = NULL;

  for( pexit = room->first_exit; pexit; pexit = pexit_next )
  {
    pexit_next = pexit->next;
    STRFREE( pexit->keyword );
    STRFREE( pexit->description );
    DISPOSE( pexit );
    top_exit--;
  }

  room->first_exit = NULL;
  room->last_exit = NULL;
  room->room_flags = 0;
  room->sector_type = 0;
  room->light = 0;

}

/*
 * clean out an object (index) (leave list pointers intact )	-Thoric
 */
void clean_obj( OBJ_INDEX_DATA * obj )
{
  AFFECT_DATA *paf;
  AFFECT_DATA *paf_next;
  EXTRA_DESCR_DATA *ed;
  EXTRA_DESCR_DATA *ed_next;

  STRFREE( obj->name );
  STRFREE( obj->short_descr );
  STRFREE( obj->description );
  STRFREE( obj->action_desc );
  obj->item_type = 0;
  obj->extra_flags = 0;
  obj->wear_flags = 0;
  obj->count = 0;
  obj->weight = 0;
  obj->cost = 0;
  obj->value[0] = 0;
  obj->value[1] = 0;
  obj->value[2] = 0;
  obj->value[3] = 0;
  obj->value[4] = 0;
  obj->value[5] = 0;

  for( paf = obj->first_affect; paf; paf = paf_next )
  {
    paf_next = paf->next;
    DISPOSE( paf );
    top_affect--;
  }
  obj->first_affect = NULL;
  obj->last_affect = NULL;
  for( ed = obj->first_extradesc; ed; ed = ed_next )
  {
    ed_next = ed->next;
    STRFREE( ed->description );
    STRFREE( ed->keyword );
    DISPOSE( ed );
    top_ed--;
  }
  obj->first_extradesc = NULL;
  obj->last_extradesc = NULL;
}

/*
 * clean out a mobile (index) (leave list pointers intact )	-Thoric
 */
void clean_mob( MOB_INDEX_DATA * mob )
{
  MPROG_DATA *mprog, *mprog_next;

  STRFREE( mob->player_name );
  STRFREE( mob->short_descr );
  STRFREE( mob->long_descr );
  STRFREE( mob->description );
  mob->spec_fun = NULL;
  mob->spec_2 = NULL;
  mob->pShop = NULL;
  mob->rShop = NULL;
  mob->progtypes = 0;

  for( mprog = mob->mudprogs; mprog; mprog = mprog_next )
  {
    mprog_next = mprog->next;
    STRFREE( mprog->arglist );
    STRFREE( mprog->comlist );
    DISPOSE( mprog );
  }
  mob->count = 0;
  mob->killed = 0;
  mob->sex = 0;
  mob->level = 0;
  mob->act = 0;
  mob->affected_by = 0;
  mob->alignment = 0;
  mob->ac = 0;
  mob->hitnodice = 0;
  mob->hitsizedice = 0;
  mob->hitplus = 0;
  mob->damnodice = 0;
  mob->damsizedice = 0;
  mob->damplus = 0;
  mob->gold = 0;
  mob->position = 0;
  mob->defposition = 0;
  mob->height = 0;
  mob->weight = 0;
}

extern int top_reset;

/*
 * "Fix" a character's stats					-Thoric
 */
void fix_char( CHAR_DATA * ch )
{
  AFFECT_DATA *aff;
  OBJ_DATA *carry[MAX_LEVEL * 200];
  OBJ_DATA *obj;
  int x, ncarry;

  de_equip_char( ch );

  ncarry = 0;
  while( ( obj = ch->first_carrying ) != NULL )
  {
    carry[ncarry++] = obj;
    obj_from_char( obj );
  }

  for( aff = ch->first_affect; aff; aff = aff->next )
    affect_modify( ch, aff, FALSE );

  ch->affected_by = 0;
  ch->mental_state = 0;
  ch->hit = UMAX( 1, ch->hit );
  ch->mana = UMAX( 1, ch->mana );
  ch->move = UMAX( 1, ch->move );
  ch->armor = 100;
  ch->mod_str = 0;
  ch->mod_dex = 0;
  ch->mod_wis = 0;
  ch->mod_int = 0;
  ch->mod_con = 0;
  ch->mod_cha = 0;
  ch->mod_lck = 0;
  ch->damroll = 0;
  ch->hitroll = 0;
  ch->alignment = URANGE( -1000, ch->alignment, 1000 );
  ch->saving_breath = 0;
  ch->saving_wand = 0;
  ch->saving_para_petri = 0;
  ch->saving_spell_staff = 0;
  ch->saving_poison_death = 0;

  ch->carry_weight = 0;
  ch->carry_number = 0;

  for( aff = ch->first_affect; aff; aff = aff->next )
    affect_modify( ch, aff, TRUE );

  for( x = 0; x < ncarry; x++ )
    obj_to_char( carry[x], ch );

  re_equip_char( ch );
}


/*
 * Show an affect verbosely to a character			-Thoric
 */
void showaffect( const CHAR_DATA * ch, const AFFECT_DATA * paf )
{
  char buf[MAX_STRING_LENGTH];
  int x;

  if( !paf )
  {
    bug( "showaffect: NULL paf", 0 );
    return;
  }
  if( paf->location != APPLY_NONE && paf->modifier != 0 )
  {
    switch ( paf->location )
    {
      default:
	sprintf( buf, "Affects %s by %d.\r\n",
	    affect_loc_name( paf->location ), paf->modifier );
	break;
      case APPLY_AFFECT:
	sprintf( buf, "Affects %s by", affect_loc_name( paf->location ) );
	for( x = 0; x < 32; x++ )
	  if( IS_SET( paf->modifier, 1 << x ) )
	  {
	    strcat( buf, " " );
	    strcat( buf, a_flags[x] );
	  }
	strcat( buf, "\r\n" );
	break;
      case APPLY_WEAPONSPELL:
      case APPLY_WEARSPELL:
      case APPLY_REMOVESPELL:
	sprintf( buf, "Casts spell '%s'\r\n",
	    IS_VALID_SN( paf->modifier ) ? skill_table[paf->modifier]->
	    name : "unknown" );
	break;
      case APPLY_RESISTANT:
      case APPLY_IMMUNE:
      case APPLY_SUSCEPTIBLE:
	sprintf( buf, "Affects %s by", affect_loc_name( paf->location ) );
	for( x = 0; x < 32; x++ )
	  if( IS_SET( paf->modifier, 1 << x ) )
	  {
	    strcat( buf, " " );
	    strcat( buf, ris_flags[x] );
	  }
	strcat( buf, "\r\n" );
	break;
    }
    send_to_char( buf, ch );
  }
}

/*
 * Set the current global object to obj				-Thoric
 */
void set_cur_obj( OBJ_DATA * obj )
{
  cur_obj = obj->serial;
  cur_obj_extracted = FALSE;
  global_objcode = rNONE;
}

/*
 * Check the recently extracted object queue for obj		-Thoric
 */
bool obj_extracted( const OBJ_DATA * obj )
{
  OBJ_DATA *cod;

  if( !obj )
    return TRUE;

  if( obj->serial == cur_obj && cur_obj_extracted )
    return TRUE;

  for( cod = extracted_obj_queue; cod; cod = cod->next )
    if( obj == cod )
      return TRUE;
  return FALSE;
}

/*
 * Stick obj onto extraction queue
 */
void queue_extracted_obj( OBJ_DATA * obj )
{

  ++cur_qobjs;
  obj->next = extracted_obj_queue;
  extracted_obj_queue = obj;
}

/* Deallocates the memory used by a single object after it's been extracted. */
void free_obj( OBJ_DATA * obj )
{
  AFFECT_DATA *paf, *paf_next;
  EXTRA_DESCR_DATA *ed, *ed_next;
  MPROG_ACT_LIST *mpact, *mpact_next;

  for( mpact = obj->mpact; mpact; mpact = mpact_next )
  {
    mpact_next = mpact->next;
    DISPOSE( mpact->buf );
    DISPOSE( mpact );
  }

  /*
   * remove affects 
   */
  for( paf = obj->first_affect; paf; paf = paf_next )
  {
    paf_next = paf->next;
    DISPOSE( paf );
  }
  obj->first_affect = obj->last_affect = NULL;

  /*
   * remove extra descriptions 
   */
  for( ed = obj->first_extradesc; ed; ed = ed_next )
  {
    ed_next = ed->next;
    STRFREE( ed->description );
    STRFREE( ed->keyword );
    DISPOSE( ed );
  }
  obj->first_extradesc = obj->last_extradesc = NULL;

  STRFREE( obj->name );
  STRFREE( obj->description );
  STRFREE( obj->short_descr );
  STRFREE( obj->action_desc );
  DISPOSE( obj );
}

/*
 * Clean out the extracted object queue
 */
void clean_obj_queue( void )
{
  OBJ_DATA *obj;

  while( extracted_obj_queue )
  {
    obj = extracted_obj_queue;
    extracted_obj_queue = extracted_obj_queue->next;
    free_obj( obj );
    --cur_qobjs;
  }
}

/*
 * Set the current global character to ch			-Thoric
 */
void set_cur_char( CHAR_DATA * ch )
{
  cur_char = ch;
  cur_char_died = FALSE;
  cur_room = ch->in_room;
  global_retcode = rNONE;
}

/*
 * Check to see if ch died recently				-Thoric
 */
bool char_died( const CHAR_DATA * ch )
{
  EXTRACT_CHAR_DATA *ccd;

  if( ch == cur_char && cur_char_died )
    return TRUE;

  for( ccd = extracted_char_queue; ccd; ccd = ccd->next )
    if( ccd->ch == ch )
      return TRUE;
  return FALSE;
}

/*
 * Add ch to the queue of recently extracted characters		-Thoric
 */
void queue_extracted_char( CHAR_DATA * ch, bool extract )
{
  EXTRACT_CHAR_DATA *ccd;

  if( !ch )
  {
    bug( "queue_extracted char: ch = NULL", 0 );
    return;
  }
  CREATE( ccd, EXTRACT_CHAR_DATA, 1 );
  ccd->ch = ch;
  ccd->room = ch->in_room;
  ccd->extract = extract;
  if( ch == cur_char )
    ccd->retcode = global_retcode;
  else
    ccd->retcode = rCHAR_DIED;
  ccd->next = extracted_char_queue;
  extracted_char_queue = ccd;
  cur_qchars++;
}

/*
 * clean out the extracted character queue
 */
void clean_char_queue()
{
  EXTRACT_CHAR_DATA *ccd;

  for( ccd = extracted_char_queue; ccd; ccd = extracted_char_queue )
  {
    extracted_char_queue = ccd->next;
    if( ccd->extract )
      free_char( ccd->ch );
    DISPOSE( ccd );
    --cur_qchars;
  }
}

/*
 * Add a timer to ch						-Thoric
 * Support for "call back" time delayed commands
 */
void add_timer( CHAR_DATA * ch, short type, short count, DO_FUN * fun,
    int value )
{
  TIMER *timer;

  for( timer = ch->first_timer; timer; timer = timer->next )
    if( timer->type == type )
    {
      timer->count = count;
      timer->do_fun = fun;
      timer->value = value;
      break;
    }
  if( !timer )
  {
    CREATE( timer, TIMER, 1 );
    timer->count = count;
    timer->type = type;
    timer->do_fun = fun;
    timer->value = value;
    LINK( timer, ch->first_timer, ch->last_timer, next, prev );
  }
}

TIMER *get_timerptr( const CHAR_DATA * ch, short type )
{
  TIMER *timer;

  for( timer = ch->first_timer; timer; timer = timer->next )
    if( timer->type == type )
      return timer;
  return NULL;
}

short get_timer( const CHAR_DATA * ch, short type )
{
  TIMER *timer;

  if( ( timer = get_timerptr( ch, type ) ) != NULL )
    return timer->count;
  else
    return 0;
}

void extract_timer( CHAR_DATA * ch, TIMER * timer )
{
  if( !timer )
  {
    bug( "extract_timer: NULL timer", 0 );
    return;
  }

  UNLINK( timer, ch->first_timer, ch->last_timer, next, prev );
  DISPOSE( timer );
  return;
}

void remove_timer( CHAR_DATA * ch, short type )
{
  TIMER *timer;

  for( timer = ch->first_timer; timer; timer = timer->next )
    if( timer->type == type )
      break;

  if( timer )
    extract_timer( ch, timer );
}


/*
 * Scryn, standard luck check 2/2/96
 */
bool luck_check( const CHAR_DATA * ch, short percent )
{
  /*  short clan_factor, ms;*/
  short deity_factor, ms;

  if( !ch )
  {
    bug( "Chance: null ch!", 0 );
    return FALSE;
  }

  /* Code for clan stuff put in by Narn, Feb/96.  The idea is to punish clan
     members who don't keep their alignment in tune with that of their clan by
     making it harder for them to succeed at pretty much everything.  Clan_factor
     will vary from 1 to 3, with 1 meaning there is no effect on the player's
     change of success, and with 3 meaning they have half the chance of doing
     whatever they're trying to do. 

     Note that since the neutral clannies can only be off by 1000 points, their
     maximum penalty will only be half that of the other clan types.

     if ( IS_CLANNED( ch ) )
     clan_factor = 1 + abs( ch->alignment - ch->pcdata->clan->alignment ) / 1000; 
     else
     clan_factor = 1;
     */
  /* Mental state bonus/penalty:  Your mental state is a ranged value with
   * zero (0) being at a perfect mental state (bonus of 10).
   * negative values would reflect how sedated one is, and
   * positive values would reflect how stimulated one is.
   * In most circumstances you'd do best at a perfectly balanced state.
   */

  deity_factor = 0;

  ms = 10 - abs( ch->mental_state );

  if( ( number_percent() - get_curr_lck( ch ) + 13 - ms ) + deity_factor <=
      percent )
    return TRUE;
  else
    return FALSE;
}

/*
 * Make a simple clone of an object (no extras...yet)		-Thoric
 */
OBJ_DATA *clone_object( const OBJ_DATA * obj )
{
  OBJ_DATA *clone;

  CREATE( clone, OBJ_DATA, 1 );
  clone->pIndexData = obj->pIndexData;
  clone->name = QUICKLINK( obj->name );
  clone->short_descr = QUICKLINK( obj->short_descr );
  clone->description = QUICKLINK( obj->description );
  clone->action_desc = QUICKLINK( obj->action_desc );
  clone->item_type = obj->item_type;
  clone->extra_flags = obj->extra_flags;
  clone->magic_flags = obj->magic_flags;
  clone->wear_flags = obj->wear_flags;
  clone->wear_loc = obj->wear_loc;
  clone->weight = obj->weight;
  clone->cost = obj->cost;
  clone->timer = obj->timer;
  clone->value[0] = obj->value[0];
  clone->value[1] = obj->value[1];
  clone->value[2] = obj->value[2];
  clone->value[3] = obj->value[3];
  clone->value[4] = obj->value[4];
  clone->value[5] = obj->value[5];
  clone->count = 1;
  ++obj->pIndexData->count;
  ++numobjsloaded;
  ++physicalobjects;
  cur_obj_serial = UMAX( ( cur_obj_serial + 1 ) & ( BV30 - 1 ), 1 );
  clone->serial = clone->pIndexData->serial = cur_obj_serial;
  LINK( clone, first_object, last_object, next, prev );
  return clone;
}

/*
 * If possible group obj2 into obj1				-Thoric
 * This code, along with clone_object, obj->count, and special support
 * for it implemented throughout handler.c and save.c should show improved
 * performance on MUDs with players that hoard tons of potions and scrolls
 * as this will allow them to be grouped together both in memory, and in
 * the player files.
 */
static OBJ_DATA *group_object( OBJ_DATA * obj1, OBJ_DATA * obj2 )
{
  if( !obj1 || !obj2 )
    return NULL;
  if( obj1 == obj2 )
    return obj1;

  if( obj1->pIndexData == obj2->pIndexData
      && !str_cmp( obj1->name, obj2->name )
      && !str_cmp( obj1->short_descr, obj2->short_descr )
      && !str_cmp( obj1->description, obj2->description )
      && !str_cmp( obj1->action_desc, obj2->action_desc )
      && obj1->item_type == obj2->item_type
      && obj1->extra_flags == obj2->extra_flags
      && obj1->magic_flags == obj2->magic_flags
      && obj1->wear_flags == obj2->wear_flags
      && obj1->wear_loc == obj2->wear_loc
      && obj1->weight == obj2->weight
      && obj1->cost == obj2->cost
      && obj1->timer == obj2->timer
      && obj1->value[0] == obj2->value[0]
      && obj1->value[1] == obj2->value[1]
      && obj1->value[2] == obj2->value[2]
      && obj1->value[3] == obj2->value[3]
      && obj1->value[4] == obj2->value[4]
      && obj1->value[5] == obj2->value[5]
      && !obj1->first_extradesc && !obj2->first_extradesc
      && !obj1->first_affect && !obj2->first_affect
      && !obj1->first_content && !obj2->first_content )
      {
	obj1->count += obj2->count;
	obj1->pIndexData->count += obj2->count;	/* to be decremented in */
	numobjsloaded += obj2->count;	/* extract_obj */
	extract_obj( obj2 );
	return obj1;
      }
  return obj2;
}

/*
 * Split off a grouped object					-Thoric
 * decreased obj's count to num, and creates a new object containing the rest
 */
void split_obj( OBJ_DATA * obj, int num )
{
  int count;
  OBJ_DATA *rest;

  if( !obj )
    return;

  count = obj->count;

  if( count <= num || num == 0 )
    return;

  rest = clone_object( obj );
  --obj->pIndexData->count;	/* since clone_object() ups this value */
  --numobjsloaded;
  rest->count = obj->count - num;
  obj->count = num;

  if( obj->carried_by )
  {
    LINK( rest, obj->carried_by->first_carrying,
	obj->carried_by->last_carrying, next_content, prev_content );
    rest->carried_by = obj->carried_by;
    rest->in_room = NULL;
    rest->in_obj = NULL;
  }
  else if( obj->in_room )
  {
    LINK( rest, obj->in_room->first_content, obj->in_room->last_content,
	next_content, prev_content );
    rest->carried_by = NULL;
    rest->in_room = obj->in_room;
    rest->in_obj = NULL;
  }
  else if( obj->in_obj )
  {
    LINK( rest, obj->in_obj->first_content, obj->in_obj->last_content,
	next_content, prev_content );
    rest->in_obj = obj->in_obj;
    rest->in_room = NULL;
    rest->carried_by = NULL;
  }
}

void separate_obj( OBJ_DATA * obj )
{
  split_obj( obj, 1 );
}

/*
 * Empty an obj's contents... optionally into another obj, or a room
 */
bool empty_obj( OBJ_DATA * obj, OBJ_DATA * destobj,
    ROOM_INDEX_DATA * destroom )
{
  OBJ_DATA *otmp, *otmp_next;
  CHAR_DATA *ch = obj->carried_by;
  bool movedsome = FALSE;

  if( !obj )
  {
    bug( "empty_obj: NULL obj", 0 );
    return FALSE;
  }
  if( destobj || ( !destroom && !ch && ( destobj = obj->in_obj ) != NULL ) )
  {
    for( otmp = obj->first_content; otmp; otmp = otmp_next )
    {
      otmp_next = otmp->next_content;
      if( destobj->item_type == ITEM_CONTAINER
	  && get_obj_weight( otmp ) + get_obj_weight( destobj )
	  > destobj->value[0] )
	continue;
      obj_from_obj( otmp );
      obj_to_obj( otmp, destobj );
      movedsome = TRUE;
    }
    return movedsome;
  }
  if( destroom || ( !ch && ( destroom = obj->in_room ) != NULL ) )
  {
    for( otmp = obj->first_content; otmp; otmp = otmp_next )
    {
      otmp_next = otmp->next_content;
      if( ch && ( otmp->pIndexData->progtypes & DROP_PROG )
	  && otmp->count > 1 )
      {
	separate_obj( otmp );
	obj_from_obj( otmp );
	if( !otmp_next )
	  otmp_next = obj->first_content;
      }
      else
	obj_from_obj( otmp );
      otmp = obj_to_room( otmp, destroom );
      if( ch )
      {
	oprog_drop_trigger( ch, otmp );	/* mudprogs */
	if( char_died( ch ) )
	  ch = NULL;
      }
      movedsome = TRUE;
    }
    return movedsome;
  }
  if( ch )
  {
    for( otmp = obj->first_content; otmp; otmp = otmp_next )
    {
      otmp_next = otmp->next_content;
      obj_from_obj( otmp );
      obj_to_char( otmp, ch );
      movedsome = TRUE;
    }
    return movedsome;
  }
  bug( "empty_obj: could not determine a destination for vnum %d",
      obj->pIndexData->vnum );
  return FALSE;
}

/*
 * Improve mental state						-Thoric
 */
void better_mental_state( CHAR_DATA * ch, int mod )
{
  int c = URANGE( 0, abs( mod ), 20 );
  int con = get_curr_con( ch );

  c += number_percent() < con ? 1 : 0;

  if( ch->mental_state < 0 )
    ch->mental_state = URANGE( -100, ch->mental_state + c, 0 );
  else if( ch->mental_state > 0 )
    ch->mental_state = URANGE( 0, ch->mental_state - c, 100 );
}

/*
 * Deteriorate mental state					-Thoric
 */
void worsen_mental_state( CHAR_DATA * ch, int mod )
{
  int c = URANGE( 0, abs( mod ), 20 );
  int con = get_curr_con( ch );


  c -= number_percent() < con ? 1 : 0;
  if( c < 1 )
    return;

  if( ch->mental_state < 0 )
    ch->mental_state = URANGE( -100, ch->mental_state - c, 100 );
  else if( ch->mental_state > 0 )
    ch->mental_state = URANGE( -100, ch->mental_state + c, 100 );
  else
    ch->mental_state -= c;
}


/*
 * Add another notch on that there belt... ;)
 * Keep track of the last so many kills by vnum			-Thoric
 */
void add_kill( CHAR_DATA * ch, CHAR_DATA * mob )
{
  int x;
  short vnum, track;

  if( IS_NPC( ch ) )
    return;

  if( !IS_NPC( mob ) )
    return;

  vnum = mob->pIndexData->vnum;
  track = MAX_KILLTRACK;
  for( x = 0; x < track; x++ )
    if( ch->pcdata->killed[x].vnum == vnum )
    {
      if( ch->pcdata->killed[x].count < 50 )
	++ch->pcdata->killed[x].count;
      return;
    }
    else if( ch->pcdata->killed[x].vnum == 0 )
      break;
  memmove( ( char * ) ch->pcdata->killed + sizeof( KILLED_DATA ),
      ch->pcdata->killed, ( track - 1 ) * sizeof( KILLED_DATA ) );
  ch->pcdata->killed[0].vnum = vnum;
  ch->pcdata->killed[0].count = 1;
  if( track < MAX_KILLTRACK )
    ch->pcdata->killed[track].vnum = 0;
}

/*
 * Return how many times this player has killed this mob	-Thoric
 * Only keeps track of so many (MAX_KILLTRACK), and keeps track by vnum
 */
int times_killed( const CHAR_DATA * ch, const CHAR_DATA * mob )
{
  int x;
  short vnum, track;

  if( IS_NPC( ch ) )
    return 0;

  if( !IS_NPC( mob ) )
    return 0;

  vnum = mob->pIndexData->vnum;
  track = MAX_KILLTRACK;

  for( x = 0; x < track; x++ )
  {
    if( ch->pcdata->killed[x].vnum == vnum )
    {
      return ch->pcdata->killed[x].count;
    }
    else if( ch->pcdata->killed[x].vnum == 0 )
    {
      break;
    }
  }

  return 0;
}

void room_extract_contents( ROOM_INDEX_DATA * room )
{
  OBJ_DATA *robj = NULL;
  OBJ_DATA *obj_next = NULL;

  for( robj = room->first_content; robj; robj = obj_next )
  {
    obj_next = robj->next_content;
    extract_obj( robj );
  }
}

void room_extract_mobiles( ROOM_INDEX_DATA * room )
{
  CHAR_DATA *victim = NULL, *vnext = NULL;

  for( victim = room->first_person; victim; victim = vnext )
  {
    vnext = victim->next_in_room;

    if( IS_NPC( victim ) )
      extract_char( victim, TRUE );
  }
}

void character_extract_carried_objects( CHAR_DATA * ch )
{
  OBJ_DATA *obj = NULL;
  OBJ_DATA *obj_next = NULL;

  for( obj = ch->last_carrying; obj; obj = obj_next )
  {
    obj_next = obj->prev_content;
    extract_obj( obj );
  }
}

/*
 * The primary output interface for formatted output.
 */
/* Major overhaul. -- Alty */
#define NAME(ch)        (IS_NPC(ch) ? ch->short_descr : ch->name)
char *act_string( const char *format, CHAR_DATA * to, CHAR_DATA * ch,
		  const void *arg1, const void *arg2 )
{
  static const char *const he_she[] = { "it", "he", "she" };
  static const char *const him_her[] = { "it", "him", "her" };
  static const char *const his_her[] = { "its", "his", "her" };
  static char buf[MAX_STRING_LENGTH];
  char fname[MAX_INPUT_LENGTH];
  char *point = buf;
  const char *str = format;
  const char *i = NULL;
  CHAR_DATA *vch = ( CHAR_DATA * ) arg2;
  OBJ_DATA *obj1 = ( OBJ_DATA * ) arg1;
  OBJ_DATA *obj2 = ( OBJ_DATA * ) arg2;
  *buf = '\0';

  while( *str != '\0' )
  {
    if( *str != '$' )
    {
      *point++ = *str++;
      continue;
    }
    ++str;
    if( !arg2 && *str >= 'A' && *str <= 'Z' )
    {
      bug( "Act: missing arg2 for code %c:", *str );
      bug( format );
      i = " <@@@> ";
    }
    else
    {
      switch ( *str )
      {
	default:
	  bug( "Act: bad code %c.", *str );
	  i = " <@@@> ";
	  break;
	case 't':
	  i = ( char * ) arg1;
	  break;
	case 'T':
	  i = ( char * ) arg2;
	  break;
	case 'n':
	  i = ( to ? PERS( ch, to ) : NAME( ch ) );
	  break;
	case 'N':
	  i = ( to ? PERS( vch, to ) : NAME( vch ) );
	  break;
	case 'e':
	  if( ch->sex > 2 || ch->sex < 0 )
	  {
	    bug( "act_string: player %s has sex set at %d!", ch->name,
		ch->sex );
	    i = "it";
	  }
	  else
	    i = he_she[URANGE( 0, ch->sex, 2 )];
	  break;
	case 'E':
	  if( vch->sex > 2 || vch->sex < 0 )
	  {
	    bug( "act_string: player %s has sex set at %d!", vch->name,
		vch->sex );
	    i = "it";
	  }
	  else
	    i = he_she[URANGE( 0, vch->sex, 2 )];
	  break;
	case 'm':
	  if( ch->sex > 2 || ch->sex < 0 )
	  {
	    bug( "act_string: player %s has sex set at %d!", ch->name,
		ch->sex );
	    i = "it";
	  }
	  else
	    i = him_her[URANGE( 0, ch->sex, 2 )];
	  break;
	case 'M':
	  if( vch->sex > 2 || vch->sex < 0 )
	  {
	    bug( "act_string: player %s has sex set at %d!", vch->name,
		vch->sex );
	    i = "it";
	  }
	  else
	    i = him_her[URANGE( 0, vch->sex, 2 )];
	  break;
	case 's':
	  if( ch->sex > 2 || ch->sex < 0 )
	  {
	    bug( "act_string: player %s has sex set at %d!", ch->name,
		ch->sex );
	    i = "its";
	  }
	  else
	    i = his_her[URANGE( 0, ch->sex, 2 )];
	  break;
	case 'S':
	  if( vch->sex > 2 || vch->sex < 0 )
	  {
	    bug( "act_string: player %s has sex set at %d!", vch->name,
		vch->sex );
	    i = "its";
	  }
	  else
	    i = his_her[URANGE( 0, vch->sex, 2 )];
	  break;
	case 'q':
	  i = ( to == ch ) ? "" : "s";
	  break;
	case 'Q':
	  i = ( to == ch ) ? "your" : his_her[URANGE( 0, ch->sex, 2 )];
	  break;
	case 'p':
	  i = ( !to || can_see_obj( to, obj1 )
	      ? obj_short( obj1 ) : "something" );
	  break;
	case 'P':
	  i = ( !to || can_see_obj( to, obj2 )
	      ? obj_short( obj2 ) : "something" );
	  break;
	case 'd':
	  if( !arg2 || ( ( char * ) arg2 )[0] == '\0' )
	    i = "door";
	  else
	  {
	    one_argument( ( char * ) arg2, fname );
	    i = fname;
	  }
	  break;
      }
    }
    ++str;
    while( ( *point = *i ) != '\0' )
      ++point, ++i;
  }

  strcpy( point, "\r\n" );
  buf[0] = UPPER( buf[0] );
  return buf;
}

#undef NAME

void act( short AType, const char *format, CHAR_DATA * ch, const void *arg1,
    const void *arg2, int type )
{
  char *txt = NULL;
  CHAR_DATA *to = NULL;
  CHAR_DATA *vch = ( CHAR_DATA * ) arg2;

  /*
   * Discard null and zero-length messages.
   */
  if( !format || format[0] == '\0' )
    return;

  if( !ch )
  {
    bug( "Act: null ch. (%s)", format );
    return;
  }

  if( !ch->in_room )
    to = NULL;
  else if( type == TO_CHAR )
    to = ch;
  else
    to = ch->in_room->first_person;

  /*
   * ACT_SECRETIVE handling
   */
  if( IS_NPC( ch ) && IS_SET( ch->act, ACT_SECRETIVE ) && type != TO_CHAR )
    return;

  if( type == TO_VICT )
  {
    if( !vch )
    {
      bug( "Act: null vch with TO_VICT." );
      bug( "%s (%s)", ch->name, format );
      return;
    }
    if( !vch->in_room )
    {
      bug( "Act: vch in NULL room!" );
      bug( "%s -> %s (%s)", ch->name, vch->name, format );
      return;
    }
    to = vch;
    /*      to = vch->in_room->first_person; */
  }

  if( MOBtrigger && type != TO_CHAR && type != TO_VICT && to )
  {
    OBJ_DATA *to_obj = NULL;

    txt = act_string( format, NULL, ch, arg1, arg2 );
    if( IS_SET( to->in_room->progtypes, ACT_PROG ) )
      rprog_act_trigger( txt, to->in_room, ch, ( OBJ_DATA * ) arg1,
	  ( void * ) arg2 );
    for( to_obj = to->in_room->first_content; to_obj;
	to_obj = to_obj->next_content )
      if( IS_SET( to_obj->pIndexData->progtypes, ACT_PROG ) )
	oprog_act_trigger( txt, to_obj, ch, ( OBJ_DATA * ) arg1,
	    ( void * ) arg2 );
  }

  /* Anyone feel like telling me the point of looping through the whole
     room when we're only sending to one char anyways..? -- Alty */
  for( ; to; to = ( type == TO_CHAR || type == TO_VICT )
      ? NULL : to->next_in_room )
  {
    if( ( !to->desc
	  && ( IS_NPC( to )
	    && !IS_SET( to->pIndexData->progtypes, ACT_PROG ) ) )
	|| !IS_AWAKE( to ) )
      continue;

    if( type == TO_CHAR && to != ch )
      continue;
    if( type == TO_VICT && ( to != vch || to == ch ) )
      continue;
    if( type == TO_ROOM && to == ch )
      continue;
    if( type == TO_NOTVICT && ( to == ch || to == vch ) )
      continue;

    txt = act_string( format, to, ch, arg1, arg2 );
    if( to->desc )
    {
      set_char_color( AType, to );
      send_to_char_color( txt, to );
    }
    if( MOBtrigger )
    {
      /* Note: use original string, not string with ANSI. -- Alty */
      mprog_act_trigger( txt, to, ch, ( OBJ_DATA * ) arg1,
	  ( void * ) arg2 );
    }
  }
  MOBtrigger = TRUE;
  return;
}

/**
 * Gets the first item in a characters inventory of a specific type.
 *
 * @param ch        Character's whos inventory is to be searched.
 * @param item_type Item type of the item to get.
 *
 * @returns First item of the specified type or NULL if none was found.
 */
OBJ_DATA *get_obj_type_char( const CHAR_DATA *ch, short item_type )
{
  OBJ_DATA *obj;

  for( obj = ch->last_carrying; obj; obj = obj->prev_content )
  {
      if( obj->pIndexData->item_type == item_type )	  
	    return obj;	  
  }

  return NULL;
}

bool mobile_is_in_area( const AREA_DATA *area, long vnum )
{
  ROOM_INDEX_DATA *room = NULL;

  for( room = area->first_room; room; room = room->next_in_area )
    {
      CHAR_DATA *rch = NULL;

      for( rch = room->first_person; rch; rch = rch->next_in_room )      
	{
	  if( IS_NPC( rch ) && rch->pIndexData->vnum == vnum )
	    {
	      return TRUE;
	    }
	}
    }

  return FALSE;
}
