--- Krell Patch
-- looping ar envelopes reset pitch every loop
-- knobs x and y control the length of each separate envelope
-- main knob multiplies x & y for overall tempo

-- set up scales and start looping
function init()
  output[1].scale({})
  output[1].scale({})
  output[3](ar(log))
  output[4](ar(log))
  metro[1].time = 0.01
  metro[1]:start()  
  seq = sequins{2,1,0,3}
  oct = 3
  position = 'middle'
  bb.pulsein[1]{ mode = 'change', direction = 'rising' }
  bb.pulsein[2]{ mode = 'change', direction = 'rising' }
end

-- switch between octaves and chromatic scale
bb.switch.change = function(position) 
if position == 'down' then 
oct = seq()
else
  if position == 'up' then
  output[1].scale({0})
  output[2].scale({0})
  else
  output[1].scale({})
  output[2].scale({})
  end
end  
end

-- check knob positions
metro[1].event = function()
  x = bb.knob.x + 0.2
  y = bb.knob.y + 0.2
  m = (bb.knob.main + 0.1) * 3
end

-- channel 1 outputs
output[3].done = function()
  if bb.connected.cv1 then
    output[1].volts = input[1].volts
    else
    output[1].volts = math.random() * oct 
  end 
  output[3](ar(aa,ra))
  aa = math.random() * (((x)*(m))+0.1)
  ra = math.random() * ((x*(m))+0.4)
  bb.pulseout[1](pulse(0.01))
end

-- channel 2 outputs
output[4].done = function()
  if bb.connected.cv2 then
    output[2].volts = input[2].volts
    else
    output[2].volts = math.random() * oct 
  end
  output[4](ar(aa,ra))
  aa = math.random() * (((x)*(m))+0.1)
  ra = math.random() * ((x*(m))+0.4)
  bb.pulseout[2](pulse(0.01)) 
end
