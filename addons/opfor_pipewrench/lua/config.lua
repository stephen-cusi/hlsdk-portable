-- Opposing Force pipe wrench; values match the installed skill defaults.
return {
 Weapon={PrintName="Pipe Wrench",ViewModel="models/v_pipe_wrench.mdl",WorldModel="models/w_pipe_wrench.mdl",PlayerModel="models/p_pipe_wrench.mdl",DeploySequence=3,AnimExtension="pipewrench",Slot=0,SlotPos=1,Weight=5,Primary={ClipSize=-1,DefaultClip=0,Ammo="none",Automatic=true},Secondary={ClipSize=-1,Ammo="none",Automatic=true}},
 Sounds={Miss={"weapons/pwrench_miss1.wav","weapons/pwrench_miss2.wav"},Body={"weapons/pwrench_hitbod1.wav","weapons/pwrench_hitbod2.wav","weapons/pwrench_hitbod3.wav"},Wall={"weapons/pwrench_hit1.wav","weapons/pwrench_hit2.wav"},HeavyBody={"weapons/pwrench_big_hitbod1.wav","weapons/pwrench_big_hitbod2.wav"},HeavyMiss="weapons/pwrench_big_miss.wav"},
 Damage=20,Range=32,ChargeMin=1.0,ChargeBase=25,ChargeMax=200,HeavyDelay=1.1,
}
