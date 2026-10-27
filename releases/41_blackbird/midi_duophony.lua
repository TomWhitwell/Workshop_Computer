--- duophonic MPE (2 voices)

-- Dune's custom MIDI card. Device or Host mode both work (switching is automatic but requires a WS power cycle)

-- CV ouputs: Pitch (+ pressure offset)
-- Pulse outputs: 10ms trigger whenever a new voice is assigned

-- constants
PRESSURE_RANGE_VOLTS = 1.0

ENV_ATTACK = 0.002
ENV_RELEASE = 0.08

-- globals
voice_data = {nil,nil}
voice_chan = {nil,nil}
voice_pressure_volts = {0.0, 0.0}
num_active_voices = 0

-- Avoid per-note allocations from constructing a new pulse() object.
PULSE_10MS = pulse(0.01)

-- init sets up actions
function init()
  -- Enable MPE routing in firmware.
  bb.mpe_mode(true)

  -- Audio outs 1/2: simple gate envelopes for voices 1/2.
  output[3].action = adsr(ENV_ATTACK, 0.0, 5.0, ENV_RELEASE)
  output[4].action = adsr(ENV_ATTACK, 0.0, 5.0, ENV_RELEASE)
  output[3](0)
  output[4](0)

  -- Start with no pressure offset.
  voice_pressure_volts[1] = 0.0
  voice_pressure_volts[2] = 0.0
end

-- When a new key is pressed, decide which voice the note-on should go to based on
-- proximity to other live voices & how many voices are currently active
function voice_dispatch(note)
  local nearby = false
  if  num_active_voices == 2 then
    if math.abs(note - voice_data[1]) < math.abs(note - voice_data[2]) then
      return 1
    else
      return 2
    end
  elseif  num_active_voices == 1 then 
    if voice_data[2] then 
      nearby = math.abs(voice_data[2] - note) < 5
      if nearby then return 2 else return 1 end
    else
      nearby = math.abs(voice_data[1] - note) < 5
      if nearby then return 1 else return 2 end
    end
  elseif  num_active_voices == 0 then
    if note%40 < 16 then
      return 1
    else
      return 2
    end
  else
    print('voice dispatch error: num_active_voices= ', num_active_voices)
    return -1
  end
end

local function voice_pitch_volts(note)
  return (note - 41) / 12
end

local function pressure_to_volts(val)
  if val == nil then return 0.0 end
  if val < 0 then val = 0 end
  if val > 127 then val = 127 end
  return (val / 127) * PRESSURE_RANGE_VOLTS
end

-- MPE note callback
bb.event_mpe_note = function(type, num, vel, chan)
  if type == 'note_on' then
    -- duophonic note on
    local voice = voice_dispatch(num)
    output[voice].volts = voice_pitch_volts(num) + (voice_pressure_volts[voice] or 0.0)
    output[voice+2](1)
    if not voice_data[voice] then
      num_active_voices = num_active_voices + 1
      bb.pulseout[voice]( PULSE_10MS )
    end
    voice_data[voice] = num
    voice_chan[voice] = chan
  else
    -- note off (same for both modes)
    for i,v in pairs(voice_data) do
      if v == num and (voice_chan[i] == nil or voice_chan[i] == chan) then
        voice_pressure_volts[i] = 0.0
        output[i].volts = 0
        output[i+2](0)
        voice_data[i] = nil
        voice_chan[i] = nil
        num_active_voices = num_active_voices - 1
      end
    end
  end
end

-- MPE pressure callback (per-channel)
-- Add pressure as a pitch CV offset per voice.
bb.event_mpe_pressure = function(val, chan)
  local v = pressure_to_volts(val)
  for i, note in pairs(voice_data) do
    if note and voice_chan[i] == chan then
      voice_pressure_volts[i] = v
      output[i].volts = voice_pitch_volts(note) + v
    end
  end
end
