-- Desert Eagle addon settings.
return {
 Weapon={PrintName="Desert Eagle",ViewModel="models/v_desert_eagle.mdl",WorldModel="models/w_desert_eagle.mdl",
  PlayerModel="models/p_desert_eagle.mdl",DeploySequence=9,AnimExtension="onehanded",Slot=1,SlotPos=2,Weight=15,
  Primary={ClipSize=7,DefaultClip=7,MaxAmmo=36,Ammo="357",Automatic=true},Secondary={ClipSize=-1,Ammo="none",Automatic=false}},
 Laser={Class="xgl_eagle_laser",Model="sprites/xgeagle_dot.spr"},
 Sounds={Fire="weapons/desert_eagle_fire.wav",On="weapons/desert_eagle_sight.wav",Off="weapons/desert_eagle_sight2.wav",
  Precache={"weapons/desert_eagle_fire.wav","weapons/desert_eagle_reload.wav","weapons/desert_eagle_sight.wav","weapons/desert_eagle_sight2.wav"}},
 Required={"models/v_desert_eagle.mdl","models/w_desert_eagle.mdl","models/p_desert_eagle.mdl","sprites/xgeagle_dot.spr",
  "sound/weapons/desert_eagle_fire.wav","sound/weapons/desert_eagle_reload.wav","sound/weapons/desert_eagle_sight.wav","sound/weapons/desert_eagle_sight2.wav"},
 Damage=34, SpreadLaser=0.001, SpreadHip=0.1, DelayLaser=0.5, DelayHip=0.22,
}
