-- Vanilla runtime: reuse the engine's activation evaluator without enabling
-- core's administration, test-spawner or experimental-module event handlers.
local mp = require("mp")
local activation = require('core')
local surf = require('surf_commands')
local vehicle = require('vehicle_commands')

local function normalizeCellId(cell)
    local x,y=tostring(cell or ''):match('^[Ee][Xx][Tt]:%s*(-?%d+)%s*,%s*(-?%d+)$')
    if x and y then return string.format('EXT:%d,%d',tonumber(x),tonumber(y)) end
    return cell
end
local function placeAtPosition(player,distance,direction)
    local p=player.position
    local result={x=p.x,y=p.y,z=p.z,rx=p.rx or 0,ry=p.ry or 0,rz=p.rz or 0}
    local yaw=result.rz
    local forward={x=math.sin(yaw),y=math.cos(yaw)}
    if direction==0 then result.x=result.x+forward.x*distance;result.y=result.y+forward.y*distance
    elseif direction==1 then result.x=result.x-forward.x*distance;result.y=result.y-forward.y*distance
    elseif direction==2 then result.x=result.x-forward.y*distance;result.y=result.y+forward.x*distance
    elseif direction==3 then result.x=result.x+forward.y*distance;result.y=result.y-forward.x*distance end
    return result
end
local environment={commandPrefix='/',normalizeCellId=normalizeCellId,placeAtPosition=placeAtPosition,
    isAdmin=function() return false end,
    requireAdmin=function(player) player:sendMessage('This command requires a server operator.');return false end,
    findPlayerByName=function(name)
        for _,p in ipairs(mp.getPlayers()) do if p.name==name then return p end end
    end}

return {
    interfaceName = "IntentPolicy",
    interface = {
        evaluateIntent = activation.interface.evaluateIntent,
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
    eventHandlers={
        Activate=activation.eventHandlers.Activate,
        OnDoorState=activation.eventHandlers.OnDoorState,
        OnPlayerDisconnect=surf.onPlayerDisconnect,
        OnPlayerSendMessage=function(data)
            local player=mp.getPlayer(data.guid)
            if not player then return false end
            if vehicle.handleChat(player,data,environment)~=nil then return false end
            if surf.handleChat(player,data,environment)~=nil then return false end
            if data.message=='/help' then
                player:sendMessage('Vanilla: /vehicle spawn, /vehicle on|off, /surf help. Activate a keyboard for FETCHER STUDIO.')
                return false
            end
            mp.relayChat(data.guid,data.message or '')
            return false
        end,
    },
}
