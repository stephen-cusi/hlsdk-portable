-- M249 addon settings.
return {
 Weapon={PrintName="M249 SAW",ViewModel="models/v_saw.mdl",WorldModel="models/w_saw.mdl",PlayerModel="models/p_saw.mdl",
  DeploySequence=5,AnimExtension="mp5",Slot=2,SlotPos=2,Weight=15,
  Primary={ClipSize=50,DefaultClip=50,MaxAmmo=200,Ammo="556",Automatic=true},Secondary={ClipSize=-1,Ammo="none",Automatic=false}},
 AmmoPickup={Class="xgl_ammo_556",Model="models/w_saw_clip.mdl",Sound="items/xgm249_ammo.wav",Amount=50},
 Models={Shell="models/saw_shell.mdl",Link="models/saw_link.mdl"},
 Sounds={Fire={"weapons/saw_fire1.wav","weapons/saw_fire2.wav","weapons/saw_fire3.wav"},
  Precache={"weapons/saw_fire1.wav","weapons/saw_fire2.wav","weapons/saw_fire3.wav","weapons/saw_reload.wav","weapons/saw_reload2.wav"}},
 Required={"models/v_saw.mdl","models/w_saw.mdl","models/p_saw.mdl","models/saw_shell.mdl","models/saw_link.mdl",
  "sound/weapons/saw_fire1.wav","sound/weapons/saw_fire2.wav","sound/weapons/saw_fire3.wav","sound/weapons/saw_reload.wav","sound/weapons/saw_reload2.wav"},
 Damage=15, FireDelay=0.067, SpreadSingle=0.05234, SpreadMulti=0.02618,
 Reload={OpenAnim=2,OpenDelay=1.33,CloseAnim=3,CloseDelay=2.4},
}
