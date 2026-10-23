local mp = require('mp')
local section = mp.storage.globalSection('FetcherStudioDeployment')
return {
    eventHandlers = {
        OnServerInit = function()
            if section:getCopy('keyboardPlaced20261022') then return end
            local queued = mp.placeObject('fetcher_procedural_synth_v1', 1, 'ToddTest',
                {x=2175, y=2256, z=-703})
            if queued then
                section:set('keyboardPlaced20261022', true)
                mp.log('[FetcherStudio] Initial keyboard placement queued in ToddTest.')
            end
        end,
    },
}
