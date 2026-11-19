--[[---------------------------------------------------------------------------
	draw.lua -- GMod-compatible high-level 2D drawing helpers (CLIENT only)
-----------------------------------------------------------------------------]]

if ( not CLIENT ) then return end
if ( XGLUA_MODULE_DRAW ) then return end
XGLUA_MODULE_DRAW = true

TEXT_ALIGN_LEFT   = 0
TEXT_ALIGN_CENTER = 1
TEXT_ALIGN_RIGHT  = 2
TEXT_ALIGN_TOP    = 3
TEXT_ALIGN_BOTTOM = 4

draw = draw or {}

function draw.SimpleText( text, font, x, y, color, xalign, yalign )
	text = tostring( text or "" )
	font = font or "Default"
	x = x or 0
	y = y or 0

	surface.SetFont( font )
	local w, h = surface.GetTextSize( text )

	if ( xalign == TEXT_ALIGN_CENTER ) then
		x = x - w * 0.5
	elseif ( xalign == TEXT_ALIGN_RIGHT ) then
		x = x - w
	end

	if ( yalign == TEXT_ALIGN_CENTER ) then
		y = y - h * 0.5
	elseif ( yalign == TEXT_ALIGN_BOTTOM ) then
		y = y - h
	end

	if ( color ~= nil ) then
		surface.SetTextColor( color.r or 255, color.g or 255, color.b or 255, color.a or 255 )
	else
		surface.SetTextColor( 255, 255, 255, 255 )
	end

	surface.SetTextPos( math.floor( x ), math.floor( y ) )
	surface.DrawText( text )
	return w, h
end

function draw.RoundedBox( borderSize, x, y, w, h, color )
	-- v1: solid blended rectangle (corner rounding can be added in v2)
	if ( color ~= nil ) then
		surface.SetDrawColor( color.r or 255, color.g or 255, color.b or 255, color.a or 255 )
	end
	surface.DrawRect( x, y, w, h )
end
