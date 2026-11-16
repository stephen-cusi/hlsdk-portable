--[[---------------------------------------------------------
    Grenade Pistol (手雷手枪) — XGLua demo SWEP
    ---------------------------------------------------------
    运行环境: XGLua (HL1/GoldSrc 引擎上的 GMod 风格 Lua 系统)
    语法: LuaJIT 5.1 (GLua 方言: != && || ! // continue 均可用,
          但本文件为了可读性统一使用标准 5.1 写法)

    服务端权威: 所有 SWEP 逻辑只在 SERVER 上运行,
    C++ 侧的 CLuaWeapon : CBasePlayerWeapon 会把虚函数
    (PrimaryAttack / SecondaryAttack / Reload / Deploy / Holster ...)
    转发到这张 SWEP 表里同名函数。

    对应 GMod API 对照 (教学注释):
      GMod 的 weapons.Register   → 这里同名, 注册进 SWEP 仓库
      GMod 的 SWEP:PrimaryAttack → 同名, 引擎开火时回调
      GMod 的 util.ThrowGrenade  → 这里绑定到 HL1 的
          CGrenade::ShootContact / ShootTimed
      GMod 的 ply:GetAimVector / EyePos → 同名
      GMod 的 wep:SetClip1 / Clip1 / TakePrimaryAmmo → 同名
-----------------------------------------------------------]]

-- SWEP 元数据 ------------------------------------------------

SWEP.PrintName  = "Grenade Pistol"            -- 对应 GMod SWEP.PrintName (HUD 名字)
SWEP.Author     = "XGLua Team"                -- 对应 GMod SWEP.Author
SWEP.Purpose    = "Launch contact-detonated grenades (LMB) or timed grenades (RMB)."
SWEP.Instructions = "LMB: contact grenade\nRMB: 3s timed grenade\nR: reload (1 round)"
SWEP.Slot       = 2                           -- 对应 GMod SWEP.Slot (第 3 类武器槽)
SWEP.SlotPos    = 1                           -- 对应 GMod SWEP.SlotPos
SWEP.Spawnable  = true                        -- 对应 GMod SWEP.Spawnable
SWEP.ClassName  = "weapon_grenadepistol"      -- 注册用的类名, C++ 侧 Give() 按这个名字查找

-- 模型 (C++ Deploy() 会用这两个路径调用 SetModel) -----------
SWEP.ViewModel  = "models/v_9mmhandgun.mdl"   -- 对应 GMod SWEP.ViewModel
SWEP.WorldModel = "models/w_9mmhandgun.mdl"   -- 对应 GMod SWEP.WorldModel

-- 弹药定义 ---------------------------------------------------
-- 子表要自己先创建 (GMod 惯例: SWEP 文件里显式声明 Primary/Secondary)
SWEP.Primary   = {}
SWEP.Secondary = {}

-- 用 HL1 的 "9mm" 弹药类型简化处理:
--   Clip1 弹匣 = 1 发, 备弹由 GiveAmmo(6, "9mm") 提供
SWEP.Primary.ClipSize   = 1    -- 对应 GMod SWEP.Primary.ClipSize
SWEP.Primary.Ammo       = "9mm" -- 对应 GMod SWEP.Primary.Ammo (HL1 弹药名)
SWEP.Primary.Automatic  = false
SWEP.Primary.Delay      = 0.5  -- 两发之间的间隔 (秒)

SWEP.Secondary.ClipSize  = -1  -- 副攻击不消耗弹匣 (定时手雷也吃弹匣, 见 SecondaryAttack)
SWEP.Secondary.Ammo      = "9mm"
SWEP.Secondary.Automatic = false
SWEP.Secondary.Delay     = 0.7

SWEP.ReloadTime = 2.0          -- 装填用时 (秒), Reload() 里用

--[[---------------------------------------------------------
    Initialize: SWEP 实体刚创建时调用一次
    (对应 GMod SWEP:Initialize)
    这里不需要额外设置, SetHoldType 之类 XGLua 暂未绑定。
-----------------------------------------------------------]]
function SWEP:Initialize()
    -- 预留: 未来可在这里 SetHoldType("pistol")
end

--[[---------------------------------------------------------
    Deploy: 玩家把武器掏出来时调用
    (对应 GMod SWEP:Deploy) 必须返回 true 表示掏枪成功
-----------------------------------------------------------]]
function SWEP:Deploy()
    -- 掏枪后立刻可以开火
    self:SetNextPrimaryFire(CurTime() + 0.4)
    self:SetNextSecondaryFire(CurTime() + 0.4)
    return true
end

--[[---------------------------------------------------------
    Holster: 玩家收起/切换武器时调用
    (对应 GMod SWEP:Holster) 返回 true 允许收起
-----------------------------------------------------------]]
function SWEP:Holster()
    return true
end

--[[---------------------------------------------------------
    PrimaryAttack: 主攻击 — 发射一颗"接触爆炸"手雷
    (对应 GMod SWEP:PrimaryAttack)

    流程:
      1. 校验持有者
      2. 取瞄准方向 GetAimVector()
      3. 在眼睛前方 16 单位处生成手雷 (避免和玩家碰撞盒重叠)
      4. 初速 = 瞄准方向 * 900 + 向上 100 (模拟一点抛物线)
      5. 弹匣 -1, 播放开火动画与音效, 设置下次开火时间
-----------------------------------------------------------]]
function SWEP:PrimaryAttack()
    local owner = self:GetOwner()

    -- Owner 校验: 武器掉在地上/没有持有者时不能开火
    if not IsValid(owner) or not owner:IsPlayer() or not owner:Alive() then
        return
    end

    -- 弹匣空了: 播放空仓音效并尝试装填 (对应 GMod 惯用写法)
    if self:Clip1() <= 0 then
        self:PlayEmptySound()   -- XGLua 内部实现好的 (空仓 "咔哒" 声)
        self:Reload()
        return
    end

    -- 瞄准方向与出手位置 (对应 GMod ply:GetAimVector / EyePos)
    local aim = owner:GetAimVector()
    local pos = owner:EyePos() + aim * 16

    -- 发射手雷 (对应 GMod util.ThrowGrenade)
    --   参数: thrower, pos, vel, contact
    --   contact = true  → CGrenade::ShootContact (碰到东西就炸)
    --   contact = false → CGrenade::ShootTimed   (定时爆炸)
    local vel = aim * 900 + Vector(0, 0, 100)
    local ent = util.ThrowGrenade(owner, pos, vel, true)
    if IsValid(ent) then
        ent:SetOwner(owner)  -- 归属: 防止立刻炸到发射者自身
    end

    -- 消耗一发弹匣 (对应 GMod wep:TakePrimaryAmmo)
    self:TakePrimaryAmmo(1)

    -- 播放开火动画
    -- 注意: 这里传的是模型动画序号, 不是 GMod 的 ACT_VM_* 枚举。
    -- 序号 3 取自 v_9mmhandgun.mdl 的开火帧, 如果换成别的模型
    -- 或动画看起来不对, 用模型查看器确认后调整这个数字。
    self:SendWeaponAnim(3)

    -- 开火音效 (HL1 手枪/手雷投掷音, 直接用路径)
    self:EmitSound("weapons/pl_gun3.wav", 0.8)

    -- 下次可开火时间 (对应 GMod wep:SetNextPrimaryFire)
    local nextFire = CurTime() + self.Primary.Delay
    self:SetNextPrimaryFire(nextFire)
    self:SetNextSecondaryFire(nextFire)

    -- 弹匣打空且还有备弹 → 自动装填
    if self:Clip1() <= 0 and owner:GetAmmoCount(self.Primary.Ammo) > 0 then
        self:Reload()
    end
end

--[[---------------------------------------------------------
    SecondaryAttack: 副攻击 — 发射一颗"定时爆炸"手雷 (3 秒)
    (对应 GMod SWEP:SecondaryAttack)

    和主攻击的区别:
      - contact = false → CGrenade::ShootTimed, 3 秒后自动爆炸
      - 初速低一些 (700), 抛物线更明显
-----------------------------------------------------------]]
function SWEP:SecondaryAttack()
    local owner = self:GetOwner()

    if not IsValid(owner) or not owner:IsPlayer() or not owner:Alive() then
        return
    end

    if self:Clip1() <= 0 then
        self:PlayEmptySound()
        self:Reload()
        return
    end

    local aim = owner:GetAimVector()
    local pos = owner:EyePos() + aim * 16

    -- 定时手雷: contact = false, C++ 侧 ShootTimed 默认 3 秒引信
    local vel = aim * 700 + Vector(0, 0, 100)
    local ent = util.ThrowGrenade(owner, pos, vel, false)
    if IsValid(ent) then
        ent:SetOwner(owner)
    end

    self:TakePrimaryAmmo(1)
    self:SendWeaponAnim(3)  -- 沿用开火动画, 序号说明见 PrimaryAttack
    self:EmitSound("weapons/pl_gun3.wav", 0.8)

    -- 副攻击有自己独立的冷却时间
    local nextFire = CurTime() + self.Secondary.Delay
    self:SetNextSecondaryFire(nextFire)
    self:SetNextPrimaryFire(nextFire)

    if self:Clip1() <= 0 and owner:GetAmmoCount(self.Primary.Ammo) > 0 then
        self:Reload()
    end
end

--[[---------------------------------------------------------
    Reload: 手动装填
    (对应 GMod SWEP:Reload)
    XGLua v1 没有绑定 DefaultReload, 所以在 Lua 里自己实现:
      条件: 弹匣 < 1 且 备弹 > 0
      动作: 播放装填动画(序号 5, 可调) → 补 1 发 → 扣 1 发备弹
      期间: 主/副攻击冷却到 CurTime() + ReloadTime (2 秒)
-----------------------------------------------------------]]
function SWEP:Reload()
    -- 防止装填期间反复触发 (下次开火时间未到就不重复装)
    if self:GetNextPrimaryFire() > CurTime() then
        return
    end

    local owner = self:GetOwner()
    if not IsValid(owner) or not owner:IsPlayer() then
        return
    end

    if self:Clip1() >= 1 then return end  -- 弹匣还是满的

    local reserve = owner:GetAmmoCount(self.Primary.Ammo)
    if reserve <= 0 then return end       -- 没备弹可装

    -- 装填动画: 序号 5 是 v_9mmhandgun.mdl 的上弹帧, 可调
    self:SendWeaponAnim(5)

    -- 补弹: 弹匣 +1, 备弹 -1 (对应 GMod SetClip1 / TakePrimaryAmmo)
    self:SetClip1(1)
    self:TakePrimaryAmmo(1)

    -- 装填期间不能开火 (2 秒)
    self:SetNextPrimaryFire(CurTime() + self.ReloadTime)
    self:SetNextSecondaryFire(CurTime() + self.ReloadTime)
end

--[[---------------------------------------------------------
    注册: 只在服务端注册一次
    (对应 GMod weapons.Register(SWEP, class))
    注册后 ents.Create("weapon_grenadepistol") 和
    ply:Give("weapon_grenadepistol") 就能找到这张表。
-----------------------------------------------------------]]
if SERVER then
    weapons.Register(SWEP, "weapon_grenadepistol")
end
