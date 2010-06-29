#!/usr/bin/ruby

# this is used to show or watch the number of active and queued
# connections on any listener socket from the command line

require 'raindrops'
require 'optparse'
usage = "Usage: #$0 [-d delay] ADDR..."
ARGV.size > 0 or abort usage
delay = false

# "normal" exits when driven on the command-line
trap(:INT) { exit 130 }
trap(:PIPE) { exit 0 }

opts = OptionParser.new('', 24, '  ') do |opts|
  opts.banner = usage
  opts.on('-d', '--delay=delay') { |nr| delay = nr.to_i }
  opts.parse! ARGV
end

fmt = "% 19s % 10u % 10u\n"
printf fmt.tr('u','s'), *%w(address active queued)

begin
  stats = Raindrops::Linux.tcp_listener_stats(ARGV)
  stats.each { |addr,stats| printf fmt, addr, stats.active, stats.queued }
end while delay && sleep(delay)
