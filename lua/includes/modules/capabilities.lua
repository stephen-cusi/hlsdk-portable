-- Realm-local service registry. It does not download or load addons.
-- Register shared helpers independently from any gamemode; callers must
-- handle missing services. A service exists only in the realm registering it.
if XGLUA_MODULE_CAPABILITIES then return end
XGLUA_MODULE_CAPABILITIES = true
local entries = {}
capabilities = capabilities or {}
function capabilities.Register(name, version, service)
    assert(type(name) == "string" and name:match("^[%w_.-]+$") and #name <= 64, "invalid capability name")
    assert(type(version) == "number" and version >= 1 and version % 1 == 0, "invalid capability version")
    assert(service ~= nil, "capability service required")
    assert(entries[name] == nil, "capability already registered: " .. name)
    entries[name] = { version = version, service = service }
end
function capabilities.Get(name, minimum)
    local entry = entries[name]
    if not entry or entry.version < (minimum or 1) then return nil end
    return entry.service, entry.version
end
function capabilities.Has(name, minimum)
    return capabilities.Get(name, minimum) ~= nil
end
