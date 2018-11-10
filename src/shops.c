#include <stdio.h>
#include <string.h>
#include "mud.h"

/*
 * Local functions
 */

CHAR_DATA *find_keeper( CHAR_DATA * ch );
CHAR_DATA *find_fixer( CHAR_DATA * ch );
int get_cost( CHAR_DATA * ch, CHAR_DATA * keeper, OBJ_DATA * obj, bool fBuy );
int get_repaircost( CHAR_DATA * keeper, OBJ_DATA * obj );

/*
 * Shopping commands.
 */
CHAR_DATA *find_keeper( CHAR_DATA * ch )
{
  CHAR_DATA *keeper = NULL;
  SHOP_DATA *pShop = NULL;

  for( keeper = ch->in_room->first_person;
      keeper; keeper = keeper->next_in_room )
    if( IS_NPC( keeper ) && ( pShop = keeper->pIndexData->pShop ) != NULL )
      break;

  if( !pShop )
  {
    send_to_char( "You can't do that here.\r\n", ch );
    return NULL;
  }

  /*
   * Shop hours.
   */
  if( time_info.hour < pShop->open_hour )
  {
    char buf[MAX_STRING_LENGTH];
    snprintf( buf, MAX_STRING_LENGTH, "%s", "Sorry, come back later." );
    do_say( keeper, buf );
    return NULL;
  }

  if( time_info.hour > pShop->close_hour )
  {
    char buf[MAX_STRING_LENGTH];
    snprintf( buf, MAX_STRING_LENGTH, "%s", "Sorry, come back tomorrow." );
    do_say( keeper, buf );
    return NULL;
  }

  return keeper;
}

/*
 * repair commands.
 */
CHAR_DATA *find_fixer( CHAR_DATA * ch )
{
  CHAR_DATA *keeper = NULL;
  REPAIR_DATA *rShop = NULL;

  for( keeper = ch->in_room->first_person;
      keeper; keeper = keeper->next_in_room )
    if( IS_NPC( keeper ) && ( rShop = keeper->pIndexData->rShop ) != NULL )
      break;

  if( !rShop )
  {
    send_to_char( "You can't do that here.\r\n", ch );
    return NULL;
  }

  /*
   * Shop hours.
   */
  if( time_info.hour < rShop->open_hour )
  {
    char buf[MAX_STRING_LENGTH];
    snprintf( buf, MAX_STRING_LENGTH, "%s", "Sorry, come back later." );
    do_say( keeper, buf );
    return NULL;
  }

  if( time_info.hour > rShop->close_hour )
  {
    char buf[MAX_STRING_LENGTH];
    snprintf( buf, MAX_STRING_LENGTH, "%s", "Sorry, come back tomorrow." );
    do_say( keeper, buf );
    return NULL;
  }

  return keeper;
}

int get_cost( CHAR_DATA * ch, CHAR_DATA * keeper, OBJ_DATA * obj, bool fBuy )
{
  SHOP_DATA *pShop = NULL;
  int cost = 0;
  bool richcustomer = FALSE;
  int profitmod = 0;

  if( !obj || ( pShop = keeper->pIndexData->pShop ) == NULL )
    return 0;

  if( ch->gold > 1000 )
    richcustomer = TRUE;
  else
    richcustomer = FALSE;

  if( fBuy )
  {
    profitmod = 13 - get_curr_cha( ch ) + richcustomer ? 15 : 0;
    cost = ( int ) ( obj->cost
	* UMAX( ( pShop->profit_sell + 1 ),
	  pShop->profit_buy + profitmod ) ) / 100;
  }
  else
  {
    OBJ_DATA *obj2 = NULL;
    int itype = 0;

    profitmod = get_curr_cha( ch ) - 13 - ( richcustomer ? 15 : 0 );
    cost = 0;
    for( itype = 0; itype < MAX_TRADE; itype++ )
    {
      if( obj->item_type == pShop->buy_type[itype] )
      {
	cost = ( int ) ( obj->cost
	    * UMIN( ( pShop->profit_buy - 1 ),
	      pShop->profit_sell +
	      profitmod ) ) / 100;
	break;
      }
    }

    for( obj2 = keeper->first_carrying; obj2; obj2 = obj2->next_content )
    {
      if( obj->pIndexData == obj2->pIndexData
	  && obj->item_type != ITEM_RESOURCE
	  && obj->item_type != ITEM_CRYSTAL && obj->item_type != ITEM_FOOD
	  && obj->item_type != ITEM_RARE_METAL )
      {
	cost /= ( obj2->count + 1 );
	break;
      }
    }

    cost = UMIN( cost, 2500 );
  }

  if( obj->item_type == ITEM_DEVICE )
    cost = ( int ) ( cost * obj->value[2] / obj->value[1] );

  return cost;
}

int get_repaircost( CHAR_DATA * keeper, OBJ_DATA * obj )
{
  REPAIR_DATA *rShop = NULL;
  int cost = 0;
  int itype = 0;
  bool found = FALSE;

  if( !obj || ( rShop = keeper->pIndexData->rShop ) == NULL )
    return 0;

  for( itype = 0; itype < MAX_FIX; itype++ )
  {
    if( obj->item_type == rShop->fix_type[itype] )
    {
      cost = ( int ) ( obj->cost * rShop->profit_fix / 1000 );
      found = TRUE;
      break;
    }
  }

  if( !found )
    cost = -1;

  if( cost == 0 )
    cost = 1;

  if( found && cost > 0 )
  {
    switch ( obj->item_type )
    {
      case ITEM_ARMOR:
	if( obj->value[0] >= obj->value[1] )
	  cost = -2;
	else
	  cost *= ( obj->value[1] - obj->value[0] );
	break;
      case ITEM_WEAPON:
	if( INIT_WEAPON_CONDITION == obj->value[0] )
	  cost = -2;
	else
	  cost *= ( INIT_WEAPON_CONDITION - obj->value[0] );
	break;
      case ITEM_DEVICE:
	if( obj->value[2] >= obj->value[1] )
	  cost = -2;
	else
	  cost *= ( obj->value[1] - obj->value[2] );
    }
  }

  return cost;
}

void do_buy( CHAR_DATA * ch, char *argument )
{
  char arg[MAX_INPUT_LENGTH];
  int maxgold = 0;

  argument = one_argument( argument, arg );

  if( arg[0] == '\0' )
  {
    send_to_char( "Buy what?\r\n", ch );
    return;
  }

  /* in case of different shop types */
  {
    CHAR_DATA *keeper = NULL;
    OBJ_DATA *obj = NULL;
    int cost = 0;
    int noi = 1;		/* Number of items */
    short mnoi = 20;		/* Max number of items to be bought at once */

    if( ( keeper = find_keeper( ch ) ) == NULL )
      return;

    maxgold = keeper->top_level * 10;

    if( is_number( arg ) )
    {
      noi = atoi( arg );
      argument = one_argument( argument, arg );

      if( noi > mnoi )
      {
	act( AT_TELL, "$n tells you 'I don't sell that many items at"
	    " once.'", keeper, NULL, ch, TO_VICT );
	ch->reply = keeper;
	return;
      }
    }

    obj = get_obj_carry( keeper, arg );

    if( !obj && arg[0] == '#' )
    {
      int onum = 0, oref = atoi( arg + 1 );
      bool ofound = FALSE;

      for( obj = keeper->last_carrying; obj; obj = obj->prev_content )
      {
	if( obj->wear_loc == WEAR_NONE && can_see_obj( ch, obj ) )
	  onum++;

	if( onum == oref )
	{
	  ofound = TRUE;
	  break;
	}
	else if( onum > oref )
	  break;
      }
      if( !ofound )
	obj = NULL;
    }

    cost = ( get_cost( ch, keeper, obj, TRUE ) * noi );
    if( cost <= 0 || !can_see_obj( ch, obj ) )
    {
      act( AT_TELL, "$n tells you 'I don't sell that -- try 'list'.'",
	  keeper, NULL, ch, TO_VICT );
      ch->reply = keeper;
      return;
    }

    if( !IS_OBJ_STAT( obj, ITEM_INVENTORY ) && ( noi > 1 ) )
    {
      char buf[MAX_STRING_LENGTH];
      snprintf( buf, MAX_STRING_LENGTH, "%s", "laugh" );
      interpret( keeper, buf );
      act( AT_TELL, "$n tells you 'I don't have enough of those in stock"
	  " to sell more than one at a time.'", keeper, NULL, ch,
	  TO_VICT );
      ch->reply = keeper;
      return;
    }

    if( ch->gold < cost )
    {
      act( AT_TELL, "$n tells you 'You can't afford to buy $p.'",
	  keeper, obj, ch, TO_VICT );
      ch->reply = keeper;
      return;
    }

    if( IS_SET( obj->extra_flags, ITEM_PROTOTYPE ) && IS_IMMORTAL( ch ) )
    {
      act( AT_TELL,
	  "$n tells you 'This is a only a prototype!  I can't sell you that...'",
	  keeper, NULL, ch, TO_VICT );
      ch->reply = keeper;
      return;
    }

    if( ch->carry_number + get_obj_number( obj ) > can_carry_n( ch ) )
    {
      send_to_char( "You can't carry that many items.\r\n", ch );
      return;
    }

    if( ch->carry_weight + ( get_obj_weight( obj ) * noi )
	+ ( noi > 1 ? 2 : 0 ) > can_carry_w( ch ) )
    {
      send_to_char( "You can't carry that much weight.\r\n", ch );
      return;
    }

    if( noi == 1 )
    {
      if( !IS_OBJ_STAT( obj, ITEM_INVENTORY ) )
	separate_obj( obj );
      act( AT_ACTION, "$n buys $p.", ch, obj, NULL, TO_ROOM );
      act( AT_ACTION, "You buy $p.", ch, obj, NULL, TO_CHAR );
    }
    else
    {
      sprintf( arg, "$n buys %d $p%s.", noi,
	  ( obj->short_descr[strlen( obj->short_descr ) - 1] == 's'
	    ? "" : "s" ) );
      act( AT_ACTION, arg, ch, obj, NULL, TO_ROOM );
      sprintf( arg, "You buy %d $p%s.", noi,
	  ( obj->short_descr[strlen( obj->short_descr ) - 1] == 's'
	    ? "" : "s" ) );
      act( AT_ACTION, arg, ch, obj, NULL, TO_CHAR );
      act( AT_ACTION, "$N puts them into a bag and hands it to you.",
	  ch, NULL, keeper, TO_CHAR );
    }

    ch->gold -= cost;
    keeper->gold += cost;

    if( keeper->gold > maxgold )
    {
      keeper->gold = maxgold / 2;
      act( AT_ACTION, "$n puts some credits into a large safe.", keeper,
	  NULL, NULL, TO_ROOM );
    }

    if( IS_OBJ_STAT( obj, ITEM_INVENTORY ) )
    {
      OBJ_DATA *buy_obj = create_object( obj->pIndexData );
      OBJ_DATA *bag = NULL;

      /*
       * Due to grouped objects and carry limitations in SMAUG
       * The shopkeeper gives you a bag with multiple-buy,
       * and also, only one object needs be created with a count
       * set to the number bought.                -Thoric
       */
      if( noi > 1 )
      {
	bag = create_object( get_obj_index( OBJ_VNUM_SHOPPING_BAG ) );
	/* perfect size bag ;) */
	bag->value[0] = bag->weight + ( buy_obj->weight * noi );
	buy_obj->count = noi;
	obj->pIndexData->count += ( noi - 1 );
	numobjsloaded += ( noi - 1 );
	obj_to_obj( buy_obj, bag );
	obj_to_char( bag, ch );
      }
      else
	obj_to_char( buy_obj, ch );
    }
    else
    {
      obj_from_char( obj );
      obj_to_char( obj, ch );
    }

    return;
  }
}


void do_list( CHAR_DATA * ch, char *argument )
{
  char arg[MAX_INPUT_LENGTH];
  CHAR_DATA *keeper = NULL;
  OBJ_DATA *obj = NULL;
  int cost = 0;
  int oref = 0;
  bool found = FALSE;

  one_argument( argument, arg );

  if( ( keeper = find_keeper( ch ) ) == NULL )
    return;

  for( obj = keeper->last_carrying; obj; obj = obj->prev_content )
  {
    if( obj->wear_loc == WEAR_NONE && can_see_obj( ch, obj ) )
    {
      oref++;
      if( ( cost = get_cost( ch, keeper, obj, TRUE ) ) > 0
	  && ( arg[0] == '\0' || nifty_is_name( arg, obj->name ) ) )
      {
	if( !found )
	{
	  found = TRUE;
	  send_to_char( "[Price] {ref} Item\r\n", ch );
	}
	ch_printf( ch, "[%5d] {%3d} %s.\r\n",
	    cost, oref, capitalize( obj->short_descr ) );
      }
    }
  }

  if( !found )
  {
    if( arg[0] == '\0' )
      send_to_char( "You can't buy anything here.\r\n", ch );
    else
      send_to_char( "You can't buy that here.\r\n", ch );
  }
}


void do_sell( CHAR_DATA * ch, char *argument )
{
  char buf[MAX_STRING_LENGTH];
  char arg[MAX_INPUT_LENGTH];
  CHAR_DATA *keeper = NULL;
  OBJ_DATA *obj = NULL;
  int cost = 0;

  one_argument( argument, arg );

  if( arg[0] == '\0' )
  {
    send_to_char( "Sell what?\r\n", ch );
    return;
  }

  if( ( keeper = find_keeper( ch ) ) == NULL )
    return;

  if( ( obj = get_obj_carry( ch, arg ) ) == NULL )
  {
    act( AT_TELL, "$n tells you 'You don't have that item.'",
	keeper, NULL, ch, TO_VICT );
    ch->reply = keeper;
    return;
  }

  if( !can_drop_obj( ch, obj ) )
  {
    send_to_char( "You can't let go of it!\r\n", ch );
    return;
  }

  if( obj->timer > 0 )
  {
    act( AT_TELL,
	"$n tells you, '$p is depreciating in value too quickly...'",
	keeper, obj, ch, TO_VICT );
    return;
  }

  if( ( cost = get_cost( ch, keeper, obj, FALSE ) ) <= 0 )
  {
    act( AT_ACTION, "$n looks uninterested in $p.", keeper, obj, ch,
	TO_VICT );
    return;
  }

  if( cost > keeper->gold )
  {
    act( AT_TELL, "$n makes a credit transaction.", keeper, obj, ch,
	TO_VICT );
  }

  separate_obj( obj );
  act( AT_ACTION, "$n sells $p.", ch, obj, NULL, TO_ROOM );
  sprintf( buf, "You sell $p for %d credit%s.", cost, cost == 1 ? "" : "s" );
  act( AT_ACTION, buf, ch, obj, NULL, TO_CHAR );
  ch->gold += cost;
  keeper->gold -= cost;
  if( keeper->gold < 0 )
    keeper->gold = 0;

  if( obj->item_type == ITEM_TRASH )
    extract_obj( obj );
  else
  {
    obj_from_char( obj );
    obj_to_char( obj, keeper );
  }

  return;
}

void do_value( CHAR_DATA * ch, char *argument )
{
  char buf[MAX_STRING_LENGTH];
  CHAR_DATA *keeper = NULL;
  OBJ_DATA *obj = NULL;
  int cost = 0;

  if( argument[0] == '\0' )
  {
    send_to_char( "Value what?\r\n", ch );
    return;
  }

  if( ( keeper = find_keeper( ch ) ) == NULL )
    return;

  if( ( obj = get_obj_carry( ch, argument ) ) == NULL )
  {
    act( AT_TELL, "$n tells you 'You don't have that item.'",
	keeper, NULL, ch, TO_VICT );
    ch->reply = keeper;
    return;
  }

  if( !can_drop_obj( ch, obj ) )
  {
    send_to_char( "You can't let go of it!\r\n", ch );
    return;
  }

  if( ( cost = get_cost( ch, keeper, obj, FALSE ) ) <= 0 )
  {
    act( AT_ACTION, "$n looks uninterested in $p.", keeper, obj, ch,
	TO_VICT );
    return;
  }

  sprintf( buf, "$n tells you 'I'll give you %d credits for $p.'", cost );
  act( AT_TELL, buf, keeper, obj, ch, TO_VICT );
  ch->reply = keeper;

  return;
}

/*
 * Repair a single object. Used when handling "repair all" - Gorog
 */
void repair_one_obj( CHAR_DATA * ch, CHAR_DATA * keeper, OBJ_DATA * obj,
		     char *arg, int maxgold, char *fixstr, char *fixstr2 )
{
  char buf[MAX_STRING_LENGTH];
  int cost = 0;

  if( !can_drop_obj( ch, obj ) )
    ch_printf( ch, "You can't let go of %s.\r\n", obj->name );
  else if( ( cost = get_repaircost( keeper, obj ) ) < 0 )
  {
    if( cost != -2 )
      act( AT_TELL, "$n tells you, 'Sorry, I can't do anything with $p.'",
	  keeper, obj, ch, TO_VICT );
    else
      act( AT_TELL, "$n tells you, '$p looks fine to me!'", keeper, obj, ch,
	  TO_VICT );
  }
  /* "repair all" gets a 10% surcharge - Gorog */

  else if( ( cost = strcmp( "all", arg ) ? cost : 11 * cost / 10 ) >
      ch->gold )
  {
    sprintf( buf,
	"$N tells you, 'It will cost %d credit%s to %s %s...'", cost,
	cost == 1 ? "" : "s", fixstr, obj->name );
    act( AT_TELL, buf, ch, NULL, keeper, TO_CHAR );
    act( AT_TELL, "$N tells you, 'Which I see you can't afford.'", ch,
	NULL, keeper, TO_CHAR );
  }
  else
  {
    sprintf( buf, "$n gives $p to $N, who quickly %s it.", fixstr2 );
    act( AT_ACTION, buf, ch, obj, keeper, TO_ROOM );
    sprintf( buf, "$N charges you %d credit%s to %s $p.",
	cost, cost == 1 ? "" : "s", fixstr );
    act( AT_ACTION, buf, ch, obj, keeper, TO_CHAR );
    ch->gold -= cost;
    keeper->gold += cost;
    if( keeper->gold < 0 )
      keeper->gold = 0;
    else if( keeper->gold > maxgold )
    {
      keeper->gold = maxgold / 2;
      act( AT_ACTION, "$n puts some credits into a large safe.", keeper,
	  NULL, NULL, TO_ROOM );
    }

    switch ( obj->item_type )
    {
      default:
	send_to_char
	  ( "For some reason, you think you got ripped off...\r\n", ch );
	break;
      case ITEM_ARMOR:
	obj->value[0] = obj->value[1];
	break;
      case ITEM_WEAPON:
	obj->value[0] = INIT_WEAPON_CONDITION;
	break;
      case ITEM_DEVICE:
	obj->value[2] = obj->value[1];
	break;
    }

    oprog_repair_trigger( ch, obj );
  }
}

void do_repair( CHAR_DATA * ch, char *argument )
{
  CHAR_DATA *keeper = NULL;
  OBJ_DATA *obj = NULL;
  char fixstr[MAX_STRING_LENGTH];
  char fixstr2[MAX_STRING_LENGTH];
  int maxgold = 0;

  if( argument[0] == '\0' )
  {
    send_to_char( "Repair what?\r\n", ch );
    return;
  }

  if( ( keeper = find_fixer( ch ) ) == NULL )
    return;

  maxgold = keeper->top_level * 10;
  switch ( keeper->pIndexData->rShop->shop_type )
  {
    default:
    case SHOP_FIX:
      sprintf( fixstr, "%s", "repair" );
      sprintf( fixstr2, "%s", "repairs" );
      break;

    case SHOP_RECHARGE:
      sprintf( fixstr, "%s", "recharge" );
      sprintf( fixstr2, "%s", "recharges" );
      break;
  }

  if( !strcmp( argument, "all" ) )
  {
    for( obj = ch->first_carrying; obj; obj = obj->next_content )
    {
      if( obj->wear_loc == WEAR_NONE
	  && can_see_obj( ch, obj )
	  && ( obj->item_type == ITEM_ARMOR
	    || obj->item_type == ITEM_WEAPON
	    || obj->item_type == ITEM_DEVICE ) )
	repair_one_obj( ch, keeper, obj, argument, maxgold,
	    fixstr, fixstr2 );
    }
    return;
  }

  if( ( obj = get_obj_carry( ch, argument ) ) == NULL )
  {
    act( AT_TELL, "$n tells you 'You don't have that item.'",
	keeper, NULL, ch, TO_VICT );
    ch->reply = keeper;
    return;
  }

  repair_one_obj( ch, keeper, obj, argument, maxgold, fixstr, fixstr2 );
}

void appraise_all( CHAR_DATA * ch, CHAR_DATA * keeper, const char *fixstr )
{
  OBJ_DATA *obj = NULL;
  char buf[MAX_STRING_LENGTH], *pbuf = buf;
  int cost = 0, total = 0;

  for( obj = ch->first_carrying; obj != NULL; obj = obj->next_content )
  {
    if( obj->wear_loc == WEAR_NONE
	&& can_see_obj( ch, obj )
	&& ( obj->item_type == ITEM_ARMOR
	  || obj->item_type == ITEM_WEAPON
	  || obj->item_type == ITEM_DEVICE ) )
    {
      if( !can_drop_obj( ch, obj ) )
      {
	ch_printf( ch, "You can't let go of %s.\r\n", obj->name );
      }
      else if( ( cost = get_repaircost( keeper, obj ) ) < 0 )
      {
	if( cost != -2 )
	{
	  act( AT_TELL,
	      "$n tells you, 'Sorry, I can't do anything with $p.'",
	      keeper, obj, ch, TO_VICT );
	}
	else
	{
	  act( AT_TELL, "$n tells you, '$p looks fine to me!'",
	      keeper, obj, ch, TO_VICT );
	}
      }
      else
      {
	sprintf( buf,
	    "$N tells you, 'It will cost %d credit%s to %s %s'",
	    cost, cost == 1 ? "" : "s", fixstr, obj->name );
	act( AT_TELL, buf, ch, NULL, keeper, TO_CHAR );
	total += cost;
      }
    }
  }

  if( total > 0 )
  {
    send_to_char( "\r\n", ch );
    sprintf( buf,
	"$N tells you, 'It will cost %d credit%s in total.'",
	total, cost == 1 ? "" : "s" );
    act( AT_TELL, buf, ch, NULL, keeper, TO_CHAR );
    strcpy( pbuf,
	"$N tells you, 'Remember there is a 10% surcharge for repair all.'" );
    act( AT_TELL, buf, ch, NULL, keeper, TO_CHAR );
  }
}


void do_appraise( CHAR_DATA * ch, char *argument )
{
  char buf[MAX_STRING_LENGTH];
  char arg[MAX_INPUT_LENGTH];
  CHAR_DATA *keeper = NULL;
  OBJ_DATA *obj = NULL;
  int cost = 0;
  const char *fixstr = "";

  one_argument( argument, arg );

  if( arg[0] == '\0' )
  {
    send_to_char( "Appraise what?\r\n", ch );
    return;
  }

  if( ( keeper = find_fixer( ch ) ) == NULL )
    return;

  switch ( keeper->pIndexData->rShop->shop_type )
  {
    default:
    case SHOP_FIX:
      fixstr = "repair";
      break;
    case SHOP_RECHARGE:
      fixstr = "recharge";
      break;
  }

  if( !strcmp( arg, "all" ) )
  {
    appraise_all( ch, keeper, fixstr );
    return;
  }

  if( ( obj = get_obj_carry( ch, arg ) ) == NULL )
  {
    act( AT_TELL, "$n tells you 'You don't have that item.'",
	keeper, NULL, ch, TO_VICT );
    ch->reply = keeper;
    return;
  }

  if( !can_drop_obj( ch, obj ) )
  {
    send_to_char( "You can't let go of it.\r\n", ch );
    return;
  }

  if( ( cost = get_repaircost( keeper, obj ) ) < 0 )
  {
    if( cost != -2 )
      act( AT_TELL, "$n tells you, 'Sorry, I can't do anything with $p.'",
	  keeper, obj, ch, TO_VICT );
    else
      act( AT_TELL, "$n tells you, '$p looks fine to me!'", keeper, obj, ch,
	  TO_VICT );
    return;
  }

  sprintf( buf,
      "$N tells you, 'It will cost %d credit%s to %s that...'", cost,
      cost == 1 ? "" : "s", fixstr );
  act( AT_TELL, buf, ch, NULL, keeper, TO_CHAR );

  if( cost > ch->gold )
    act( AT_TELL, "$N tells you, 'Which I see you can't afford.'", ch,
	NULL, keeper, TO_CHAR );
}


/* ------------------ Shop Building and Editing Section ----------------- */


void do_makeshop( CHAR_DATA * ch, char *argument )
{
  SHOP_DATA *shop = NULL;
  short vnum = 0;
  MOB_INDEX_DATA *mob = NULL;

  if( !argument || argument[0] == '\0' )
  {
    send_to_char( "Usage: makeshop <mobvnum>\r\n", ch );
    return;
  }

  vnum = atoi( argument );

  if( ( mob = get_mob_index( vnum ) ) == NULL )
  {
    send_to_char( "Mobile not found.\r\n", ch );
    return;
  }

  if( !can_medit( ch, mob ) )
    return;

  if( mob->pShop )
  {
    send_to_char( "This mobile already has a shop.\r\n", ch );
    return;
  }

  CREATE( shop, SHOP_DATA, 1 );

  LINK( shop, first_shop, last_shop, next, prev );
  shop->keeper = vnum;
  shop->profit_buy = 120;
  shop->profit_sell = 90;
  shop->open_hour = 0;
  shop->close_hour = 23;
  mob->pShop = shop;
  send_to_char( "Done.\r\n", ch );
}

void do_shopset( CHAR_DATA * ch, char *argument )
{
  SHOP_DATA *shop = NULL;
  MOB_INDEX_DATA *mob = NULL, *mob2 = NULL;
  char arg1[MAX_INPUT_LENGTH];
  char arg2[MAX_INPUT_LENGTH];
  short vnum = 0;
  int value = 0;

  argument = one_argument( argument, arg1 );
  argument = one_argument( argument, arg2 );

  if( arg1[0] == '\0' || arg2[0] == '\0' )
  {
    send_to_char( "Usage: shopset <mob vnum> <field> value\r\n", ch );
    send_to_char( "\r\nField being one of:\r\n", ch );
    send_to_char
      ( "  buy0 buy1 buy2 buy3 buy4 buy sell open close keeper\r\n", ch );
    return;
  }

  vnum = atoi( arg1 );

  if( ( mob = get_mob_index( vnum ) ) == NULL )
  {
    send_to_char( "Mobile not found.\r\n", ch );
    return;
  }

  if( !can_medit( ch, mob ) )
    return;

  if( !mob->pShop )
  {
    send_to_char( "This mobile doesn't keep a shop.\r\n", ch );
    return;
  }

  shop = mob->pShop;
  value = atoi( argument );

  if( !str_cmp( arg2, "buy0" ) )
  {
    if( !is_number( argument ) )
      value = get_otype( argument );
    if( value < 0 || value > MAX_ITEM_TYPE )
    {
      send_to_char( "Invalid item type!\r\n", ch );
      return;
    }
    shop->buy_type[0] = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  if( !str_cmp( arg2, "buy1" ) )
  {
    if( !is_number( argument ) )
      value = get_otype( argument );
    if( value < 0 || value > MAX_ITEM_TYPE )
    {
      send_to_char( "Invalid item type!\r\n", ch );
      return;
    }
    shop->buy_type[1] = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  if( !str_cmp( arg2, "buy2" ) )
  {
    if( !is_number( argument ) )
      value = get_otype( argument );
    if( value < 0 || value > MAX_ITEM_TYPE )
    {
      send_to_char( "Invalid item type!\r\n", ch );
      return;
    }
    shop->buy_type[2] = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  if( !str_cmp( arg2, "buy3" ) )
  {
    if( !is_number( argument ) )
      value = get_otype( argument );
    if( value < 0 || value > MAX_ITEM_TYPE )
    {
      send_to_char( "Invalid item type!\r\n", ch );
      return;
    }
    shop->buy_type[3] = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  if( !str_cmp( arg2, "buy4" ) )
  {
    if( !is_number( argument ) )
      value = get_otype( argument );
    if( value < 0 || value > MAX_ITEM_TYPE )
    {
      send_to_char( "Invalid item type!\r\n", ch );
      return;
    }
    shop->buy_type[4] = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  if( !str_cmp( arg2, "buy" ) )
  {
    if( value <= ( shop->profit_sell + 5 ) || value > 1000 )
    {
      send_to_char( "Out of range.\r\n", ch );
      return;
    }
    shop->profit_buy = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  if( !str_cmp( arg2, "sell" ) )
  {
    if( value < 0 || value >= ( shop->profit_buy - 5 ) )
    {
      send_to_char( "Out of range.\r\n", ch );
      return;
    }
    shop->profit_sell = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  if( !str_cmp( arg2, "open" ) )
  {
    if( value < 0 || value > 23 )
    {
      send_to_char( "Out of range.\r\n", ch );
      return;
    }
    shop->open_hour = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  if( !str_cmp( arg2, "close" ) )
  {
    if( value < 0 || value > 23 )
    {
      send_to_char( "Out of range.\r\n", ch );
      return;
    }
    shop->close_hour = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  if( !str_cmp( arg2, "keeper" ) )
  {
    if( ( mob2 = get_mob_index( vnum ) ) == NULL )
    {
      send_to_char( "Mobile not found.\r\n", ch );
      return;
    }
    if( !can_medit( ch, mob ) )
      return;
    if( mob2->pShop )
    {
      send_to_char( "That mobile already has a shop.\r\n", ch );
      return;
    }
    mob->pShop = NULL;
    mob2->pShop = shop;
    shop->keeper = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  do_shopset( ch, STRLIT_EMPTY );
}


void do_shopstat( CHAR_DATA * ch, char *argument )
{
  SHOP_DATA *shop = NULL;
  MOB_INDEX_DATA *mob = NULL;
  short vnum = 0;

  if( argument[0] == '\0' )
  {
    send_to_char( "Usage: shopstat <keeper vnum>\r\n", ch );
    return;
  }

  vnum = atoi( argument );

  if( ( mob = get_mob_index( vnum ) ) == NULL )
  {
    send_to_char( "Mobile not found.\r\n", ch );
    return;
  }

  if( !mob->pShop )
  {
    send_to_char( "This mobile doesn't keep a shop.\r\n", ch );
    return;
  }
  shop = mob->pShop;

  ch_printf( ch, "Keeper: %d  %s\r\n", shop->keeper, mob->short_descr );
  ch_printf( ch, "buy0 [%s]  buy1 [%s]  buy2 [%s]  buy3 [%s]  buy4 [%s]\r\n",
      o_types[shop->buy_type[0]],
      o_types[shop->buy_type[1]],
      o_types[shop->buy_type[2]],
      o_types[shop->buy_type[3]], o_types[shop->buy_type[4]] );
  ch_printf( ch, "Profit:  buy %3d%%  sell %3d%%\r\n",
      shop->profit_buy, shop->profit_sell );
  ch_printf( ch, "Hours:   open %2d  close %2d\r\n",
      shop->open_hour, shop->close_hour );
}


void do_shops( CHAR_DATA * ch, char *argument )
{
  SHOP_DATA *shop = NULL;

  if( !first_shop )
  {
    send_to_char( "There are no shops.\r\n", ch );
    return;
  }

  set_char_color( AT_NOTE, ch );
  for( shop = first_shop; shop; shop = shop->next )
    ch_printf( ch,
	"Keeper: %5d Buy: %3d Sell: %3d Open: %2d Close: %2d Buy: %2d %2d %2d %2d %2d\r\n",
	shop->keeper, shop->profit_buy, shop->profit_sell,
	shop->open_hour, shop->close_hour, shop->buy_type[0],
	shop->buy_type[1], shop->buy_type[2], shop->buy_type[3],
	shop->buy_type[4] );
}


/* -------------- Repair Shop Building and Editing Section -------------- */


void do_makerepair( CHAR_DATA * ch, char *argument )
{
  REPAIR_DATA *repair = NULL;
  short vnum = 0;
  MOB_INDEX_DATA *mob = NULL;

  if( !argument || argument[0] == '\0' )
  {
    send_to_char( "Usage: makerepair <mobvnum>\r\n", ch );
    return;
  }

  vnum = atoi( argument );

  if( ( mob = get_mob_index( vnum ) ) == NULL )
  {
    send_to_char( "Mobile not found.\r\n", ch );
    return;
  }

  if( !can_medit( ch, mob ) )
    return;

  if( mob->rShop )
  {
    send_to_char( "This mobile already has a repair shop.\r\n", ch );
    return;
  }

  CREATE( repair, REPAIR_DATA, 1 );

  LINK( repair, first_repair, last_repair, next, prev );
  repair->keeper = vnum;
  repair->profit_fix = 100;
  repair->shop_type = SHOP_FIX;
  repair->open_hour = 0;
  repair->close_hour = 23;
  mob->rShop = repair;
  send_to_char( "Done.\r\n", ch );
}


void do_repairset( CHAR_DATA * ch, char *argument )
{
  REPAIR_DATA *repair = NULL;
  MOB_INDEX_DATA *mob = NULL, *mob2 = NULL;
  char arg1[MAX_INPUT_LENGTH];
  char arg2[MAX_INPUT_LENGTH];
  short vnum = 0;
  int value = 0;

  argument = one_argument( argument, arg1 );
  argument = one_argument( argument, arg2 );

  if( arg1[0] == '\0' || arg2[0] == '\0' )
  {
    send_to_char( "Usage: repairset <mob vnum> <field> value\r\n", ch );
    send_to_char( "\r\nField being one of:\r\n", ch );
    send_to_char( "  fix0 fix1 fix2 profit type open close keeper\r\n",
	ch );
    return;
  }

  vnum = atoi( arg1 );

  if( ( mob = get_mob_index( vnum ) ) == NULL )
  {
    send_to_char( "Mobile not found.\r\n", ch );
    return;
  }

  if( !can_medit( ch, mob ) )
    return;

  if( !mob->rShop )
  {
    send_to_char( "This mobile doesn't keep a repair shop.\r\n", ch );
    return;
  }
  repair = mob->rShop;
  value = atoi( argument );

  if( !str_cmp( arg2, "fix0" ) )
  {
    if( !is_number( argument ) )
      value = get_otype( argument );
    if( value < 0 || value > MAX_ITEM_TYPE )
    {
      send_to_char( "Invalid item type!\r\n", ch );
      return;
    }
    repair->fix_type[0] = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  if( !str_cmp( arg2, "fix1" ) )
  {
    if( !is_number( argument ) )
      value = get_otype( argument );
    if( value < 0 || value > MAX_ITEM_TYPE )
    {
      send_to_char( "Invalid item type!\r\n", ch );
      return;
    }
    repair->fix_type[1] = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  if( !str_cmp( arg2, "fix2" ) )
  {
    if( !is_number( argument ) )
      value = get_otype( argument );
    if( value < 0 || value > MAX_ITEM_TYPE )
    {
      send_to_char( "Invalid item type!\r\n", ch );
      return;
    }
    repair->fix_type[2] = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  if( !str_cmp( arg2, "profit" ) )
  {
    if( value < 1 || value > 1000 )
    {
      send_to_char( "Out of range.\r\n", ch );
      return;
    }
    repair->profit_fix = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  if( !str_cmp( arg2, "type" ) )
  {
    if( value < 1 || value > 2 )
    {
      send_to_char( "Out of range.\r\n", ch );
      return;
    }
    repair->shop_type = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  if( !str_cmp( arg2, "open" ) )
  {
    if( value < 0 || value > 23 )
    {
      send_to_char( "Out of range.\r\n", ch );
      return;
    }
    repair->open_hour = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  if( !str_cmp( arg2, "close" ) )
  {
    if( value < 0 || value > 23 )
    {
      send_to_char( "Out of range.\r\n", ch );
      return;
    }
    repair->close_hour = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  if( !str_cmp( arg2, "keeper" ) )
  {
    if( ( mob2 = get_mob_index( vnum ) ) == NULL )
    {
      send_to_char( "Mobile not found.\r\n", ch );
      return;
    }
    if( !can_medit( ch, mob ) )
      return;
    if( mob2->rShop )
    {
      send_to_char( "That mobile already has a repair shop.\r\n", ch );
      return;
    }
    mob->rShop = NULL;
    mob2->rShop = repair;
    repair->keeper = value;
    send_to_char( "Done.\r\n", ch );
    return;
  }

  do_repairset( ch, STRLIT_EMPTY );
}


void do_repairstat( CHAR_DATA * ch, char *argument )
{
  REPAIR_DATA *repair = NULL;
  MOB_INDEX_DATA *mob = NULL;
  short vnum = 0;

  if( argument[0] == '\0' )
  {
    send_to_char( "Usage: repairstat <keeper vnum>\r\n", ch );
    return;
  }

  vnum = atoi( argument );

  if( ( mob = get_mob_index( vnum ) ) == NULL )
  {
    send_to_char( "Mobile not found.\r\n", ch );
    return;
  }

  if( !mob->rShop )
  {
    send_to_char( "This mobile doesn't keep a repair shop.\r\n", ch );
    return;
  }
  repair = mob->rShop;

  ch_printf( ch, "Keeper: %d  %s\r\n", repair->keeper, mob->short_descr );
  ch_printf( ch, "fix0 [%s]  fix1 [%s]  fix2 [%s]\r\n",
      o_types[repair->fix_type[0]],
      o_types[repair->fix_type[1]], o_types[repair->fix_type[2]] );
  ch_printf( ch, "Profit: %3d%%  Type: %d\r\n",
      repair->profit_fix, repair->shop_type );
  ch_printf( ch, "Hours:   open %2d  close %2d\r\n",
      repair->open_hour, repair->close_hour );
}

void do_repairshops( CHAR_DATA * ch, char *argument )
{
  REPAIR_DATA *repair = NULL;

  if( !first_repair )
  {
    send_to_char( "There are no repair shops.\r\n", ch );
    return;
  }

  set_char_color( AT_NOTE, ch );
  for( repair = first_repair; repair; repair = repair->next )
    ch_printf( ch,
	"Keeper: %5d Profit: %3d Type: %d Open: %2d Close: %2d Fix: %2d %2d %2d\r\n",
	repair->keeper, repair->profit_fix, repair->shop_type,
	repair->open_hour, repair->close_hour, repair->fix_type[0],
	repair->fix_type[1], repair->fix_type[2] );
}
