function includeAddon(name,path) return assert(loadfile("../../addons/"..name.."/lua/"..path))() end
SERVER=true
SWEP={}
file={Exists=function() return true end}
game={AddAmmoType=function() end}
Vector=function(x,y,z) return {x=x,y=y,z=z} end
weapons={Register=function() end}
local registered={}
scripted_ents={Register=function(t,n) registered[n]=t end}
IsValid=function(x) return x ~= nil end
assert(loadfile('../../addons/opfor_m249/lua/weapons/weapon_m249.lua'))()
ENT={}
assert(loadfile('../../addons/opfor_m249/lua/entities/xgl_ammo_556.lua'))()
local box=ENT
assert(box.Model=='models/w_saw_clip.mdl')
local removed, sounds, count=0,0,100
local ent=setmetatable({Remove=function() removed=removed+1 end, EmitSound=function() sounds=sounds+1 end},{__index=box})
local player={IsPlayer=function() return true end,GetAmmoCount=function() return count end,GiveAmmo=function(_,n,name,max) assert(n==50 and name=='556' and max==200); count=math.min(count+n,max) end}
box.Touch(ent,player)
assert(count==150 and removed==1 and sounds==1)
count=199; removed=0; sounds=0
box.Touch(ent,player)
assert(count==200 and removed==1 and sounds==1)
count=200; removed=0; sounds=0
box.Touch(ent,player)
assert(count==200 and removed==0 and sounds==0)
box.Touch(ent,{IsPlayer=function() return false end})
assert(removed==0)
print('M249 ammo pickup: partial, capped, full and non-player checks passed')
