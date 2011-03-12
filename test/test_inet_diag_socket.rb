# -*- encoding: binary -*-
require 'test/unit'
require 'raindrops'
$stderr.sync = $stdout.sync = true

class TestInetDiagSocket < Test::Unit::TestCase
  def test_new
    sock = Raindrops::InetDiagSocket.new
    assert_kind_of Socket, sock
    assert_kind_of Fixnum, sock.fileno
    assert_nil sock.close
  end
end if RUBY_PLATFORM =~ /linux/
