local inifile = require "inifile"

-- os.chdir(App:GetScriptDir())
App:ChScriptDir()

local config = inifile.parse("iniconfig.ini")

Alert(config['square']['name'])