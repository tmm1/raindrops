# -*- encoding: binary -*-
require "test/unit"
require "raindrops"
require "rack"
require "rack/lobster"
require "open-uri"
begin
  require "unicorn"
rescue => e
  warn "W: #{e} skipping test since Unicorn was not found"
end

begin
  tmp = TCPServer.new(ENV["TEST_HOST6"] || '::1', 0)
  ipv6_enabled = true
rescue => e
  warn "skipping IPv6 tests, host does not seem to be IPv6 enabled:"
  warn "  #{e.class}: #{e}"
  ipv6_enabled = false
end
$stderr.sync = $stdout.sync = true

class TestMiddlewareUnicornIPv6 < Test::Unit::TestCase

  def setup
    @host = ENV["TEST_HOST6"] || "::1"
    sock = TCPServer.new @host, 0
    @port = sock.addr[1]
    ENV["UNICORN_FD"] = sock.fileno.to_s
    @host_with_port = "[#@host]:#@port"
    @opts = { :listeners => [ @host_with_port ] }
    @addr_regexp = Regexp.escape @host_with_port
  end

  def test_auto_listener
    @app = Rack::Builder.new do
      use Raindrops::Middleware
      run Rack::Lobster.new
    end
    @srv = fork { Unicorn.run(@app, @opts) }
    s = TCPSocket.new @host, @port
    s.write "GET /_raindrops HTTP/1.0\r\n\r\n"
    resp = s.read
    head, body = resp.split /\r\n\r\n/, 2
    assert_match %r{^#@addr_regexp active: 1$}, body
    assert_match %r{^#@addr_regexp queued: 0$}, body
  end

  def teardown
    Process.kill :QUIT, @srv
    _, status = Process.waitpid2 @srv
    assert status.success?
  end
end if defined?(Unicorn) && RUBY_PLATFORM =~ /linux/ && ipv6_enabled

