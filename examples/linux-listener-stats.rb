#!/usr/bin/ruby
# -*- encoding: binary -*-
$stdout.sync = $stderr.sync = true
# this is used to show or watch the number of active and queued
# connections on any listener socket from the command line

require 'raindrops'
require 'optparse'
require 'ipaddr'
require 'time'
usage = "Usage: #$0 [-d DELAY] [-t QUEUED_THRESHOLD] ADDR..."
ARGV.size > 0 or abort usage
delay = false
queued_thresh = -1

# "normal" exits when driven on the command-line
trap(:INT) { exit 130 }
trap(:PIPE) { exit 0 }

opts = OptionParser.new('', 24, '  ') do |opts|
  opts.banner = usage
  opts.on('-d', '--delay=DELAY', Float) { |n| delay = n }
  opts.on('-t', '--queued-threshold=INT', Integer) { |n| queued_thresh = n }
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

len = "address".size
now = nil
tcp, unix = [], []
ARGV.each do |addr|
  bs = addr.respond_to?(:bytesize) ? addr.bytesize : addr.size
  len = bs if bs > len
  (addr =~ %r{\A/} ? unix : tcp) << addr
end
combined = {}
tcp = nil if tcp.empty?
unix = nil if unix.empty?

len = 35 if len > 35
fmt = "%20s % #{len}s % 10u % 10u\n"
$stderr.printf fmt.tr('u','s'), *%w(timestamp address active queued)

begin
  if now
    combined.clear
    now = nil
  end
  tcp and combined.merge! Raindrops::Linux.tcp_listener_stats(tcp)
  unix and combined.merge! Raindrops::Linux.unix_listener_stats(unix)
  combined.each do |addr,stats|
    next if stats.queued < queued_thresh
    printf fmt, now ||= Time.now.utc.iso8601, addr, stats.active, stats.queued
  end
end while delay && sleep(delay)
