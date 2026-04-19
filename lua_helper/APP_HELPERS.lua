-- app_helper.lua

function string.envstr(str)
    return App.Call('envstr', str)
end

function string.resstr(str)
    return App.Call('resstr', str)
end

function math.band(a, b)
    return App.Call('band', a, b)
end

--

local clsOption = {
    cmd = nil,
}
clsOption.__index = clsOption

function clsOption:Print()
    print(self.cmd)
end

function clsOption:HasOption(opt)
    local cmd = self.cmd
    return (string.find(cmd, opt) and true or false)
end

function  clsOption:GetOption(opt)
    local val, val2
    local cmd = self.cmd
    val = string.match(cmd, opt .. '%s(.+)$')
    if val == nil then return nil end

    --  -xyz 'v a l u e' -abc
    val2 = string.match(val, '^[\'](.+)[\']%s')
    if val2 ~= nil then return val2 end
    --  -xyz "v a l u e" -abc
    val2 = string.match(val, '^[\"](.+)[\"]%s')
    if val2 ~= nil then return val2 end

    --  -xyz 'v a l u e'
    val2 = string.match(val, '^[\'](.+)[\']$')
    if val2 ~= nil then return val2 end
    --  -xyz "v a l u e"
    val2 = string.match(val, '^[\"](.+)[\"]$')
    if val2 ~= nil then return val2 end

    --  -xyz abc -abc
    val2 = string.match(val, '([^%s]+)')
    if val2 ~= nil then return val2 end

    return val
end

Option = {}
function Option.New(cmd)
    local o = {}
    setmetatable(o, clsOption)
    if cmd == nil then cmd = "" end
    o.cmd = cmd
    return o
end

--

Alert = App.Alert
alert = Alert

--

App.Version =  App:Info('Version')
App.Ver = App.Version

Lua = {}
Lua.Version =  App:Info('LuaVersion')
Lua.Ver = Lua.Version

App.Path = App:Info('Path')
App.Name = App:Info('Name')
App.FullPath = App:Info('FullPath')

App.CmdLine = App:Info('CmdLine')
App.Option = Option.New(App.CmdLine)

App.ScriptEncoding = 'ANSI'

function App:HasOption(...)
  return self.Option:HasOption(...)
end

function App:GetOption(...)
  return self.Option:GetOption(...)
end

function App:Pause()
  App:Call('Pause')
end

function TEXT(s)
  if App.ScriptEncoding == 'ANSI' then
    return s
  elseif App.ScriptEncoding == 'UTF-8' then
    return App:Call('utf8toansi', s)
  end

  return s
end

--

TID_APP = 10000
TID_AUTO = 20000
TID_USER = 30000

AppTimer = {}
AppTimerId = {}
AppTimerStrId = {}
AppTimer.NextId = TID_AUTO

function App:SetTimer(tid, interval)
  local timerid = 0
  local strid = tostring(tid)
  if type(tid) == "number" then
     if math.type(tid) == "integer" then
        timerid = tid
     end
  end
  if timerid == 0 then
    timerid = AppTimer.NextId
    AppTimer.NextId = AppTimer.NextId + 1
  end
  AppTimerId[timerid] = strid
  AppTimerStrId[strid] = timerid
  App:Debug("[DEBUG] App:SetTimer(" .. timerid .. "['" .. strid .. "'] ," .. interval)
  App.Call('SetTimer', timerid, interval)
end

function App:KillTimer(tid)
  local timerid = AppTimerStrId[tostring(tid)]
  if timerid ~= nil then
    App.Call('KillTimer', timerid)
  end
end


function App:_onTimer(tid)
  local strid = AppTimerId[tid]
  if strid == nil then
    App:Info("[INFO] The AppTimer['" .. tid .."'] function is not defined.")
  elseif type(AppTimer[strid]) ~= "function" then
    App:Error("[ERROR] The AppTimer['" .. tid .."'] is not function.")
    return
  end
  if strid == nil then
    App:onTimer(tid)
    return
  end
  AppTimer[strid](tid)
end

function App:onTimer(tid)
  App:Info("[INFO] App:onTimer(" .. tid ..").")
end

---

function App:_onFirstShellRun()
  -- VERSTR = Reg:Read([[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion]], 'CurrentVersion')
  local win_ver = os.info('winver')['1.2']
  if App.Arg:Has('-wes') then
    if win_ver == '6.2' or win_ver == '6.3' then -- only Windows 8, 8.1
      -- init control panel timer
      App:SetTimer('_InitControlPanel', 200) -- use timer to make main shell running
    end
  end
end

AppTimer['_InitControlPanel'] = function(tid)
  local win_ver = os.info('winver')['1.2']
  App:initControlPanel(win_ver)
  App:KillTimer(tid)
end

function App:_onDaemon()
  regist_shortcut_ocf()
  regist_system_property()
  regist_protocols()
end

function App:_PreShell()
end

function App:_onShell()
  regist_folder_shell()

  App:_onDaemon()
end

function App:WxsProtocol(url, dumy)
  return wxs_protocol(url)
end

function App:initControlPanel(ver)
  --  4161    Control Panel
  local ctrlPanelTitle = string.resstr('#{@shell32.dll,4161}')
  App:Run('control.exe')
  App:Sleep(500)
  if Window.Find('CabinetWClass', ctrlPanelTitle):Close() == 0 then
    -- 32012    All Control Panel Items
    ctrlpanel_title = string.resstr('#{@shell32.dll,32012}')
    Window.Find('CabinetWClass', ctrlPanelTitle):Close()
  end
end

--

Cmd = {}
function Cmd:Echo(s)
    App.Write(1, s)
end

function Cmd:Error(s)
    App.Write(2, s)
end


-- debug_helper.lua
if suilib then
  print = suilib.print
end

function string.dump(self)
  local i = 1
  local out = ''
    while true do
      local num1=self:byte(i)
      local hex
      if num1 == nil then break end
      hex = string.format("%0x ", num1)
      out = out .. hex
      if i % 8 == 0 then
        print(out)
        out = ''
      end
      i = i + 1
    end
    if out ~= '' then
      print(out)
    end
end

function print_r( t )
    local print_r_cache={}
    local function sub_print_r(t,indent)
        if (print_r_cache[tostring(t)]) then
            print(indent.."*"..tostring(t))
        else
            print_r_cache[tostring(t)]=true
            if (type(t)=="table") then
                for pos,val in pairs(t) do
                    if (type(val)=="table") then
                        print(indent.."["..pos.."] => "..tostring(t).." {")
                        sub_print_r(val,indent..string.rep(" ",string.len(pos)+8))
                        print(indent..string.rep(" ",string.len(pos)+6).."}")
                    elseif (type(val)=="string") then
                        print(indent.."["..pos..'] => "'..val..'"')
                    else
                        print(indent.."["..pos.."] => "..tostring(val))
                    end
                end
            else
                print(indent..tostring(t))
            end
        end
    end
    if (type(t)=="table") then
        print(tostring(t).." {")
        sub_print_r(t,"  ")
        print("}")
    else
        sub_print_r(t,"  ")
    end
    print()
end

table.print = print_r

-- io_helper.lua
Disk = {}
File = {}
Folder = {}

function Disk.BitLockerProtection(path)
  return App.Call('volume::bitlockerprotection', path)
end

function Disk.IsLocked(path)
  return Disk.BitLockerProtection(path) == 6
end

function File.Exists(path)
  return App.Call('file::exists', path) == 1
end

function File.Delete(path)
  local f = App.Call('envstr', path)
  return os.remove(f)
end

File.Remove = File.Delete

function File.GetFullPath(path)
  return App.Call('path::getfullpath', path)
end

function File.GetShortPath(path)
  return App.Call('path::getshortpath', path)
end

function Folder.Exists(path)
  return App.Call('folder::exists', path) == 1
end

Folder.GetFullPath = File.GetFullPath
Folder.GetShortPath = File.GetShortPath

function os.exists(path)
  if path == nil then return 1 end
  local f = App.Call('envstr', path)
  if File.Exists(path) then return 1 end
  if Folder.Exists(path) then return 1 end
  return 0
end

-- reg_helper.lua
-- require 'winapi'

local function reg_getdata(regkey, value)
  local data, data_type = regkey:get_value(value)
  if data_type == winapi.REG_DWORD then
    data = data | 0 -- to integer
  end
  return data, data_type
end

function reg_read(key, values)
  local res = {}
  local data, data_type = nil, 0

  local k, err = winapi.open_reg_key(key, false)
  if not k then return nil end

  if not (type(values) == 'table') then
    data, data_type = reg_getdata(k, values)
    k:close()
    return data, data_type
  end

  for i, v in ipairs(values) do
    data = reg_getdata(k, v)
    res[v] = data
    res[i] = data
  end
  k:close()

  return res
end

function reg_write(key, name, value, data_type)
  local k, err = winapi.open_reg_key(key, true)
  if not k then -- create the key if the key isn't exists
    winapi.create_reg_key(key)
    k,err = winapi.open_reg_key(key, true) -- open again
    if not k then return nil end
  end
  if data_type == nil then data_type = winapi.REG_SZ end
  k:set_value(name, value, data_type)
  k:close()
  return 1
end

function reg_delete(key, name)
  local subkey, k, err
  if name == nil then
    name = key:match("[^\\]+$")
    key = key:match(".+\\")
    subkey = key
  end

  k,err = winapi.open_reg_key(key, true)
  if not k then return nil end
  if subkey ~= nil then
    k:delete(name)
  else
    k:delete_value(name)
  end
  k:close()
  return 0
end

-- Export Reg Class

Reg = {}


REG_NONE = 0
REG_SZ  = 1
REG_EXPAND_SZ = 2
REG_BINARY = 3
REG_DWORD = 4

REG_LINK = 6
REG_MULTI_SZ = 7
REG_QWORD = 11

function Reg:Read(...) return reg_read(...) end
function Reg:Write(...) return reg_write(...) end
function Reg:Delete(...) return reg_delete(...) end

function Reg:GetSubKeys(key)
  local k, err
  k, err = winapi.open_reg_key(key, true)
  if not k then
    return nil
  end
  local keys = k:get_keys()
  k:close()
  if #keys >= 1 then
    return keys
  end
  return nil
end


-- os_helper.lua

function os.putenv(var, str)
  App.Call('putenv', var, str)
end

os.setenv = os.putenv

local _osinfo_isWinPE = nil
local _osinfo_isX = nil

function os.info(key, ...)
  local arr = {}
  if key == nil then return end
  if key:lower() == 'cpu' then
    local cpu_info = Reg:Read([[HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0]]
      ,{'ProcessorNameString', '~MHz'})

    if cpu_info then
      local cpu_f = cpu_info['~MHz']
      cpu_f = cpu_f / 1000
      cpu_info['name'] = cpu_info['ProcessorNameString']
      cpu_info['desc'] = string.format("%s %.2fGHz", cpu_info['name'], cpu_f)
      return cpu_info
    end
  elseif key:lower() == 'winver' then
    local v, v1, v2, v3, v4 =  App.Call('os::info', key, 4)
    arr[1] = v1; arr[2] = v2; arr[3] = v3; arr[4] = v4
    arr['1.2'] = string.format("%d.%d", v1, v2)
    arr['ver'] = v

    --[[

    return setmetatable(arr, {
      __tostring = function()
      return arr.ver
    end
    })

    ]]
    return arr
  elseif key:lower() == 'mem' then
    arr[1], arr[2], arr[3] = App.Call('os::info', 'mem')
    arr['installed'] = arr[1]; arr[1] = tonumber(arr[1])
    arr['total'] = arr[2]; arr[2] = tonumber(arr[2])
    arr['avail'] = arr[3]; arr[3] = tonumber(arr[3])
    arr[4] = arr[2] - arr[3]
    arr['used'] = tostring(arr[4])
    arr["used%"] = math.ceil(arr[4] * 1000 / arr['total']) / 10.0
    arr['total_gb'] = math.ceil(arr[2] / 1024 / 1024 / 1024)
    arr['avail_gb'] = arr[3] / 1024 / 1024 / 1024
    arr['used_gb'] = arr[4] / 1024 / 1024 / 1024
    return arr
  elseif key:lower() == 'iswinpe' then
    if _osinfo_isWinPE == nil then _osinfo_isWinPE = is_winpe() end
    return _osinfo_isWinPE
  elseif key:lower() == 'isx' then
    if _osinfo_isX == nil then _osinfo_isX = (os.getenv("SystemDrive") == 'X:') end
    return _osinfo_isX
  end
   return App.Call('os::info', key, ...)
end

function is_winpe()
  local start_opt = Reg:Read([[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control]],
    'SystemStartOptions')
  return string.find(start_opt, 'minint') ~= nil
end


function os_ver_info()
  return  Reg:Read([[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion]]
    ,{'ProductName', 'CSDVersion'})
end

function cpu_info()
  return  Reg:Read([[HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0]]
    ,{'ProcessorNameString', '~MHz'})
end

function mem_info()
  local mem = {}
  mem[1], mem[2], mem[3] = App:Call('os::info', 'mem')
  return mem
end

function localename()
  return App:Call('os::info', 'localename')
end

function res_str(file, id)
  local strid = string.format('#{@%s,%s}', file, id)
  return App:Call('resstr', strid)
end

function mui_str(file, id)
  LN = LN or localename()
  local mui_file = string.format('%s\\%s.mui', LN, file)
  return res_str(mui_file, id)
end

function win_copyright()
  return App:Call('os::info', 'copyright')
end

function rundll(...)
  return App:Call('rundll', ...)
end

os.rundll = rundll


-- cmd_helper.lua

function parse_option(opt_str)
    local opt = {}
    opt.wait = false
    opt.showcmd = 1
    opt.verb = nil
    if opt_str == nil then return opt end
    opt_str = opt_str .. ' '
    if string.find(opt_str, '/nowait ') then opt.wait = false end
    if string.find(opt_str, '/wait ') then opt.wait = true end
    if string.find(opt_str, '/hide ') then
        opt.hide = 1
        opt.showcmd = 0
    end
    if string.find(opt_str, '/min ') then
        opt.min = 1
        opt.showcmd = 2
    end
    if string.find(opt_str, '/max ') then
        opt.max = 1
        opt.showcmd = 3
    end
    if string.find(opt_str, '/admin ') then
        opt.verb = 'runas'
    end
    return opt
end


function exec(option, cmd)
    if cmd == nil then
        cmd = option
        option = nil
    end
    local opt = parse_option(option)
    return App:Call('exec', cmd, opt.wait, opt.showcmd, opt.verb)
end


function link(lnk, target, param, icon, index, showcmd)
    local opt = parse_option(showcmd)
    return App:Call('link', lnk, target, param, icon, index, opt.showcmd)
end

os.exec = exec
os.link = link

--[[
-- test
print(parse_option('/wait /hide').wait)
print(parse_option('/hide').wait)
print(parse_option('/wait /hide').showcmd)
print(parse_option('/wait').showcmd)
print(parse_option('/wait /min').showcmd)
print(parse_option('/wait /max').showcmd)
print(parse_option(nil).showcmd)
--]]

-- proc_helper.lua
require "winapi"

-- const
-- Window operations for Window.show

SW_SHOW = winapi.SW_SHOW
SW_HIDE = winapi.SW_HIDE
SW_MINIMIZE = winapi.SW_MINIMIZE
SW_MAXIMIZE = winapi.SW_MAXIMIZE
SW_SHOWNORMAL = winapi.SW_SHOWNORMAL
SW_RESTORE = winapi.SW_RESTORE

SW_MIN = SW_MINIMIZE
SW_MAX = SW_MAXIMIZE

WM_QUIT       =   0x12 
WM_SYSCOMMAND = 0x0112
SC_CLOSE      = 0xf060

local _clsProc = {
  winObj = nil,
  procObj = nil

  --[[
  GetClassName()
  GetFileName()
  GetHandle()
  IsVisible()
  Show()
  Activate()
  Hide()
  Minimize()
  Maximize()
  SendMessage()
  PostMessage()
  Close()
  Quit()
  Kill()
]]

}


function _clsProc.New(self, win_or_proc, o)
  o = o or {}
  self.__index = self
  setmetatable(o, self)
  self.winObj = nil
  self.procObj = nil
  if not win_or_proc then return o end

  if win_or_proc.__name == "Window" then
    if win_or_proc:get_handle() > 0 then
      self.winObj = win_or_proc
      self.procObj = win_or_proc:get_process()
    end
  end

  if win_or_proc.__name == "Process" then
    self.procObj = win_or_proc
  end

  return o
end

function _clsProc:GetClassName()
  local o = self.winObj
  if o then return o:get_class_name() end
  return ""
end

function _clsProc:GetFileName()
  local o = self.winObj
  if o then return o:get_module_filename() end
  return ""
end

function _clsProc:GetHandle()
  local o = self.winObj
  if o then return o:get_handle() | 0 end
  return 0
end

function _clsProc:IsVisible()
  local o = self.winObj
  if o then return o:is_visible() end
  return 0
end

function _clsProc:Show(flag, active)
  local rc = ""
  local o = self.winObj

  if flag == nil then
    flag = SW_SHOW
    if active == nil then active = true end
  end
  if active == nil then active = false end

  if o then
    rc = o:show(flag)
    if active then o:set_foreground() end
  end
  return rc
end

function _clsProc:Activate()
  local hwnd = self.GetHandle()
  App:Call('AppActivate', hwnd)
end

function _clsProc:Hide()
  return _clsProc:Show(SW_HIDE)
end

function _clsProc:Minimize()
  return _clsProc:Show(SW_MIN)
end

function _clsProc:Maximize()
  return _clsProc:Show(SW_MAX)
end

function _clsProc:SendMessage(msg, wparam, lparam)
  local o = self.winObj
  if o then return "OK", o:send_message(msg, wparam, lparam) end
  return "", 0
end

function _clsProc:PostWindow(msg, wparam, lparam)
  local o = self.winObj
  if o then return "OK", o:post_message(msg, wparam, lparam) end
  return "", 0
end

function _clsProc:Close()
  return _clsProc:SendMessage(WM_SYSCOMMAND, SC_CLOSE, 0)
end

function _clsProc:Quit()
  return _clsProc:PostWindow(WM_QUIT, 0, 0)
end

function _clsProc:Kill()
  local o = self.procObj
  if o then return o:kill() end
  return 0
end


----------------------------------------------------
Proc = {}

-- Proc.Find
-- Proc.Match
-- Proc.Run
-- Proc.Exec

function Proc.Find(title_or_pid, class)
  local obj = nil
  if type(title_or_pid) == "number" then
    obj = winapi.process_from_id(title_or_pid)
  else
    obj = winapi.find_window(class, title_or_pid)
  end
  return _clsProc:New(obj)
end

function Proc.Match(pattern)
  local obj = winapi.find_window_match(pattern)
  return _clsProc:New(obj)
end


Window = {
  __name = "AppWindow"
}

Window.Find = Proc.Find
Window.Match = Proc.Match


--[[
local p = Window.Find("*Untitled")
-- p = Proc.Find(10912)

print(p:GetClassName())
print(p:GetHandle())
Alert(p:GetClassName())
p:Show()
p:Close()
p:Kill()

]]

-- win_helper.lua
-- require 'winapi'

WM_QUIT       =   0x12 
WM_SYSCOMMAND = 0x0112
SC_CLOSE      = 0xf060

function FindWindow(class, title)
  local win = nil
  if title and title:find('*') ~= nil then
    win = winapi.find_window_match(title)
  else
    win = winapi.find_window(class, title)
  end
  return win
end

function ShowWindow(class, title, show)
  local win = FindWindow(class, title)
  if show == nil then show = winapi.SW_SHOW end
  if win ~= nil then win:show(show) end
end

function HideWindow(class, title)
  ShowWindow(class, title, winapi.SW_HIDE)
end

function MinimizeWindow(class, title)
  ShowWindow(class, title, winapi.SW_MINIMIZE)
end

function SendWindow(class, title, msg, wparam, lparam)
  local win, hwnd
  win = FindWindow(class, title)
  if win ~= nil then 
    hwnd = win:get_handle()
    App:Print('SendWindow(' .. string.format("%s, %s, 0x%x", title, class, hwnd) .. ')')
    win:send_message(msg, wparam, lparam)
    return hwnd
  end
  return nil
end

function PostWindow(class, title, msg, wparam, lparam)
  local win, hwnd
  win = FindWindow(class, title)
  if win ~= nil then 
    hwnd = win:get_handle()
    App:Print('PostWindow(' .. string.format("%s, %s, 0x%x", title, class, hwnd) .. ')')
    win:post_message(msg, wparam, lparam)
    return hwnd
  end
  return nil
end

function ActivateWindow(class, title)
  local win = FindWindow(class, title)
  if win ~= nil then
    win:set_foreground()
  end
end

function CloseWindow(class, title)
  return SendWindow(class, title, WM_SYSCOMMAND, SC_CLOSE, 0)
end

function QuitWindow(class, title)
  return PostWindow(class, title, WM_QUIT, 0, 0)
end

-- shell_helper.lua
-- require 'reg_helper'

System = {}
local regkey_user = 'HKEY_CURRENT_USER'
if os.getenv('USERNAME') == 'SYSTEM' then regkey_user = 'HKEY_LOCAL_MACHINE' end
local regkey_colortheme = [[HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize]]

function System:GetSetting(key)
  if key == 'AppsColorTheme' then
    return (Reg:Read(regkey_colortheme, 'AppsUseLightTheme') or 1) | 0 -- convert to integer
  elseif key == 'SysColorTheme' then
    return (Reg:Read(regkey_colortheme, 'SystemUsesLightTheme') or 1) | 0
  elseif key == 'ShellColorPrevalence' then
    return (Reg:Read(regkey_colortheme, 'ColorPrevalence') or 1) | 0
  elseif key == 'WindowColorPrevalence' then
    local regkey = regkey_user .. [[\SOFTWARE\Microsoft\Windows\DWM]]
    return (Reg:Read(regkey, 'ColorPrevalence') or 1) | 0
  elseif key == 'Colors.Transparency' then
    return (Reg:Read(regkey_colortheme, 'EnableTransparency') or 1) | 0
  end
  return 0
end

function System:SetSetting(key, val)
  if val ~= 0 then val = 1 end
  if key == 'ShellColorPrevalence' then
    Reg:Write(regkey_colortheme, 'ColorPrevalence', val, winapi.REG_DWORD)
    App:Call('System::ChangeColorThemeNotify')
  elseif key == 'WindowColorPrevalence' then
    local regkey = regkey_user .. [[\SOFTWARE\Microsoft\Windows\DWM]]
    Reg:Write(regkey, 'ColorPrevalence', val, winapi.REG_DWORD)
  elseif key == 'Colors.Transparency' then
    Reg:Write(regkey_colortheme, 'EnableTransparency', val, winapi.REG_DWORD)
    App:Call('System::ChangeColorThemeNotify')
  end
  return 0
end

function System:SysColorTheme(mode)
    if mode == 'light' then
        Reg:Write(regkey_colortheme, 'SystemUsesLightTheme', 1, winapi.REG_DWORD)
    else
        Reg:Write(regkey_colortheme, 'SystemUsesLightTheme', 0, winapi.REG_DWORD)
    end
    App:Call('System::ChangeColorThemeNotify')
end

function System:AppsColorTheme(mode)
    if mode == 'light' then
        Reg:Write(regkey_colortheme, 'AppsUseLightTheme', 1, winapi.REG_DWORD)
    else
        Reg:Write(regkey_colortheme, 'AppsUseLightTheme', 0, winapi.REG_DWORD)
    end
    App:Call('System::ChangeColorThemeNotify')
end

function System:ReloadCursors()
    App:Call('system::setcursors')
end

local function power_helper(wu_param, sd_param)
  local sd = os.getenv("SystemDrive")
  if File.Exists(sd ..'\\Windows\\System32\\Wpeutil.exe') then
    App:Run('wpeutil.exe', wu_param, 0) -- SW_HIDE(0)
    return 0
  elseif File.exists(sd ..'\\Windows\\System32\\shutdown.exe') then
    App:Run('shutdown.exe', sd_param .. ' -t 0')
    return 0
  end
  return 1
end

function System:CreatePageFile(file, min, max)
  return App:Call('System::CreatePageFile', file, min, max);
end

function System:NetJoin(domain, joinOpt, server, accountOU, account, password)
  domain = domain or 'WORKGROUP'
  return os.rundll('Netapi32.dll', 'NetJoinDomain', server, domain, accountOU, account, password, joinOpt)
end

function System:EnableEUDC(fEnableEUDC)
  return App:Call('System::EnableEUDC', fEnableEUDC);
end

function System:AppxSysprepInit()
  return App:Call('System::AppxSysprepInit');
end

function System:Reboot()
  return power_helper('Reboot', '-r')
end

function System:Shutdown()
  return power_helper('Shutdown', '-s')
end


WinPE = {}
function WinPE:SystemInit()
  System:EnableEUDC(1)
  System:AppxSysprepInit()
end

local function PinCommand(class, target, name, param, icon, index, showcmd)
  local ext = string.sub(target, -4)
  local case_ext = string.lower(ext)

  local pinned_path = [[%APPDATA%\Microsoft\Internet Explorer\Quick Launch\User Pinned\]] .. class .. '\\'
  local lnk_name = name
  if lnk_name == nil then lnk_name = string.match(target, '([^\\]+)' .. ext .. '$') end
  lnk_name = lnk_name .. '.lnk'
  if case_ext == '.lnk' then
    if App:Info('isshell') ~= 0 then
      exec('/hide', 'cmd.exe /c copy /y \"' .. target .. '\" \"' .. pinned_path .. lnk_name .. '\"')
    else
      App:Call(class .. '::Pin', target)
    end
    return
  end
  if case_ext ~= '.exe' then return end

  local lnk = target
  if name ~= nil or param ~= nil or icon ~= nil then
    if App:Info('isshell') ~= 0 then
      lnk = pinned_path .. lnk_name
    else
      lnk = '%TEMP%\\' .. class .. 'Pinned\\' .. lnk_name
    end
    App:Call('link', lnk, target, param, icon, index, showcmd)
  end
  if App:Info('isshell') ~= 0 then
    if lnk == target then
      lnk = pinned_path .. lnk_name
      App:Call('link', lnk, target)
    end
  else
    App:Call(class .. '::Pin', lnk)
  end
end

Shell = {}
function Shell:Run(cmd)
  shel(cmd)
end

function Shell:Close()
  App:Call('closeshell')
  App:Sleep(500)
end

function Shell:WaitAndClose()
  Taskbar:WaitForReady()
  Shell:Close()
end

Shell.onHotKey = {}
Shell.onHotKey['WIN+S'] = function()
end

Shell.onHotKey['WIN+F'] = function()
end

function Shell:_onHotKey(hotkey)
  Shell.onHotKey[hotkey]()
end

Desktop = {}
Desktop.Path = ""

function Desktop:Refresh()
  App:Call('Desktop::Refresh')
end

function Desktop:GetPath()
  return App:Call('Desktop::GetPath')
end

function Desktop:GetWallpaper()
  return App:Call('Desktop::Getwallpaper')
end

function Desktop:SetWallpaper(wallpaper)
  App:Call('Desktop::Setwallpaper', wallpaper)
end

function Desktop:SetIconSize(size)
  App:Call('Desktop::SetIconSize', size)
end

function Desktop:AutoArrange(checked)
  App:Call('Desktop::AutoArrange', checked)
end

function Desktop:SnapToGrid(checked)
  App:Call('Desktop::SnapToGrid', checked)
end

function Desktop:ShowIcons(checked)
  App:Call('Desktop::ShowIcons', checked)
end

function Desktop:Link(lnk, ...)
  os.link(Desktop.Path .. '\\' .. lnk , ...)
end

Desktop.Path = Desktop:GetPath()


Taskbar = {}
local regkey_setting = [[HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced]]

function Taskbar:IsReady(sec)
  local sh_win = winapi.find_window('Shell_TrayWnd', nil)
  local n = -1
  while (n <= sec and (sh_win == nil or sh_win:get_handle() == 0)) do
    App:Print("shell Handle:0x0")
    App:Sleep(1000)
    sh_win = winapi.find_window('Shell_TrayWnd', nil)
    if sec ~= -1 then n = n + 1 end
  end
  if sh_win == nil or sh_win:get_handle() == 0 then
    return false
  end
  return true
end

function Taskbar:WaitForReady()
  Taskbar:IsReady(-1)
end

function Taskbar:GetSetting(key)
  if key == 'AutoHide' then return App:Call('Taskbar::AutoHideState') end
  return Reg:Read(regkey_setting, key)
end

function Taskbar:SetSetting(key, value, type)
  return Reg:Write(regkey_setting, key, value, type)
end

function Taskbar:CombineButtons(value, update)
  if value == 'always' then value = 0
  elseif value == 'auto' then value = 1
  else value = 2 end --never

  Taskbar:SetSetting('TaskbarGlomLevel', value, winapi.REG_DWORD)
  if update ~= 0 then App:Call('Taskbar::ChangeNotify') end
end

function Taskbar:UseSmallIcons(value, update)
  Taskbar:SetSetting('TaskbarSmallIcons', value, winapi.REG_DWORD)
  if update ~= 0 then App:Call('Taskbar::ChangeNotify') end
end

function Taskbar:AutoHide(value)
  App:Call('Taskbar::AutoHide', value)
end

function Taskbar:Pin(target, name, param, icon, index, showcmd)
  PinCommand('Taskbar',target, name, param, icon, index, showcmd)
end

function Taskbar:UnPin(name)
  App:Call('Taskbar::UnPin', name)
end

function Taskbar:Show()
    Window.Find(nil, 'Shell_TrayWnd'):Show()
end

function Taskbar:Hide()
    Window.Find(nil, 'Shell_TrayWnd'):Hide()
end

Startmenu = {}
Startmenu.ProgramsPath = ""

function Startmenu:GetProgramsPath()
  return App:Call('Startmenu::GetProgramsPath')
end

function Startmenu:Pin(target, name, param, icon, index, showcmd)
  PinCommand('Startmenu',target, name, param, icon, index, showcmd)
end

function Startmenu:UnPin(target)
  App:Call('startmenu::unpin', target)
end

function Startmenu:Link(lnk, ...)
  os.link(Startmenu.ProgramsPath .. '\\' .. lnk , ...)
end

Startmenu.ProgramsPath = Startmenu:GetProgramsPath()


Screen = {}

function Screen:Adjust()
  App:Call('Desktop::UpdateWallpaper')
  App:Sleep(200)
  App:Call('Taskbar::ChangeNotify')
end

function  Screen:Get(...)
  return App:Call('Screen::Get', ...)
end

function  Screen:Set(...)
  return App:Call('Screen::Set', ...)
end

function Screen:GetX()
  return App:Call('Screen::Get', 'x')
end

function Screen:GetY()
  return App:Call('Screen::Get', 'y')
end

function Screen:GetRotation()
  return App:Call('Screen::Get', 'rotation')
end

function Screen:GetDPI()
  return App:Call('Screen::Get', 'dpi')
end

function Screen:Disp(w, h)
  local ret = -1
  if w == nil then
    ret = App:Call('Screen::Set', 'maxresolution')
  else
    ret = App:Call('Screen::Set', 'resolution', w, h)
  end
  Screen:Adjust()
  return ret
end

-- arr = {'1152x864', '1366x768', '1024x768'}
function Screen:DispTest(arr)
  local i, w, h, ret = 0
  for i = 1, #arr do
    w, h = string.match(arr[i], '(%d+)[x*](%d+)')
    if h ~= nil then
      App:Print(w, h)
      if Screen:Disp(tonumber(w), tonumber(h)) == 0 then return end
    end
  end
end

function Screen:DPI(scale)
  App:Call('Screen::Set', 'dpi', scale)
end


Volume = {}

function Volume:GetName()
  return App:Call('Volume::GetName')
end

function Volume:GetLevel()
  return App:Call('Volume::GetLevel')
end

function Volume:SetLevel(...)
  return App:Call('Volume::SetLevel', ...)
end

function Volume:IsMuted()
  return App:Call('Volume::IsMuted')
end

function Volume:Mute(...)
  return App:Call('Volume::Mute', ...)
end


FolderOptions = {}

-- Opt =
--   'ShowAll'     - Show the hidden files / folders
--   'ShowExt'     - Show the known extension
--   'ShowSuperHidden' - Always hide the system files / folders

function FolderOptions:Set(opt, val)
  App:Call('FolderOptions::Set', opt, val)
end

function FolderOptions:Get(opt)
  return App:Call('FolderOptions::Get', opt)
end

function FolderOptions:Toggle(opt)
  local val = FolderOptions:Get(opt)
  FolderOptions:Set(opt, val - 1)
end

Dialog = {}
function Dialog:OpenFile(...)
  return App:Call('Dialog::OpenFile', ...)
end

function Dialog:SaveFile(...)
  return App:Call('Dialog::SaveFile', ...)
end

function Dialog:OpenSaveFile(...)
  return App:Call('Dialog::OpenSaveFile', ...)
end

function Dialog:BrowseFolder(...)
  return App:Call('Dialog::BrowseFolder', ...)
end

-- Helper(alias)
function LinkToDesktop(...) Desktop:Link(...) end
function LinkToStartmenu(...) Startmenu:Link(...) end

function PinToTaskbar(...) Taskbar:Pin(...) end
function PinToStartMenu(...) Startmenu:Pin(...) end

-- wxs_helper.lua
-- require('io_helper')

WxsHandler = {}
TrayNotify = {}
TrayClock = {}

-- 'auto', 'ui_systemInfo', 'system', '' or nil
WxsHandler.SystemProperty = 'auto'

 -- nil or a handler function
WxsHandler.OpenContainingFolder = nil
WxsHandler.DisplayChangedHandler = nil

WxsHandler.TrayClockTextFormatter = nil


function wxsUI(ui, jcfg, opt, app_path)
  if jcfg == nil then jcfg = 'main.jcfg' end
  if opt == nil then opt = '' else opt = ' ' .. opt end
  if app_path == nil then app_path = App.FullPath end
  if File.Exists(App.Path .. '\\wxsUI\\' .. ui .. '.zip') then
    ui = ui .. '.zip'
  end
  App:Run(app_path, ' -ui -jcfg wxsUI\\' .. ui .. '\\' .. jcfg .. opt)
end

function wxs_ui(url)
    App:Debug(url)
    if string.find(url, 'wxs-ui:', 1, true) then
      url = url:gsub('wxs[-]ui:', '')
    end

    if url == "systeminfo" then
      wxsUI('UI_SystemInfo')
    elseif url == "settings" then
      wxsUI('UI_Settings', 'main.jcfg', '-fixscreen')
    elseif url == "wifi" then
      wxsUI('UI_WIFI')
    elseif url == "volume" then
      wxsUI('UI_Volume')
    end
end

function wxs_open(url)
    App:Debug(url)
    local sd = os.getenv('SystemDrive')
    if string.find(url, 'wxs-open:', 1, true) ~= nil then
      url = url:gsub('wxs[-]open:', '')
    end

    if url == 'controlpanel' then
      if not File.Exists(sd .. '\\Windows\\explorer.exe') then return end
      App:Run('control.exe')
    elseif url == 'system' then
      App:Call('wxs_open', 'system')
    elseif url == 'netsettings' then
      if File.Exists(sd .. '\\Windows\\System32\\netcenter.dll') then
        App:Call('wxs_open', 'networkcenter')
      elseif File.Exists(sd .. '\\Windows\\System32\\netshell.dll') then
        App:Call('wxs_open', 'networkconnections')
      end
    elseif url == 'networkconnections' then
      App:Call('wxs_open', 'networkconnections')
    elseif url == 'printers' then
      App:Call('wxs_open', 'printers')
    elseif url == 'userslibraries' then
      App:Call('wxs_open', 'userslibraries')
    elseif url == 'devices' then
      App:Call('wxs_open', 'devicesandprinters')
    elseif url == 'wifi' then
      wxsUI('UI_WIFI')
    elseif url == 'volume' then
      wxsUI('UI_Volume')
    end
end

function ms_settings(url)
    App:Debug(url)
    if string.find(url, 'ms-settings:', 1, true) == nil then return end
    url = url:gsub('ms[-]settings:', '')

    if url == 'taskbar' then
      wxsUI('UI_Settings', 'main.jcfg', '-fixscreen')
    elseif url == 'dateandtime' then
      App:Run('timedate.cpl')
    elseif url == 'display' then
      --wxsUI('UI_Resolution', 'main.jcfg')
      wxsUI('UI_Settings', 'main.jcfg', '-display -fixscreen')
    elseif url == 'personalization' then
      wxsUI('UI_Settings', 'main.jcfg', '-colors -fixscreen')
    elseif url == 'personalization-background' then
      wxsUI('UI_Settings', 'main.jcfg', '-colors -fixscreen')
    elseif url == 'sound' then
      wxsUI('UI_Volume')
    elseif url == 'network' then
      wxs_open('networkconnections')
    elseif url == 'about' then
      wxsUI('UI_SystemInfo')
    else
       -- winapi.show_message('', url)
       wxs_open('controlpanel')
    end
end

function wxs_protocol(url)
  App:Print(url)
  if url == 'ms-availablenetworks:' then
    wxsUI('UI_WIFI')
  else
    ms_settings(url)
  end
end

function regist_folder_shell()
  local sd = os.getenv("SystemDrive")
  if File.Exists(sd .. '\\Windows\\explorer.exe') then return end

  local key = [[HKEY_CLASSES_ROOT\Folder\shell]]
  local val = Reg:Read(key, 'WinXShell_Registered')

  if val ~= nil then return end
  Reg:Write(key, 'WinXShell_Registered', 'done')

  key = [[HKEY_CLASSES_ROOT\Folder\shell\open\command]]
  val = Reg:Read(key, 'DelegateExecute')
  if val ~= nil then Reg:Write(key, 'DelegateExecute_Backup', val) end
  Reg:Delete(key, 'DelegateExecute')
  Reg:Write(key, '', '"' .. App.FullPath .. '" "%1"')

  -- explore,opennewprocess,opennewtab
end

local function regist_protocol(protocol, type)
  local key = 'HKEY_CLASSES_ROOT\\' .. protocol
  local val = Reg:Read(key .. [[\Shell\Open\Command]], 'DelegateExecute')
  App:Print(val)
  if val == nil or val ~= '{C59C9814-F038-4B71-A341-6024882458AF}' then
    Reg:Write(key, '', 'URL:' .. protocol)
    Reg:Write(key, 'URL Protocol', '')
    Reg:Write(key .. [[\Shell\Open\Command]], 'DelegateExecute', '{C59C9814-F038-4B71-A341-6024882458AF}')

    if type == 'App' then
      Reg:Write(key .. [[\Application]], 'AppUserModelId', '')
      Reg:Write(key .. [[\Shell\Open]], 'PackageId', '')
    end
  end
end

function regist_protocols()
  if os.getenv('WIN_VSDEBUG') ~= nil then return end
  local win_ver = os.info('winver')['1.2']
  if tonumber(win_ver) < 10 then return end
  if File.Exists('X:\\Windows\\System32\\AppxSysprep.dll') then return end
  regist_protocol('ms-settings')
  regist_protocol('ms-availablenetworks', 'App')
  App:Run(App.FullPath, '-Embedding')
end

function regist_system_property() -- handle This PC's property menu
    if not os.info('isX') then return end
    --if File.Exists('X:\\Windows\\explorer.exe') then return end

    if WxsHandler.SystemProperty == nil then return end
    if WxsHandler.SystemProperty == '' then return end
    if WxsHandler.SystemProperty == 'auto' then
        -- 'control system' works in x86_x64 with explorer.exe
        if os.ARCH == 'x64' and File.Exists('X:\\Windows\\explorer.exe') and
            File.Exists('X:\\Windows\\SysWOW64\\wow32.dll') then
            return
        elseif os.ARCH == 'x86' and File.Exists('X:\\Windows\\explorer.exe') then
            return
        end
    end

    local key = [[HKEY_CLASSES_ROOT\CLSID\{20D04FE0-3AEA-1069-A2D8-08002B30309D}\shell\properties]]
    if Reg:Read(key, '') then return end -- already exists
    -- show This PC on the Desktop
    -- Reg:Write([[HKEY_USERS\.DEFAULT\Software\Microsoft\Windows\CurrentVersion\Explorer\HideDesktopIcons\NewStartPanel]], '{20D04FE0-3AEA-1069-A2D8-08002B30309D}', 0, winapi.REG_DWORD)
    -- handle Property menu to UI_SystemInfo
    Reg:Write(key, '', '@shell32.dll,-33555')
    Reg:Write(key, 'Position', 'Bottom')

    if WxsHandler.SystemProperty == 'ui_systemInfo' then
      Reg:Write(key ..'\\command', '', App.FullPath .. ' wxs-ui:systeminfo')
    else
      Reg:Write(key ..'\\command', '', App.FullPath .. ' wxs-open:system')
    end
end

function regist_shortcut_ocf() -- handle shortcut's OpenContainingFolder menu
    if not os.info('isX') then return end
    if File.Exists('X:\\Windows\\explorer.exe') then
      if File.Exists('X:\\Windows\\System32\\ieframe.dll') then return end
    end

    local key = [[HKEY_CLASSES_ROOT\lnkfile\shell\OpenContainingFolderMenu_wxsStub]]
    if Reg:Read(key, '') then return end -- already exists
    Reg:Write([[HKEY_CLASSES_ROOT\lnkfile\shell\OpenContainingFolderMenu_wxsStub]], '', 'Open Containing Folder Menu Wrap')
    Reg:Write([[HKEY_CLASSES_ROOT\lnkfile\shell\OpenContainingFolderMenu_wxsStub]], '#MUIVerb', '@shell32.dll,-1033')
    Reg:Write([[HKEY_CLASSES_ROOT\lnkfile\shell\OpenContainingFolderMenu_wxsStub]], 'Extended', '')
    Reg:Write([[HKEY_CLASSES_ROOT\lnkfile\shell\OpenContainingFolderMenu_wxsStub]], 'Position', 'Bottom')
    local explorer_opt = ''
    -- if File.Exists('X:\\Windows\\explorer.exe') then explorer_opt = '-explorer' end
    Reg:Write([[HKEY_CLASSES_ROOT\lnkfile\shell\OpenContainingFolderMenu_wxsStub\command]], '', App.FullPath .. ' '.. explorer_opt ..' -ocf \"%1\"')

    Reg:Write([[HKEY_CLASSES_ROOT\lnkfile\shellex\ContextMenuHandlers\OpenContainingFolderMenu]], '', 'disable-{37ea3a21-7493-4208-a011-7f9ea79ce9f5}')
    Reg:Write([[HKEY_CLASSES_ROOT\lnkfile\shellex\ContextMenuHandlers\OpenContainingFolderMenu_wxsStub]], '', '{B1FD8E8F-DC08-41BC-AF14-AAC87FE3073B}')
    Reg:Write([[HKEY_CLASSES_ROOT\lnkfile\shellex\PropertySheetHandlers\wxsStub]], '', '{B1FD8E8F-DC08-41BC-AF14-AAC87FE3073B}')

    Reg:Write([[HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{B1FD8E8F-DC08-41BC-AF14-AAC87FE3073B}]], '', 'wxsStub')
    local stub_dll = 'wxsStub.dll'
    if os.ARCH ~= 'x64' then stub_dll = 'wxsStub32.dll' end
    Reg:Write([[HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{B1FD8E8F-DC08-41BC-AF14-AAC87FE3073B}\InProcServer32]], '', App.Path .. '\\' .. stub_dll)
    Reg:Write([[HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{B1FD8E8F-DC08-41BC-AF14-AAC87FE3073B}\InProcServer32]], 'ThreadingModel', 'Apartment')

    if os.ARCH == 'x64' then
      Reg:Write([[HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Classes\CLSID\{B1FD8E8F-DC08-41BC-AF14-AAC87FE3073B}]], '', 'wxsStub')
      Reg:Write([[HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Classes\CLSID\{B1FD8E8F-DC08-41BC-AF14-AAC87FE3073B}\InProcServer32]], '', App.Path .. '\\wxsStub32.dll')
      Reg:Write([[HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Classes\CLSID\{B1FD8E8F-DC08-41BC-AF14-AAC87FE3073B}\InProcServer32]], 'ThreadingModel', 'Apartment')
    end

end

--------------------------------------------------------------------------------

StartButton = {}

-- return the resource id for startmenu logo
function Startmenu:SetLogoId()
  local map = {
    ["none"] = 0, ["windows"] = 1, ["winpe"] = 2,
    ["custom1"] = 11, ["custom2"] = 12, ["custom3"] = 13,
    ["default"] = 1
  }
  -- use next line for custom (remove "--" and change "none" to what you like)
  -- if true then return map["none"] end
  if os.info('isWinPE') then return map["winpe"] end
  return map["windows"]
end

function Startmenu:Logoff()
  -- return 1 -- for call system process
end

function Startmenu:Reboot()
  -- restart computer directly
  -- System:Reboot()
  wxsUI('UI_Shutdown', 'full.jcfg')
  return 0
  -- return 1 -- for call system dialog
end

function Startmenu:Shutdown()
  -- shutdown computer directly
  -- System::Shutdown()
  wxsUI('UI_Shutdown', 'full.jcfg')
  return 0
  -- return 1 -- for call system dialog
end

function Startmenu:ControlPanel()
  if App:HasOption('-wes') then
    App:Run('control.exe')
    return 0
  end
  return 1
end

function TrayClock:onClick()
  wxsUI('UI_Calendar', 'main.jcfg')
  return 0
end

function TrayClock:onDblClick()
  App:Run('control.exe', 'timedate.cpl')
  return 0
end

-- loader_helper.lua
function WaitForSession(user)
    App:Call('WaitForSession', user)
end

function SwitchSession(user)
    App:Call('SwitchSession', user)
end

function CloseShellWindow()
  Taskbar:WaitForReady()
  Shell:Close()
end

function ShellDaemon(wait, cmd)
  while (1) do
    print('ShellDaemon')
    if wait == true then
      Taskbar:WaitForReady()
      App:Print('WaitForReady')
      App:Sleep(5000)
    end
    wait = false
    if Taskbar:IsReady(5) == false then
      exec('/wait', cmd)
    end
    App:Sleep(5000)
  end
end

function shel(cmd)
  local wait_opt = 'false'
  if os.getenv('USERNAME') ~= 'SYSTEM' then wait_opt = 'true' end
  exec(App.Name .. ' -code "ShellDaemon(' .. wait_opt .. ',[[' .. cmd .. ']])"')
end

LINK = os.link

-- i18n.lua
i18n = {}

i18n.loaded = false

function i18n.load(lang)
  if lang == nil then lang = os.info('locale') end
  if File.Exists('locales\\' .. lang .. '.lua') then
    require('locales.' .. lang)
  end
  i18n.loaded = true
end

function i18n.t(key)
  if not i18n.loaded then i18n.load() end
  if i18n_str == nil then return key end
  return i18n_str[key] or key
end


-- compat_helper.lua


App.info = App.Info
App.call = App.Call
App.print = App.Print

app = App


function get_option(...)
  return App:GetOption(...)
end

function has_option(...)
  return App:HasOption(...)
end

File.exists = File.Exists
Folder.exists = Folder.Exists

-- ui_helper.lua
UI = {}
UI.OnClick = {}
UI.OnChanged = {}
UI.OnTimer = {}

function UI.UpdateText(name)
    sui:find(name).text = sui:find(name).text
end

UIGroup = {}

local clsUITab = {
    Name = '$Nav',
    List = {},
    Layout = nil,
    LayoutName = ''
}
clsUITab.__index = clsUITab

function clsUITab:SetList(list)
    local name
    for i = 1, #list do
      name = list[i]
      list[name] = i
    end
    self.List = list
end

function clsUITab:BindLayout(name)
    self.LayoutName = name
    self.Layout = sui:find(name)
end

function clsUITab:Click(tab)
    sui:click(self.Name .. '[' .. tab .. ']')
end

function clsUITab:OnClick(tab, ctrl)
    App:Debug('UITab:OnClick - ' .. tab  .. ' ' .. tostring(ctrl))
    self.Layout.selectedid = self.List[tab]
end

clsUITab.DefOnClick = clsUITab.OnClick

---
UITab = {}
function UITab.New(name)
    local o = {}
    setmetatable(o, clsUITab)
    o.Name = name
    UIGroup[name .. '[*]'] = o
    return o
end

UIPages = {
    tab = nil,
    index = 1
}

function UIPages:Init(page, tab)
    local page = UIPages[page]
    if tab then UIPages.tab = tab end
    if page then page:Init(tab) end
    UIPages.index = UIPages.index + 1
end

UICheckBox = {}
function UICheckBox.Check(elem, val)
    val = val or 1
    if type(elem) == "string" then elem = sui:find(elem) end
    elem.selected = val
end

UIOption = {}
function UIOption.Set(ctrl, val, opts)
    local name
    -- true, false
    if type(val) == "boolean" then
        local i = 2
        if val then i = 1 end
        name = ctrl:gsub('?', opts[i])
    elseif type(val) == "number" then
        name = ctrl:gsub('?', opts[val])
    else
        name = ctrl:gsub('?', opts[tostring(val)])
    end

    sui:find(name).selected = 1
end


UISwitch = {}
function UISwitch.Set(elem, val)
    if type(elem) == "string" then elem = sui:find(elem) end
    elem.selected = val
    if val == 0 then elem.text = "%{Off}" else elem.text = "%{On}" end
end

function UISwitch.SetText(elem, val)
    if type(elem) == "string" then elem = sui:find(elem) end
    if val == 0 then elem.text = "%{Off}" else elem.text = "%{On}" end
end


---
UIDispatcher = {}

UIEvent = {}
UIEvent.Trigger = true

UIEvent.OnTimer = nil
UIEvent.OnClick = nil
UIEvent.OnChanged = nil

local function OverRideFunc(funcs, ...)
    for i = 1, #funcs do
        if type(funcs[i]) == 'function' then
            funcs[i](...)
            return 1
        end
    end
    return nil
end

---
-- TID_APP = 10000
-- TID_AUTO = 20000
-- TID_USER = 30000

UITimer = {}
UITimerId = {}
UITimerStrId = {}
UITimer.NextId = TID_AUTO

function UI:SetTimer(tid, interval)
    local strid = tostring(tid)
    local timerid = UITimerId[strid]

    -- not exist
    if timerid == nil then
        if math.type(tid) == 'integer' then
            timerid = tid
        else
            -- auto timerid
            timerid = UITimer.NextId
            UITimer.NextId = UITimer.NextId + 1 
        end

        UITimerId[strid] =  timerid
        UITimerStrId[timerid] = strid
    end

    App:Debug("[DEBUG] UI:SetTimer(" .. timerid .. "['" .. strid .. "'] ," .. interval .. ")")
    if strid:sub(1, 6) == 'RESET.' then
        UI:KillTimer(strid)
    end
    suilib.call('SetTimer', timerid, interval)
end

function UI:KillTimer(tid)
    local timerid = UITimerId[tostring(tid)]
    if timerid ~= nil then
        suilib.call('KillTimer', timerid)
    end
end

function UIDispatcher.OnTimer(tid)
    local strid = UITimerStrId[tid]
    if strid == nil then
        App:Info("[INFO] The UITimer[" .. tid .."] is not defined.")
    elseif type(UI.OnTimer[strid]) == "function" then
        if strid:sub(1, 6) == 'RESET.' then
            UI:KillTimer(strid)
        end
        UI.OnTimer[strid](tid)
        return
    end

    return OverRideFunc({UIEvent.OnTimer, ontimer}, tid)
end

---

function UIDispatcher.OnClick(ctrl)
    if UIWindow.Inited == 0 then return 0 end
    if UIEvent.Trigger ~= true then return 0 end

    local obj, group, func, param1
    if ctrl:sub(1, 1) == "$" then
        group = ctrl:match("($.+)%[")
        if group then
            App:Debug('UIEvent.OnClick:' .. group)
            param1 = ctrl:match(group .. "%[(.+)%]")
            App:Debug('UIEvent.OnClick:' .. param1)
            obj = UIGroup[group .. '[*]']
            if obj then
                obj:OnClick(param1, ctrl)
                return 1
            end
        end
    end
    func = UI.OnClick[ctrl]
    if func == nil and group ~= nil then func = UI.OnClick[group] end
    if type(func) == 'function' then
        App:Debug("[DEBUG] UIEvent.OnClick[" .. ctrl .."] function()")
        if param1 then
            func(param1, ctrl)
        else
            func(ctrl)
        end
        return 1
    end

    return OverRideFunc({UIEvent.OnClick, onclick}, ctrl)
end


function UIDispatcher.OnChanged(ctrl, val)
    if UIWindow.Inited == 0 then return 0 end
    if UIEvent.Trigger ~= true then return 0 end

    local func = UI.OnChanged[ctrl]
    App:Debug('[DEBUG] UIEvent.OnChanged', ctrl, val)
    if ctrl:sub(1, 8) == '$Switch.' then
        UISwitch.SetText(ctrl, val)
    end
    if type(func) == 'function' then
        func(val, ctrl)
        return 1
    end

    return OverRideFunc({UIEvent.OnChanged, onchanged}, ctrl, val)
end

function UIDispatcher.OnReturn(ctrl)
    return OverRideFunc({UIEvent.OnReturn, onreturn}, ctrl)
end

function UIDispatcher.OnLink(ctrl)
    return OverRideFunc({UIEvent.OnLink, onlink}, ctrl)
end

function UIDispatcher.OnFocus(ctrl)
    return OverRideFunc({UIEvent.OnFocus, onfocus}, ctrl)
end

function UIDispatcher.OnHover(ctrl)
    return OverRideFunc({UIEvent.OnFocus, onhover}, ctrl)
end

function UIDispatcher.OnHoverChanged(ctrl)
    return OverRideFunc({UIEvent.OnFocus, onhoverchanged}, ctrl)
end

function UIDispatcher.OnDeactive(ctrl)
    if UIWindow.Inited == 0 then return 0 end
    return OverRideFunc({UIEvent.OnDeactive, ondeactive}, ctrl)
end

function UIDispatcher.OnMessage(msg, wparam, lparam)
    return OverRideFunc({UIEvent.OnMessage, onmessage}, msg, wparam, lparam)
end

---
UIWindow = {}
UIWindow.Inited = 0

function UIWindow:Init()
    -- for backward compatibility
    if type(init) == 'function' then
        init()
    end
end

function UIWindow:OnLoad()
    -- for backward compatibility
    if type(onload) == 'function' then
        onload()
    end

    UIWindow.Inited = 1
end

function UIWindow:OnShow()
    -- for backward compatibility
    if type(onshow) == 'function' then
        onshow()
    end
end

function UIWindow:OnHide()
  --
end

function UIWindow:Close()
    sui:close()
end

