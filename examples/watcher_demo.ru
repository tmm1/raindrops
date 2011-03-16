# This is the exact config that powers http://raindrops-demo.bogomips.org/
# This is used with zbatery.conf.rb
#
# zbatery -c zbatery.conf.ru watcher_demo.ru -E none
require "raindrops"
require "clogger" # http://clogger.rubyforge.org/
use Clogger, :format => Clogger::Format::Rack_1_0, :logger => $stdout
use Raindrops::Middleware
run Raindrops::Watcher.new :listeners => %w(0.0.0.0:80 /tmp/.raindrops)
