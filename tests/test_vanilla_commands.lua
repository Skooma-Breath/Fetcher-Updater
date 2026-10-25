-- lua tests/test_vanilla_commands.lua [path-to-openmw]
local engine = arg[1] or '../openmw'
package.path = 'launcher/server/vanilla/?.lua;' .. engine .. '/apps/openmw-server/scripts/?.lua;' .. package.path
local checks = 0
local function check(value, message)
    checks = checks + 1
    assert(value, message)
end
local messages, broadcasts, relayed, placed, runtime, physics = {}, {}, {}, {}, {}, {}
local uptime = 65
local player = {guid=1, name='Operator', cell='ToddTest', position={x=10,y=20,z=30,rz=0}}
function player:sendMessage(message) messages[#messages+1] = message end
function player:setNickname(name) self.nickname = name end
function player:getNickname() return self.nickname or '' end
local players = {[1]=player}
package.loaded.config = {ADMIN_PASSWORD='test-only-password', ANNOUNCE_JOIN_LEAVE=false}
package.loaded['openmw.types'] = {}
package.loaded.intent_policy = {
    registerIntent=function() end,
    evaluate=function() return {accepted=true} end,
}
package.loaded.recordstore = {
    onServerInit=function() end,
    handleChat=function() return nil end,
}
package.loaded.mp = {
    storage={LIFE_TIME={GameSession=1},globalSection=function()
        return {
            setLifeTime=function() end,
            set=function(_, k, v) runtime[k]=v end,
            reset=function() runtime={} end,
        }
    end},
    getPlayer=function(guid) return players[guid] end,
    getPlayers=function() return {player} end,
    getPlayerCount=function() return 1 end,
    getUptime=function() return uptime end,
    getWorldTime=function() return 8.5 end,
    broadcast=function(text) broadcasts[#broadcasts+1]=text end,
    relayChat=function(guid,text) relayed[#relayed+1]={guid,text} end,
    log=function() end,
    setCellPhysics=function(cell, settings) physics[cell]=settings end,
    setGlobalPhysics=function() return true end,
    clearPlayerPhysics=function() end,
    listDatabaseTables=function() return {'accounts','characters'} end,
    getPlayerMarks=function() return {} end,
    placeObject=function(ref,count,cell,position)
        placed[#placed+1]={ref=ref,count=count,cell=cell,position=position}; return true
    end,
}
local adapter = require('vanilla_runtime')
local function chat(text, guid)
    messages={}
    return adapter.eventHandlers.OnPlayerSendMessage({guid=guid or 1,message=text})
end
local function output() return table.concat(messages,'\n') end
adapter.eventHandlers.OnServerInit({})
check(physics.surf_mesa_mw ~= nil, 'shared travel setup initializes')
check(chat('/f') == false and #broadcasts == 1 and broadcasts[1]:find('pays respects',1,true), '/f broadcasts')
chat('/who'); check(output():find('Operator',1,true), '/who lists players')
chat('/list'); check(output():find('[1]',1,true), '/list includes player IDs')
chat('/time'); check(output():find('08:30',1,true), '/time works')
chat('/uptime'); check(output():find('01m 05s',1,true), '/uptime works')
chat('/help')
check(output():find('/f',1,true) and output():find('/marks',1,true), 'help includes common commands')
check(not output():find('/placeat',1,true), 'non-admin help hides admin commands')
check(not output():find('/bc',1,true) and not output():find('/spawner',1,true), 'help excludes unsupported modules')
chat('/placeat "daedric mace" 2 64 0')
check(#placed == 0 and output():find('Permission denied',1,true), 'placement requires admin')
chat('/login wrong'); check(runtime['1'] == nil and #placed == 0, 'incorrect password rejected')
chat('/login test-only-password'); check(runtime['1'] == true, 'configured admin login succeeds')
chat('/help'); check(output():find('/placeat',1,true) and not output():find('/spawner',1,true), 'admin help is profile-filtered')
chat('/placeat "daedric mace" 2 64 0')
check(#placed == 1 and placed[1].ref == 'daedric mace' and placed[1].count == 2, 'quoted placement preserves ref/count')
check(placed[1].cell == 'ToddTest' and placed[1].position.y == 84, 'placement uses player cell and distance')
chat('/placeat iron_dagger 0'); check(#placed == 1, 'invalid counts rejected')
chat('/nick Performer'); check(player.nickname == 'Performer', 'nickname works without experimental modules')
chat('/marks'); check(output():find('No saved marks',1,true), 'mark commands dispatch')
chat('/speechhelp'); check(#messages > 0, 'speech helper loads its real data dependencies')
chat('/bccommunity on'); check(output():find('Unknown command',1,true), 'unsupported feature blocked before dispatch')
chat('/not-a-command'); check(output():find('Unknown command',1,true), 'unknown command handled without nil module errors')
chat('hello'); check(#relayed == 1 and relayed[1][2] == 'hello', 'ordinary chat relays exactly once')
check(chat('/f',999) == false and #broadcasts == 1, 'unknown sender ignored')
adapter.eventHandlers.OnPlayerDisconnect({guid=1,name=player.name})
chat('/placeat iron_dagger'); check(#placed == 1 and runtime['1'] == nil, 'disconnect removes admin privilege')
local health = adapter.interface.handleAdminUiHttp({action='health'})
check(health.status == 200 and health.body:find('"ok":true',1,true), 'existing health endpoint preserved')
check(adapter.interface.handleAdminUiHttp({action='other'}).status == 404, 'other HTTP actions remain disabled')
-- Exercise the real timer through the Vanilla adapter and shared core, using
-- the same event shape sent by the installed surf-map triggers.
check(type(adapter.eventHandlers.SurfTimerTrigger) == 'function', 'Vanilla forwards surf timer events')
player.cell='surf_kitsune_mw'
messages={}; local broadcastsBefore=#broadcasts
adapter.eventHandlers.SurfTimerTrigger({pid=1,map=player.cell,trigger='start'})
check(#messages==0 and #broadcasts==broadcastsBefore, 'surf start is silent')
uptime=130.25
adapter.eventHandlers.SurfTimerTrigger({pid=1,map=player.cell,trigger='finish'})
check(output():find('Surf finished in 1:05.250s.',1,true), 'finish reports elapsed server time to player')
check(#broadcasts==broadcastsBefore+1 and broadcasts[#broadcasts]:find('Operator finished surf_kitsune_mw in 1:05.250s',1,true), 'finish broadcasts player, map and time')
messages={}; adapter.eventHandlers.SurfTimerTrigger({pid=1,map=player.cell,trigger='finish'})
check(output():find('no active timer',1,true) and #broadcasts==broadcastsBefore+1, 'duplicate finish cannot award another run')
messages={}; adapter.eventHandlers.SurfTimerTrigger({pid=1,map='surf_mesa_mw',trigger='start'})
adapter.eventHandlers.SurfTimerTrigger({pid=1,map=player.cell,trigger='finish'})
check(output():find('no active timer',1,true), 'trigger from another cell cannot start a run')
adapter.eventHandlers.SurfTimerTrigger({pid=1,map=player.cell,trigger='start'})
adapter.eventHandlers.OnPlayerDisconnect({guid=1,name=player.name})
messages={}; adapter.eventHandlers.SurfTimerTrigger({pid=1,map=player.cell,trigger='finish'})
check(output():find('no active timer',1,true), 'disconnect clears an active run')
-- No implicit default admin password on minimal server configurations.
for _, password in ipairs({false, ''}) do
    package.loaded.config.ADMIN_PASSWORD = password or nil
    package.loaded.core=nil; package.loaded.vanilla_runtime=nil
    adapter=require('vanilla_runtime')
    chat('/login changeme')
    check(output():find('not configured',1,true) and runtime['1'] == nil, 'unconfigured login fails closed')
end
-- The optional profile filter does not change the full default command catalog.
local registry=require('command_registry')
local full=registry.getCatalog('/',true)
local filtered=registry.getCatalog('/',true,{f=true,placeat=true})
check(#full > #filtered and #filtered == 2, 'full catalog remains available')
check(#registry.getCatalog('/',false,{placeat=true}) == 0, 'filter cannot bypass admin-only visibility')
print(string.format('Vanilla commands: %d checks passed',checks))
