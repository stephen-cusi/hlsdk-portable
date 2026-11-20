-- M40A1 addon settings; paths are relative to the mounted game filesystem.
return {
 Weapon = {
  PrintName="M40A1 Sniper Rifle", ViewModel="models/v_m40a1.mdl",
  WorldModel="models/w_m40a1.mdl", PlayerModel="models/p_m40a1.mdl",
  DeploySequence=0, AnimExtension="bow", Slot=5, SlotPos=2, Weight=10,
  Primary={ClipSize=5, DefaultClip=5, MaxAmmo=15, Ammo="762", Automatic=true},
  Secondary={ClipSize=-1, Ammo="none", Automatic=false},
 },
 AmmoPickup={Class="xgl_ammo_762", Model="models/w_m40a1clip.mdl", Sound="items/xgm40_ammo.wav", Amount=5},
 Sounds={Fire="weapons/sniper_fire.wav", Zoom="weapons/sniper_zoom.wav",
  Precache={"weapons/sniper_bolt1.wav","weapons/sniper_bolt2.wav","weapons/sniper_fire.wav",
   "weapons/sniper_reload_first_seq.wav","weapons/sniper_reload_second_seq.wav","weapons/sniper_reload3.wav","weapons/sniper_zoom.wav"}},
 Required={"models/v_m40a1.mdl","models/w_m40a1.mdl","models/p_m40a1.mdl",
  "sound/weapons/sniper_bolt1.wav","sound/weapons/sniper_bolt2.wav","sound/weapons/sniper_fire.wav",
  "sound/weapons/sniper_reload_first_seq.wav","sound/weapons/sniper_reload_second_seq.wav","sound/weapons/sniper_reload3.wav","sound/weapons/sniper_zoom.wav"},
 Damage=100, FireDelay=1.75, Spread=0.001, ZoomFOV=15,
}
