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

function draw.SimpleTextOutlined( text, font, x, y, color, xalign, yalign, outlineWidth, outlineColor )
	local steps = math.max( 1, math.floor( outlineWidth or 1 ) )
	local oc = outlineColor or { r = 0, g = 0, b = 0, a = 255 }
	for dx = -steps, steps, steps do
		for dy = -steps, steps, steps do
			if ( dx ~= 0 or dy ~= 0 ) then
				draw.SimpleText( text, font, ( x or 0 ) + dx, ( y or 0 ) + dy, oc, xalign, yalign )
			end
		end
	end
	return draw.SimpleText( text, font, x, y, color, xalign, yalign )
end

function draw.DrawText( text, font, x, y, color, xalign )
	text = tostring( text or "" )
	font = font or "Default"
	x = x or 0
	y = y or 0

	surface.SetFont( font )
	local _, lineH = surface.GetTextSize( "W" )
	if ( lineH <= 0 ) then lineH = 14 end

	local curY = y
	for line in string.gmatch( text .. "\n", "(.-)\n" ) do
		draw.SimpleText( line, font, x, curY, color, xalign, TEXT_ALIGN_TOP )
		curY = curY + lineH
	end
end

function draw.RoundedBoxEx( borderSize, x, y, w, h, color, tl, tr, bl, br )
	x = math.floor( x or 0 )
	y = math.floor( y or 0 )
	w = math.floor( w or 0 )
	h = math.floor( h or 0 )
	if ( w <= 0 or h <= 0 ) then return end

	if ( color ~= nil ) then
		surface.SetDrawColor( color.r or 255, color.g or 255, color.b or 255, color.a or 255 )
	end

	local r = math.floor( borderSize or 0 )
	if ( r <= 0 ) then
		surface.DrawRect( x, y, w, h )
		return
	end

	r = math.min( r, math.floor( w * 0.5 ), math.floor( h * 0.5 ) )
	-- Center body between top and bottom corner bands
	if ( h > r * 2 ) then
		surface.DrawRect( x, y + r, w, h - r * 2 )
	end

	-- Scanline approximation for rounded corners
	for row = 0, r - 1 do
		local dy = r - row - 0.5
		local inset = r - math.floor( math.sqrt( math.max( 0, r * r - dy * dy ) ) + 0.5 )

		local topLeftInset  = tl and inset or 0
		local topRightInset = tr and inset or 0
		surface.DrawRect( x + topLeftInset, y + row, w - topLeftInset - topRightInset, 1 )

		local botLeftInset  = bl and inset or 0
		local botRightInset = br and inset or 0
		surface.DrawRect( x + botLeftInset, y + h - 1 - row, w - botLeftInset - botRightInset, 1 )
	end
end

function draw.RoundedBox( borderSize, x, y, w, h, color )
	draw.RoundedBoxEx( borderSize, x, y, w, h, color, true, true, true, true )
end

