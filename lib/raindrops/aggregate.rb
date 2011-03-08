# -*- encoding: binary -*-
require "aggregate"
module Raindrops::Aggregate
  autoload :PMQ, "raindrops/aggregate/pmq"
  autoload :LastDataRecv, "raindrops/aggregate/last_data_recv"
end
