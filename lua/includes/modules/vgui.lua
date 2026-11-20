--[[---------------------------------------------------------------------------
	vgui.lua -- Pure-Lua lightweight UI panel tree (CLIENT only, M7-7C)
	Provides GMod-compatible vgui.Register / vgui.Create and Panel base class
	driven automatically by Think and HUDPaint hooks.
-----------------------------------------------------------------------------]]

if ( not CLIENT ) then return end
if ( XGLUA_MODULE_VGUI ) then return end
XGLUA_MODULE_VGUI = true

vgui = vgui or {}

local Classes   = {}
local RootList  = {}
local Hovered   = nil
local Pressed   = nil
local LastMouseDown = false

--===========================================================================
-- Base Panel implementation
--===========================================================================

local PanelBase = {}
PanelBase.__index = PanelBase

function PanelBase:Init()
	self.x = 0
	self.y = 0
	self.w = 64
	self.h = 24
	self.visible = true
	self.mouseEnabled = true
	self.text = ""
	self.parent = nil
	self.children = {}
	self.valid = true
end

function PanelBase:IsValid()
	return self.valid == true
end

function PanelBase:SetPos( x, y )
	self.x = math.floor( x or 0 )
	self.y = math.floor( y or 0 )
end

function PanelBase:GetPos()
	return self.x, self.y
end

function PanelBase:SetSize( w, h )
	self.w = math.max( 0, math.floor( w or 0 ) )
	self.h = math.max( 0, math.floor( h or 0 ) )
end

function PanelBase:GetSize()
	return self.w, self.h
end

function PanelBase:SetWide( w ) self.w = math.max( 0, math.floor( w or 0 ) ) end
function PanelBase:SetTall( h ) self.h = math.max( 0, math.floor( h or 0 ) ) end
function PanelBase:GetWide() return self.w end
function PanelBase:GetTall() return self.h end

function PanelBase:Center()
	local pw = self.parent and self.parent.w or ScrW()
	local ph = self.parent and self.parent.h or ScrH()
	self:SetPos( ( pw - self.w ) * 0.5, ( ph - self.h ) * 0.5 )
end

function PanelBase:SetVisible( b )
	self.visible = ( b == true )
end

function PanelBase:IsVisible()
	return self.visible == true
end

function PanelBase:SetMouseInputEnabled( b )
	self.mouseEnabled = ( b == true )
end

function PanelBase:SetText( str )
	self.text = tostring( str or "" )
end

function PanelBase:GetText()
	return self.text or ""
end

function PanelBase:GetParent()
	return self.parent
end

function PanelBase:GetChildren()
	return self.children
end

function PanelBase:SetParent( parent )
	if ( self.parent == parent ) then return end
	if ( self.parent ) then
		for i = #self.parent.children, 1, -1 do
			if ( self.parent.children[ i ] == self ) then
				table.remove( self.parent.children, i )
			end
		end
	else
		for i = #RootList, 1, -1 do
			if ( RootList[ i ] == self ) then
				table.remove( RootList, i )
			end
		end
	end

	self.parent = parent
	if ( parent ) then
		table.insert( parent.children, self )
	else
		table.insert( RootList, self )
	end
end

function PanelBase:LocalToScreen( lx, ly )
	local sx = ( lx or 0 ) + self.x
	local sy = ( ly or 0 ) + self.y
	local p = self.parent
	while ( p ) do
		sx = sx + p.x
		sy = sy + p.y
		p = p.parent
	end
	return sx, sy
end

function PanelBase:IsHovered()
	return Hovered == self
end

function PanelBase:Remove()
	if ( not self.valid ) then return end
	for i = #self.children, 1, -1 do
		self.children[ i ]:Remove()
	end
	self.children = {}
	if ( Hovered == self ) then Hovered = nil end
	if ( Pressed == self ) then Pressed = nil end
	self:SetParent( nil )
	for i = #RootList, 1, -1 do
		if ( RootList[ i ] == self ) then
			table.remove( RootList, i )
		end
	end
	self.valid = false
end

function PanelBase:Paint( w, h )
	-- Default subtle dark box; override in derived panels
	draw.RoundedBox( 4, 0, 0, w, h, Color( 24, 28, 36, 220 ) )
end

function PanelBase:Think() end
function PanelBase:OnMousePressed( mouseCode ) end
function PanelBase:OnMouseReleased( mouseCode ) end
function PanelBase:OnCursorEntered() end
function PanelBase:OnCursorExited() end
function PanelBase:DoClick() end

Classes[ "Panel" ] = PanelBase

-- Built-in DButton convenience control
local ButtonBase = setmetatable( {}, { __index = PanelBase } )
ButtonBase.__index = ButtonBase

function ButtonBase:Paint( w, h )
	local bg = self:IsHovered() and Color( 58, 110, 165, 240 ) or Color( 40, 48, 60, 230 )
	draw.RoundedBox( 4, 0, 0, w, h, bg )
	if ( self.text and self.text ~= "" ) then
		draw.SimpleText( self.text, "Default", w * 0.5, h * 0.5, Color( 255, 255, 255, 255 ), TEXT_ALIGN_CENTER, TEXT_ALIGN_CENTER )
	end
end

function ButtonBase:OnMouseReleased( code )
	if ( code == ( MOUSE_LEFT or 107 ) and self:IsHovered() ) then
		self:DoClick()
	end
end

Classes[ "DButton" ] = ButtonBase

--===========================================================================
-- Registry & factory
--===========================================================================

function vgui.Register( name, tbl, baseName )
	if ( type( name ) ~= "string" or name == "" or type( tbl ) ~= "table" ) then
		error( "vgui.Register: expected (name, table[, baseName])" )
	end
	local parentMeta = Classes[ baseName or "Panel" ] or PanelBase
	tbl.__index = tbl
	tbl.BaseClass = parentMeta
	setmetatable( tbl, { __index = parentMeta } )
	Classes[ name ] = tbl
	return tbl
end

function vgui.Create( classname, parent )
	local meta = Classes[ classname ] or PanelBase
	local pnl = setmetatable( {}, meta )
	PanelBase.Init( pnl )
	if ( pnl.Init ~= PanelBase.Init ) then
		pnl:Init()
	end
	if ( parent and parent.IsValid and parent:IsValid() ) then
		pnl:SetParent( parent )
	else
		table.insert( RootList, pnl )
	end
	return pnl
end

function vgui.GetHoveredPanel()
	return Hovered
end

--===========================================================================
-- Hit-testing, Think & Paint traversal
--===========================================================================

local function HitTestPanel( pnl, mx, my, ox, oy )
	if ( not pnl.valid or not pnl.visible ) then return nil end
	local sx = ox + pnl.x
	local sy = oy + pnl.y
	if ( mx < sx or my < sy or mx >= sx + pnl.w or my >= sy + pnl.h ) then
		return nil
	end
	for i = #pnl.children, 1, -1 do
		local hit = HitTestPanel( pnl.children[ i ], mx, my, sx, sy )
		if ( hit ) then return hit end
	end
	if ( pnl.mouseEnabled ) then
		return pnl
	end
	return nil
end

local function ThinkPanel( pnl )
	if ( not pnl.valid or not pnl.visible ) then return end
	pnl:Think()
	for i = 1, #pnl.children do
		if ( pnl.children[ i ] ) then
			ThinkPanel( pnl.children[ i ] )
		end
	end
end

local function PaintPanel( pnl, ox, oy )
	if ( not pnl.valid or not pnl.visible ) then return end
	local sx = ox + pnl.x
	local sy = oy + pnl.y

	-- Translate surface.SetTextPos / DrawRect via lightweight origin wrapper
	local origRect = surface.DrawRect
	local origTextPos = surface.SetTextPos
	surface.DrawRect = function( rx, ry, rw, rh )
		origRect( sx + rx, sy + ry, rw, rh )
	end
	surface.SetTextPos = function( tx, ty )
		origTextPos( sx + tx, sy + ty )
	end

	local ok, err = pcall( pnl.Paint, pnl, pnl.w, pnl.h )
	surface.DrawRect = origRect
	surface.SetTextPos = origTextPos
	if ( not ok ) then
		error( err, 0 )
	end

	for i = 1, #pnl.children do
		if ( pnl.children[ i ] ) then
			PaintPanel( pnl.children[ i ], sx, sy )
		end
	end
end

function XGLua_VGUI_Step( mx, my, mouseDown )
	local hit = nil
	for i = #RootList, 1, -1 do
		hit = HitTestPanel( RootList[ i ], mx, my, 0, 0 )
		if ( hit ) then break end
	end

	if ( hit ~= Hovered ) then
		if ( Hovered and Hovered.valid ) then Hovered:OnCursorExited() end
		Hovered = hit
		if ( Hovered and Hovered.valid ) then Hovered:OnCursorEntered() end
	end

	local code = MOUSE_LEFT or 107
	if ( mouseDown and not LastMouseDown ) then
		Pressed = Hovered
		if ( Pressed and Pressed.valid ) then
			Pressed:OnMousePressed( code )
		end
	elseif ( not mouseDown and LastMouseDown ) then
		if ( Pressed and Pressed.valid ) then
			Pressed:OnMouseReleased( code )
		end
		Pressed = nil
	end
	LastMouseDown = mouseDown

	for i = 1, #RootList do
		if ( RootList[ i ] ) then
			ThinkPanel( RootList[ i ] )
		end
	end
end

if ( hook and hook.Add ) then
	hook.Add( "Think", "XGLua_VGUI_Think", function()
		if ( #RootList == 0 ) then return end
		local mx, my = input.GetCursorPos()
		local down = input.IsMouseDown( MOUSE_LEFT or 107 )
		XGLua_VGUI_Step( mx, my, down )
	end )

	hook.Add( "HUDPaint", "XGLua_VGUI_Paint", function()
		for i = 1, #RootList do
			if ( RootList[ i ] ) then
				PaintPanel( RootList[ i ], 0, 0 )
			end
		end
	end )
end
