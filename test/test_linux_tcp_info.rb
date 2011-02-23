# -*- encoding: binary -*-
require 'test/unit'
require 'tempfile'
require 'raindrops'
require 'socket'
require 'pp'
$stderr.sync = $stdout.sync = true
class TestLinuxTCP_Info < Test::Unit::TestCase

  TEST_ADDR = ENV['UNICORN_TEST_ADDR'] || '127.0.0.1'

  def test_tcp_server
    s = TCPServer.new(TEST_ADDR, 0)
    rv = Raindrops::TCP_Info.new s
    c = TCPSocket.new TEST_ADDR, s.addr[1]
    tmp = Raindrops::TCP_Info.new s
    assert_equal 1, tmp.unacked
    assert_equal 0, rv.unacked
    a = s.accept
    tmp = Raindrops::TCP_Info.new s
    assert_equal 0, tmp.unacked
    ensure
      c.close if c
      a.close if a
      s.close
  end

  def test_accessors
    s = TCPServer.new TEST_ADDR, 0
    tmp = Raindrops::TCP_Info.new s
    tcp_info_methods = tmp.methods - Object.new.methods
    assert tcp_info_methods.size >= 32
    tcp_info_methods.each do |m|
      val = tmp.__send__ m
      assert_kind_of Integer, val
      assert val >= 0
    end
    ensure
      s.close
  end
end
