# Used for running Raindrops::Watcher, which requires a multi-threaded
# Rack server capable of streaming a response.  Threads must be used,
# so Zbatery is recommended: http://zbatery.bogomip.org/
Rainbows! do
  use :ThreadSpawn
end
