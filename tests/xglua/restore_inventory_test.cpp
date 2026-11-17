#include <cstdio>
#include <cstring>
#include "xglua_restore_inventory.h"

struct FakeItem { FakeItem *m_pNext; int slot, id, owner, missing; };
struct FakeEdict { int free, isWeapon, owner; FakeItem *privateData; };

int main()
{
	FakeItem playerA={0,1,2,1,0}, playerB={0,2,5,1,0}, missing={0,1,17,1,1};
	FakeItem boxA={0,2,3,9,0}, boxMissing={0,1,19,9,1}, ground={0,1,4,0,0}, freed={0,1,20,9,0};
	FakeEdict edicts[]={{0,1,1,&playerA},{0,1,1,&playerB},{0,1,1,&missing},
		{0,1,9,&boxA},{0,1,9,&boxMissing},{0,1,0,&ground},{1,1,9,&freed},{0,0,1,0}};
	int getItemCalls=0, classifyCalls=0;
	FakeItem *playerItems[8]={};
	int playerCount=XGLua_CollectOwnedItems( edicts, 8, 0, playerItems, 8,
		[&](FakeEdict *e){ return !e->free; },
		[&](FakeEdict *e){ ++classifyCalls; return e->isWeapon!=0; },
		[&](FakeEdict *e){ ++getItemCalls; return e->privateData; },
		[](FakeEdict *e,FakeItem *i){ return e->owner==1 && i->owner==1; } );
	if(playerCount!=3 || getItemCalls!=6 || classifyCalls!=7) return 1; // freed entity never dereferenced/classified
	// Filter missing scripts after safely collecting live private data.
	FakeItem *heads[4]={&freed,0,0,0};
	FakeItem *active=&missing,*last=&playerB;
	bool activeRemoved=false;
	playerCount=XGLua_FilterMissingRestoreItems(playerItems,playerCount,active,last,activeRemoved,
		[](FakeItem *i){return i->missing!=0;},[](FakeItem *i){i->m_pNext=0;});
	if(!activeRemoved || active!=0 || last!=&playerB || playerCount!=2) return 2;
	bool activeFound=XGLua_RebuildItemChains(playerItems,playerCount,heads,4,active,last,
		[](FakeItem *i){return i->slot;});
	if(!activeFound || active!=0 || last!=&playerB || heads[1]!=&playerA || playerA.m_pNext!=0 ||
		heads[2]!=&playerB || playerB.m_pNext!=0 || heads[0]!=0) return 2;
	unsigned bits=XGLua_BuildWeaponBits(heads,4,1u<<31,[](FakeItem *i){return i->id;});
	if(bits!=((1u<<31)|(1u<<2)|(1u<<5))) return 3;

	FakeItem *boxItems[4]={};
	int boxCount=XGLua_CollectOwnedItems(edicts,8,0,boxItems,4,
		[](FakeEdict *e){return !e->free;},[](FakeEdict *e){return e->isWeapon!=0;},
		[](FakeEdict *e){return e->privateData;},[](FakeEdict *e,FakeItem *i){return e->owner==9 && i->owner==9;});
	if(boxCount!=2 || boxItems[0]!=&boxA || boxItems[1]!=&boxMissing) return 4;
	FakeItem *boxHeads[4]={}; active=0; last=0;
	boxCount=XGLua_FilterMissingRestoreItems(boxItems,boxCount,active,last,activeRemoved,
		[](FakeItem *i){return i->missing!=0;},[](FakeItem *i){i->m_pNext=0;});
	if(boxCount!=1 || boxItems[0]!=&boxA || !XGLua_RebuildItemChains(boxItems,boxCount,boxHeads,4,active,last,[](FakeItem *i){return i->slot;}) ||
		boxHeads[2]!=&boxA || boxA.m_pNext!=0) return 5;

	XGLuaRestoreMarker<FakeItem,FakeEdict> marker={&playerA,&edicts[0],7};
	if(!XGLua_MarkerMatches(marker,&playerA,&edicts[0],7,true) ||
		XGLua_MarkerMatches(marker,&playerA,&edicts[0],8,true) ||
		XGLua_MarkerMatches(marker,&playerA,&edicts[0],7,false)) return 6;
	std::puts("restore inventory scan, unlink, reindex, and marker tests passed");
	return 0;
}
