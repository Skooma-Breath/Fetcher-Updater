-- Vanilla's shared commands, without experimental gameplay/test modules.
-- The allowlist controls dispatch and /help, including commands which would
-- otherwise require experimental client-side mods.
local allowed = {}
for _, name in ipairs({
    "help", "who", "list", "f", "suicide", "time", "uptime", "nick", "login",
    "speech", "s", "speechhelp", "mark", "recall", "marks", "unmark",
    "surf", "vehicle", "recordstore", "kick", "placeat", "delete", "resetcell",
    "resetallcells", "mpwhere", "tomp", "toref", "tpto", "tp", "bringmp",
    "bringref", "spawnat", "settime",
}) do
    allowed[name] = true
end

return {
    commandAllowlist = allowed,
    markRecallCommands = require("mark_recall"),
    speechCommands = require("speech_commands"),
    surfCommands = require("surf_commands"),
    vehicleCommands = require("vehicle_commands"),
}
