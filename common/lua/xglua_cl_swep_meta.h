#ifndef XGLUA_CL_SWEP_META_H
#define XGLUA_CL_SWEP_META_H

#include <cstdint>
#include <cstring>
#include <vector>
#include "lua/xglua_netcodec.h"

#define XGLUA_CL_SWEP_MAX 64

struct XGLuaCLSWEPMeta
{
	int id;
	char classname[64];
	int slot;
	int slotpos;
	bool primaryAutomatic;
	bool secondaryAutomatic;
	int clipsize;
};

struct XGLuaCLSWEPRegistry
{
	std::vector<XGLuaCLSWEPMeta> items;

	void Clear()
	{
		items.clear();
	}

	void Upsert( const XGLuaCLSWEPMeta &meta )
	{
		if( !meta.classname[0] || meta.id <= 0 )
			return;
		for( size_t i = 0; i < items.size(); ++i )
		{
			if( items[i].id == meta.id || std::strcmp( items[i].classname, meta.classname ) == 0 )
			{
				items[i] = meta;
				return;
			}
		}
		if( items.size() < XGLUA_CL_SWEP_MAX )
			items.push_back( meta );
	}

	const XGLuaCLSWEPMeta *FindById( int id ) const
	{
		if( id <= 0 ) return NULL;
		for( size_t i = 0; i < items.size(); ++i )
		{
			if( items[i].id == id )
				return &items[i];
		}
		return NULL;
	}

	const XGLuaCLSWEPMeta *FindByClass( const char *classname ) const
	{
		if( !classname || !classname[0] ) return NULL;
		for( size_t i = 0; i < items.size(); ++i )
		{
			if( std::strcmp( items[i].classname, classname ) == 0 )
				return &items[i];
		}
		return NULL;
	}

	bool EncodeAll( XGLuaNetWriter &w ) const
	{
		uint32_t count = (uint32_t)items.size();
		w.WriteUInt( count, 16 );
		for( size_t i = 0; i < items.size(); ++i )
		{
			const XGLuaCLSWEPMeta &m = items[i];
			w.WriteUInt( (uint32_t)m.id, 16 );
			w.WriteString( m.classname );
			w.WriteUInt( (uint32_t)( m.slot < 0 ? 0 : m.slot ), 8 );
			w.WriteUInt( (uint32_t)( m.slotpos < 0 ? 0 : m.slotpos ), 8 );
			w.WriteBool( m.primaryAutomatic );
			w.WriteBool( m.secondaryAutomatic );
			w.WriteInt( (int32_t)m.clipsize, 16 );
		}
		return !w.overflow;
	}

	bool DecodeAll( XGLuaNetReader &r, bool replaceExisting = true )
	{
		uint32_t count = r.ReadUInt( 16 );
		if( r.underflow || count > XGLUA_CL_SWEP_MAX )
			return false;
		if( replaceExisting )
			items.clear();
		for( uint32_t i = 0; i < count; ++i )
		{
			XGLuaCLSWEPMeta m;
			std::memset( &m, 0, sizeof( m ) );
			m.id = (int)r.ReadUInt( 16 );
			if( !r.ReadString( m.classname, sizeof( m.classname ) ) )
				return false;
			m.slot = (int)r.ReadUInt( 8 );
			m.slotpos = (int)r.ReadUInt( 8 );
			m.primaryAutomatic = r.ReadBool();
			m.secondaryAutomatic = r.ReadBool();
			m.clipsize = (int)r.ReadInt( 16 );
			if( r.underflow )
				return false;
			Upsert( m );
		}
		return true;
	}
};

#endif // XGLUA_CL_SWEP_META_H
