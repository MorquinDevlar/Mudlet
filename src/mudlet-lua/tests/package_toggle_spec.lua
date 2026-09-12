-- Specs for enablePackage()/disablePackage() - the one switch that turns every
-- trigger, alias, timer, script, key and button a package installed off or on.
--
-- The package is built here rather than taken from fixtures/packages: it has to
-- carry one item in each of the six units, and what is being checked is that
-- every one of them goes quiet, so the file and the assertions belong together.
--
-- Everything is installed once for the whole block and uninstalled at the end -
-- an install or uninstall costs a full profile save, which is by far the most
-- expensive thing here.

-- pumpEvents() is inert outside test mode, and without it the profile save
-- never finishes: the uninstall at the end fails and strands the package in the
-- self-test profile, which the next run then refuses to install over.
if not os.getenv("MUDLET_TEST_MODE") then
  describe("Tests turning a whole package off and on", function()
    it("needs test mode", function()
      pending("the package toggle specs need MUDLET_TEST_MODE (pumpEvents() does nothing without it)")
    end)
  end)
  return
end

local packageName = "mudlet-spec-toggle"
local scratchDirectory = getMudletHomeDir() .. "/busted-package-toggle"
local packagePath = scratchDirectory .. "/" .. packageName .. ".xml"

local triggerName = packageName .. " trigger"
local timerName = packageName .. " timer"
local aliasName = packageName .. " alias"
local buttonName = packageName .. " button"
local scriptName = packageName .. " script"
local keyName = packageName .. " key"

local function packageBody()
  return ([[
  <TriggerPackage>
    <Trigger isActive="yes" isFolder="no" isTempTrigger="no" isMultiline="no" isPerlSlashGOption="no" isColorizerTrigger="no" isFilterTrigger="no" isSoundTrigger="no" isColorTrigger="no" isColorTriggerFg="no" isColorTriggerBg="no">
      <name>%s trigger</name>
      <script>mudletSpecToggleTriggerFired = true</script>
      <triggerType>0</triggerType>
      <conditonLineDelta>0</conditonLineDelta>
      <mStayOpen>0</mStayOpen>
      <mCommand></mCommand>
      <packageName></packageName>
      <mFgColor>#000000</mFgColor>
      <mBgColor>#000000</mBgColor>
      <mSoundFile></mSoundFile>
      <colorTriggerFgColor>#000000</colorTriggerFgColor>
      <colorTriggerBgColor>#000000</colorTriggerBgColor>
      <regexCodeList>
        <string>mudlet spec toggle line</string>
      </regexCodeList>
      <regexCodePropertyList>
        <integer>0</integer>
      </regexCodePropertyList>
    </Trigger>
  </TriggerPackage>
  <TimerPackage>
    <Timer isActive="yes" isFolder="no" isTempTimer="no" isOffsetTimer="no">
      <name>%s timer</name>
      <script>mudletSpecToggleTimerFired = true</script>
      <command></command>
      <packageName></packageName>
      <time>00:00:00.200</time>
    </Timer>
  </TimerPackage>
  <AliasPackage>
    <Alias isActive="yes" isFolder="no">
      <name>%s alias</name>
      <script>mudletSpecToggleAliasFired = true</script>
      <command></command>
      <packageName></packageName>
      <regex>^mudletSpecToggleAlias$</regex>
    </Alias>
  </AliasPackage>
  <ActionPackage>
    <ActionGroup isActive="yes" isFolder="yes" isPushButton="no" isFlatButton="no" useCustomLayout="no">
      <name>%s toolbar</name>
      <packageName></packageName>
      <script></script>
      <css></css>
      <commandButtonUp></commandButtonUp>
      <commandButtonDown></commandButtonDown>
      <icon></icon>
      <orientation>0</orientation>
      <location>0</location>
      <posX>0</posX>
      <posY>0</posY>
      <mButtonState>1</mButtonState>
      <sizeX>80</sizeX>
      <sizeY>30</sizeY>
      <buttonColumn>1</buttonColumn>
      <buttonFillerOffset>0</buttonFillerOffset>
      <buttonRotation>0</buttonRotation>
      <Action isActive="yes" isFolder="no" isPushButton="no" isFlatButton="no" useCustomLayout="no">
        <name>%s button</name>
        <packageName></packageName>
        <script>mudletSpecToggleButtonPressed = true</script>
        <css></css>
        <commandButtonUp></commandButtonUp>
        <commandButtonDown></commandButtonDown>
        <icon></icon>
        <orientation>0</orientation>
        <location>0</location>
        <posX>0</posX>
        <posY>0</posY>
        <mButtonState>1</mButtonState>
        <sizeX>80</sizeX>
        <sizeY>30</sizeY>
        <buttonColumn>1</buttonColumn>
        <buttonFillerOffset>0</buttonFillerOffset>
        <buttonRotation>0</buttonRotation>
      </Action>
    </ActionGroup>
  </ActionPackage>
  <ScriptPackage>
    <Script isActive="yes" isFolder="no">
      <name>%s script</name>
      <packageName></packageName>
      <script>mudletSpecToggleScriptRuns = (mudletSpecToggleScriptRuns or 0) + 1</script>
      <eventHandlerList />
    </Script>
  </ScriptPackage>
  <KeyPackage>
    <Key isActive="yes" isFolder="no">
      <name>%s key</name>
      <packageName></packageName>
      <script>mudletSpecToggleKeyPressed = true</script>
      <command></command>
      <keyCode>16777268</keyCode>
      <keyModifier>0</keyModifier>
    </Key>
  </KeyPackage>
  <VariablePackage>
    <HiddenVariables />
  </VariablePackage>]]):format(packageName, packageName, packageName, packageName, packageName, packageName, packageName)
end

local function writePackageFile()
  lfs.mkdir(scratchDirectory)
  local file = io.open(packagePath, "wb")
  assert.is_not_nil(file, "could not write " .. packagePath)
  assert.is_not_nil(file:write(table.concat({
    '<?xml version="1.0" encoding="UTF-8"?>',
    '<!DOCTYPE MudletPackage>',
    '<MudletPackage version="1.001">',
    packageBody(),
    '</MudletPackage>',
    '',
  }, "\n")), "could not write " .. packagePath)
  file:close()
end

local function waitUntil(condition, timeoutMilliseconds)
  local waited = 0
  while waited < timeoutMilliseconds do
    if condition() then
      return true
    end
    pumpEvents(50)
    waited = waited + 50
  end
  return condition() and true or false
end

local function listContains(list, name)
  for _, entry in ipairs(list) do
    if entry == name then
      return true
    end
  end
  return false
end

local function packageInstalled()
  return listContains(getPackages(), packageName)
end

-- Lua has no way to ask whether a profile save is running, but installPackage()
-- gives it away: while one is in flight it postpones what it was asked to do and
-- answers true even for a path it would otherwise refuse outright.
local function waitForProfileSaveToPass()
  return waitUntil(function() return installPackage("") == nil end, 5000)
end

local function removePackage()
  for _ = 1, 3 do
    if not packageInstalled() then
      return
    end
    assert.is_true(waitForProfileSaveToPass(), "a profile save was still running after 5s, so this uninstall would be refused")
    assert.is_true(waitUntil(function() return uninstallPackage(packageName) == true end, 5000), "could not uninstall " .. packageName)
    pumpEvents(200)
  end
  assert.is_false(packageInstalled(), packageName .. " reinstalled itself")
end

local function installPackageFile()
  for attempt = 1, 3 do
    if packageInstalled() then
      return
    end
    assert.is_true(waitForProfileSaveToPass(), "a profile save was still running after 5s, so this install would be postponed")
    local ok, err = installPackage(packagePath)
    if ok ~= true and not (type(err) == "string" and err:find("already installed", 1, true)) then
      assert.is_true(false, tostring(err))
    end
    if packageInstalled() then
      return
    end
    pumpEvents(400 * attempt)
  end
  assert.is_true(false, "could not install " .. packageName)
end

-- What each of the six items reads as. The third argument matters: a package is
-- switched off at its master folders, and every item of it sits underneath one,
-- so only a check that walks up the tree sees the switch.
local function itemsActive()
  return {
    trigger = isActive(triggerName, "trigger", true),
    timer = isActive(timerName, "timer", true),
    alias = isActive(aliasName, "alias", true),
    button = isActive(buttonName, "button", true),
    script = isActive(scriptName, "script", true),
    key = isActive(keyName, "keybind", true),
  }
end

local allOn = {trigger = 1, timer = 1, alias = 1, button = 1, script = 1, key = 1}
local allOff = {trigger = 0, timer = 0, alias = 0, button = 0, script = 0, key = 0}

local function clearFlags()
  _G.mudletSpecToggleAliasFired = nil
  _G.mudletSpecToggleScriptRuns = nil
  _G.mudletSpecToggleTriggerFired = nil
  _G.mudletSpecToggleTimerFired = nil
end

describe("Tests turning a whole package off and on", function()

  setup(function()
    -- the self-test profile is reused between runs, so a package a failed run
    -- left behind would make every install here fail as "already installed"
    removePackage()
    writePackageFile()
    installPackageFile()
  end)

  teardown(function()
    removePackage()
    os.remove(packagePath)
    lfs.rmdir(scratchDirectory)
  end)

  after_each(function()
    -- whatever a spec left switched off goes back on, so the next one starts
    -- from an installed, running package
    enablePackage(packageName)
    pumpEvents(100)
    clearFlags()
  end)

  it("installs with every item of it running", function()
    assert.is_true(packageInstalled(), "the package was not installed")
    assert.are.same(allOn, itemsActive())
    assert.is_not_nil(_G.mudletSpecToggleScriptRuns, "the package's script never ran")
  end)

  it("switches every item off and back on again", function()
    clearFlags()

    assert.is_true(disablePackage(packageName))
    pumpEvents(100)
    assert.are.same(allOff, itemsActive())

    -- each item is driven, not merely read: an off root has to stop the alias
    -- matching, the trigger matching and the timer's own QTimer firing
    expandAlias("mudletSpecToggleAlias", false)
    feedTriggers("mudlet spec toggle line\n")
    pumpEvents(400)
    assert.is_nil(_G.mudletSpecToggleAliasFired, "an alias of a switched-off package still matched")
    assert.is_nil(_G.mudletSpecToggleTriggerFired, "a trigger of a switched-off package still matched")
    assert.is_nil(_G.mudletSpecToggleTimerFired, "a timer of a switched-off package kept firing")
    assert.is_nil(_G.mudletSpecToggleScriptRuns, "a switched-off package ran its script")

    assert.is_true(enablePackage(packageName))
    pumpEvents(100)
    assert.are.same(allOn, itemsActive())
    assert.is_not_nil(_G.mudletSpecToggleScriptRuns, "switching the package back on did not run its script again")

    expandAlias("mudletSpecToggleAlias", false)
    feedTriggers("mudlet spec toggle line\n")
    pumpEvents(400)
    assert.is_true(_G.mudletSpecToggleAliasFired, "the alias did not match again after the package came back")
    assert.is_true(_G.mudletSpecToggleTriggerFired, "the trigger did not match again after the package came back")
    assert.is_true(_G.mudletSpecToggleTimerFired, "the timer did not start again after the package came back")
  end)

  it("leaves an item that was switched off by hand switched off", function()
    assert.is_true(disableTrigger(triggerName))
    assert.are.equal(0, isActive(triggerName, "trigger", true))

    assert.is_true(disablePackage(packageName))
    pumpEvents(100)
    assert.is_true(enablePackage(packageName))
    pumpEvents(100)

    assert.are.equal(0, isActive(triggerName, "trigger", true), "a trigger the user had switched off came back on with its package")
    assert.are.equal(1, isActive(aliasName, "alias", true), "the rest of the package did not come back on")

    assert.is_true(enableTrigger(triggerName))
  end)

  it("says so twice when asked for a state it is already in", function()
    assert.is_true(disablePackage(packageName))
    assert.is_true(disablePackage(packageName), "switching an already switched-off package off again was refused")
    assert.is_true(enablePackage(packageName))
    assert.is_true(enablePackage(packageName), "switching an already running package on again was refused")
  end)

  it("announces the switch", function()
    local seen = {}
    local offHandler = registerAnonymousEventHandler("sysDisablePackage", function(_, name) seen[#seen + 1] = {"off", name} end)
    local onHandler = registerAnonymousEventHandler("sysEnablePackage", function(_, name) seen[#seen + 1] = {"on", name} end)
    finally(function()
      killAnonymousEventHandler(offHandler)
      killAnonymousEventHandler(onHandler)
    end)

    assert.is_true(disablePackage(packageName))
    assert.is_true(enablePackage(packageName))
    pumpEvents(100)

    assert.are.same({{"off", packageName}, {"on", packageName}}, seen)
  end)

  it("refuses a name that is not an installed package", function()
    local ok, err = disablePackage("mudlet-spec-no-such-package")
    assert.is_nil(ok)
    assert.is_true(type(err) == "string" and err:find("not installed", 1, true) ~= nil, tostring(err))

    ok, err = enablePackage("mudlet-spec-no-such-package")
    assert.is_nil(ok)
    assert.is_true(type(err) == "string" and err:find("not installed", 1, true) ~= nil, tostring(err))
  end)

  it("refuses a module", function()
    local modules = getModules()
    if #modules == 0 then
      pending("this profile has no module installed to refuse")
      return
    end
    local ok, err = disablePackage(modules[1])
    assert.is_nil(ok)
    assert.is_true(type(err) == "string" and err:find("is a module", 1, true) ~= nil, tostring(err))
  end)
end)
