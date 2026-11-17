#ifndef XGLUA_RESTORE_INVENTORY_H
#define XGLUA_RESTORE_INVENTORY_H

template <typename Object, typename Edict>
struct XGLuaRestoreMarker
{
	Object *object;
	Edict *edict;
	int serial;
};

template <typename Object, typename Edict>
static bool XGLua_MarkerMatches( const XGLuaRestoreMarker<Object, Edict> &marker,
	Object *object, Edict *edict, int serial, bool live )
{
	return live && marker.object==object && marker.edict==edict && marker.serial==serial;
}

template <typename Edict, typename Item, typename LiveFn, typename IsItemFn, typename GetItemFn, typename OwnedFn>
static int XGLua_CollectOwnedItems( Edict *edicts, int count, int first, Item **out, int capacity,
	LiveFn live, IsItemFn isItem, GetItemFn getItem, OwnedFn owned )
{
	if( !edicts || !out || capacity <= 0 || count <= first ) return 0;
	int found=0;
	for( int i=first; i<count && found<capacity; ++i )
	{
		Edict *edict=&edicts[i];
		if( !live( edict ) || !isItem( edict ) ) continue;
		Item *item=getItem( edict );
		if( item && owned( edict, item ) ) out[found++]=item;
	}
	return found;
}

// Rebuild linked inventory chains from validated live items. Never follow a
// serialized m_pNext pointer: a transition can free an item before all tables
// have restored, leaving the old raw link dangling.
template <typename Item, typename SlotFn>
static bool XGLua_RebuildItemChains( Item **items, int itemCount,
	Item **heads, int slotCount, Item *&active, Item *&last, SlotFn slotOf )
{
	Item *tails[32] = {};
	if( slotCount <= 0 || slotCount > 32 ) return false;
	for( int i = 0; i < slotCount; ++i ) heads[i] = NULL;
	bool activeFound = active == NULL;
	bool lastFound = last == NULL;
	for( int i = 0; i < itemCount; ++i )
	{
		Item *item = items[i];
		if( !item ) continue;
		int slot = slotOf( item );
		if( slot < 0 || slot >= slotCount ) continue;
		item->m_pNext = NULL;
		if( tails[slot] ) tails[slot]->m_pNext = item;
		else heads[slot] = item;
		tails[slot] = item;
		if( item == active ) activeFound = true;
		if( item == last ) lastFound = true;
	}
	if( !activeFound ) active = NULL;
	if( !lastFound ) last = NULL;
	return activeFound;
}

template <typename Item, typename IdFn>
static unsigned XGLua_BuildWeaponBits( Item **heads, int slotCount, unsigned preserveBits, IdFn idOf )
{
	unsigned bits = preserveBits;
	for( int slot = 0; slot < slotCount; ++slot )
		for( Item *item = heads[slot]; item; item = item->m_pNext )
		{
			const int id = idOf( item );
			if( id > 0 && id < 32 ) bits |= ( 1u << id );
		}
	return bits;
}

template <typename Item, typename MissingFn, typename RemoveFn>
static int XGLua_FilterMissingRestoreItems( Item **items, int count, Item *&active, Item *&last,
	bool &activeRemoved, MissingFn missing, RemoveFn remove )
{
	int write=0;
	for( int i=0; i<count; ++i )
	{
		Item *item=items[i];
		if( missing( item ) )
		{
			if( item==active ) { active=NULL; activeRemoved=true; }
			if( item==last ) last=NULL;
			remove( item );
			continue;
		}
		items[write++]=item;
	}
	return write;
}

#endif
