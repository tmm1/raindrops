#!/usr/bin/ruby
$stdout.sync = $stderr.sync = true
# this is used to show or watch the number of active and queued
# connections on any listener socket from the command line

require 'raindrops'
require 'optparse'
require 'ipaddr'
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

ARGV.each do |addr|
  addr =~ %r{\A(127\..+):(\d+)\z} or next
  host, port = $1, $2
  hex_port = '%X' % port.to_i
  ip_addr = IPAddr.new(host)
  hex_host = ip_addr.hton.each_byte.inject('') { |s,o| s << '%02X' % o }
  socks = File.readlines('/proc/net/tcp')
  hex_addr = "#{hex_host}:#{hex_port}"
  if socks.grep(/^\s+\d+:\s+#{hex_addr}\s+/).empty? &&
     ! socks.grep(/^\s+\d+:\s+00000000:#{hex_port}\s+/).empty?
    warn "W: #{host}:#{port} (#{hex_addr}) not found in /proc/net/tcp"
    warn "W: Did you mean 0.0.0.0:#{port}?"
  end
end

fmt = "% 19s % 10u % 10u\n"
printf fmt.tr('u','s'), *%w(address active queued)

begin
  stats = Raindrops::Linux.tcp_listener_stats(ARGV)
  stats.each { |addr,stats| printf fmt, addr, stats.active, stats.queued }
end while delay && sleep(delay)
