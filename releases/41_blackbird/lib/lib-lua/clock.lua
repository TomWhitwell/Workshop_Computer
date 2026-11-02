--- clock coroutines
-- @module clock

local clock = { threads = {}
              , transport = {}
              , id = 0
              }

local source_map = {
  auto = 0,
  internal = 1,
  midi = 2,
  link = 3,
  crow = 4,
}

local source_names = {
  [0] = 'auto',
  [1] = 'internal',
  [2] = 'midi',
  [3] = 'link',
  [4] = 'crow',
}

--- create a coroutine to run but do not immediately run it;
-- @tparam function f
-- @treturn integer : coroutine ID that can be used to resume/stop it later
clock.create = function(f)
  local coro = coroutine.create(f)
  clock.id = clock.id + 1 -- create a new id
  clock.threads[clock.id] = coro
  return clock.id
end

--- create a coroutine from the given function and immediately run it;
-- the function parameter is a task that will suspend when clock.sleep and clock.sync are called inside it and will wake up again after specified time.
-- @tparam function f
-- @treturn integer : coroutine ID that can be used to stop it later
clock.run = function(f, ...)
  local coro_id = clock.create(f)
  clock.resume(coro_id, ...)
  return coro_id
end

--- stop execution of a coroutine started using clock.run.
-- @tparam integer coro_id : coroutine ID
clock.cancel = function(coro_id)
  clock_cancel(coro_id)
  clock.threads[coro_id] = nil
  
  -- Reset ID counter when all threads are cancelled to prevent memory leak
  local has_active = false
  for _, coro in pairs(clock.threads) do
    if coro then 
      has_active = true
      break
    end
  end
  if not has_active then
    clock.id = 0  -- Reset counter when no active threads
  end
end

--- yield and schedule waking up the coroutine in s seconds;
-- must be called from within a coroutine started with clock.run.
-- @tparam float s : seconds
clock.sleep = function(...)
  return coroutine.yield(0, ...)
end

--- yield and schedule waking up the coroutine at beats beat;
-- the coroutine will suspend for the time required to reach the given fraction of a beat;
-- must be called from within a coroutine started with clock.run.
-- @tparam float beats : next fraction of a beat at which the coroutine will be resumed. may be larger than 1.
clock.sync = function(...)
  return coroutine.yield(1, ...)
end

clock.beat = function(...)
  return coroutine.yield(2, ...)
end

clock.resume = function(coro_id, ...)
  local coro = clock.threads[coro_id]

  if coro == nil then
    print('cant resume cancelled clock')
    return
  end

  local result, mode, time = coroutine.resume(coro, ...)

  if coroutine.status(coro) == 'dead' then
    if result then
      clock.cancel(coro_id)
    else
      print('error: ' .. mode)
    end
  else
    if result then -- not dead
      if mode == 0 then -- SLEEP
        clock_schedule_sleep(coro_id, time)
      elseif mode == 1 then -- SYNC
        clock_schedule_sync(coro_id, time)
      elseif mode == 2 then -- BEATSYNC
        clock_schedule_beat(coro_id, time)
      end
    end
  end
end

clock.cleanup = function()
  -- Do not mutate clock.threads while iterating it.
  -- (pairs() iteration + deletions can skip entries, leaving coroutines referenced.)
  local ids = {}
  for id, coro in pairs(clock.threads) do
    if coro then ids[#ids+1] = id end
  end
  for i = 1, #ids do
    local id = ids[i]
    if clock.threads[id] then
      clock.cancel(id)
    end
  end

  -- Hard reset of coroutine bookkeeping
  clock.threads = {}
  clock.id = 0
  clock.tempo = 120
  clock.transport.start = nil
  clock.transport.stop = nil
end

clock.get_beats = clock_get_time_beats
clock.get_beat_sec = function(x) return (x or 1) * 60.0 / clock.tempo end

clock.start = function(beat) return clock_internal_start(beat or 0) end
clock.stop = clock_internal_stop


-- event handlers (called from C)
clock_resume_handler = clock.resume
function clock_start_handler() if clock.transport.start then clock.transport.start() end end
function clock_stop_handler()  if clock.transport.stop then clock.transport.stop() end end

clock.__newindex = function(self, ix, val)
  if ix == 'tempo' then
    clock_internal_set_tempo(val)
  elseif ix == 'source' then
    if type(val) == 'string' then
      val = source_map[val]
    end

    if val == nil then
      error('clock.source must be string or number')
    end

    clock_set_source(val)
  end
end
clock.__index = function(self, ix)
  if ix == 'tempo' then
    return clock_get_tempo()
  elseif ix == 'source' then
    local src = clock_get_source() or 1
    return source_names[src] or src
  end
end

setmetatable(clock, clock)

return clock
