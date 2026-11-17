--[[---------------------------------------------------------------------------
	base gamemode - server
	GM:PlayerLoadout mirrors the exact hardcoded default set that
	CHalfLifeMultiplay::PlayerSpawn would have given (crowbar, 9mm pistol,
	68 rounds of 9mm with the native _9MM_MAX_CARRY limit of 250). Returning
	a strict true makes the C++ side skip its own hardcoded defaults, so the
	result is one copy of the vanilla loadout - never two. A map-provided
	game_player_equip keeps its priority and this function is never consulted.
-----------------------------------------------------------------------------]]

function GM:PlayerLoadout( ply )
	ply:Give( "weapon_crowbar" )
	ply:Give( "weapon_9mmhandgun" )
	ply:GiveAmmo( 68, "9mm", 250 )	-- count, ammo type, _9MM_MAX_CARRY
	return true
end
