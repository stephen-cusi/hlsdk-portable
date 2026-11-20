-- Opposing Force combat knife; values match the installed skill defaults.
return {
 Weapon={PrintName="Combat Knife",ViewModel="models/v_knife.mdl",WorldModel="models/w_knife.mdl",PlayerModel="models/p_knife.mdl",DeploySequence=1,AnimExtension="knife",Slot=0,SlotPos=2,Weight=5,Primary={ClipSize=-1,DefaultClip=0,Ammo="none",Automatic=true},Secondary={ClipSize=-1,Ammo="none",Automatic=false}},
 Sounds={Miss={"weapons/knife1.wav","weapons/knife2.wav","weapons/knife3.wav"},Body={"weapons/knife_hit_flesh1.wav","weapons/knife_hit_flesh2.wav"},Wall={"weapons/knife_hit_wall1.wav","weapons/knife_hit_wall2.wav"}},
 Damage=10,Range=32,
}
