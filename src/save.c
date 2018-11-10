#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "mud.h"

/*
 * Increment with every major format change.
 */
#define SAVEVERSION 3

/*
 * Array to keep track of equipment temporarily.		-Thoric
 */
OBJ_DATA *save_equipment[MAX_WEAR][8];
CHAR_DATA *quitting_char = NULL, *loading_char = NULL, *saving_char = NULL;

int file_ver = 0;
extern FILE *fpArea;
extern char strArea[MAX_INPUT_LENGTH];

/*
 * Array of containers read for proper re-nesting of objects.
 */
static OBJ_DATA *rgObjNest[MAX_NEST];

/*
 * Local functions.
 */
void fwrite_char( const CHAR_DATA * ch, FILE * fp );
void fread_char( CHAR_DATA * ch, FILE * fp, bool preload );
void write_corpses( const CHAR_DATA * ch, const char *name );
static void fread_corpse( const char *name );

extern int falling;

void save_home( const CHAR_DATA * ch )
{
  if( ch->plr_home )
  {
    FILE *fp = NULL;
    char filename[256];

    sprintf( filename, "%s%c/%s.home", PLAYER_DIR,
	tolower( ( int ) ch->name[0] ), capitalize( ch->name ) );

    if( ( fp = fopen( filename, "w" ) ) == NULL )
    {
    }
    else
    {
      OBJ_DATA *contents = ch->plr_home->last_content;

      if( contents )
	fwrite_obj( ch, contents, fp, 0, OS_CARRY );

      fprintf( fp, "#END\n" );
      fclose( fp );
    }
  }
}

void load_home( CHAR_DATA * ch )
{
  char filename[256];
  FILE *fph = 0;
  ROOM_INDEX_DATA *storeroom = ch->plr_home;

  sprintf( filename, "%s%c/%s.home", PLAYER_DIR,
      tolower( ( int ) ch->name[0] ), capitalize( ch->name ) );

  if( ( fph = fopen( filename, "r" ) ) )
  {
    int iNest = 0;
    OBJ_DATA *tobj = 0;
    OBJ_DATA *tobj_next = 0;

    rset_supermob( storeroom );

    for( iNest = 0; iNest < MAX_NEST; ++iNest )
    {
      rgObjNest[iNest] = NULL;
    }

    for( ;; )
    {
      char letter = fread_letter( fph );
      const char *word = NULL;

      if( letter == '*' )
      {
	fread_to_eol( fph );
	continue;
      }

      if( letter != '#' )
      {
	bug( "Load_plr_home: # not found." );
	bug( ch->name );
	break;
      }

      word = fread_word( fph );

      if( !str_cmp( word, "OBJECT" ) )
      {
	fread_obj( supermob, fph, OS_CARRY );
      }
      else if( !str_cmp( word, "END" ) )
      {
	break;
      }
      else
      {

	bug( "Load_plr_home: bad section." );
	bug( ch->name );
	break;
      }
    }

    fclose( fph );

    for( tobj = supermob->first_carrying; tobj; tobj = tobj_next )
    {
      tobj_next = tobj->next_content;
      obj_from_char( tobj );
      obj_to_room( tobj, storeroom );
    }

    release_supermob();
  }
}

/*
 * Un-equip character before saving to ensure proper	-Thoric
 * stats are saved in case of changes to or removal of EQ
 */
void de_equip_char( CHAR_DATA * ch )
{
  char buf[MAX_STRING_LENGTH];
  OBJ_DATA *obj = NULL;
  int x = 0, y = 0;

  for( x = 0; x < MAX_WEAR; x++ )
    for( y = 0; y < MAX_LAYERS; y++ )
      save_equipment[x][y] = NULL;

  for( obj = ch->first_carrying; obj; obj = obj->next_content )
    if( obj->wear_loc > -1 && obj->wear_loc < MAX_WEAR )
    {
      for( x = 0; x < MAX_LAYERS; x++ )
	if( !save_equipment[obj->wear_loc][x] )
	{
	  save_equipment[obj->wear_loc][x] = obj;
	  break;
	}

      if( x == MAX_LAYERS )
      {
	sprintf( buf,
	    "%s had on more than %d layers of clothing in one location (%d): %s",
	    ch->name, MAX_LAYERS, obj->wear_loc, obj->name );
	bug( buf, 0 );
      }

      unequip_char( ch, obj );
    }
}

/*
 * Re-equip character					-Thoric
 */
void re_equip_char( CHAR_DATA * ch )
{
  int x, y;

  for( x = 0; x < MAX_WEAR; x++ )
    for( y = 0; y < MAX_LAYERS; y++ )
      if( save_equipment[x][y] != NULL )
      {
	if( quitting_char != ch )
	  equip_char( ch, save_equipment[x][y], x );
	save_equipment[x][y] = NULL;
      }
      else
	break;
}


/*
 * Save a character and inventory.
 * Would be cool to save NPC's too for quest purposes,
 *   some of the infrastructure is provided.
 */
void save_char_obj( CHAR_DATA * ch )
{
  char strsave[MAX_INPUT_LENGTH];
  char strback[MAX_INPUT_LENGTH];
  FILE *fp;

  if( !ch )
  {
    bug( "Save_char_obj: null ch!", 0 );
    return;
  }

  if( IS_NPC( ch ) || NOT_AUTHED( ch ) )
    return;

  saving_char = ch;
  /* save pc's clan's data while we're at it to keep the data in sync */
  if( !IS_NPC( ch ) && ch->pcdata->clan )
    save_clan( ch->pcdata->clan );

  if( ch->desc && ch->desc->original )
    ch = ch->desc->original;

  de_equip_char( ch );

  ch->save_time = current_time;
  sprintf( strsave, "%s%c/%s", PLAYER_DIR, tolower( ( int ) ch->name[0] ),
      capitalize( ch->name ) );

  /*
   * Auto-backup pfile (can cause lag with high disk access situtations
   */
  if( IS_SET( sysdata.save_flags, SV_BACKUP ) )
  {
    sprintf( strback, "%s%c/%s", BACKUP_DIR, tolower( ( int ) ch->name[0] ),
	capitalize( ch->name ) );
    rename( strsave, strback );
  }

  if( ( fp = fopen( strsave, "w" ) ) == NULL )
  {
    bug( "Save_char_obj: fopen", 0 );
    perror( strsave );
  }
  else
  {
    fwrite_char( ch, fp );
    if( ch->first_carrying )
      fwrite_obj( ch, ch->last_carrying, fp, 0, OS_CARRY );
    fprintf( fp, "#END\n" );
    fclose( fp );
  }

  re_equip_char( ch );

  write_corpses( ch, NULL );
  quitting_char = NULL;
  saving_char = NULL;
  return;
}

void save_clone( CHAR_DATA * ch )
{
  char strsave[MAX_INPUT_LENGTH];
  char strback[MAX_INPUT_LENGTH];
  FILE *fp;

  if( !ch )
  {
    bug( "Save_char_obj: null ch!", 0 );
    return;
  }

  if( IS_NPC( ch ) || NOT_AUTHED( ch ) )
    return;

  if( ch->desc && ch->desc->original )
    ch = ch->desc->original;

  de_equip_char( ch );

  ch->save_time = current_time;
  sprintf( strsave, "%s%c/%s.clone", PLAYER_DIR,
      tolower( ( int ) ch->name[0] ), capitalize( ch->name ) );

  /*
   * Auto-backup pfile (can cause lag with high disk access situtations
   */
  if( IS_SET( sysdata.save_flags, SV_BACKUP ) )
  {
    sprintf( strback, "%s%c/%s", BACKUP_DIR, tolower( ( int ) ch->name[0] ),
	capitalize( ch->name ) );
    rename( strsave, strback );
  }

  if( ( fp = fopen( strsave, "w" ) ) == NULL )
  {
    bug( "Save_char_obj: fopen", 0 );
    perror( strsave );
  }
  else
  {
    fwrite_char( ch, fp );
    fprintf( fp, "#END\n" );
    fclose( fp );
  }

  re_equip_char( ch );

  write_corpses( ch, NULL );
  quitting_char = NULL;
  saving_char = NULL;
  return;
}

/*
 * Write the char.
 */
void fwrite_char( const CHAR_DATA * ch, FILE * fp )
{
  AFFECT_DATA *paf = NULL;
  int sn = 0, track = 0;
  SKILLTYPE *skill = NULL;

  fprintf( fp, "#%s\n", IS_NPC( ch ) ? "MOB" : "PLAYER" );

  fprintf( fp, "Version      %d\n", SAVEVERSION );
  fprintf( fp, "Name         %s~\n", ch->name );
  if( ch->short_descr && ch->short_descr[0] != '\0' )
    fprintf( fp, "ShortDescr   %s~\n", ch->short_descr );
  if( ch->long_descr && ch->long_descr[0] != '\0' )
    fprintf( fp, "LongDescr    %s~\n", ch->long_descr );
  if( ch->description && ch->description[0] != '\0' )
    fprintf( fp, "Description  %s~\n", ch->description );
  fprintf( fp, "Sex          %d\n", ch->sex );
  fprintf( fp, "Toplevel     %d\n", ch->top_level );
  if( ch->trust )
    fprintf( fp, "Trust        %d\n", ch->trust );
  fprintf( fp, "Played       %d\n",
      ch->played + ( int ) ( current_time - ch->logon ) );
  fprintf( fp, "Room         %ld\n",
      ( ch->in_room == get_room_index( ROOM_VNUM_LIMBO )
	&& ch->was_in_room )
      ? ch->was_in_room->vnum : ch->in_room->vnum );
  if( ch->plr_home != NULL )
    fprintf( fp, "PlrHome      %ld\n", ch->plr_home->vnum );

  fprintf( fp, "HpManaMove   %d %d 0 0 %d %d\n",
      ch->hit, ch->max_hit, ch->move, ch->max_move );
  fprintf( fp, "Force        %d %d %d %d\n", ch->perm_frc, ch->mod_frc,
      ch->mana, ch->max_mana );
  fprintf( fp, "Gold         %d\n", ch->gold );
  fprintf( fp, "Bank         %ld\n", ch->pcdata->bank );
  if( ch->act )
    fprintf( fp, "Act          %d\n", ch->act );
  if( ch->affected_by )
    fprintf( fp, "AffectedBy   %d\n", ch->affected_by );
  fprintf( fp, "Position     %d\n",
	   ch->position == POS_FIGHTING ? (short)POS_STANDING : ch->position );

  fprintf( fp, "SavingThrows %d %d %d %d %d\n",
      ch->saving_poison_death,
      ch->saving_wand,
      ch->saving_para_petri, ch->saving_breath, ch->saving_spell_staff );
  fprintf( fp, "Alignment    %d\n", ch->alignment );
  fprintf( fp, "Hitroll      %d\n", ch->hitroll );
  fprintf( fp, "Damroll      %d\n", ch->damroll );
  fprintf( fp, "Armor        %d\n", ch->armor );
  if( ch->wimpy )
    fprintf( fp, "Wimpy        %d\n", ch->wimpy );
  if( ch->deaf )
    fprintf( fp, "Deaf         %d\n", ch->deaf );
  if( ch->resistant )
    fprintf( fp, "Resistant    %d\n", ch->resistant );
  if( ch->immune )
    fprintf( fp, "Immune       %d\n", ch->immune );
  if( ch->susceptible )
    fprintf( fp, "Susceptible  %d\n", ch->susceptible );
  if( ch->pcdata && ch->pcdata->outcast_time )
    fprintf( fp, "Outcast_time %ld\n", (long) ch->pcdata->outcast_time );
  if( ch->pcdata && ch->pcdata->restore_time )
    fprintf( fp, "Restore_time %ld\n", (long) ch->pcdata->restore_time );
  if( ch->mental_state != -10 )
    fprintf( fp, "Mentalstate  %d\n", ch->mental_state );

  if( IS_NPC( ch ) )
  {
    fprintf( fp, "Vnum         %ld\n", ch->pIndexData->vnum );
    fprintf( fp, "Mobinvis     %d\n", ch->mobinvis );
  }
  else
  {
    fprintf( fp, "Password     %s~\n", ch->pcdata->pwd );
    fprintf( fp, "Lastplayed   %d\n", ( int ) current_time );
    if( ch->pcdata->bamfin && ch->pcdata->bamfin[0] != '\0' )
      fprintf( fp, "Bamfin       %s~\n", ch->pcdata->bamfin );
    if( ch->pcdata->email && ch->pcdata->email[0] != '\0' )
      fprintf( fp, "Email       %s~\n", ch->pcdata->email );
    if( ch->pcdata->bamfout && ch->pcdata->bamfout[0] != '\0' )
      fprintf( fp, "Bamfout      %s~\n", ch->pcdata->bamfout );
    if( ch->pcdata->rank && ch->pcdata->rank[0] != '\0' )
      fprintf( fp, "Rank         %s~\n", ch->pcdata->rank );
    if( ch->pcdata->bestowments && ch->pcdata->bestowments[0] != '\0' )
      fprintf( fp, "Bestowments  %s~\n", ch->pcdata->bestowments );
    fprintf( fp, "Title        %s~\n", ch->pcdata->title );
    if( ch->pcdata->homepage && ch->pcdata->homepage[0] != '\0' )
      fprintf( fp, "Homepage     %s~\n", ch->pcdata->homepage );
    if( ch->pcdata->bio && ch->pcdata->bio[0] != '\0' )
      fprintf( fp, "Bio          %s~\n", ch->pcdata->bio );
    if( ch->pcdata->authed_by && ch->pcdata->authed_by[0] != '\0' )
      fprintf( fp, "AuthedBy     %s~\n", ch->pcdata->authed_by );
    if( ch->pcdata->min_snoop )
      fprintf( fp, "Minsnoop     %d\n", ch->pcdata->min_snoop );
    if( ch->pcdata->prompt && *ch->pcdata->prompt )
      fprintf( fp, "Prompt       %s~\n", ch->pcdata->prompt );
    if( ch->pcdata->pagerlen != 24 )
      fprintf( fp, "Pagerlen     %d\n", ch->pcdata->pagerlen );

    if( IS_IMMORTAL( ch ) || ch->pcdata->area )
    {
      fprintf( fp, "WizInvis     %d\n", ch->pcdata->wizinvis );
    }

    if( ch->pcdata->clan )
      {
	fprintf( fp, "Clan         %s~\n", ch->pcdata->clan->name );

	if( ch->pcdata->clan_permissions[0] != '\0' )
	  fprintf( fp, "ClanPerms    %s~\n", ch->pcdata->clan_permissions );
      }

    fprintf( fp, "Flags        %d\n", ch->pcdata->flags );
    if( ch->pcdata->release_date > current_time )
      fprintf( fp, "Helled       %d %s~\n",
	  ( int ) ch->pcdata->release_date, ch->pcdata->helled_by );
    if( ch->pcdata->pkills )
      fprintf( fp, "PKills       %d\n", ch->pcdata->pkills );
    if( ch->pcdata->pdeaths )
      fprintf( fp, "PDeaths      %d\n", ch->pcdata->pdeaths );
    if( get_timer( ch, TIMER_PKILLED )
	&& ( get_timer( ch, TIMER_PKILLED ) > 0 ) )
      fprintf( fp, "PTimer       %d\n", get_timer( ch, TIMER_PKILLED ) );
    fprintf( fp, "AttrPerm     %d %d %d %d %d %d %d\n",
	ch->perm_str,
	ch->perm_int,
	ch->perm_wis,
	ch->perm_dex, ch->perm_con, ch->perm_cha, ch->perm_lck );

    fprintf( fp, "AttrMod      %d %d %d %d %d %d %d\n",
	ch->mod_str,
	ch->mod_int,
	ch->mod_wis,
	ch->mod_dex, ch->mod_con, ch->mod_cha, ch->mod_lck );

    fprintf( fp, "Condition    %d %d %d %d\n",
	ch->pcdata->condition[0],
	ch->pcdata->condition[1],
	ch->pcdata->condition[2], ch->pcdata->condition[3] );
    if( ch->desc && ch->desc->host )
      fprintf( fp, "Site         %s\n", ch->desc->host );
    else
      fprintf( fp, "Site         (Link-Dead)\n" );

    for( sn = 1; sn < top_sn; sn++ )
    {
      if( skill_table[sn]->name && character_skill_level( ch, sn ) > 0 )
	switch ( skill_table[sn]->type )
	{
	  default:
	    fprintf( fp, "Skill        %d '%s'\n",
		character_skill_level( ch, sn ),
		skill_table[sn]->name );
	    break;
	  case SKILL_SPELL:
	    fprintf( fp, "Spell        %d '%s'\n",
		character_skill_level( ch, sn ),
		skill_table[sn]->name );
	    break;
	  case SKILL_WEAPON:
	    fprintf( fp, "Weapon       %d '%s'\n",
		character_skill_level( ch, sn ),
		skill_table[sn]->name );
	    break;
	}
    }
  }

  for( paf = ch->first_affect; paf; paf = paf->next )
  {
    if( paf->type >= 0 && ( skill = get_skilltype( paf->type ) ) == NULL )
      continue;

    if( paf->type >= 0 && paf->type < TYPE_PERSONAL )
      fprintf( fp, "AffectData   '%s' %3d %3d %3d %10d\n",
	  skill->name,
	  paf->duration,
	  paf->modifier, paf->location, paf->bitvector );
    else
      fprintf( fp, "Affect       %3d %3d %3d %3d %10d\n",
	  paf->type,
	  paf->duration,
	  paf->modifier, paf->location, paf->bitvector );
  }

  track = MAX_KILLTRACK;
  for( sn = 0; sn < track; sn++ )
  {
    if( ch->pcdata->killed[sn].vnum == 0 )
      break;
    fprintf( fp, "Killed       %ld %d\n",
	ch->pcdata->killed[sn].vnum, ch->pcdata->killed[sn].count );
  }

#ifdef SWR2_USE_IMC
  imc_savechar( ch, fp );
#endif

  fprintf( fp, "End\n\n" );
  return;
}



/*
 * Write an object and its contents.
 */
void fwrite_obj( const CHAR_DATA * ch, const OBJ_DATA * obj, FILE * fp,
    int iNest, short os_type )
{
  EXTRA_DESCR_DATA *ed = NULL;
  AFFECT_DATA *paf = NULL;
  short wear = 0, wear_loc = 0, x = 0;

  if( iNest >= MAX_NEST )
  {
    bug( "fwrite_obj: iNest hit MAX_NEST %d", iNest );
    return;
  }

  /*
   * Slick recursion to write lists backwards,
   *   so loading them will load in forwards order.
   */
  if( obj->prev_content && os_type != OS_CORPSE )
    fwrite_obj( ch, obj->prev_content, fp, iNest, OS_CARRY );

  /*
   * Catch deleted objects                                      -Thoric
   */
  if( obj_extracted( obj ) )
    return;

  /*
   * Do NOT save prototype items!                               -Thoric
   */
  if( IS_OBJ_STAT( obj, ITEM_PROTOTYPE ) )
    return;

  /* Corpse saving. -- Altrag */
  fprintf( fp, ( os_type == OS_CORPSE ? "#CORPSE\n" : "#OBJECT\n" ) );

  if( iNest )
    fprintf( fp, "Nest         %d\n", iNest );

  if( obj->count > 1 )
    fprintf( fp, "Count        %d\n", obj->count );

  if( obj->name && ( !obj->pIndexData->name
	|| str_cmp( obj->name, obj->pIndexData->name ) ) )
    fprintf( fp, "Name         %s~\n", obj->name );

  if( obj->short_descr
      && ( !obj->pIndexData->short_descr
	|| str_cmp( obj->short_descr, obj->pIndexData->short_descr ) ) )
    fprintf( fp, "ShortDescr   %s~\n", obj->short_descr );

  if( obj->description
      && ( !obj->pIndexData->description
	|| str_cmp( obj->description, obj->pIndexData->description ) ) )
    fprintf( fp, "Description  %s~\n", obj->description );

  if( obj->action_desc
      && ( !obj->pIndexData->action_desc
	|| str_cmp( obj->action_desc, obj->pIndexData->action_desc ) ) )
    fprintf( fp, "ActionDesc   %s~\n", obj->action_desc );

  fprintf( fp, "Vnum         %ld\n", obj->pIndexData->vnum );

  if( os_type == OS_CORPSE && obj->in_room )
    fprintf( fp, "Room         %ld\n", obj->in_room->vnum );

  if( obj->extra_flags != obj->pIndexData->extra_flags )
    fprintf( fp, "ExtraFlags   %d\n", obj->extra_flags );

  if( obj->wear_flags != obj->pIndexData->wear_flags )
    fprintf( fp, "WearFlags    %d\n", obj->wear_flags );

  wear_loc = -1;

  for( wear = 0; wear < MAX_WEAR; wear++ )
  {
    for( x = 0; x < MAX_LAYERS; x++ )
    {
      if( obj == save_equipment[wear][x] )
      {
	wear_loc = wear;
	break;
      }
      else if( !save_equipment[wear][x] )
      {
	break;
      }
    }
  }

  if( wear_loc != -1 )
    fprintf( fp, "WearLoc      %d\n", wear_loc );

  if( obj->item_type != obj->pIndexData->item_type )
    fprintf( fp, "ItemType     %d\n", obj->item_type );

  if( obj->weight != obj->pIndexData->weight )
    fprintf( fp, "Weight       %d\n", obj->weight );

  if( obj->timer )
    fprintf( fp, "Timer        %d\n", obj->timer );

  if( obj->cost != obj->pIndexData->cost )
    fprintf( fp, "Cost         %d\n", obj->cost );

  if( obj->value[0] || obj->value[1] || obj->value[2]
      || obj->value[3] || obj->value[4] || obj->value[5] )
    fprintf( fp, "Values       %d %d %d %d %d %d\n",
	obj->value[0], obj->value[1], obj->value[2],
	obj->value[3], obj->value[4], obj->value[5] );

  switch ( obj->item_type )
  {
    case ITEM_DEVICE:
      if( IS_VALID_SN( obj->value[3] ) )
	fprintf( fp, "Spell 3      '%s'\n",
	    skill_table[obj->value[3]]->name );
      break;
  }

  for( paf = obj->first_affect; paf; paf = paf->next )
  {
    /*
     * Save extra object affects                              -Thoric
     */
    if( paf->type < 0 || paf->type >= top_sn )
    {
      fprintf( fp, "Affect       %d %d %d %d %d\n",
	  paf->type,
	  paf->duration,
	  ( ( paf->location == APPLY_WEAPONSPELL
	      || paf->location == APPLY_WEARSPELL
	      || paf->location == APPLY_REMOVESPELL
	      || paf->location == APPLY_STRIPSN )
	    && IS_VALID_SN( paf->modifier ) )
	  ? skill_table[paf->modifier]->slot : paf->modifier,
	  paf->location, paf->bitvector );
    }
    else
    {
      fprintf( fp, "AffectData   '%s' %d %d %d %d\n",
	  skill_table[paf->type]->name,
	  paf->duration,
	  ( ( paf->location == APPLY_WEAPONSPELL
	      || paf->location == APPLY_WEARSPELL
	      || paf->location == APPLY_REMOVESPELL
	      || paf->location == APPLY_STRIPSN )
	    && IS_VALID_SN( paf->modifier ) )
	  ? skill_table[paf->modifier]->slot : paf->modifier,
	  paf->location, paf->bitvector );
    }
  }

  for( ed = obj->first_extradesc; ed; ed = ed->next )
    fprintf( fp, "ExtraDescr   %s~ %s~\n", ed->keyword, ed->description );

  fprintf( fp, "End\n\n" );

  if( obj->first_content )
    fwrite_obj( ch, obj->last_content, fp, iNest + 1, OS_CARRY );
}



/*
 * Load a char and inventory into a new ch structure.
 */
bool load_char_obj( DESCRIPTOR_DATA * d, const char *name, bool preload )
{
  char strsave[MAX_INPUT_LENGTH];
  CHAR_DATA *ch;
  FILE *fp;
  bool found;
  struct stat fst;
  int i, x;
  char buf[MAX_INPUT_LENGTH];

  CREATE( ch, CHAR_DATA, 1 );
  for( x = 0; x < MAX_WEAR; x++ )
    for( i = 0; i < MAX_LAYERS; i++ )
      save_equipment[x][i] = NULL;
  clear_char( ch );
  loading_char = ch;

  CREATE( ch->pcdata, PC_DATA, 1 );
  d->character = ch;
  ch->desc = d;
  ch->name = STRALLOC( name );
  ch->act = PLR_BLANK | PLR_COMBINE | PLR_PROMPT;
  ch->top_level = 0;
  ch->perm_str = 10;
  ch->perm_int = 10;
  ch->perm_wis = 10;
  ch->perm_dex = 10;
  ch->perm_con = 10;
  ch->perm_cha = 10;
  ch->perm_lck = 10;
  ch->pcdata->condition[COND_THIRST] = 48;
  ch->pcdata->condition[COND_FULL] = 48;
  ch->pcdata->condition[COND_BLOODTHIRST] = 10;
  ch->pcdata->wizinvis = 0;
  ch->mental_state = -10;
  ch->mobinvis = 0;

  for( i = 0; i < MAX_SKILL; i++ )
    set_skill_level( ch, i, 0 );

  ch->pcdata->release_date = 0;
  ch->pcdata->helled_by = NULL;
  ch->saving_poison_death = 0;
  ch->saving_wand = 0;
  ch->saving_para_petri = 0;
  ch->saving_breath = 0;
  ch->saving_spell_staff = 0;
  ch->pcdata->pagerlen = 24;
  ch->mob_clan = NULL;
  ch->guard_data = NULL;
  ch->was_sentinel = NULL;
  ch->plr_home = NULL;

#ifdef SWR2_USE_IMC
  imc_initchar( ch );
#endif

  found = FALSE;
  sprintf( strsave, "%s%c/%s", PLAYER_DIR, tolower( ( int ) name[0] ),
	   capitalize( name ) );

#if !defined(AMIGA) && !defined(__MORPHOS__)
  if( stat( strsave, &fst ) != -1 )
  {
    if( fst.st_size == 0 )
    {
      sprintf( strsave, "%s%c/%s", BACKUP_DIR, tolower( ( int ) name[0] ),
	  capitalize( name ) );
      send_to_char( "Restoring your backup player file...", ch );
    }
    else
    {
      sprintf( buf, "%s player data for: %s (%dK)",
	  preload ? "Preloading" : "Loading", ch->name,
	  ( int ) fst.st_size / 1024 );
      log_string_plus( buf, LOG_COMM );
    }
  }
#endif
  /* else no player file */

  if( ( fp = fopen( strsave, "r" ) ) != NULL )
  {
    int iNest;

    for( iNest = 0; iNest < MAX_NEST; iNest++ )
      rgObjNest[iNest] = NULL;

    found = TRUE;
    /* Cheat so that bug will show line #'s -- Altrag */
    fpArea = fp;
    strcpy( strArea, strsave );
    for( ;; )
    {
      char letter;
      char *word;

      letter = fread_letter( fp );
      if( letter == '*' )
      {
	fread_to_eol( fp );
	continue;
      }

      if( letter != '#' )
      {
	bug( "Load_char_obj: # not found.", 0 );
	bug( name, 0 );
	break;
      }

      word = fread_word( fp );
      if( !str_cmp( word, "PLAYER" ) )
      {
	fread_char( ch, fp, preload );
	if( preload )
	  break;
      }
      else if( !str_cmp( word, "OBJECT" ) )	/* Objects      */
	fread_obj( ch, fp, OS_CARRY );
      else if( !str_cmp( word, "END" ) )	/* Done         */
	break;
      else
      {
	bug( "Load_char_obj: bad section.", 0 );
	bug( name, 0 );
	break;
      }
    }
    fclose( fp );
    fpArea = NULL;
    strcpy( strArea, "$" );
  }


  if( !found )
  {
    ch->short_descr = STRALLOC( "" );
    ch->long_descr = STRALLOC( "" );
    ch->description = STRALLOC( "" );
    ch->editor = NULL;
    ch->pcdata->clan_permissions = str_dup( "" );
    ch->pcdata->clan = NULL;
    ch->pcdata->pwd = str_dup( "" );
    ch->pcdata->email = str_dup( "" );
    ch->pcdata->bamfin = str_dup( "" );
    ch->pcdata->bamfout = str_dup( "" );
    ch->pcdata->rank = str_dup( "" );
    ch->pcdata->bestowments = str_dup( "" );
    ch->pcdata->title = STRALLOC( "" );
    ch->pcdata->homepage = str_dup( "" );
    ch->pcdata->bio = STRALLOC( "" );
    ch->pcdata->authed_by = STRALLOC( "" );
    ch->pcdata->prompt = STRALLOC( "" );
    ch->pcdata->wizinvis = 0;
    ch->pcdata->num_skills = 0;
    ch->pcdata->adept_skills = 0;
  }
  else
  {
    int sn;
    int num_skills = 0;
    int adept_skills = 0;

    if( !ch->pcdata->bio )
      ch->pcdata->bio = STRALLOC( "" );

    if( !ch->pcdata->authed_by )
      ch->pcdata->authed_by = STRALLOC( "" );

    if( !IS_NPC( ch ) && IS_IMMORTAL( ch ) )
    {
      if( ch->pcdata->wizinvis < 2 )
	ch->pcdata->wizinvis = ch->top_level;
    }
    if( file_ver > 1 )
    {
      for( i = 0; i < MAX_WEAR; i++ )
      {
	for( x = 0; x < MAX_LAYERS; x++ )
	{
	  if( save_equipment[i][x] )
	  {
	    equip_char( ch, save_equipment[i][x], i );
	    save_equipment[i][x] = NULL;
	  }
	  else
	  {
	    break;
	  }
	}
      }
    }

    for( sn = 0; sn < top_sn; sn++ )
      if( character_skill_level( ch, sn ) > 0 )
      {
	num_skills++;
	if( character_skill_level( ch, sn ) >= 100 )
	  adept_skills++;
      }
    ch->pcdata->num_skills = num_skills;
    ch->pcdata->adept_skills = adept_skills;

  }

  loading_char = NULL;
  return found;
}



/*
 * Read in a char.
 */
void fread_char( CHAR_DATA * ch, FILE * fp, bool preload )
{
  char buf[MAX_STRING_LENGTH];
  const char *line = NULL;
  const char *word = NULL;
  int x1 = 0, x2 = 0, x3 = 0, x4 = 0, x5 = 0, x6 = 0, x7 = 0;
  short killcnt = 0;
  bool fMatch = FALSE;
  time_t lastplayed = 0;
  int extra = 0;

  file_ver = 0;

  for( ;; )
  {
    word = feof( fp ) ? "End" : fread_word( fp );

    if( word[0] == '\0' )
    {
      bug( "%s: EOF encountered reading file!", __FUNCTION__ );
      word = "End";
    }

    fMatch = FALSE;

    switch ( UPPER( word[0] ) )
    {
      case '*':
	fMatch = TRUE;
	fread_to_eol( fp );
	break;

      case 'A':
	KEY( "Act", ch->act, fread_number( fp ) );
	KEY( "AffectedBy", ch->affected_by, fread_number( fp ) );
	KEY( "Alignment", ch->alignment, fread_number( fp ) );
	KEY( "Armor", ch->armor, fread_number( fp ) );

	if( !str_cmp( word, "Affect" ) || !str_cmp( word, "AffectData" ) )
	{
	  AFFECT_DATA *paf;

	  if( preload )
	  {
	    fMatch = TRUE;
	    fread_to_eol( fp );
	    break;
	  }
	  CREATE( paf, AFFECT_DATA, 1 );
	  if( !str_cmp( word, "Affect" ) )
	  {
	    paf->type = fread_number( fp );
	  }
	  else
	  {
	    int sn;
	    char *sname = fread_word( fp );

	    if( ( sn = skill_lookup( sname ) ) < 0 )
	    {
	      bug( "Fread_char: unknown skill.", 0 );
	    }
	    paf->type = sn;
	  }

	  paf->duration = fread_number( fp );
	  paf->modifier = fread_number( fp );
	  paf->location = fread_number( fp );
	  paf->bitvector = fread_number( fp );
	  LINK( paf, ch->first_affect, ch->last_affect, next, prev );
	  fMatch = TRUE;
	  break;
	}

	if( !str_cmp( word, "AttrMod" ) )
	{
	  line = fread_line( fp );
	  x1 = x2 = x3 = x4 = x5 = x6 = x7 = 13;
	  sscanf( line, "%d %d %d %d %d %d %d",
	      &x1, &x2, &x3, &x4, &x5, &x6, &x7 );
	  ch->mod_str = x1;
	  ch->mod_int = x2;
	  ch->mod_wis = x3;
	  ch->mod_dex = x4;
	  ch->mod_con = x5;
	  ch->mod_cha = x6;
	  ch->mod_lck = x7;
	  if( !x7 )
	    ch->mod_lck = 0;
	  fMatch = TRUE;
	  break;
	}

	if( !str_cmp( word, "AttrPerm" ) )
	{
	  line = fread_line( fp );
	  x1 = x2 = x3 = x4 = x5 = x6 = x7 = 0;
	  sscanf( line, "%d %d %d %d %d %d %d",
	      &x1, &x2, &x3, &x4, &x5, &x6, &x7 );
	  ch->perm_str = x1;
	  ch->perm_int = x2;
	  ch->perm_wis = x3;
	  ch->perm_dex = x4;
	  ch->perm_con = x5;
	  ch->perm_cha = x6;
	  ch->perm_lck = x7;
	  if( !x7 || x7 == 0 )
	    ch->perm_lck = 13;
	  fMatch = TRUE;
	  break;
	}
	KEY( "AuthedBy", ch->pcdata->authed_by, fread_string( fp ) );
	break;

      case 'B':
	KEY( "Bamfin", ch->pcdata->bamfin, fread_string_nohash( fp ) );
	KEY( "Bamfout", ch->pcdata->bamfout, fread_string_nohash( fp ) );
	KEY( "Bestowments", ch->pcdata->bestowments,
	    fread_string_nohash( fp ) );
	KEY( "Bio", ch->pcdata->bio, fread_string( fp ) );
	KEY( "Bank", ch->pcdata->bank, fread_number( fp ) );
	break;

      case 'C':
	KEY( "ClanPerms", ch->pcdata->clan_permissions,
	     fread_string_nohash( fp ) );

	if( !str_cmp( word, "Clan" ) )
	{
	  char *clan_name = fread_string( fp );

	  if( !preload && clan_name[0] != '\0'
	      && ( ch->pcdata->clan = get_clan( clan_name ) ) == NULL )
	  {
	    sprintf( buf, "Warning: the organization %s no longer exists, and therefore you no longer\r\nbelong to that organization.\r\n", clan_name );
	    send_to_char( buf, ch );
	  }

	  STRFREE( clan_name );
	  fMatch = TRUE;
	  break;
	}

	if( !str_cmp( word, "Condition" ) )
	{
	  line = fread_line( fp );
	  sscanf( line, "%d %d %d %d", &x1, &x2, &x3, &x4 );
	  ch->pcdata->condition[0] = x1;
	  ch->pcdata->condition[1] = x2;
	  ch->pcdata->condition[2] = x3;
	  ch->pcdata->condition[3] = x4;
	  fMatch = TRUE;
	  break;
	}
	break;

      case 'D':
	KEY( "Damroll", ch->damroll, fread_number( fp ) );
	KEY( "Deaf", ch->deaf, fread_number( fp ) );
	KEY( "Description", ch->description, fread_string( fp ) );
	break;

	/* 'E' was moved to after 'S' */
      case 'F':
	KEY( "Flags", ch->pcdata->flags, fread_number( fp ) );
	if( !str_cmp( word, "Force" ) )
	{
	  line = fread_line( fp );
	  x1 = x2 = x3 = x4 = x5 = x6 = 0;
	  sscanf( line, "%d %d %d %d", &x1, &x2, &x3, &x4 );
	  ch->perm_frc = x1;
	  ch->mod_frc = x2;
	  ch->mana = x3;
	  ch->max_mana = x4;
	  fMatch = TRUE;
	  break;
	}
	break;

      case 'G':
	KEY( "Gold", ch->gold, fread_number( fp ) );
	break;

      case 'H':
	if( !str_cmp( word, "Helled" ) )
	{
	  ch->pcdata->release_date = fread_number( fp );
	  ch->pcdata->helled_by = fread_string( fp );
	  if( ch->pcdata->release_date < current_time )
	  {
	    STRFREE( ch->pcdata->helled_by );
	    ch->pcdata->helled_by = NULL;
	    ch->pcdata->release_date = 0;
	  }
	  fMatch = TRUE;
	  break;
	}

	KEY( "Hitroll", ch->hitroll, fread_number( fp ) );
	KEY( "Homepage", ch->pcdata->homepage, fread_string_nohash( fp ) );

	if( !str_cmp( word, "HpManaMove" ) )
	{
	  line = fread_line( fp );
	  x1 = x2 = x3 = x4 = x5 = x6 = 0;
	  sscanf( line, "%d %d %d %d %d %d",
	      &x1, &x2, &x3, &x4, &x5, &x6 );
	  ch->hit = x1;
	  ch->max_hit = x2;
	  ch->move = x5;
	  ch->max_move = x6;
	  if( x4 >= 100 )
	  {
	    ch->perm_frc = number_range( 1, 20 );
	    ch->max_mana = x4;
	    ch->mana = x4;
	  }
	  else if( x4 >= 10 )
	  {
	    ch->perm_frc = 1;
	    ch->max_mana = x4;
	  }
	  fMatch = TRUE;
	  break;
	}

	break;

      case 'I':
	KEY( "Immune", ch->immune, fread_number( fp ) );

#ifdef SWR2_USE_IMC
	if( ( fMatch = imc_loadchar( ch, fp, word ) ) )
	  break;
#endif
	break;

      case 'K':
	if( !str_cmp( word, "Killed" ) )
	{
	  fMatch = TRUE;
	  if( killcnt >= MAX_KILLTRACK )
	    bug( "fread_char: killcnt (%d) >= MAX_KILLTRACK", killcnt );
	  else
	  {
	    ch->pcdata->killed[killcnt].vnum = fread_number( fp );
	    ch->pcdata->killed[killcnt++].count = fread_number( fp );
	  }
	}
	break;

      case 'L':
	if( !str_cmp( word, "Lastplayed" ) )
	{
	  lastplayed = fread_number( fp );
	  fMatch = TRUE;
	  break;
	}
	KEY( "LongDescr", ch->long_descr, fread_string( fp ) );
	break;

      case 'M':
	KEY( "Mentalstate", ch->mental_state, fread_number( fp ) );
	KEY( "Minsnoop", ch->pcdata->min_snoop, fread_number( fp ) );
	KEY( "Mobinvis", ch->mobinvis, fread_number( fp ) );
	break;

      case 'N':
	if( !str_cmp( word, "Name" ) )
	{
	  /*
	   * Name already set externally.
	   */
	  fread_to_eol( fp );
	  fMatch = TRUE;
	  break;
	}
	break;

      case 'O':
	KEY( "Outcast_time", ch->pcdata->outcast_time, fread_number( fp ) );
	break;

      case 'P':
	KEY( "Pagerlen", ch->pcdata->pagerlen, fread_number( fp ) );
	KEY( "Password", ch->pcdata->pwd, fread_string_nohash( fp ) );
	KEY( "PDeaths", ch->pcdata->pdeaths, fread_number( fp ) );
	KEY( "PKills", ch->pcdata->pkills, fread_number( fp ) );
	KEY( "Played", ch->played, fread_number( fp ) );
	KEY( "Position", ch->position, fread_number( fp ) );
	KEY( "Practice", extra, fread_number( fp ) );
	KEY( "Prompt", ch->pcdata->prompt, fread_string( fp ) );
	if( !str_cmp( word, "PTimer" ) )
	{
	  add_timer( ch, TIMER_PKILLED, fread_number( fp ), NULL, 0 );
	  fMatch = TRUE;
	  break;
	}
	if( !str_cmp( word, "PlrHome" ) )
	{
	  ch->plr_home = get_room_index( fread_number( fp ) );
	  if( !ch->plr_home )
	    ch->plr_home = NULL;
	  fMatch = TRUE;
	  break;
	}
	break;

      case 'R':
	KEY( "Rank", ch->pcdata->rank, fread_string_nohash( fp ) );
	KEY( "Resistant", ch->resistant, fread_number( fp ) );
	KEY( "Restore_time", ch->pcdata->restore_time, fread_number( fp ) );

	if( !str_cmp( word, "Room" ) )
	{
	  ch->in_room = get_room_index( fread_number( fp ) );
	  if( !ch->in_room )
	    ch->in_room = get_room_index( wherehome( ch ) );
	  fMatch = TRUE;
	  break;
	}
	break;

      case 'S':
	KEY( "Sex", ch->sex, fread_number( fp ) );
	KEY( "ShortDescr", ch->short_descr, fread_string( fp ) );
	KEY( "Susceptible", ch->susceptible, fread_number( fp ) );
	if( !str_cmp( word, "SavingThrow" ) )
	{
	  ch->saving_wand = fread_number( fp );
	  ch->saving_poison_death = ch->saving_wand;
	  ch->saving_para_petri = ch->saving_wand;
	  ch->saving_breath = ch->saving_wand;
	  ch->saving_spell_staff = ch->saving_wand;
	  fMatch = TRUE;
	  break;
	}

	if( !str_cmp( word, "SavingThrows" ) )
	{
	  ch->saving_poison_death = fread_number( fp );
	  ch->saving_wand = fread_number( fp );
	  ch->saving_para_petri = fread_number( fp );
	  ch->saving_breath = fread_number( fp );
	  ch->saving_spell_staff = fread_number( fp );
	  fMatch = TRUE;
	  break;
	}

	if( !str_cmp( word, "Site" ) )
	{
	  if( !preload )
	  {
	    sprintf( buf, "Last connected from: %s\r\n",
		fread_word( fp ) );
	    send_to_char( buf, ch );
	  }
	  else
	    fread_to_eol( fp );
	  fMatch = TRUE;
	  if( preload )
	    word = "End";
	  else
	    break;
	}

	if( !str_cmp( word, "Skill" ) )
	{
	  int value;

	  if( preload )
	    word = "End";
	  else
	  {
	    int sn = 0;
	    value = fread_number( fp );
	    if( file_ver < 3 )
	      sn = skill_lookup( fread_word( fp ) );
	    else
	      sn =
		bsearch_skill_exact( fread_word( fp ), gsn_first_skill,
		    gsn_first_weapon - 1 );
	    if( sn < 0 )
	      bug( "Fread_char: unknown skill.", 0 );
	    else
	    {
	      set_skill_level( ch, sn, value );
	    }
	    fMatch = TRUE;
	    break;
	  }
	}

	if( !str_cmp( word, "Spell" ) )
	{
	  int sn;
	  int value;

	  if( preload )
	    word = "End";
	  else
	  {
	    value = fread_number( fp );

	    sn =
	      bsearch_skill_exact( fread_word( fp ), gsn_first_spell,
		  gsn_first_skill - 1 );
	    if( sn < 0 )
	      bug( "Fread_char: unknown spell.", 0 );
	    else
	    {
	      set_skill_level( ch, sn, value );
	    }
	    fMatch = TRUE;
	    break;
	  }
	}
	if( str_cmp( word, "End" ) )
	  break;

      case 'E':
	if( !str_cmp( word, "End" ) )
	{
	  if( !ch->short_descr )
	    ch->short_descr = STRALLOC( "" );
	  if( !ch->long_descr )
	    ch->long_descr = STRALLOC( "" );
	  if( !ch->description )
	    ch->description = STRALLOC( "" );
	  if( !ch->pcdata->pwd )
	    ch->pcdata->pwd = str_dup( "" );
	  if( !ch->pcdata->email )
	    ch->pcdata->email = str_dup( "" );
	  if( !ch->pcdata->bamfin )
	    ch->pcdata->bamfin = str_dup( "" );
	  if( !ch->pcdata->bamfout )
	    ch->pcdata->bamfout = str_dup( "" );
	  if( !ch->pcdata->bio )
	    ch->pcdata->bio = STRALLOC( "" );
	  if( !ch->pcdata->rank )
	    ch->pcdata->rank = str_dup( "" );
	  if( !ch->pcdata->bestowments )
	    ch->pcdata->bestowments = str_dup( "" );
	  if( !ch->pcdata->title )
	    ch->pcdata->title = STRALLOC( "" );
	  if( !ch->pcdata->homepage )
	    ch->pcdata->homepage = str_dup( "" );
	  if( !ch->pcdata->authed_by )
	    ch->pcdata->authed_by = STRALLOC( "" );
	  if( !ch->pcdata->prompt )
	    ch->pcdata->prompt = STRALLOC( "" );
	  if( !ch->pcdata->clan_permissions )
	    ch->pcdata->clan_permissions = str_dup( "" );

	  ch->editor = NULL;
	  killcnt = MAX_KILLTRACK;

	  if( killcnt < MAX_KILLTRACK )
	    ch->pcdata->killed[killcnt].vnum = 0;
	  if( !ch->pcdata->prompt )
	    ch->pcdata->prompt = STRALLOC( "" );

	  if( lastplayed != 0 )
	  {
	    int hitgain;
	    hitgain = ( ( int ) ( current_time - lastplayed ) / 60 );
	    ch->hit = URANGE( 1, ch->hit + hitgain, ch->max_hit );
	    ch->move = URANGE( 1, ch->move + hitgain, ch->max_move );
	    ch->mana = URANGE( 0, ch->mana + hitgain, ch->max_mana );
	    better_mental_state( ch, hitgain );
	  }
	  return;
	}
	KEY( "Email", ch->pcdata->email, fread_string_nohash( fp ) );
	break;

      case 'T':
	KEY( "Toplevel", ch->top_level, fread_number( fp ) );
	KEY( "Trust", ch->trust, fread_number( fp ) );
	/* Let no character be trusted higher than one below maxlevel -- Narn */
	ch->trust = UMIN( ch->trust, MAX_LEVEL - 1 );

	if( !str_cmp( word, "Title" ) )
	{
	  ch->pcdata->title = fread_string( fp );
	  if( isalpha( ( int ) ch->pcdata->title[0] )
	      || isdigit( ( int ) ch->pcdata->title[0] ) )
	  {
	    sprintf( buf, " %s", ch->pcdata->title );
	    if( ch->pcdata->title )
	      STRFREE( ch->pcdata->title );
	    ch->pcdata->title = STRALLOC( buf );
	  }
	  fMatch = TRUE;
	  break;
	}

	break;

      case 'V':
	KEY( "Version", file_ver, fread_number( fp ) );
	break;

      case 'W':
	if( !str_cmp( word, "Weapon" ) )
	{
	  int sn;
	  int value;

	  if( preload )
	    word = "End";
	  else
	  {
	    value = fread_number( fp );

	    sn =
	      bsearch_skill_exact( fread_word( fp ), gsn_first_weapon,
		  gsn_top_sn - 1 );
	    if( sn < 0 )
	      bug( "Fread_char: unknown weapon.", 0 );
	    else
	    {
	      set_skill_level( ch, sn, value );
	    }
	    fMatch = TRUE;
	  }
	  break;
	}
	KEY( "Wimpy", ch->wimpy, fread_number( fp ) );
	KEY( "WizInvis", ch->pcdata->wizinvis, fread_number( fp ) );
	break;
    }

    if( !fMatch )
    {
      sprintf( buf, "Fread_char: no match: %s", word );
      bug( buf, 0 );
    }
  }
}


void fread_obj( CHAR_DATA * ch, FILE * fp, short os_type )
{
  OBJ_DATA *obj = NULL;
  const char *word = NULL;
  int iNest = 0;
  bool fMatch = FALSE;
  bool fNest = FALSE;
  bool fVnum = FALSE;
  ROOM_INDEX_DATA *room = NULL;

  CREATE( obj, OBJ_DATA, 1 );
  obj->count = 1;
  obj->wear_loc = -1;
  obj->weight = 1;

  fNest = TRUE;			/* Requiring a Nest 0 is a waste */
  fVnum = TRUE;
  iNest = 0;

  for( ;; )
  {
    word = feof( fp ) ? "End" : fread_word( fp );

    if( word[0] == '\0' )
    {
      bug( "%s: EOF encountered reading file!", __FUNCTION__ );
      word = "End";
    }

    fMatch = FALSE;

    switch ( UPPER( word[0] ) )
    {
      case '*':
	fMatch = TRUE;
	fread_to_eol( fp );
	break;

      case 'A':
	if( !str_cmp( word, "Affect" ) || !str_cmp( word, "AffectData" ) )
	{
	  AFFECT_DATA *paf = NULL;
	  int pafmod = 0;

	  CREATE( paf, AFFECT_DATA, 1 );
	  if( !str_cmp( word, "Affect" ) )
	  {
	    paf->type = fread_number( fp );
	  }
	  else
	  {
	    int sn = skill_lookup( fread_word( fp ) );

	    if( sn < 0 )
	      bug( "Fread_obj: unknown skill.", 0 );
	    else
	      paf->type = sn;
	  }
	  paf->duration = fread_number( fp );
	  pafmod = fread_number( fp );
	  paf->location = fread_number( fp );
	  paf->bitvector = fread_number( fp );
	  if( paf->location == APPLY_WEAPONSPELL
	      || paf->location == APPLY_WEARSPELL
	      || paf->location == APPLY_REMOVESPELL )
	    paf->modifier = slot_lookup( pafmod );
	  else
	    paf->modifier = pafmod;
	  LINK( paf, obj->first_affect, obj->last_affect, next, prev );
	  fMatch = TRUE;
	  break;
	}
	KEY( "Actiondesc", obj->action_desc, fread_string( fp ) );
	break;

      case 'C':
	KEY( "Cost", obj->cost, fread_number( fp ) );
	KEY( "Count", obj->count, fread_number( fp ) );
	break;

      case 'D':
	KEY( "Description", obj->description, fread_string( fp ) );
	break;

      case 'E':
	KEY( "ExtraFlags", obj->extra_flags, fread_number( fp ) );

	if( !str_cmp( word, "ExtraDescr" ) )
	{
	  EXTRA_DESCR_DATA *ed;

	  CREATE( ed, EXTRA_DESCR_DATA, 1 );
	  ed->keyword = fread_string( fp );
	  ed->description = fread_string( fp );
	  LINK( ed, obj->first_extradesc, obj->last_extradesc, next,
	      prev );
	  fMatch = TRUE;
	}

	if( !str_cmp( word, "End" ) )
	{
	  if( !fNest || !fVnum )
	  {
	    bug( "Fread_obj: incomplete object.", 0 );
	    if( obj->name )
	      STRFREE( obj->name );
	    if( obj->description )
	      STRFREE( obj->description );
	    if( obj->short_descr )
	      STRFREE( obj->short_descr );
	    DISPOSE( obj );
	    return;
	  }
	  else
	  {
	    short wear_loc = obj->wear_loc;

	    if( !obj->name )
	      obj->name = QUICKLINK( obj->pIndexData->name );
	    if( !obj->description )
	      obj->description =
		QUICKLINK( obj->pIndexData->description );
	    if( !obj->short_descr )
	      obj->short_descr =
		QUICKLINK( obj->pIndexData->short_descr );
	    if( !obj->action_desc )
	      obj->action_desc =
		QUICKLINK( obj->pIndexData->action_desc );
	    LINK( obj, first_object, last_object, next, prev );
	    obj->pIndexData->count += obj->count;
	    if( !obj->serial )
	    {
	      cur_obj_serial =
		UMAX( ( cur_obj_serial + 1 ) & ( BV30 - 1 ), 1 );
	      obj->serial = obj->pIndexData->serial = cur_obj_serial;
	    }
	    if( fNest )
	      rgObjNest[iNest] = obj;
	    numobjsloaded += obj->count;
	    ++physicalobjects;
	    if( file_ver > 1 || obj->wear_loc < -1
		|| obj->wear_loc >= MAX_WEAR )
	      obj->wear_loc = -1;
	    /* Corpse saving. -- Altrag */
	    if( os_type == OS_CORPSE )
	    {
	      if( !room )
	      {
		bug( "Fread_obj: Corpse without room", 0 );
		room = get_room_index( ROOM_VNUM_LIMBO );
	      }
	      obj = obj_to_room( obj, room );
	    }
	    else if( iNest == 0 || rgObjNest[iNest] == NULL )
	    {
	      int slot = 0;
	      bool reslot = FALSE;

	      if( file_ver > 1
		  && wear_loc > -1 && wear_loc < MAX_WEAR )
	      {
		int x = 0;

		for( x = 0; x < MAX_LAYERS; x++ )
		  if( !save_equipment[wear_loc][x] )
		  {
		    save_equipment[wear_loc][x] = obj;
		    slot = x;
		    reslot = TRUE;
		    break;
		  }
		if( x == MAX_LAYERS )
		  bug( "Fread_obj: too many layers %d", wear_loc );
	      }
	      obj = obj_to_char( obj, ch );
	      if( reslot )
		save_equipment[wear_loc][slot] = obj;
	    }
	    else
	    {
	      if( rgObjNest[iNest - 1] )
	      {
		separate_obj( rgObjNest[iNest - 1] );
		obj = obj_to_obj( obj, rgObjNest[iNest - 1] );
	      }
	      else
		bug( "Fread_obj: nest layer missing %d", iNest - 1 );
	    }
	    if( fNest )
	      rgObjNest[iNest] = obj;
	    return;
	  }
	}
	break;

      case 'I':
	KEY( "ItemType", obj->item_type, fread_number( fp ) );
	break;

      case 'N':
	KEY( "Name", obj->name, fread_string( fp ) );

	if( !str_cmp( word, "Nest" ) )
	{
	  iNest = fread_number( fp );
	  if( iNest < 0 || iNest >= MAX_NEST )
	  {
	    bug( "Fread_obj: bad nest %d.", iNest );
	    iNest = 0;
	    fNest = FALSE;
	  }
	  fMatch = TRUE;
	}
	break;

      case 'R':
	KEY( "Room", room, get_room_index( fread_number( fp ) ) );

      case 'S':
	KEY( "ShortDescr", obj->short_descr, fread_string( fp ) );

	if( !str_cmp( word, "Spell" ) )
	{
	  int iValue = fread_number( fp );
	  int sn = skill_lookup( fread_word( fp ) );

	  if( iValue < 0 || iValue > 5 )
	    bug( "Fread_obj: bad iValue %d.", iValue );
	  else if( sn < 0 )
	    bug( "Fread_obj: unknown skill.", 0 );
	  else
	    obj->value[iValue] = sn;
	  fMatch = TRUE;
	  break;
	}

	break;

      case 'T':
	KEY( "Timer", obj->timer, fread_number( fp ) );
	break;

      case 'V':
	if( !str_cmp( word, "Values" ) )
	{
	  int x1 = 0, x2 = 0, x3 = 0, x4 = 0, x5 = 0, x6 = 0;
	  char *ln = fread_line( fp );

	  sscanf( ln, "%d %d %d %d %d %d", &x1, &x2, &x3, &x4, &x5, &x6 );
	  /* clean up some garbage */
	  if( file_ver < 3 )
	    x5 = x6 = 0;

	  obj->value[0] = x1;
	  obj->value[1] = x2;
	  obj->value[2] = x3;
	  obj->value[3] = x4;
	  obj->value[4] = x5;
	  obj->value[5] = x6;
	  fMatch = TRUE;
	  break;
	}

	if( !str_cmp( word, "Vnum" ) )
	{
	  int vnum = fread_number( fp );
	  if( ( obj->pIndexData = get_obj_index( vnum ) ) == NULL )
	  {
	    fVnum = FALSE;
	    bug( "Fread_obj: bad vnum %d.", vnum );
	  }
	  else
	  {
	    fVnum = TRUE;
	    obj->cost = obj->pIndexData->cost;
	    obj->weight = obj->pIndexData->weight;
	    obj->item_type = obj->pIndexData->item_type;
	    obj->wear_flags = obj->pIndexData->wear_flags;
	    obj->extra_flags = obj->pIndexData->extra_flags;
	  }
	  fMatch = TRUE;
	  break;
	}
	break;

      case 'W':
	KEY( "WearFlags", obj->wear_flags, fread_number( fp ) );
	KEY( "WearLoc", obj->wear_loc, fread_number( fp ) );
	KEY( "Weight", obj->weight, fread_number( fp ) );
	break;

    }

    if( !fMatch )
    {
      EXTRA_DESCR_DATA *ed = NULL;
      AFFECT_DATA *paf = NULL;

      bug( "Fread_obj: no match.", 0 );
      bug( word, 0 );
      fread_to_eol( fp );
      if( obj->name )
	STRFREE( obj->name );
      if( obj->description )
	STRFREE( obj->description );
      if( obj->short_descr )
	STRFREE( obj->short_descr );
      while( ( ed = obj->first_extradesc ) != NULL )
      {
	STRFREE( ed->keyword );
	STRFREE( ed->description );
	UNLINK( ed, obj->first_extradesc, obj->last_extradesc, next,
	    prev );
	DISPOSE( ed );
      }
      while( ( paf = obj->first_affect ) != NULL )
      {
	UNLINK( paf, obj->first_affect, obj->last_affect, next, prev );
	DISPOSE( paf );
      }
      DISPOSE( obj );
      return;
    }
  }
}

void set_alarm( long seconds )
{
#if !defined(AMIGA) && !defined(__MORPHOS__) && !defined( _WIN32 )
  alarm( seconds );
#endif
}

/*
 * Based on last time modified, show when a player was last on	-Thoric
 */
void do_last( CHAR_DATA * ch, char *argument )
{
  char buf[MAX_STRING_LENGTH];
  char arg[MAX_INPUT_LENGTH];
  char name[MAX_INPUT_LENGTH];
  struct stat fst;

  one_argument( argument, arg );
  if( arg[0] == '\0' )
  {
    send_to_char( "Usage: last <playername>\r\n", ch );
    return;
  }
  strcpy( name, capitalize( arg ) );
  sprintf( buf, "%s%c/%s", PLAYER_DIR, tolower( ( int ) arg[0] ), name );
  if( stat( buf, &fst ) != -1 )
    sprintf( buf, "%s was last on: %s\r\n", name, ctime( &fst.st_mtime ) );
  else
    sprintf( buf, "%s was not found.\r\n", name );
  send_to_char( buf, ch );
}

void write_corpses( const CHAR_DATA * ch, const char *name )
{
  OBJ_DATA *corpse = NULL;
  FILE *fp = NULL;

  /* Name and ch support so that we dont have to have a char to save their
     corpses.. (ie: decayed corpses while offline) */
  if( ch && IS_NPC( ch ) )
  {
    bug( "Write_corpses: writing NPC corpse.", 0 );
    return;
  }
  if( ch )
    name = ch->name;
  /* Go by vnum, less chance of screwups. -- Altrag */
  for( corpse = first_object; corpse; corpse = corpse->next )
    if( corpse->pIndexData->vnum == OBJ_VNUM_CORPSE_PC &&
	corpse->in_room != NULL &&
	!str_cmp( corpse->short_descr + 14, name ) )
    {
      if( !fp )
      {
	char buf[127];

	sprintf( buf, "%s%s", CORPSE_DIR, capitalize( name ) );
	if( !( fp = fopen( buf, "w" ) ) )
	{
	  bug( "Write_corpses: Cannot open file.", 0 );
	  perror( buf );
	  return;
	}
      }
      fwrite_obj( ch, corpse, fp, 0, OS_CORPSE );
    }
  if( fp )
  {
    fprintf( fp, "#END\n\n" );
    fclose( fp );
  }
  else
  {
    char buf[127];

    sprintf( buf, "%s%s", CORPSE_DIR, capitalize( name ) );
    remove( buf );
  }
  return;
}

/* Directory scanning with Amiga and MorphOS */
#if defined(AMIGA) || defined(__MORPHOS__)

void load_corpses( void )
{
  BPTR sourcelock = NULL;
  struct ExAllControl *excontrol = NULL;
  struct ExAllData buffer, *ead = NULL;
  BOOL exmore = TRUE;

  memset( &buffer, 0, sizeof( buffer ) );
  sourcelock = Lock( (CONST_STRPTR) CORPSE_DIR, SHARED_LOCK );
  excontrol = (struct ExAllControl*) AllocDosObject( DOS_EXALLCONTROL, NULL );
  excontrol->eac_LastKey = 0;

  do
  {
    exmore = ExAll( sourcelock, &buffer, sizeof( buffer ),
	ED_NAME, excontrol );

    if( !exmore && IoErr() != ERROR_NO_MORE_ENTRIES )
      continue;

    ead = &buffer;

    do
    {
      if( ead->ed_Name[0] != '.' )
      {
	fread_corpse( (const char*) ead->ed_Name );
      }

      ead = ead->ed_Next;
    }
    while( ead );
  }
  while( exmore );

  FreeDosObject( DOS_EXALLCONTROL, excontrol );
  UnLock( sourcelock );
}

/* Directory scanning with Windows */
#elif defined(_WIN32)

void load_corpses( void )
{
  char buf[256];
  WIN32_FIND_DATA info;
  HANDLE h;

  sprintf( buf, "%s*.*", CORPSE_DIR );
  h = FindFirstFile( buf, &info );

  if( h != INVALID_HANDLE_VALUE )
  {
    do
    {
      if( info.cFileName[0] != '.' )
      {
	fread_corpse( info.cFileName );
      }
    }
    while( FindNextFile( h, &info ) );

    FindClose( h );
  }
}

/* Directory scanning with POSIX */
#else
void load_corpses( void )
{
  DIR *dp = NULL;
  struct dirent *de = NULL;

  if( !( dp = opendir( CORPSE_DIR ) ) )
  {
    bug( "Load_corpses: can't open CORPSE_DIR", 0 );
    perror( CORPSE_DIR );
    return;
  }

  while( ( de = readdir( dp ) ) != NULL )
  {
    if( de->d_name[0] != '.' )
    {
      fread_corpse( de->d_name );
    }
  }
}
#endif

void fread_corpse( const char *name )
{
  falling = 1;
  sprintf( strArea, "%s%s", CORPSE_DIR, name );
  fprintf( out_stream, "Corpse -> %s\n", strArea );

  if( !( fpArea = fopen( strArea, "r" ) ) )
  {
    perror( strArea );
    return;
  }

  for( ;; )
  {
    const char *word;
    char letter = fread_letter( fpArea );

    if( letter == '*' )
    {
      fread_to_eol( fpArea );
      continue;
    }

    if( letter != '#' )
    {
      bug( "Load_corpses: # not found." );
      break;
    }

    word = fread_word( fpArea );

    if( !str_cmp( word, "CORPSE" ) )
    {
      fread_obj( NULL, fpArea, OS_CORPSE );
    }
    else if( !str_cmp( word, "OBJECT" ) )
    {
      fread_obj( NULL, fpArea, OS_CARRY );
    }
    else if( !str_cmp( word, "END" ) )
    {
      break;
    }
    else
    {
      bug( "Load_corpses: bad section." );
      break;
    }
  }

  fclose( fpArea );
  fpArea = NULL;
  sprintf( strArea, "$" );
  falling = 0;
}
