-- Reuse shared chat/admin and activation behavior with Vanilla's explicit
-- module list. Keep the existing health endpoint and Studio deployment intact.
local mp = require("mp")
local core = require("core")

return {
    interfaceName = "IntentPolicy",
    interface = {
        evaluateIntent = core.interface.evaluateIntent,
        handleAdminUiHttp = function(request)
            if request and request.action == "health" then
                local tables = type(mp.listDatabaseTables) == "function"
                    and (mp.listDatabaseTables() or {}) or {}
                return {
                    status = 200,
                    contentType = "application/json; charset=utf-8",
                    body = string.format(
                        '{"ok":true,"playerCount":%d,"uptime":%.3f,"worldHour":%.3f,"tableCount":%d}',
                        mp.getPlayerCount(), mp.getUptime(), mp.getWorldTime(), #tables),
                }
            end
            return {
                status = 404,
                body = '{"ok":false,"error":"vanilla_admin_api_not_configured"}',
            }
        end,
    },
    eventHandlers = {
        OnServerInit = core.eventHandlers.OnServerInit,
        OnPlayerConnect = core.eventHandlers.OnPlayerConnect,
        OnPlayerDisconnect = core.eventHandlers.OnPlayerDisconnect,
        OnPlayerSendMessage = core.eventHandlers.OnPlayerSendMessage,
        SurfTimerTrigger = core.eventHandlers.SurfTimerTrigger,
        Activate = core.eventHandlers.Activate,
        OnDoorState = core.eventHandlers.OnDoorState,
    },
}
