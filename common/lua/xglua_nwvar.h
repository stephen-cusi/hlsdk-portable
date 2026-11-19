#ifndef XGLUA_NWVAR_H
#define XGLUA_NWVAR_H

// Shared networked entity variable store & wire codec (M4 Phase 4A).
// Zero engine/Lua dependencies; no exceptions, no RTTI.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "lua/xglua_netcodec.h"

enum XGLuaNWType
{
	XGLUA_NW_NONE   = 0,
	XGLUA_NW_BOOL   = 1,
	XGLUA_NW_INT    = 2,
	XGLUA_NW_FLOAT  = 3,
	XGLUA_NW_STRING = 4,
	XGLUA_NW_VECTOR = 5,
	XGLUA_NW_ANGLE  = 6,
	XGLUA_NW_ENTITY = 7
};

enum XGLuaNWOp
{
	XGLUA_NW_OP_END       = 0,
	XGLUA_NW_OP_SET       = 1,
	XGLUA_NW_OP_CLEAR_ENT = 2
};

struct XGLuaNWValue
{
	uint8_t type;
	bool b;
	int32_t i;
	float f;
	float v[3];
	uint16_t entIdx;
	std::string s;

	XGLuaNWValue() : type( XGLUA_NW_NONE ), b( false ), i( 0 ), f( 0.0f ), entIdx( 0 )
	{
		v[0] = v[1] = v[2] = 0.0f;
	}

	static XGLuaNWValue MakeBool( bool val ) { XGLuaNWValue x; x.type = XGLUA_NW_BOOL; x.b = val; return x; }
	static XGLuaNWValue MakeInt( int32_t val ) { XGLuaNWValue x; x.type = XGLUA_NW_INT; x.i = val; return x; }
	static XGLuaNWValue MakeFloat( float val ) { XGLuaNWValue x; x.type = XGLUA_NW_FLOAT; x.f = val; return x; }
	static XGLuaNWValue MakeString( const char *val ) { XGLuaNWValue x; x.type = XGLUA_NW_STRING; x.s = val ? val : ""; return x; }
	static XGLuaNWValue MakeVector( float x0, float y0, float z0 )
	{
		XGLuaNWValue x; x.type = XGLUA_NW_VECTOR; x.v[0] = x0; x.v[1] = y0; x.v[2] = z0; return x;
	}
	static XGLuaNWValue MakeAngle( float p, float y, float r )
	{
		XGLuaNWValue x; x.type = XGLUA_NW_ANGLE; x.v[0] = p; x.v[1] = y; x.v[2] = r; return x;
	}
	static XGLuaNWValue MakeEntity( uint16_t idx ) { XGLuaNWValue x; x.type = XGLUA_NW_ENTITY; x.entIdx = idx; return x; }

	bool Equals( const XGLuaNWValue &o ) const
	{
		if( type != o.type ) return false;
		switch( type )
		{
		case XGLUA_NW_BOOL:   return b == o.b;
		case XGLUA_NW_INT:    return i == o.i;
		case XGLUA_NW_FLOAT:  return f == o.f;
		case XGLUA_NW_STRING: return s == o.s;
		case XGLUA_NW_VECTOR:
		case XGLUA_NW_ANGLE:  return v[0] == o.v[0] && v[1] == o.v[1] && v[2] == o.v[2];
		case XGLUA_NW_ENTITY: return entIdx == o.entIdx;
		default:              return true;
		}
	}
};

struct XGLuaNWEntry
{
	uint16_t entIndex;
	std::string key;
	XGLuaNWValue val;
	bool dirty;
};

struct XGLuaNWStore
{
	std::vector<XGLuaNWEntry> entries;
	std::vector<uint16_t> clearedEnts;

	void Reset()
	{
		entries.clear();
		clearedEnts.clear();
	}

	// Returns true if the value changed and was marked dirty.
	bool Set( uint16_t entIndex, const char *key, const XGLuaNWValue &val, bool markDirty = true )
	{
		if( !key || !key[0] ) return false;
		for( size_t i = 0; i < entries.size(); ++i )
		{
			if( entries[i].entIndex == entIndex && entries[i].key == key )
			{
				if( entries[i].val.Equals( val ) )
					return false;
				entries[i].val = val;
				if( markDirty ) entries[i].dirty = true;
				return true;
			}
		}
		XGLuaNWEntry e;
		e.entIndex = entIndex;
		e.key = key;
		e.val = val;
		e.dirty = markDirty;
		entries.push_back( e );
		return true;
	}

	const XGLuaNWValue *Get( uint16_t entIndex, const char *key ) const
	{
		if( !key || !key[0] ) return NULL;
		for( size_t i = 0; i < entries.size(); ++i )
		{
			if( entries[i].entIndex == entIndex && entries[i].key == key )
				return &entries[i].val;
		}
		return NULL;
	}

	void ClearEntity( uint16_t entIndex, bool recordDelta = true )
	{
		bool hadAny = false;
		for( size_t i = 0; i < entries.size(); )
		{
			if( entries[i].entIndex == entIndex )
			{
				entries[i] = entries.back();
				entries.pop_back();
				hadAny = true;
			}
			else
			{
				++i;
			}
		}
		if( hadAny && recordDelta )
		{
			for( size_t i = 0; i < clearedEnts.size(); ++i )
				if( clearedEnts[i] == entIndex ) return;
			clearedEnts.push_back( entIndex );
		}
	}

	static bool WriteEntry( XGLuaNetWriter &w, const XGLuaNWEntry &e )
	{
		w.WriteUInt( XGLUA_NW_OP_SET, 2 );
		w.WriteUInt( e.entIndex, 16 );
		w.WriteString( e.key.c_str() );
		w.WriteUInt( e.val.type, 4 );
		switch( e.val.type )
		{
		case XGLUA_NW_BOOL:   w.WriteBool( e.val.b ); break;
		case XGLUA_NW_INT:    w.WriteInt( e.val.i, 32 ); break;
		case XGLUA_NW_FLOAT:  w.WriteFloat( e.val.f ); break;
		case XGLUA_NW_STRING: w.WriteString( e.val.s.c_str() ); break;
		case XGLUA_NW_VECTOR:
		case XGLUA_NW_ANGLE:  w.WriteVec3( e.val.v[0], e.val.v[1], e.val.v[2] ); break;
		case XGLUA_NW_ENTITY: w.WriteUInt( e.val.entIdx, 16 ); break;
		default: break;
		}
		return !w.overflow;
	}

	// Encodes cleared entities + dirty entries and resets dirty flags.
	// Returns true if at least one op was written.
	bool EncodeDelta( XGLuaNetWriter &w )
	{
		bool any = false;
		for( size_t i = 0; i < clearedEnts.size(); ++i )
		{
			w.WriteUInt( XGLUA_NW_OP_CLEAR_ENT, 2 );
			w.WriteUInt( clearedEnts[i], 16 );
			any = true;
		}
		clearedEnts.clear();

		for( size_t i = 0; i < entries.size(); ++i )
		{
			if( !entries[i].dirty ) continue;
			if( !WriteEntry( w, entries[i] ) ) break;
			entries[i].dirty = false;
			any = true;
		}
		if( any )
			w.WriteUInt( XGLUA_NW_OP_END, 2 );
		return any && !w.overflow;
	}

	// Encodes full snapshot for newly connected clients (does not clear dirty).
	bool EncodeSnapshot( XGLuaNetWriter &w ) const
	{
		if( entries.empty() ) return false;
		for( size_t i = 0; i < entries.size(); ++i )
		{
			if( !WriteEntry( w, entries[i] ) ) break;
		}
		w.WriteUInt( XGLUA_NW_OP_END, 2 );
		return !w.overflow;
	}

	// Applies a received delta or snapshot packet into this store.
	bool ApplyPacket( XGLuaNetReader &r )
	{
		while( r.BitsLeft() >= 2 )
		{
			uint32_t op = r.ReadUInt( 2 );
			if( r.underflow || op == XGLUA_NW_OP_END )
				break;
			if( op == XGLUA_NW_OP_CLEAR_ENT )
			{
				uint16_t entIdx = (uint16_t)r.ReadUInt( 16 );
				if( r.underflow ) return false;
				ClearEntity( entIdx, false );
			}
			else if( op == XGLUA_NW_OP_SET )
			{
				uint16_t entIdx = (uint16_t)r.ReadUInt( 16 );
				char key[128] = { 0 };
				if( !r.ReadString( key, sizeof( key ) ) ) return false;
				uint8_t type = (uint8_t)r.ReadUInt( 4 );
				if( r.underflow ) return false;

				XGLuaNWValue val;
				val.type = type;
				switch( type )
				{
				case XGLUA_NW_BOOL:   val.b = r.ReadBool(); break;
				case XGLUA_NW_INT:    val.i = r.ReadInt( 32 ); break;
				case XGLUA_NW_FLOAT:  val.f = r.ReadFloat(); break;
				case XGLUA_NW_STRING:
				{
					char buf[1024] = { 0 };
					if( !r.ReadString( buf, sizeof( buf ) ) ) return false;
					val.s = buf;
					break;
				}
				case XGLUA_NW_VECTOR:
				case XGLUA_NW_ANGLE:
					if( !r.ReadVec3( &val.v[0], &val.v[1], &val.v[2] ) ) return false;
					break;
				case XGLUA_NW_ENTITY: val.entIdx = (uint16_t)r.ReadUInt( 16 ); break;
				default: return false;
				}
				if( r.underflow ) return false;
				Set( entIdx, key, val, false );
			}
			else
			{
				return false;
			}
		}
		return true;
	}
};

#endif // XGLUA_NWVAR_H
