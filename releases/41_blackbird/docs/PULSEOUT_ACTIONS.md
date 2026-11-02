# Pulse Output Actions API

The Blackbird hardware has two pulse outputs that can be controlled via Lua. This document describes how to use the `bb.pulseout[1]` and `bb.pulseout[2]` API to control these outputs with crow-style methods and custom actions.

## Overview

- **`bb.pulseout[1]`** - First pulse output
- **`bb.pulseout[2]`** - Second pulse output

**Important:** Pulse outputs are **1-bit digital outputs** (on/off only). They support `pulse()` actions, clock-synced timing, and manual high/low control. Voltage ramps, levels, and other complex ASL actions are not supported since these outputs cannot produce variable voltages.

## Default Behavior

Both pulse outputs stay low until a script sets them up. To send a clock from Pulse Out 1, for example:

```lua
bb.pulseout[1]:clock(0.5)  -- a 10ms pulse twice per beat of the internal clock
```

Like `output[1]:clock(0.5)`, `:clock` installs a `pulse()` action if you haven't provided one.

## Quick Reference

```lua
-- Clock-synced pulses (crow-style)
bb.pulseout[1]:clock(1)           -- 10ms pulse every beat
bb.pulseout[1]:clock(4)           -- 10ms pulse every 4 beats
bb.pulseout[1]:clock('off')       -- Stop clocked pulses

-- Manual control
bb.pulseout[1]:high()             -- Set high indefinitely
bb.pulseout[1]:low()              -- Set low indefinitely

-- Custom actions
bb.pulseout[1].action = pulse(0.050)         -- Set custom pulse width
bb.pulseout[1](pulse(0.100))                 -- Execute action immediately
bb.pulseout[1].action = function() ... end   -- Lua function action

-- Check state
local is_high = bb.pulseout[1].state
```

## Methods

### `:clock(division)`
Start a clock coroutine that triggers pulses on beat divisions. This is the recommended way to create rhythmic pulse patterns.

```lua
bb.pulseout[1]:clock(1)    -- Pulse on every beat
bb.pulseout[2]:clock(2)    -- Pulse every 2 beats
bb.pulseout[1]:clock(0.5)  -- Pulse twice per beat
bb.pulseout[1]:clock('off') -- Stop the clock
```

**How it works:**
- Creates a Lua coroutine that syncs to the internal clock
- Executes the action (default: `pulse(0.010)`) on each trigger
- Can be stopped with `:clock('off')` or `clock.cleanup()`
- Calling `:clock(div)` again replaces the previous clock coroutine
- Mirrors `output[n]:clock(div)` semantics: if `.action` is nil, it automatically injects `pulse()` so you always get a trigger without extra setup

### `:high()`
Set the output high (5V) indefinitely and stop any running clock coroutine.

```lua
bb.pulseout[1]:high()  -- Stay high until changed
```

### `:low()`
Set the output low (0V) indefinitely and stop any running clock coroutine.

```lua
bb.pulseout[1]:low()   -- Stay low until changed
```

## Properties

### `.action`
Get or set the action for the pulse output. When using `:clock()`, this is the action that gets executed on each beat.

```lua
-- Get current action
local current = bb.pulseout[1].action

-- Set a new action (used by :clock() or manual execution)
bb.pulseout[1].action = pulse(0.050)  -- 50ms pulses
```

### `.state`
Read-only. Returns the current state of the pulse output (true = high, false = low).

```lua
local is_high = bb.pulseout[1].state
```

## Common Patterns

### Clock-Synced Pulses
The recommended way to create rhythmic pulses:

```lua
-- Short trigger pulses on every beat
bb.pulseout[1]:clock(1)
bb.pulseout[1].action = pulse(0.001)  -- 1ms trigger

-- Longer gates on every 2 beats
bb.pulseout[2]:clock(2)
bb.pulseout[2].action = pulse(0.050)  -- 50ms gate

-- Half-time pulses
bb.pulseout[1]:clock(0.5)  -- Twice per beat

-- Stop the clock
bb.pulseout[1]:clock('off')
```

### Manual Gate Control

```lua
-- Open gate
bb.pulseout[1]:high()

-- Close gate
bb.pulseout[1]:low()

-- Toggle based on condition
if bb.switch.position == 'down' then
    bb.pulseout[2]:high()
else
    bb.pulseout[2]:low()
end
```

### Custom Pulse Durations

Use the `pulse()` function to define pulse duration:

```lua
-- Set custom pulse width with :clock()
bb.pulseout[1]:clock(1)
bb.pulseout[1].action = pulse(0.020)  -- 20ms pulses

-- Or execute immediately
bb.pulseout[1](pulse(0.100))  -- Single 100ms pulse

-- Variable duration based on knob
bb.pulseout[1].action = pulse(0.001 + bb.knob.main * 0.099)  -- 1-100ms
```

**Note:** The `level` and `polarity` parameters from crow's `pulse()` are ignored for these digital outputs - they only go high/low.

### Euclidean Rhythms with Sequins

```lua
-- Use sequins for euclidean patterns
local pattern = sequins{1, 0, 0, 1, 0, 1, 0, 0}

bb.pulseout[1]:clock(1)
bb.pulseout[1].action = function()
    if pattern() == 1 then
        _c.tell('output', 3, pulse(0.010))
    end
    -- If pattern returns 0, no pulse is generated
end
```

### Switch-Triggered Pulses

```lua
-- Trigger a pulse when switch moves to down position
bb.switch.change = function(state)
    if state == 'down' then
        bb.pulseout[2](pulse(0.050))  -- 50ms pulse
    end
end
```

### Dynamic Pulse Width

```lua
-- Pulse duration controlled by main knob (1-100ms)
bb.pulseout[1]:clock(1)
bb.pulseout[1].action = function()
    local pw = 0.001 + (bb.knob.main * 0.099)
    _c.tell('output', 3, pulse(pw))
end
```

### Using Lua Functions for Complex Logic

For advanced control, use a Lua function as the action:

```lua
bb.pulseout[1]:clock(1)
bb.pulseout[1].action = function()
    -- Custom logic here
    if some_condition then
        _c.tell('output', 3, pulse(0.020))
    end
end
```

## Integration with Clock System

All pulse output timing is controlled via Lua coroutines using the `:clock()` method. This integrates seamlessly with crow's clock system:

```lua
-- Start clock-synced pulses
bb.pulseout[1]:clock(1)

-- Change the action that gets executed
bb.pulseout[1].action = pulse(0.020)

-- Stop when cleaning up
clock.cleanup()  -- Automatically stops all pulse output clocks
```

The `:clock()` method creates a coroutine that:
1. Syncs to the internal clock using `clock.sync(division)`
2. Executes the current action on each beat
3. Runs until stopped with `:clock('off')` or `clock.cleanup()`

## Stopping and Clearing

### Stop Clock Coroutine

```lua
bb.pulseout[1]:clock('off')  -- Stop clocked pulses, leave output low
```

### Set Output State

```lua
bb.pulseout[1]:high()  -- Set high, stop any clock
bb.pulseout[1]:low()   -- Set low, stop any clock
```

### Clear Action

```lua
bb.pulseout[1].action = 'none'  -- Clear the action
bb.pulseout[1]('none')          -- Crow-style syntax
```

### Clean Up All Clocks

The `clock.cleanup()` function automatically stops all pulse output clocks:

```lua
clock.cleanup()  -- Stops all clock coroutines including pulse outputs
```

## Implementation Details

- All pulse triggering is done via Lua coroutines (no C code interference)
- The `:clock()` method creates a `clock.run()` coroutine that executes actions on beat divisions
- Actions are executed by calling `_c.tell('output', channel, action)` which routes to the hardware
- Pulse width is extracted from ASL tables at `action[1][3][3]` (accounting for `_if` wrapper)
- The hardware sets output HIGH, then schedules a `clock.run()` to set it LOW after the duration

## Limitations

Since pulse outputs are **1-bit digital outputs**, they have the following limitations:

- ✅ **Supported:** `pulse(duration)` for timed pulses
- ✅ **Supported:** `:clock(division)` for rhythmic patterns
- ✅ **Supported:** `:high()` and `:low()` for manual control
- ✅ **Supported:** Lua function actions with `_c.tell()`
- ❌ **Not Supported:** Voltage ramping (e.g., `to(5, 0.1)`)
- ❌ **Not Supported:** Variable voltage levels
- ❌ **Not Supported:** Complex ASL sequences with multiple voltage targets
- ❌ **Not Supported:** LFOs, envelopes, or other voltage-based actions

These outputs are purely on/off - perfect for gates, triggers, and clock signals, but not for CV.

## Notes

- Pulse outputs are **5V digital outputs** (high = 5V, low = 0V)
- All timing is controlled via Lua coroutines, not C code
- Default behavior is set up on startup and can be fully overridden
- You can check the current state with `.state` property
- `clock.cleanup()` automatically stops all pulse output clocks

## See Also

- `examples/pulseout_demo.lua` - Working examples
- crow documentation - Clock system and action reference
